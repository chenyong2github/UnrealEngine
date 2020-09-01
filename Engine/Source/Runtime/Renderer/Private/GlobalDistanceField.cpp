// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GlobalDistanceField.cpp
=============================================================================*/

#include "GlobalDistanceField.h"
#include "DistanceFieldLightingShared.h"
#include "RendererModule.h"
#include "ClearQuad.h"
#include "Engine/VolumeTexture.h"
#include "DynamicMeshBuilder.h"
#include "DynamicPrimitiveDrawing.h"

DECLARE_GPU_STAT(GlobalDistanceFieldUpdate);

int32 GAOGlobalDistanceField = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceField(
	TEXT("r.AOGlobalDistanceField"), 
	GAOGlobalDistanceField,
	TEXT("Whether to use a global distance field to optimize occlusion cone traces.\n")
	TEXT("The global distance field is created by compositing object distance fields into clipmaps as the viewer moves through the level."),
	ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldNumClipmaps = 4;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldNumClipmaps(
	TEXT("r.AOGlobalDistanceField.NumClipmaps"), 
	GAOGlobalDistanceFieldNumClipmaps,
	TEXT("Num clipmaps in the global distance field.  Setting this to anything other than 4 is currently only supported by Lumen."),
	ECVF_RenderThreadSafe
	);

int32 GAOUpdateGlobalDistanceField = 1;
FAutoConsoleVariableRef CVarAOUpdateGlobalDistanceField(
	TEXT("r.AOUpdateGlobalDistanceField"),
	GAOUpdateGlobalDistanceField,
	TEXT("Whether to update the global distance field, useful for debugging."),
	ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldCacheMostlyStaticSeparately = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldCacheMostlyStaticSeparately(
	TEXT("r.AOGlobalDistanceFieldCacheMostlyStaticSeparately"),
	GAOGlobalDistanceFieldCacheMostlyStaticSeparately,
	TEXT("Whether to cache mostly static primitives separately from movable primitives, which reduces global DF update cost when a movable primitive is modified.  Adds another 12Mb of volume textures."),
	ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldPartialUpdates = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldPartialUpdates(
	TEXT("r.AOGlobalDistanceFieldPartialUpdates"),
	GAOGlobalDistanceFieldPartialUpdates,
	TEXT("Whether to allow partial updates of the global distance field.  When profiling it's useful to disable this and get the worst case composition time that happens on camera cuts."),
	ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldStaggeredUpdates = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldStaggeredUpdatess(
	TEXT("r.AOGlobalDistanceFieldStaggeredUpdates"),
	GAOGlobalDistanceFieldStaggeredUpdates,
	TEXT("Whether to allow the larger clipmaps to be updated less frequently."),
	ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldClipmapUpdatesPerFrame = 2;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldClipmapUpdatesPerFrame(
	TEXT("r.AOGlobalDistanceFieldClipmapUpdatesPerFrame"),
	GAOGlobalDistanceFieldClipmapUpdatesPerFrame,
	TEXT("How many clipmaps to update each frame, only 1 or 2 supported.  With values less than 2, the first clipmap is only updated every other frame, which can cause incorrect self occlusion during movement."),
	ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldForceFullUpdate = 0;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldForceFullUpdate(
	TEXT("r.AOGlobalDistanceFieldForceFullUpdate"),
	GAOGlobalDistanceFieldForceFullUpdate,
	TEXT("Whether to force full global distance field update every frame."),
	ECVF_RenderThreadSafe
);

int32 GAOGlobalDistanceFieldForceMovementUpdate = 0;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldForceMovementUpdate(
	TEXT("r.AOGlobalDistanceFieldForceMovementUpdate"),
	GAOGlobalDistanceFieldForceMovementUpdate,
	TEXT("Whether to force N texel border on X, Y and Z update each frame."),
	ECVF_RenderThreadSafe
);

int32 GAOLogGlobalDistanceFieldModifiedPrimitives = 0;
FAutoConsoleVariableRef CVarAOLogGlobalDistanceFieldModifiedPrimitives(
	TEXT("r.AOGlobalDistanceFieldLogModifiedPrimitives"),
	GAOLogGlobalDistanceFieldModifiedPrimitives,
	TEXT("Whether to log primitive modifications (add, remove, updatetransform) that caused an update of the global distance field.\n")
	TEXT("This can be useful for tracking down why updating the global distance field is always costing a lot, since it should be mostly cached.\n")
	TEXT("Pass 2 to log only non movable object updates."),
	ECVF_RenderThreadSafe
	);

int32 GAODrawGlobalDistanceFieldModifiedPrimitives = 0;
FAutoConsoleVariableRef CVarAODrawGlobalDistanceFieldModifiedPrimitives(
	TEXT("r.AOGlobalDistanceFieldDrawModifiedPrimitives"),
	GAODrawGlobalDistanceFieldModifiedPrimitives,
	TEXT("Whether to lodrawg primitive modifications (add, remove, updatetransform) that caused an update of the global distance field.\n")
	TEXT("This can be useful for tracking down why updating the global distance field is always costing a lot, since it should be mostly cached."),
	ECVF_RenderThreadSafe
	);

float GAOGlobalDFClipmapDistanceExponent = 2;
FAutoConsoleVariableRef CVarAOGlobalDFClipmapDistanceExponent(
	TEXT("r.AOGlobalDFClipmapDistanceExponent"),
	GAOGlobalDFClipmapDistanceExponent,
	TEXT("Exponent used to derive each clipmap's size, together with r.AOInnerGlobalDFClipmapDistance."),
	ECVF_RenderThreadSafe
	);

int32 GAOGlobalDFResolution = 128;
FAutoConsoleVariableRef CVarAOGlobalDFResolution(
	TEXT("r.AOGlobalDFResolution"),
	GAOGlobalDFResolution,
	TEXT("Resolution of the global distance field.  Higher values increase fidelity but also increase memory and composition cost."),
	ECVF_RenderThreadSafe
	);

float GAOGlobalDFStartDistance = 100;
FAutoConsoleVariableRef CVarAOGlobalDFStartDistance(
	TEXT("r.AOGlobalDFStartDistance"),
	GAOGlobalDFStartDistance,
	TEXT("World space distance along a cone trace to switch to using the global distance field instead of the object distance fields.\n")
	TEXT("This has to be large enough to hide the low res nature of the global distance field, but smaller values result in faster cone tracing."),
	ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldRepresentHeightfields = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldRepresentHeightfields(
	TEXT("r.AOGlobalDistanceFieldRepresentHeightfields"),
	GAOGlobalDistanceFieldRepresentHeightfields,
	TEXT("Whether to put landscape in the global distance field.  Changing this won't propagate until the global distance field gets recached (fly away and back)."),
	ECVF_RenderThreadSafe
	);

float GGlobalDistanceFieldHeightFieldThicknessScale = 4.0f;
FAutoConsoleVariableRef CVarGlobalDistanceFieldHeightFieldThicknessScale(
	TEXT("r.GlobalDistanceFieldHeightFieldThicknessScale"),
	GGlobalDistanceFieldHeightFieldThicknessScale,
	TEXT("Thickness of the height field when it's entered into the global distance field, measured in distance field voxels. Defaults to 4 which means 4x the voxel size as thickness."),
	ECVF_RenderThreadSafe
	);

float GAOGlobalDistanceFieldMinMeshSDFRadius = 20;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldMinMeshSDFRadius(
	TEXT("r.AOGlobalDistanceField.MinMeshSDFRadius"),
	GAOGlobalDistanceFieldMinMeshSDFRadius,
	TEXT("Meshes with a smaller world space radius than this are culled from the global SDF."),
	ECVF_RenderThreadSafe
	);

float GAOGlobalDistanceFieldMinMeshSDFRadiusInVoxels = .5f;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldMinMeshSDFRadiusInVoxels(
	TEXT("r.AOGlobalDistanceField.MinMeshSDFRadiusInVoxels"),
	GAOGlobalDistanceFieldMinMeshSDFRadiusInVoxels,
	TEXT("Meshes with a smaller radius than this number of voxels are culled from the global SDF."),
	ECVF_RenderThreadSafe
	);

float GAOGlobalDistanceFieldCameraPositionVelocityOffsetDecay = .7f;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldCameraPositionVelocityOffsetDecay(
	TEXT("r.AOGlobalDistanceField.CameraPositionVelocityOffsetDecay"),
	GAOGlobalDistanceFieldCameraPositionVelocityOffsetDecay,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldFastCameraMode = 0;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldFastCameraMode(
	TEXT("r.AOGlobalDistanceField.FastCameraMode"),
	GAOGlobalDistanceFieldFastCameraMode,
	TEXT("Whether to update the Global SDF for fast camera movement - lower quality, faster updates so lighting can keep up with the camera."),
	ECVF_RenderThreadSafe
);

// Approximation of margin added to SDF objects during generation inside FMeshUtilities::GenerateSignedDistanceFieldVolumeData.
constexpr float MESH_SDF_APPROX_MARGIN = 0.7f;

bool ShouldCompileGlobalDistanceFieldShader(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
}

float GetMinMeshSDFRadius(float VoxelWorldSize)
{
	float MinRadius = GAOGlobalDistanceFieldMinMeshSDFRadius * (GAOGlobalDistanceFieldFastCameraMode ? 10.0f : 1.0f);
	float MinVoxelRadius = GAOGlobalDistanceFieldMinMeshSDFRadiusInVoxels * VoxelWorldSize * (GAOGlobalDistanceFieldFastCameraMode ? 5.0f : 1.0f);

	return FMath::Max(MinRadius, MinVoxelRadius);
}

int32 GetNumClipmapUpdatesPerFrame()
{
	return GAOGlobalDistanceFieldFastCameraMode ? 1 : GAOGlobalDistanceFieldClipmapUpdatesPerFrame;
}

int32 GetNumGlobalDistanceFieldClipmaps()
{
	int32 WantedClipmaps = GAOGlobalDistanceFieldNumClipmaps;

	extern int32 GLumenDistantScene;
	if (GAOGlobalDistanceFieldFastCameraMode && GLumenDistantScene == 0)
	{
		WantedClipmaps++;
	}
	return FMath::Clamp<int32>(WantedClipmaps, 0, GMaxGlobalDistanceFieldClipmaps);
}

// For reading back the distance field data
static FGlobalDistanceFieldReadback* GDFReadbackRequest = nullptr;
void RequestGlobalDistanceFieldReadback(FGlobalDistanceFieldReadback* Readback)
{
	if (ensure(GDFReadbackRequest == nullptr))
	{
		ensure(Readback->ReadbackComplete.IsBound());
		ensure(Readback->CallbackThread != ENamedThreads::UnusedAnchor);
		GDFReadbackRequest = Readback;
	}
}

void FGlobalDistanceFieldInfo::UpdateParameterData(float MaxOcclusionDistance)
{
	if (Clipmaps.Num() > 0)
	{
		for (int32 ClipmapIndex = 0; ClipmapIndex < GMaxGlobalDistanceFieldClipmaps; ClipmapIndex++)
		{
			FRHITexture* TextureValue = ClipmapIndex < Clipmaps.Num()
				? Clipmaps[ClipmapIndex].RenderTarget->GetRenderTargetItem().ShaderResourceTexture 
				: NULL;

			ParameterData.Textures[ClipmapIndex] = TextureValue;

			if (ClipmapIndex < Clipmaps.Num())
			{
				const FGlobalDistanceFieldClipmap& Clipmap = Clipmaps[ClipmapIndex];
				ParameterData.CenterAndExtent[ClipmapIndex] = FVector4(Clipmap.Bounds.GetCenter(), Clipmap.Bounds.GetExtent().X);

				// GlobalUV = (WorldPosition - GlobalVolumeCenterAndExtent[ClipmapIndex].xyz + GlobalVolumeScollOffset[ClipmapIndex].xyz) / (GlobalVolumeCenterAndExtent[ClipmapIndex].w * 2) + .5f;
				// WorldToUVMul = 1.0f / (GlobalVolumeCenterAndExtent[ClipmapIndex].w * 2)
				// WorldToUVAdd = (GlobalVolumeScollOffset[ClipmapIndex].xyz - GlobalVolumeCenterAndExtent[ClipmapIndex].xyz) / (GlobalVolumeCenterAndExtent[ClipmapIndex].w * 2) + .5f
				const FVector WorldToUVAdd = (Clipmap.ScrollOffset - Clipmap.Bounds.GetCenter()) / (Clipmap.Bounds.GetExtent().X * 2) + FVector(.5f);
				ParameterData.WorldToUVAddAndMul[ClipmapIndex] = FVector4(WorldToUVAdd, 1.0f / (Clipmap.Bounds.GetExtent().X * 2));
			}
			else
			{
				ParameterData.CenterAndExtent[ClipmapIndex] = FVector4(0, 0, 0, 0);
				ParameterData.WorldToUVAddAndMul[ClipmapIndex] = FVector4(0, 0, 0, 0);
			}
		}

		ParameterData.GlobalDFResolution = GAOGlobalDFResolution;

		extern float GAOConeHalfAngle;
		const float GlobalMaxSphereQueryRadius = MaxOcclusionDistance / (1 + FMath::Tan(GAOConeHalfAngle));
		ParameterData.MaxDistance = GlobalMaxSphereQueryRadius;
		ParameterData.NumGlobalSDFClipmaps = Clipmaps.Num();
	}
	else
	{
		FPlatformMemory::Memzero(&ParameterData, sizeof(ParameterData));
	}

	bInitialized = true;
}

uint32 CullObjectsGroupSize = 64;
const int32 GMaxGridCulledObjects = 2048;
const int32 GCullGridTileSize = 16;
const int32 HeightfieldCompositeTileSize = 8;

class FComposeHeightfieldsIntoGlobalDistanceFieldCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComposeHeightfieldsIntoGlobalDistanceFieldCS);
	SHADER_USE_PARAMETER_STRUCT(FComposeHeightfieldsIntoGlobalDistanceFieldCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWGlobalDistanceFieldTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_TEXTURE(Texture2D, HeightfieldTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HeightfieldSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, VisibilityTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VisibilitySampler)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, HeightfieldDescriptions)
		SHADER_PARAMETER(uint32, NumHeightfields)
		SHADER_PARAMETER(float, InfluenceRadius)
		SHADER_PARAMETER(uint32, ClipmapIndex)
		SHADER_PARAMETER(FVector, UpdateRegionVolumeMin)
		SHADER_PARAMETER(float, UpdateRegionVolumeStep)
		SHADER_PARAMETER(FIntVector, UpdateRegionSize)
		SHADER_PARAMETER(float, HeightfieldThickness)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) &&
				!IsMetalPlatform(Parameters.Platform) && !IsVulkanMobileSM5Platform(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DISTANCE_FIELD_IN_VIEW_UB"), 1);
		OutEnvironment.SetDefine(TEXT("COMPOSITE_HEIGHTFIELDS_THREADGROUP_SIZE"), HeightfieldCompositeTileSize);
	}
};

IMPLEMENT_SHADER_TYPE(, FComposeHeightfieldsIntoGlobalDistanceFieldCS, TEXT("/Engine/Private/GlobalDistanceField.usf"), TEXT("ComposeHeightfieldsIntoGlobalDistanceFieldCS"), SF_Compute);

extern void UploadHeightfieldDescriptions(const TArray<FHeightfieldComponentDescription>& HeightfieldDescriptions, FVector2D InvLightingAtlasSize, float InvDownsampleFactor);
extern FRDGBufferRef UploadHeightfieldDescriptions(FRDGBuilder& GraphBuilder, const TArray<FHeightfieldComponentDescription>& HeightfieldDescriptions, FVector2D InvLightingAtlasSize, float InvDownsampleFactor);

void FHeightfieldLightingViewInfo::ComposeHeightfieldsIntoGlobalDistanceField(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	FRDGTextureRef GlobalDistanceFieldTexture,
	float InfluenceRadius,
	const FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo,
	FGlobalDistanceFieldClipmap const& Clipmap,
	int32 ClipmapIndexValue,
	const FVolumeUpdateRegion& UpdateRegion) const
{
	const int32 NumPrimitives = Scene->DistanceFieldSceneData.HeightfieldPrimitives.Num();
	const FPooledRenderTarget* ClipmapRT = static_cast<const FPooledRenderTarget*>(
		Clipmap.RenderTarget.GetReference());
	const EPixelFormat ClipmapPixelFormat = ClipmapRT->GetDesc().Format;

	if (GAOGlobalDistanceFieldRepresentHeightfields
		&& GDynamicRHI->RHIIsTypedUAVLoadSupported(ClipmapPixelFormat)
		&& NumPrimitives > 0
		&& SupportsDistanceFieldAO(Scene->GetFeatureLevel(), Scene->GetShaderPlatform())
		&& !IsMetalPlatform(Scene->GetShaderPlatform())
		&& !IsVulkanMobileSM5Platform(Scene->GetShaderPlatform()))
	{
		FHeightfieldDescription UpdateRegionHeightfield;

		for (int32 HeightfieldPrimitiveIndex = 0; HeightfieldPrimitiveIndex < NumPrimitives; HeightfieldPrimitiveIndex++)
		{
			const FPrimitiveSceneInfo* HeightfieldPrimitive = Scene->DistanceFieldSceneData.HeightfieldPrimitives[HeightfieldPrimitiveIndex];
			const FBoxSphereBounds& PrimitiveBounds = HeightfieldPrimitive->Proxy->GetBounds();

			// Expand bounding box by a SDF max influence distance (only in local Z axis, as distance is computed from a top down projected heightmap point).
			const FVector QueryInfluenceExpand = HeightfieldPrimitive->Proxy->GetLocalToWorld().GetUnitAxis(EAxis::Z) * FVector(0.0f, 0.0f, InfluenceRadius);
			const FBox HeightfieldInfluenceBox = PrimitiveBounds.GetBox().ExpandBy(QueryInfluenceExpand, QueryInfluenceExpand);

			if (UpdateRegion.Bounds.Intersect(HeightfieldInfluenceBox))
			{
				UTexture2D* HeightfieldTexture = NULL;
				UTexture2D* DiffuseColorTexture = NULL;
				UTexture2D* VisibilityTexture = NULL;
				FHeightfieldComponentDescription NewComponentDescription(HeightfieldPrimitive->Proxy->GetLocalToWorld());
				HeightfieldPrimitive->Proxy->GetHeightfieldRepresentation(HeightfieldTexture, DiffuseColorTexture, VisibilityTexture, NewComponentDescription);

				if (HeightfieldTexture && HeightfieldTexture->Resource->TextureRHI)
				{
					const FIntPoint HeightfieldSize = NewComponentDescription.HeightfieldRect.Size();

					if (UpdateRegionHeightfield.Rect.Area() == 0)
					{
						UpdateRegionHeightfield.Rect = NewComponentDescription.HeightfieldRect;
					}
					else
					{
						UpdateRegionHeightfield.Rect.Union(NewComponentDescription.HeightfieldRect);
					}

					TArray<FHeightfieldComponentDescription>& ComponentDescriptions = UpdateRegionHeightfield.ComponentDescriptions.FindOrAdd(FHeightfieldComponentTextures(HeightfieldTexture, DiffuseColorTexture, VisibilityTexture));
					ComponentDescriptions.Add(NewComponentDescription);
				}
			}
		}

		if (UpdateRegionHeightfield.ComponentDescriptions.Num() > 0)
		{
			for (TMap<FHeightfieldComponentTextures, TArray<FHeightfieldComponentDescription>>::TConstIterator It(UpdateRegionHeightfield.ComponentDescriptions); It; ++It)
			{
				const TArray<FHeightfieldComponentDescription>& HeightfieldDescriptions = It.Value();

				if (HeightfieldDescriptions.Num() > 0)
				{
					FRDGBufferRef HeightfieldDescriptionBuffer = UploadHeightfieldDescriptions(
						GraphBuilder,
						HeightfieldDescriptions,
						FVector2D(1, 1),
						1.0f / UpdateRegionHeightfield.DownsampleFactor);

					UTexture2D* HeightfieldTexture = It.Key().HeightAndNormal;
					UTexture2D* VisibilityTexture = It.Key().Visibility;

					const float VolumeStep = (2.0f * GlobalDistanceFieldInfo.ParameterData.CenterAndExtent[ClipmapIndexValue].W) / GAOGlobalDFResolution;

					FComposeHeightfieldsIntoGlobalDistanceFieldCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeHeightfieldsIntoGlobalDistanceFieldCS::FParameters>();
					PassParameters->RWGlobalDistanceFieldTexture = GraphBuilder.CreateUAV(GlobalDistanceFieldTexture);
					PassParameters->View = View.ViewUniformBuffer;
					PassParameters->NumHeightfields = HeightfieldDescriptions.Num();
					PassParameters->InfluenceRadius = InfluenceRadius;
					PassParameters->ClipmapIndex = ClipmapIndexValue;
					PassParameters->UpdateRegionVolumeMin = UpdateRegion.Bounds.Min + 0.5f * VolumeStep; // World space value for corner texel.
					PassParameters->UpdateRegionVolumeStep = VolumeStep;
					PassParameters->UpdateRegionSize = UpdateRegion.CellsSize;
					PassParameters->HeightfieldThickness = VolumeStep * GGlobalDistanceFieldHeightFieldThicknessScale;
					PassParameters->HeightfieldTexture = HeightfieldTexture->Resource->TextureRHI;
					PassParameters->HeightfieldSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
					PassParameters->VisibilityTexture = VisibilityTexture ? VisibilityTexture->Resource->TextureRHI : GBlackTexture->TextureRHI;
					PassParameters->VisibilitySampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
					PassParameters->HeightfieldDescriptions = GraphBuilder.CreateSRV(HeightfieldDescriptionBuffer, EPixelFormat::PF_A32B32G32R32F);

					auto ComputeShader = View.ShaderMap->GetShader<FComposeHeightfieldsIntoGlobalDistanceFieldCS>();

					//@todo - match typical update sizes.  Camera movement creates narrow slabs.
					const uint32 NumGroupsX = FMath::DivideAndRoundUp<int32>(UpdateRegion.CellsSize.X, HeightfieldCompositeTileSize);
					const uint32 NumGroupsY = FMath::DivideAndRoundUp<int32>(UpdateRegion.CellsSize.Y, HeightfieldCompositeTileSize);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("ComposeHeightfields"),
						ComputeShader,
						PassParameters,
						FIntVector(NumGroupsX, NumGroupsY, 1));
				}
			}
		}
	}
}

/** Constructs and adds an update region based on camera movement for the given axis. */
static void AddUpdateRegionForAxis(FIntVector Movement, 
	const FBox& ClipmapBounds,
	float CellSize,
	int32 ComponentIndex, 
	TArray<FVolumeUpdateRegion, TInlineAllocator<3> >& UpdateRegions,
	TArray<FClipmapUpdateBounds, TInlineAllocator<64>>& UpdateBounds)
{
	FVolumeUpdateRegion UpdateRegion;
	UpdateRegion.Bounds = ClipmapBounds;
	UpdateRegion.CellsSize = FIntVector(GAOGlobalDFResolution);
	UpdateRegion.CellsSize[ComponentIndex] = FMath::Min(FMath::Abs(Movement[ComponentIndex]), GAOGlobalDFResolution);

	if (Movement[ComponentIndex] > 0)
	{
		// Positive axis movement, set the min of that axis to contain the newly exposed area
		UpdateRegion.Bounds.Min[ComponentIndex] = FMath::Max(ClipmapBounds.Max[ComponentIndex] - Movement[ComponentIndex] * CellSize, ClipmapBounds.Min[ComponentIndex]);
	}
	else if (Movement[ComponentIndex] < 0)
	{
		// Negative axis movement, set the max of that axis to contain the newly exposed area
		UpdateRegion.Bounds.Max[ComponentIndex] = FMath::Min(ClipmapBounds.Min[ComponentIndex] - Movement[ComponentIndex] * CellSize, ClipmapBounds.Max[ComponentIndex]);
	}

	if (UpdateRegion.CellsSize[ComponentIndex] > 0)
	{
		UpdateBounds.Add(FClipmapUpdateBounds(UpdateRegion.Bounds.GetCenter(), UpdateRegion.Bounds.GetExtent(), false));
		UpdateRegions.Add(UpdateRegion);
	}
}

/** Constructs and adds an update region based on the given primitive bounds. */
static void AddUpdateRegionForPrimitive(const FBox& Bounds, float MaxSphereQueryRadius, const FBox& ClipmapBounds, float CellSize, TArray<FVolumeUpdateRegion, TInlineAllocator<3> >& UpdateRegions)
{
	// Object influence bounds
	FBox InfluenceBounds(Bounds.Min - FVector(MaxSphereQueryRadius), Bounds.Max + FVector(MaxSphereQueryRadius));

	FVolumeUpdateRegion UpdateRegion;
	UpdateRegion.Bounds.Init();
	// Snap the min and clamp to clipmap bounds
	UpdateRegion.Bounds.Min.X = FMath::Max(CellSize * FMath::FloorToFloat(InfluenceBounds.Min.X / CellSize), ClipmapBounds.Min.X);
	UpdateRegion.Bounds.Min.Y = FMath::Max(CellSize * FMath::FloorToFloat(InfluenceBounds.Min.Y / CellSize), ClipmapBounds.Min.Y);
	UpdateRegion.Bounds.Min.Z = FMath::Max(CellSize * FMath::FloorToFloat(InfluenceBounds.Min.Z / CellSize), ClipmapBounds.Min.Z);

	FVector ExtentInCells;
	ExtentInCells.X = FMath::CeilToFloat((Bounds.GetExtent().X + MaxSphereQueryRadius) * 2 / CellSize);
	ExtentInCells.Y = FMath::CeilToFloat((Bounds.GetExtent().Y + MaxSphereQueryRadius) * 2 / CellSize);
	ExtentInCells.Z = FMath::CeilToFloat((Bounds.GetExtent().Z + MaxSphereQueryRadius) * 2 / CellSize);

	// Derive the max from the snapped min and size, clamp to clipmap bounds
	UpdateRegion.Bounds.Max = UpdateRegion.Bounds.Min + ExtentInCells * CellSize;
	UpdateRegion.Bounds.Max.X = FMath::Min(UpdateRegion.Bounds.Max.X, ClipmapBounds.Max.X);
	UpdateRegion.Bounds.Max.Y = FMath::Min(UpdateRegion.Bounds.Max.Y, ClipmapBounds.Max.Y);
	UpdateRegion.Bounds.Max.Z = FMath::Min(UpdateRegion.Bounds.Max.Z, ClipmapBounds.Max.Z);

	const FVector UpdateRegionSize = UpdateRegion.Bounds.GetSize();
	UpdateRegion.CellsSize.X = FMath::TruncToInt(UpdateRegionSize.X / CellSize + .5f);
	UpdateRegion.CellsSize.Y = FMath::TruncToInt(UpdateRegionSize.Y / CellSize + .5f);
	UpdateRegion.CellsSize.Z = FMath::TruncToInt(UpdateRegionSize.Z / CellSize + .5f);

	// Only add update regions with positive area
	if (UpdateRegion.CellsSize.X > 0 && UpdateRegion.CellsSize.Y > 0 && UpdateRegion.CellsSize.Z > 0)
	{
		checkSlow(UpdateRegion.CellsSize.X <= GAOGlobalDFResolution && UpdateRegion.CellsSize.Y <= GAOGlobalDFResolution && UpdateRegion.CellsSize.Z <= GAOGlobalDFResolution);
		UpdateRegions.Add(UpdateRegion);
	}
}

static void TrimOverlappingAxis(int32 TrimAxis, float CellSize, const FVolumeUpdateRegion& OtherUpdateRegion, FVolumeUpdateRegion& UpdateRegion)
{
	int32 OtherAxis0 = (TrimAxis + 1) % 3;
	int32 OtherAxis1 = (TrimAxis + 2) % 3;

	// Check if the UpdateRegion is entirely contained in 2d
	if (UpdateRegion.Bounds.Max[OtherAxis0] <= OtherUpdateRegion.Bounds.Max[OtherAxis0]
		&& UpdateRegion.Bounds.Min[OtherAxis0] >= OtherUpdateRegion.Bounds.Min[OtherAxis0]
		&& UpdateRegion.Bounds.Max[OtherAxis1] <= OtherUpdateRegion.Bounds.Max[OtherAxis1]
		&& UpdateRegion.Bounds.Min[OtherAxis1] >= OtherUpdateRegion.Bounds.Min[OtherAxis1])
	{
		if (UpdateRegion.Bounds.Min[TrimAxis] >= OtherUpdateRegion.Bounds.Min[TrimAxis] && UpdateRegion.Bounds.Min[TrimAxis] <= OtherUpdateRegion.Bounds.Max[TrimAxis])
		{
			// Min on this axis is completely contained within the other region, clip it so there's no overlapping update region
			UpdateRegion.Bounds.Min[TrimAxis] = OtherUpdateRegion.Bounds.Max[TrimAxis];
		}
		else
		{
			// otherwise Max on this axis must be inside the other region, because we know the two volumes intersect
			UpdateRegion.Bounds.Max[TrimAxis] = OtherUpdateRegion.Bounds.Min[TrimAxis];
		}

		UpdateRegion.CellsSize[TrimAxis] = FMath::TruncToInt((FMath::Max(UpdateRegion.Bounds.Max[TrimAxis] - UpdateRegion.Bounds.Min[TrimAxis], 0.0f)) / CellSize + .5f);
	}
}

static void AllocateClipmapTexture(FRHICommandListImmediate& RHICmdList, int32 ClipmapIndex, FGlobalDFCacheType CacheType, TRefCountPtr<IPooledRenderTarget>& Texture)
{
	const TCHAR* TextureName = CacheType == GDF_MostlyStatic ? TEXT("MostlyStaticGlobalDistanceField0") : TEXT("GlobalDistanceField0");

	if (ClipmapIndex == 1)
	{
		TextureName = CacheType == GDF_MostlyStatic ? TEXT("MostlyStaticGlobalDistanceField1") : TEXT("GlobalDistanceField1");
	}
	else if (ClipmapIndex == 2)
	{
		TextureName = CacheType == GDF_MostlyStatic ? TEXT("MostlyStaticGlobalDistanceField2") : TEXT("GlobalDistanceField2");
	}
	else if (ClipmapIndex == 3)
	{
		TextureName = CacheType == GDF_MostlyStatic ? TEXT("MostlyStaticGlobalDistanceField3") : TEXT("GlobalDistanceField3");
	}

	FPooledRenderTargetDesc VolumeDesc = FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
		GAOGlobalDFResolution,
		GAOGlobalDFResolution,
		GAOGlobalDFResolution,
		PF_R16F,
		FClearValueBinding::None,
		0,
		// TexCreate_ReduceMemoryWithTilingMode used because 128^3 texture comes out 4x bigger on PS4 with recommended volume texture tiling modes
		TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_ReduceMemoryWithTilingMode | TexCreate_3DTiling,
		false));
	VolumeDesc.AutoWritable = false;

	GRenderTargetPool.FindFreeElement(
		RHICmdList,
		VolumeDesc,
		Texture,
		TextureName,
		true,
		ERenderTargetTransience::NonTransient
	);
}

static void GetUpdateFrequencyForClipmap(int32 ClipmapIndex, int32 NumClipmaps, int32& OutFrequency, int32& OutPhase)
{
	if (!GAOGlobalDistanceFieldStaggeredUpdates)
	{
		OutFrequency = 1;
		OutPhase = 0;
	}
	else if (GetNumClipmapUpdatesPerFrame() == 1)
	{
		if (ClipmapIndex == 0)
		{
			OutFrequency = 2;
			OutPhase = 0;
		}
		else if (ClipmapIndex == 1)
		{
			OutFrequency = 4;
			OutPhase = 1;
		}
		else if (ClipmapIndex == 2)
		{
			OutFrequency = 8;
			OutPhase = 3;
		}
		else
		{
			if (NumClipmaps > 4)
			{
				if (ClipmapIndex == 3)
				{
					OutFrequency = 16;
					OutPhase = 7;
				}
				else
				{
					OutFrequency = 16;
					OutPhase = 15;
				}
			}
			else
			{
				OutFrequency = 8;
				OutPhase = 7;
			}
		}
	}
	else
	{
		if (ClipmapIndex == 0)
		{
			OutFrequency = 1;
			OutPhase = 0;
		}
		else if (ClipmapIndex == 1)
		{
			OutFrequency = 2;
			OutPhase = 0;
		}
		else if (ClipmapIndex == 2)
		{
			OutFrequency = 4;
			OutPhase = 1;
		}
		else
		{
			if (NumClipmaps > 4)
			{
				if (ClipmapIndex == 3)
				{
					OutFrequency = 8;
					OutPhase = 3;
				}
				else
				{
					OutFrequency = 8;
					OutPhase = 7;
				}
			}
			else
			{
				OutFrequency = 4;
				OutPhase = 3;
			}
		}
	}
}

/** Staggers clipmap updates so there are only 2 per frame */
static bool ShouldUpdateClipmapThisFrame(int32 ClipmapIndex, int32 NumClipmaps, int32 GlobalDistanceFieldUpdateIndex)
{
	int32 Frequency;
	int32 Phase;
	GetUpdateFrequencyForClipmap(ClipmapIndex, NumClipmaps, Frequency, Phase);

	return GlobalDistanceFieldUpdateIndex % Frequency == Phase;
}

static float ComputeClipmapExtent(int32 ClipmapIndex, const FScene* Scene)
{
	const float InnerClipmapDistance = Scene->GlobalDistanceFieldViewDistance / FMath::Pow(GAOGlobalDFClipmapDistanceExponent, 3);
	return InnerClipmapDistance * FMath::Pow(GAOGlobalDFClipmapDistanceExponent, ClipmapIndex);
}

void UpdateGlobalDistanceFieldViewOrigin(const FViewInfo& View)
{
	if (View.ViewState)
	{
		if (GAOGlobalDistanceFieldFastCameraMode != 0)
		{
			FVector& CameraVelocityOffset = View.ViewState->GlobalDistanceFieldCameraVelocityOffset;
			const FVector CameraVelocity = View.ViewMatrices.GetViewOrigin() - View.PrevViewInfo.ViewMatrices.GetViewOrigin();
			// Framerate independent decay
			CameraVelocityOffset = CameraVelocityOffset * FMath::Pow(GAOGlobalDistanceFieldCameraPositionVelocityOffsetDecay, View.Family->DeltaWorldTime) + CameraVelocity;

			const FScene* Scene = (const FScene*)View.Family->Scene;
			const int32 NumClipmaps = GetNumGlobalDistanceFieldClipmaps();

			if (Scene && NumClipmaps > 0)
			{
				// Clamp the view origin offset to stay inside the current clipmap extents
				const float LargestVoxelClipmapExtent = ComputeClipmapExtent(NumClipmaps - 1, Scene);
				const float MaxCameraDriftFraction = .75f;
				CameraVelocityOffset.X = FMath::Clamp<float>(CameraVelocityOffset.X, -LargestVoxelClipmapExtent * MaxCameraDriftFraction, LargestVoxelClipmapExtent * MaxCameraDriftFraction);
				CameraVelocityOffset.Y = FMath::Clamp<float>(CameraVelocityOffset.Y, -LargestVoxelClipmapExtent * MaxCameraDriftFraction, LargestVoxelClipmapExtent * MaxCameraDriftFraction);
				CameraVelocityOffset.Z = FMath::Clamp<float>(CameraVelocityOffset.Z, -LargestVoxelClipmapExtent * MaxCameraDriftFraction, LargestVoxelClipmapExtent * MaxCameraDriftFraction);
			}
		}
		else
		{
			View.ViewState->GlobalDistanceFieldCameraVelocityOffset = FVector(0.0f, 0.0f, 0.0f);
		}
	}
}

FVector GetGlobalDistanceFieldViewOrigin(const FViewInfo& View, int32 ClipmapIndex)
{
	FVector CameraOrigin = View.ViewMatrices.GetViewOrigin();

	if (View.ViewState)
	{
		FVector CameraVelocityOffset = View.ViewState->GlobalDistanceFieldCameraVelocityOffset;

		const FScene* Scene = (const FScene*)View.Family->Scene;

		if (Scene)
		{
			// Clamp the view origin to stay inside the current clipmap extents
			const float ClipmapExtent = ComputeClipmapExtent(ClipmapIndex, Scene);
			const float MaxCameraDriftFraction = .75f;
			CameraVelocityOffset.X = FMath::Clamp<float>(CameraVelocityOffset.X, -ClipmapExtent * MaxCameraDriftFraction, ClipmapExtent * MaxCameraDriftFraction);
			CameraVelocityOffset.Y = FMath::Clamp<float>(CameraVelocityOffset.Y, -ClipmapExtent * MaxCameraDriftFraction, ClipmapExtent * MaxCameraDriftFraction);
			CameraVelocityOffset.Z = FMath::Clamp<float>(CameraVelocityOffset.Z, -ClipmapExtent * MaxCameraDriftFraction, ClipmapExtent * MaxCameraDriftFraction);
		}

		CameraOrigin += CameraVelocityOffset;
	}

	return CameraOrigin;
}

static void ComputeUpdateRegionsAndUpdateViewState(
	FRHICommandListImmediate& RHICmdList, 
	FViewInfo& View, 
	const FScene* Scene, 
	FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo, 
	int32 NumClipmaps, 
	float MaxOcclusionDistance)
{
	GlobalDistanceFieldInfo.Clipmaps.AddZeroed(NumClipmaps);
	GlobalDistanceFieldInfo.MostlyStaticClipmaps.AddZeroed(NumClipmaps);

	// Cache the heightfields update region boxes for fast reuse for each clip region.
	TArray<FBox> PendingStreamingHeightfieldBoxes;
	for (const FPrimitiveSceneInfo* HeightfieldPrimitive : Scene->DistanceFieldSceneData.HeightfieldPrimitives)
	{
		if (HeightfieldPrimitive->Proxy->HeightfieldHasPendingStreaming())
		{
			PendingStreamingHeightfieldBoxes.Add(HeightfieldPrimitive->Proxy->GetBounds().GetBox());
		}
	}

	if (View.ViewState)
	{
		View.ViewState->GlobalDistanceFieldUpdateIndex++;

		if (View.ViewState->GlobalDistanceFieldUpdateIndex > 128)
		{
			View.ViewState->GlobalDistanceFieldUpdateIndex = 0;
		}

		int32 NumClipmapUpdateRequests = 0;

		FViewElementPDI ViewPDI(&View, nullptr, &View.DynamicPrimitiveShaderData);

		for (int32 ClipmapIndex = 0; ClipmapIndex < NumClipmaps; ClipmapIndex++)
		{
			FGlobalDistanceFieldClipmapState& ClipmapViewState = View.ViewState->GlobalDistanceFieldClipmapState[ClipmapIndex];

			const float Extent = ComputeClipmapExtent(ClipmapIndex, Scene);
			const float CellSize = (Extent * 2) / GAOGlobalDFResolution;

			bool bReallocated = false;

			// Accumulate primitive modifications in the viewstate in case we don't update the clipmap this frame
			for (uint32 CacheType = 0; CacheType < GDF_Num; CacheType++)
			{
				const uint32 SourceCacheType = GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? CacheType : GDF_Full;
				ClipmapViewState.Cache[CacheType].PrimitiveModifiedBounds.Append(Scene->DistanceFieldSceneData.PrimitiveModifiedBounds[SourceCacheType]);

				if (CacheType == GDF_Full || GAOGlobalDistanceFieldCacheMostlyStaticSeparately)
				{
					TRefCountPtr<IPooledRenderTarget>& RenderTarget = ClipmapViewState.Cache[CacheType].VolumeTexture;

					if (!RenderTarget || RenderTarget->GetDesc().Extent.X != GAOGlobalDFResolution)
					{
						AllocateClipmapTexture(RHICmdList, ClipmapIndex, (FGlobalDFCacheType)CacheType, RenderTarget);
						bReallocated = true;
					}
				}
			}

			const bool bForceFullUpdate = bReallocated
				|| !View.ViewState->bInitializedGlobalDistanceFieldOrigins
				// Detect when max occlusion distance has changed
				|| ClipmapViewState.CachedMaxOcclusionDistance != MaxOcclusionDistance
				|| ClipmapViewState.CachedGlobalDistanceFieldViewDistance != Scene->GlobalDistanceFieldViewDistance
				|| ClipmapViewState.CacheMostlyStaticSeparately != GAOGlobalDistanceFieldCacheMostlyStaticSeparately
				|| ClipmapViewState.LastUsedSceneDataForFullUpdate != &Scene->DistanceFieldSceneData
				|| GAOGlobalDistanceFieldForceFullUpdate
				|| GDFReadbackRequest != nullptr;

			const bool bUpdateRequested = ShouldUpdateClipmapThisFrame(ClipmapIndex, NumClipmaps, View.ViewState->GlobalDistanceFieldUpdateIndex);

			if (bUpdateRequested)
			{
				NumClipmapUpdateRequests++;
			}

			if (bUpdateRequested || bForceFullUpdate)
			{
				const FVector GlobalDistanceFieldViewOrigin = GetGlobalDistanceFieldViewOrigin(View, ClipmapIndex);

				FIntVector GridCenter;
				GridCenter.X = FMath::FloorToInt(GlobalDistanceFieldViewOrigin.X / CellSize);
				GridCenter.Y = FMath::FloorToInt(GlobalDistanceFieldViewOrigin.Y / CellSize);
				GridCenter.Z = FMath::FloorToInt(GlobalDistanceFieldViewOrigin.Z / CellSize);

				const FVector SnappedCenter = FVector(GridCenter) * CellSize;
				const FBox ClipmapBounds(SnappedCenter - Extent, SnappedCenter + Extent);

				const bool bUsePartialUpdates = GAOGlobalDistanceFieldPartialUpdates && !bForceFullUpdate;

				if (!bUsePartialUpdates)
				{
					// Store the location of the full update
					ClipmapViewState.FullUpdateOrigin = GridCenter;
					View.ViewState->bInitializedGlobalDistanceFieldOrigins = true;
					ClipmapViewState.LastUsedSceneDataForFullUpdate = &Scene->DistanceFieldSceneData;
				}

				const FGlobalDFCacheType StartCacheType = GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? GDF_MostlyStatic : GDF_Full;
				
				extern float GAOConeHalfAngle;
				const float GlobalMaxSphereQueryRadius = MaxOcclusionDistance / (1 + FMath::Tan(GAOConeHalfAngle));

				for (uint32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
				{
					FGlobalDistanceFieldClipmap& Clipmap = *(CacheType == GDF_MostlyStatic
						? &GlobalDistanceFieldInfo.MostlyStaticClipmaps[ClipmapIndex]
						: &GlobalDistanceFieldInfo.Clipmaps[ClipmapIndex]);

					const TArray<FBox>& PrimitiveModifiedBounds = ClipmapViewState.Cache[CacheType].PrimitiveModifiedBounds;

					TArray<FBox, SceneRenderingAllocator> CulledPrimitiveModifiedBounds;
					CulledPrimitiveModifiedBounds.Empty(ClipmapViewState.Cache[CacheType].PrimitiveModifiedBounds.Num() / 2);

					Clipmap.UpdateBounds.Empty(ClipmapViewState.Cache[CacheType].PrimitiveModifiedBounds.Num() / 2);

					for (int32 BoundsIndex = 0; BoundsIndex < ClipmapViewState.Cache[CacheType].PrimitiveModifiedBounds.Num(); BoundsIndex++)
					{
						const FBox PrimBounds = ClipmapViewState.Cache[CacheType].PrimitiveModifiedBounds[BoundsIndex];
						const FVector PrimWorldCenter = PrimBounds.GetCenter();
						const FVector PrimWorldExtent = PrimBounds.GetExtent() * MESH_SDF_APPROX_MARGIN;
						const FBox ModifiedBounds(PrimWorldCenter - PrimWorldExtent, PrimWorldCenter + PrimWorldExtent);

						if (ModifiedBounds.ComputeSquaredDistanceToBox(ClipmapBounds) < GlobalMaxSphereQueryRadius * GlobalMaxSphereQueryRadius)
						{
							CulledPrimitiveModifiedBounds.Add(ModifiedBounds);

							Clipmap.UpdateBounds.Add(FClipmapUpdateBounds(ModifiedBounds.GetCenter(), ModifiedBounds.GetExtent(), true));
							
							if (GAODrawGlobalDistanceFieldModifiedPrimitives)
							{
								const uint8 MarkerHue = ((ClipmapIndex * 10 + BoundsIndex) * 10) & 0xFF;
								const uint8 MarkerSaturation = 0xFF;
								const uint8 MarkerValue = 0xFF;

								FLinearColor MarkerColor = FLinearColor::MakeFromHSV8(MarkerHue, MarkerSaturation, MarkerValue);
								MarkerColor.A = 0.5f;
	
								DrawWireBox(&ViewPDI, ModifiedBounds, MarkerColor, SDPG_World);
							}
						}
					}

					if (bUsePartialUpdates)
					{
						FIntVector Movement = GridCenter - ClipmapViewState.LastPartialUpdateOrigin;

						if (GAOGlobalDistanceFieldForceMovementUpdate != 0)
						{
							Movement = FIntVector(GAOGlobalDistanceFieldForceMovementUpdate, GAOGlobalDistanceFieldForceMovementUpdate, GAOGlobalDistanceFieldForceMovementUpdate);
						}

						if (CacheType == GDF_MostlyStatic || !GAOGlobalDistanceFieldCacheMostlyStaticSeparately)
						{
							// Add an update region for each potential axis of camera movement
							AddUpdateRegionForAxis(Movement, ClipmapBounds, CellSize, 0, Clipmap.UpdateRegions, Clipmap.UpdateBounds);
							AddUpdateRegionForAxis(Movement, ClipmapBounds, CellSize, 1, Clipmap.UpdateRegions, Clipmap.UpdateBounds);
							AddUpdateRegionForAxis(Movement, ClipmapBounds, CellSize, 2, Clipmap.UpdateRegions, Clipmap.UpdateBounds);
						}
						else
						{
							// Inherit from parent
							Clipmap.UpdateBounds.Append(GlobalDistanceFieldInfo.MostlyStaticClipmaps[ClipmapIndex].UpdateBounds);
							Clipmap.UpdateRegions.Append(GlobalDistanceFieldInfo.MostlyStaticClipmaps[ClipmapIndex].UpdateRegions);
						}
					}

					// Only use partial updates with small numbers of primitive modifications
					bool bUsePartialUpdatesForUpdateBounds = bUsePartialUpdates
						&& CulledPrimitiveModifiedBounds.Num() < 1024;

					bool bUsePartialUpdatesForUpdateRegions = bUsePartialUpdates
						&& CulledPrimitiveModifiedBounds.Num() < 100;

					if (bUsePartialUpdatesForUpdateRegions)
					{
						// Add an update region for each primitive that has been modified
						for (int32 BoundsIndex = 0; BoundsIndex < CulledPrimitiveModifiedBounds.Num(); BoundsIndex++)
						{
							AddUpdateRegionForPrimitive(CulledPrimitiveModifiedBounds[BoundsIndex], GlobalMaxSphereQueryRadius, ClipmapBounds, CellSize, Clipmap.UpdateRegions);
						}

						int32 TotalTexelsBeingUpdated = 0;

						// Trim fully contained update regions
						for (int32 UpdateRegionIndex = 0; UpdateRegionIndex < Clipmap.UpdateRegions.Num(); UpdateRegionIndex++)
						{
							const FVolumeUpdateRegion& UpdateRegion = Clipmap.UpdateRegions[UpdateRegionIndex];
							bool bCompletelyContained = false;

							for (int32 OtherUpdateRegionIndex = 0; OtherUpdateRegionIndex < Clipmap.UpdateRegions.Num(); OtherUpdateRegionIndex++)
							{
								if (UpdateRegionIndex != OtherUpdateRegionIndex)
								{
									const FVolumeUpdateRegion& OtherUpdateRegion = Clipmap.UpdateRegions[OtherUpdateRegionIndex];

									if (OtherUpdateRegion.Bounds.IsInsideOrOn(UpdateRegion.Bounds.Min)
										&& OtherUpdateRegion.Bounds.IsInsideOrOn(UpdateRegion.Bounds.Max))
									{
										bCompletelyContained = true;
										break;
									}
								}
							}

							if (bCompletelyContained)
							{
								Clipmap.UpdateRegions.RemoveAt(UpdateRegionIndex);
								UpdateRegionIndex--;
							}
						}

						// Trim overlapping regions
						for (int32 UpdateRegionIndex = 0; UpdateRegionIndex < Clipmap.UpdateRegions.Num(); UpdateRegionIndex++)
						{
							FVolumeUpdateRegion& UpdateRegion = Clipmap.UpdateRegions[UpdateRegionIndex];
							bool bEmptyRegion = false;

							for (int32 OtherUpdateRegionIndex = 0; OtherUpdateRegionIndex < Clipmap.UpdateRegions.Num(); OtherUpdateRegionIndex++)
							{
								if (UpdateRegionIndex != OtherUpdateRegionIndex)
								{
									const FVolumeUpdateRegion& OtherUpdateRegion = Clipmap.UpdateRegions[OtherUpdateRegionIndex];

									if (OtherUpdateRegion.Bounds.Intersect(UpdateRegion.Bounds))
									{
										TrimOverlappingAxis(0, CellSize, OtherUpdateRegion, UpdateRegion);
										TrimOverlappingAxis(1, CellSize, OtherUpdateRegion, UpdateRegion);
										TrimOverlappingAxis(2, CellSize, OtherUpdateRegion, UpdateRegion);

										if (UpdateRegion.CellsSize.X == 0 || UpdateRegion.CellsSize.Y == 0 || UpdateRegion.CellsSize.Z == 0)
										{
											bEmptyRegion = true;
											break;
										}
									}
								}
							}

							if (bEmptyRegion)
							{
								Clipmap.UpdateRegions.RemoveAt(UpdateRegionIndex);
								UpdateRegionIndex--;
							}
						}

						// Count how many texels are being updated
						for (int32 UpdateRegionIndex = 0; UpdateRegionIndex < Clipmap.UpdateRegions.Num(); UpdateRegionIndex++)
						{
							const FVolumeUpdateRegion& UpdateRegion = Clipmap.UpdateRegions[UpdateRegionIndex];
							TotalTexelsBeingUpdated += UpdateRegion.CellsSize.X * UpdateRegion.CellsSize.Y * UpdateRegion.CellsSize.Z;
						}

						// Fall back to a full update if the partial updates were going to do more work
						if (TotalTexelsBeingUpdated >= GAOGlobalDFResolution * GAOGlobalDFResolution * GAOGlobalDFResolution)
						{
							bUsePartialUpdatesForUpdateRegions = false;
						}
					}

					if (!bUsePartialUpdatesForUpdateRegions)
					{
						Clipmap.UpdateRegions.Reset();

						FVolumeUpdateRegion UpdateRegion;
						UpdateRegion.Bounds = ClipmapBounds;
						UpdateRegion.CellsSize = FIntVector(GAOGlobalDFResolution);
						Clipmap.UpdateRegions.Add(UpdateRegion);
					}

					if (!bUsePartialUpdatesForUpdateBounds)
					{
						Clipmap.UpdateBounds.Reset();
						Clipmap.UpdateBounds.Add(FClipmapUpdateBounds(ClipmapBounds.GetCenter(), ClipmapBounds.GetExtent(), false));
					}

					// Check if the clipmap intersects with a pending update region
					bool bHasPendingStreaming = false;
					for (const FBox& HeightfieldBox : PendingStreamingHeightfieldBoxes)
					{
						if (ClipmapBounds.Intersect(HeightfieldBox))
						{
							bHasPendingStreaming = true;
							break;
						}
					}

					// If some of the height fields has pending streaming regions, postpone a full update.
					if (bHasPendingStreaming)
					{
						// Mark a pending update for this height field. It will get processed when all pending texture streaming affecting it will be completed.
						View.ViewState->DeferredGlobalDistanceFieldUpdates[CacheType].AddUnique(ClipmapIndex);
						// Remove the height fields from the update.
						for (FVolumeUpdateRegion& UpdateRegion : Clipmap.UpdateRegions)
						{
							UpdateRegion.UpdateType = (EVolumeUpdateType)(UpdateRegion.UpdateType & ~VUT_Heightfields);
						}
					}
					else if (View.ViewState->DeferredGlobalDistanceFieldUpdates[CacheType].Remove(ClipmapIndex) > 0)
					{
						// Remove the height fields from the current update as we are pushing a new full update.
						for (FVolumeUpdateRegion& UpdateRegion : Clipmap.UpdateRegions)
						{
							UpdateRegion.UpdateType = (EVolumeUpdateType)(UpdateRegion.UpdateType & ~VUT_Heightfields);
						}

						FVolumeUpdateRegion UpdateRegion;
						UpdateRegion.Bounds = ClipmapBounds;
						UpdateRegion.CellsSize = FIntVector(GAOGlobalDFResolution);
						UpdateRegion.UpdateType = VUT_Heightfields;
						Clipmap.UpdateRegions.Add(UpdateRegion);
					}

					ClipmapViewState.Cache[CacheType].PrimitiveModifiedBounds.Reset();
				}

				ClipmapViewState.LastPartialUpdateOrigin = GridCenter;
			}

			const FVector Center = FVector(ClipmapViewState.LastPartialUpdateOrigin) * CellSize;
			const FGlobalDFCacheType StartCacheType = GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? GDF_MostlyStatic : GDF_Full;

			for (uint32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
			{
				FGlobalDistanceFieldClipmap& Clipmap = *(CacheType == GDF_MostlyStatic 
					? &GlobalDistanceFieldInfo.MostlyStaticClipmaps[ClipmapIndex] 
					: &GlobalDistanceFieldInfo.Clipmaps[ClipmapIndex]);

				// Setup clipmap properties from view state exclusively, so we can skip updating on some frames
				Clipmap.RenderTarget = ClipmapViewState.Cache[CacheType].VolumeTexture;
				Clipmap.Bounds = FBox(Center - Extent, Center + Extent);
				// Scroll offset so the contents of the global distance field don't have to be moved as the camera moves around, only updated in slabs
				Clipmap.ScrollOffset = FVector(ClipmapViewState.LastPartialUpdateOrigin - ClipmapViewState.FullUpdateOrigin) * CellSize;
			}

			ClipmapViewState.CachedMaxOcclusionDistance = MaxOcclusionDistance;
			ClipmapViewState.CachedGlobalDistanceFieldViewDistance = Scene->GlobalDistanceFieldViewDistance;
			ClipmapViewState.CacheMostlyStaticSeparately = GAOGlobalDistanceFieldCacheMostlyStaticSeparately;
		}

		ensureMsgf(GAOGlobalDistanceFieldStaggeredUpdates || NumClipmapUpdateRequests <= GetNumClipmapUpdatesPerFrame(), TEXT("ShouldUpdateClipmapThisFrame needs to be adjusted for the NumClipmaps to even out the work distribution"));
	}
	else
	{
		for (int32 ClipmapIndex = 0; ClipmapIndex < NumClipmaps; ClipmapIndex++)
		{
			const FGlobalDFCacheType StartCacheType = GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? GDF_MostlyStatic : GDF_Full;

			for (uint32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
			{
				FGlobalDistanceFieldClipmap& Clipmap = *(CacheType == GDF_MostlyStatic
					? &GlobalDistanceFieldInfo.MostlyStaticClipmaps[ClipmapIndex]
					: &GlobalDistanceFieldInfo.Clipmaps[ClipmapIndex]);

				AllocateClipmapTexture(RHICmdList, ClipmapIndex, (FGlobalDFCacheType)CacheType, Clipmap.RenderTarget);
				Clipmap.ScrollOffset = FVector::ZeroVector;

				const float Extent = ComputeClipmapExtent(ClipmapIndex, Scene);
				const FVector GlobalDistanceFieldViewOrigin = GetGlobalDistanceFieldViewOrigin(View, ClipmapIndex);

				const float CellSize = (Extent * 2) / GAOGlobalDFResolution;

				FIntVector GridCenter;
				GridCenter.X = FMath::FloorToInt(GlobalDistanceFieldViewOrigin.X / CellSize);
				GridCenter.Y = FMath::FloorToInt(GlobalDistanceFieldViewOrigin.Y / CellSize);
				GridCenter.Z = FMath::FloorToInt(GlobalDistanceFieldViewOrigin.Z / CellSize);

				FVector Center = FVector(GridCenter) * CellSize;

				FBox ClipmapBounds(Center - Extent, Center + Extent);
				Clipmap.Bounds = ClipmapBounds;

				FVolumeUpdateRegion UpdateRegion;
				UpdateRegion.Bounds = ClipmapBounds;
				UpdateRegion.CellsSize = FIntVector(GAOGlobalDFResolution);
				Clipmap.UpdateRegions.Add(UpdateRegion);

				Clipmap.UpdateBounds.Reset();
				Clipmap.UpdateBounds.Add(FClipmapUpdateBounds(ClipmapBounds.GetCenter(), ClipmapBounds.GetExtent(), false));
			}
		}
	}

	GlobalDistanceFieldInfo.UpdateParameterData(MaxOcclusionDistance);
}

void FViewInfo::SetupDefaultGlobalDistanceFieldUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const
{
	// Initialize global distance field members to defaults, because View.GlobalDistanceFieldInfo is not valid yet
	for (int32 Index = 0; Index < GMaxGlobalDistanceFieldClipmaps; Index++)
	{
		ViewUniformShaderParameters.GlobalVolumeCenterAndExtent[Index] = FVector4(0);
		ViewUniformShaderParameters.GlobalVolumeWorldToUVAddAndMul[Index] = FVector4(0);
	}
	ViewUniformShaderParameters.GlobalVolumeDimension = 0.0f;
	ViewUniformShaderParameters.GlobalVolumeTexelSize = 0.0f;
	ViewUniformShaderParameters.MaxGlobalDistance = 0.0f;
	ViewUniformShaderParameters.NumGlobalSDFClipmaps = 0;

	ViewUniformShaderParameters.GlobalDistanceFieldTexture0 = OrBlack3DIfNull(GBlackVolumeTexture->TextureRHI.GetReference());
	ViewUniformShaderParameters.GlobalDistanceFieldSampler0 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	ViewUniformShaderParameters.GlobalDistanceFieldTexture1 = OrBlack3DIfNull(GBlackVolumeTexture->TextureRHI.GetReference());
	ViewUniformShaderParameters.GlobalDistanceFieldSampler1 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	ViewUniformShaderParameters.GlobalDistanceFieldTexture2 = OrBlack3DIfNull(GBlackVolumeTexture->TextureRHI.GetReference());
	ViewUniformShaderParameters.GlobalDistanceFieldSampler2 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	ViewUniformShaderParameters.GlobalDistanceFieldTexture3 = OrBlack3DIfNull(GBlackVolumeTexture->TextureRHI.GetReference());
	ViewUniformShaderParameters.GlobalDistanceFieldSampler3 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	ViewUniformShaderParameters.GlobalDistanceFieldTexture4 = OrBlack3DIfNull(GBlackVolumeTexture->TextureRHI.GetReference());
	ViewUniformShaderParameters.GlobalDistanceFieldSampler4 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
}

void FViewInfo::SetupGlobalDistanceFieldUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const
{
	check(GlobalDistanceFieldInfo.bInitialized);

	for (int32 Index = 0; Index < GMaxGlobalDistanceFieldClipmaps; Index++)
	{
		ViewUniformShaderParameters.GlobalVolumeCenterAndExtent[Index] = GlobalDistanceFieldInfo.ParameterData.CenterAndExtent[Index];
		ViewUniformShaderParameters.GlobalVolumeWorldToUVAddAndMul[Index] = GlobalDistanceFieldInfo.ParameterData.WorldToUVAddAndMul[Index];
	}
	ViewUniformShaderParameters.GlobalVolumeDimension = GlobalDistanceFieldInfo.ParameterData.GlobalDFResolution;
	ViewUniformShaderParameters.GlobalVolumeTexelSize = 1.0f / GlobalDistanceFieldInfo.ParameterData.GlobalDFResolution;
	ViewUniformShaderParameters.MaxGlobalDistance = GlobalDistanceFieldInfo.ParameterData.MaxDistance;
	ViewUniformShaderParameters.NumGlobalSDFClipmaps = GlobalDistanceFieldInfo.ParameterData.NumGlobalSDFClipmaps;

	ViewUniformShaderParameters.GlobalDistanceFieldTexture0 = OrBlack3DIfNull(GlobalDistanceFieldInfo.ParameterData.Textures[0]);
	ViewUniformShaderParameters.GlobalDistanceFieldSampler0 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	ViewUniformShaderParameters.GlobalDistanceFieldTexture1 = OrBlack3DIfNull(GlobalDistanceFieldInfo.ParameterData.Textures[1]);
	ViewUniformShaderParameters.GlobalDistanceFieldSampler1 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	ViewUniformShaderParameters.GlobalDistanceFieldTexture2 = OrBlack3DIfNull(GlobalDistanceFieldInfo.ParameterData.Textures[2]);
	ViewUniformShaderParameters.GlobalDistanceFieldSampler2 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	ViewUniformShaderParameters.GlobalDistanceFieldTexture3 = OrBlack3DIfNull(GlobalDistanceFieldInfo.ParameterData.Textures[3]);
	ViewUniformShaderParameters.GlobalDistanceFieldSampler3 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	ViewUniformShaderParameters.GlobalDistanceFieldTexture4 = OrBlack3DIfNull(GlobalDistanceFieldInfo.ParameterData.Textures[4]);
	ViewUniformShaderParameters.GlobalDistanceFieldSampler4 = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
}

void ReadbackDistanceFieldClipmap(FRHICommandListImmediate& RHICmdList, FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
{
	FGlobalDistanceFieldReadback* Readback = GDFReadbackRequest;
	GDFReadbackRequest = nullptr;

	FGlobalDistanceFieldClipmap& ClipMap = GlobalDistanceFieldInfo.Clipmaps[0];
	FTextureRHIRef SourceTexture = ClipMap.RenderTarget->GetRenderTargetItem().ShaderResourceTexture;
	FIntVector Size = SourceTexture->GetSizeXYZ();
	
	RHICmdList.Read3DSurfaceFloatData(SourceTexture, FIntRect(0, 0, Size.X, Size.Y), FIntPoint(0, Size.Z), Readback->ReadbackData);
	Readback->Bounds = ClipMap.Bounds;
	Readback->Size = Size;
	
	// Fire the callback to notify that the request is complete
	DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.DistanceFieldReadbackDelegate"), STAT_FSimpleDelegateGraphTask_DistanceFieldReadbackDelegate, STATGROUP_TaskGraphTasks);
	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		Readback->ReadbackComplete,
		GET_STATID(STAT_FSimpleDelegateGraphTask_DistanceFieldReadbackDelegate),
		nullptr,
		Readback->CallbackThread
		);	
}

BEGIN_SHADER_PARAMETER_STRUCT(FUpdateBoundsUploadParameters, )
	SHADER_PARAMETER_RDG_BUFFER_UPLOAD(Buffer<float4>, UpdateBoundsBuffer)
END_SHADER_PARAMETER_STRUCT()

class FCullObjectsToClipmapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullObjectsToClipmapCS);
	SHADER_USE_PARAMETER_STRUCT(FCullObjectsToClipmapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWObjectIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWObjectIndexNumBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectBounds)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
		SHADER_PARAMETER(uint32, NumSceneObjects)
		SHADER_PARAMETER(FVector, ClipmapWorldCenter)
		SHADER_PARAMETER(FVector, ClipmapWorldExtent)
		SHADER_PARAMETER(uint32, AcceptOftenMovingObjectsOnly)
		SHADER_PARAMETER(float, MeshSDFRadiusThreshold)
		SHADER_PARAMETER(float, InfluenceRadiusSq)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("CULLOBJECTS_THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FCullObjectsToClipmapCS, "/Engine/Private/GlobalDistanceField.usf", "CullObjectsToClipmapCS", SF_Compute);

class FClearIndirectArgBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearIndirectArgBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FClearIndirectArgBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCullGridIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWComposeIndirectArgBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static int32 GetGroupSize()
	{
		return 1;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearIndirectArgBufferCS, "/Engine/Private/GlobalDistanceField.usf", "ClearIndirectArgBufferCS", SF_Compute);

class FBuildGridTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildGridTilesCS);
	SHADER_USE_PARAMETER_STRUCT(FBuildGridTilesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWGridTileBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWGridIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, UpdateBoundsBuffer)
		SHADER_PARAMETER(uint32, NumUpdateBounds)
		SHADER_PARAMETER(float, InfluenceRadiusSq)
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(FVector, GridCoordToWorldCenterScale)
		SHADER_PARAMETER(FVector, GridCoordToWorldCenterBias)
		SHADER_PARAMETER(FVector, TileWorldExtent)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FBuildGridTilesCS, "/Engine/Private/GlobalDistanceField.usf", "BuildGridTilesCS", SF_Compute);

class FCullObjectsToGridCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullObjectsToGridCS);
	SHADER_USE_PARAMETER_STRUCT(FCullObjectsToGridCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCullGridAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCullGridObjectHeader)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCullGridObjectArray)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, CullGridIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CullGridTileBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ObjectIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ObjectIndexNumBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectBounds)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
		SHADER_PARAMETER(FIntVector, CullGridResolution)
		SHADER_PARAMETER(FVector, CullGridCoordToWorldCenterScale)
		SHADER_PARAMETER(FVector, CullGridCoordToWorldCenterBias)
		SHADER_PARAMETER(FVector, CullTileWorldExtent)
		SHADER_PARAMETER(float, InfluenceRadiusSq)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("CULL_GRID_TILE_SIZE"), GCullGridTileSize);
		OutEnvironment.SetDefine(TEXT("MAX_GRID_CULLED_DF_OBJECTS"), GMaxGridCulledObjects);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCullObjectsToGridCS, "/Engine/Private/GlobalDistanceField.usf", "CullObjectsToGridCS", SF_Compute);

class FComposeObjectDistanceFieldsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComposeObjectDistanceFieldsCS);
	SHADER_USE_PARAMETER_STRUCT(FComposeObjectDistanceFieldsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWGlobalDistanceFieldTexture)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, ComposeIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ComposeTileBuffer)
		SHADER_PARAMETER_TEXTURE(Texture3D, DistanceFieldTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DistanceFieldSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, ParentGlobalDistanceFieldTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CullGridObjectHeader)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CullGridObjectArray)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ObjectIndexNumBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ObjectIndexBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectBounds)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
		SHADER_PARAMETER(float, InfluenceRadius)
		SHADER_PARAMETER(float, InfluenceRadiusSq)
		SHADER_PARAMETER(uint32, NumSceneObjects)
		SHADER_PARAMETER(FIntVector, CullGridResolution)
		SHADER_PARAMETER(FIntVector, GlobalDistanceFieldScrollOffset)
		SHADER_PARAMETER(FIntVector, ClipmapResolution)
		SHADER_PARAMETER(FVector, VoxelCoordToWorldVoxelCenterScale)
		SHADER_PARAMETER(FVector, VoxelCoordToWorldVoxelCenterBias)
		SHADER_PARAMETER(FVector, ComposeGridCoordToWorldCenterScale)
		SHADER_PARAMETER(FVector, ComposeGridCoordToWorldCenterBias)
		SHADER_PARAMETER(FVector, ComposeTileWorldExtent)
	END_SHADER_PARAMETER_STRUCT()

	class FComposeParentDistanceField : SHADER_PERMUTATION_BOOL("COMPOSE_PARENT_DISTANCE_FIELD");
	using FPermutationDomain = TShaderPermutationDomain<FComposeParentDistanceField>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(4, 4, 4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("CULL_GRID_TILE_SIZE"), GCullGridTileSize);
		OutEnvironment.SetDefine(TEXT("MAX_GRID_CULLED_DF_OBJECTS"), GMaxGridCulledObjects);
		OutEnvironment.SetDefine(TEXT("COMPOSITE_THREADGROUP_SIZEX"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("COMPOSITE_THREADGROUP_SIZEY"), GetGroupSize().Y);
		OutEnvironment.SetDefine(TEXT("COMPOSITE_THREADGROUP_SIZEZ"), GetGroupSize().Z);
	}
};

IMPLEMENT_GLOBAL_SHADER(FComposeObjectDistanceFieldsCS, "/Engine/Private/GlobalDistanceField.usf", "ComposeObjectDistanceFieldsCS", SF_Compute);

/** 
 * Updates the global distance field for a view.  
 * Typically issues updates for just the newly exposed regions of the volume due to camera movement.
 * In the worst case of a camera cut or large distance field scene changes, a full update of the global distance field will be done.
 **/
void UpdateGlobalDistanceFieldVolume(
	FRHICommandListImmediate& RHICmdList, 
	FViewInfo& View, 
	FScene* Scene, 
	float MaxOcclusionDistance, 
	FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
{
	SCOPED_GPU_STAT(RHICmdList, GlobalDistanceFieldUpdate);

	extern float GAOConeHalfAngle;
	const float GlobalMaxSphereQueryRadius = MaxOcclusionDistance / (1 + FMath::Tan(GAOConeHalfAngle));
	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	UpdateGlobalDistanceFieldViewOrigin(View);

	if (Scene->DistanceFieldSceneData.NumObjectsInBuffer > 0)
	{
		const int32 NumClipmaps = FMath::Clamp<int32>(GetNumGlobalDistanceFieldClipmaps(), 0, GMaxGlobalDistanceFieldClipmaps);
		ComputeUpdateRegionsAndUpdateViewState(RHICmdList, View, Scene, GlobalDistanceFieldInfo, NumClipmaps, MaxOcclusionDistance);

		// Recreate the view uniform buffer now that we have updated GlobalDistanceFieldInfo
		View.SetupGlobalDistanceFieldUniformBufferParameters(*View.CachedViewUniformShaderParameters);
		View.ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*View.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
		Scene->UniformBuffers.InvalidateCachedView();

		bool bHasUpdateRegions = false;

		for (int32 ClipmapIndex = 0; ClipmapIndex < GlobalDistanceFieldInfo.Clipmaps.Num(); ClipmapIndex++)
		{
			bHasUpdateRegions = bHasUpdateRegions || GlobalDistanceFieldInfo.Clipmaps[ClipmapIndex].UpdateRegions.Num() > 0;
		}

		for (int32 ClipmapIndex = 0; ClipmapIndex < GlobalDistanceFieldInfo.MostlyStaticClipmaps.Num(); ClipmapIndex++)
		{
			bHasUpdateRegions = bHasUpdateRegions || GlobalDistanceFieldInfo.MostlyStaticClipmaps[ClipmapIndex].UpdateRegions.Num() > 0;
		}

		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("UpdateGlobalDistanceFieldVolume"));

		if (bHasUpdateRegions && GAOUpdateGlobalDistanceField)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "UpdateGlobalDistanceField");

			const uint32 MaxCullGridDimension = GAOGlobalDFResolution / GCullGridTileSize;

			const FGlobalDFCacheType StartCacheType = GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? GDF_MostlyStatic : GDF_Full;

			// Register GlobalDistanceFieldTexture
			FRDGTextureRef GlobalDistanceFieldTextures[GDF_Num][GMaxGlobalDistanceFieldClipmaps];
			for (int32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
			{
				TArray<FGlobalDistanceFieldClipmap>& Clipmaps = CacheType == GDF_MostlyStatic
					? GlobalDistanceFieldInfo.MostlyStaticClipmaps
					: GlobalDistanceFieldInfo.Clipmaps;

				for (int32 ClipmapIndex = 0; ClipmapIndex < Clipmaps.Num(); ClipmapIndex++)
				{
					FGlobalDistanceFieldClipmap& Clipmap = Clipmaps[ClipmapIndex];

					GlobalDistanceFieldTextures[CacheType][ClipmapIndex] = GraphBuilder.RegisterExternalTexture(Clipmap.RenderTarget, TEXT("GlobalDistanceFieldTexture"));
				}
			}

			for (int32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
			{
				TArray<FGlobalDistanceFieldClipmap>& Clipmaps = CacheType == GDF_MostlyStatic
					? GlobalDistanceFieldInfo.MostlyStaticClipmaps
					: GlobalDistanceFieldInfo.Clipmaps;

				for (int32 ClipmapIndex = 0; ClipmapIndex < Clipmaps.Num(); ClipmapIndex++)
				{
					RDG_EVENT_SCOPE(GraphBuilder, "Clipmap:%d CacheType:%s", ClipmapIndex, CacheType == GDF_MostlyStatic ? TEXT("MostlyStatic") : TEXT("Movable"));

					FGlobalDistanceFieldClipmap& Clipmap = Clipmaps[ClipmapIndex];

					FRDGTextureRef GlobalDistanceFieldTexture = GlobalDistanceFieldTextures[CacheType][ClipmapIndex];
					FRDGTextureRef ParentDistanceFieldTexture = nullptr;

					if (CacheType == GDF_Full && GAOGlobalDistanceFieldCacheMostlyStaticSeparately && GlobalDistanceFieldTextures[GDF_MostlyStatic][ClipmapIndex])
					{
						ParentDistanceFieldTexture = GlobalDistanceFieldTextures[GDF_MostlyStatic][ClipmapIndex];
					}

					int32 MaxSDFMeshObjects = FMath::RoundUpToPowerOfTwo(DistanceFieldSceneData.NumObjectsInBuffer);
					FRDGBufferRef ObjectIndexBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxSDFMeshObjects), TEXT("ObjectIndices"));
					FRDGBufferRef ObjectIndexNumBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("ObjectIndexNum"));

					// Upload update bounds data
					FRDGBufferRef UpdateBoundsBuffer = nullptr;
					uint32 NumUpdateBounds = 0;
					{
						const uint32 BufferStrideInFloat4 = 2;
						const uint32 BufferStride = BufferStrideInFloat4 * sizeof(FVector4);
						TArray<FVector4, SceneRenderingAllocator> UpdateBoundsData;
						UpdateBoundsData.SetNum(BufferStrideInFloat4 * Clipmap.UpdateBounds.Num());

						for (int32 UpdateBoundsIndex = 0; UpdateBoundsIndex < Clipmap.UpdateBounds.Num(); ++UpdateBoundsIndex)
						{
							const FClipmapUpdateBounds& UpdateBounds = Clipmap.UpdateBounds[UpdateBoundsIndex];

							UpdateBoundsData[NumUpdateBounds * BufferStrideInFloat4 + 0] = FVector4(UpdateBounds.Center, UpdateBounds.bExpandByInfluenceRadius ? 1.0f : 0.0f);
							UpdateBoundsData[NumUpdateBounds * BufferStrideInFloat4 + 1] = FVector4(UpdateBounds.Extent, 0.0f);
							++NumUpdateBounds;
						}

						check(UpdateBoundsData.Num() % BufferStrideInFloat4 == 0);

						UpdateBoundsBuffer = GraphBuilder.CreateBuffer(
							FRDGBufferDesc::CreateUploadDesc(sizeof(FVector4), FMath::RoundUpToPowerOfTwo(FMath::Max(UpdateBoundsData.Num(), 2))),
							TEXT("UpdateBoundsBuffer"));

						FUpdateBoundsUploadParameters* PassParameters = GraphBuilder.AllocParameters<FUpdateBoundsUploadParameters>();
						PassParameters->UpdateBoundsBuffer = UpdateBoundsBuffer;

						const uint32 UploadBytes = UpdateBoundsData.Num() * sizeof(FVector4);
						const void* UploadPtr = UpdateBoundsData.GetData();

						GraphBuilder.AddPass(
							RDG_EVENT_NAME("Upload %d update bounds", NumUpdateBounds),
							PassParameters,
							ERDGPassFlags::Copy,
							[PassParameters, UploadBytes, UploadPtr](FRHICommandListImmediate& RHICmdList)
							{
								if (UploadBytes > 0)
								{
									void* DestCardIdPtr = RHILockVertexBuffer(PassParameters->UpdateBoundsBuffer->GetRHIVertexBuffer(), 0, UploadBytes, RLM_WriteOnly);
									FPlatformMemory::Memcpy(DestCardIdPtr, UploadPtr, UploadBytes);
									RHIUnlockVertexBuffer(PassParameters->UpdateBoundsBuffer->GetRHIVertexBuffer());
								}
							});
					}

					if (NumUpdateBounds > 0)
					{
						// Cull the global objects to the update regions
						{
							uint32 AcceptOftenMovingObjectsOnlyValue = 0;

							if (!GAOGlobalDistanceFieldCacheMostlyStaticSeparately)
							{
								AcceptOftenMovingObjectsOnlyValue = 2;
							}
							else if (CacheType == GDF_Full)
							{
								// First cache is for mostly static, second contains both, inheriting static objects distance fields with a lookup
								// So only composite often moving objects into the full global distance field
								AcceptOftenMovingObjectsOnlyValue = 1;
							}

							const float VoxelWorldSize = Clipmap.Bounds.GetSize().X / GAOGlobalDFResolution;

							AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ObjectIndexNumBuffer, PF_R32_UINT), 0);

							FCullObjectsToClipmapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullObjectsToClipmapCS::FParameters>();
							PassParameters->RWObjectIndexBuffer = GraphBuilder.CreateUAV(ObjectIndexBuffer, PF_R32_UINT);
							PassParameters->RWObjectIndexNumBuffer = GraphBuilder.CreateUAV(ObjectIndexNumBuffer, PF_R32_UINT);
							PassParameters->SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
							PassParameters->SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
							PassParameters->NumSceneObjects = DistanceFieldSceneData.NumObjectsInBuffer;
							PassParameters->ClipmapWorldCenter = Clipmap.Bounds.GetCenter();
							PassParameters->ClipmapWorldExtent = Clipmap.Bounds.GetExtent();
							PassParameters->AcceptOftenMovingObjectsOnly = AcceptOftenMovingObjectsOnlyValue;
							PassParameters->MeshSDFRadiusThreshold = GetMinMeshSDFRadius(VoxelWorldSize);
							PassParameters->InfluenceRadiusSq = GlobalMaxSphereQueryRadius * GlobalMaxSphereQueryRadius;

							auto ComputeShader = View.ShaderMap->GetShader<FCullObjectsToClipmapCS>();
							const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(DistanceFieldSceneData.NumObjectsInBuffer, FCullObjectsToClipmapCS::GetGroupSize());

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("CullToClipmap"),
								ComputeShader,
								PassParameters,
								GroupSize);
						}

						const uint32 CullGridDim = GAOGlobalDFResolution / GCullGridTileSize;
						const uint32 CullGridSize = CullGridDim * CullGridDim * CullGridDim;
						const FIntVector CullGridResolution(CullGridDim, CullGridDim, CullGridDim);

						const FVector ClipmapSize = Clipmap.Bounds.GetSize();
						const FVector CullTileWorldExtent = 0.5f * ClipmapSize / FVector(CullGridResolution);
						const FVector CullGridCoordToWorldCenterScale = ClipmapSize / FVector(CullGridResolution);
						const FVector CullGridCoordToWorldCenterBias = Clipmap.Bounds.Min + CullTileWorldExtent;

						const uint32 ComposeTileSize = 4;
						const uint32 ComposeGridDim = GAOGlobalDFResolution / ComposeTileSize;
						const uint32 ComposeGridSize = ComposeGridDim * ComposeGridDim * ComposeGridDim;
						const FIntVector ComposeGridResolution(ComposeGridDim, ComposeGridDim, ComposeGridDim);

						const FVector ComposeTileWorldExtent = 0.5f * ClipmapSize / FVector(ComposeGridResolution);
						const FVector ComposeGridCoordToWorldCenterScale = ClipmapSize / FVector(ComposeGridResolution);
						const FVector ComposeGridCoordToWorldCenterBias = Clipmap.Bounds.Min + ComposeTileWorldExtent;

						FRDGBufferRef ComposeTileBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), ComposeGridSize), TEXT("ComposeTiles"));
						FRDGBufferRef CullGridTileBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CullGridSize), TEXT("CullGridTiles"));

						FRDGBufferRef ComposeIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("ComposeIndirectArgs"));
						FRDGBufferRef CullGridIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("CullGridIndirectArgs"));

						// Clear indirect dispatch arguments
						{
							FClearIndirectArgBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearIndirectArgBufferCS::FParameters>();
							PassParameters->RWComposeIndirectArgBuffer = GraphBuilder.CreateUAV(ComposeIndirectArgBuffer, PF_R32_UINT);
							PassParameters->RWCullGridIndirectArgBuffer = GraphBuilder.CreateUAV(CullGridIndirectArgBuffer, PF_R32_UINT);

							auto ComputeShader = View.ShaderMap->GetShader<FClearIndirectArgBufferCS>();

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("ClearIndirectArgBuffer"),
								ComputeShader,
								PassParameters,
								FIntVector(1, 1, 1));
						}

						// Prepare CullGrid tiles which need to be updated for update regions
						{
							FBuildGridTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildGridTilesCS::FParameters>();
							PassParameters->RWGridTileBuffer = GraphBuilder.CreateUAV(CullGridTileBuffer, PF_R32_UINT);
							PassParameters->RWGridIndirectArgBuffer = GraphBuilder.CreateUAV(CullGridIndirectArgBuffer, PF_R32_UINT);
							PassParameters->UpdateBoundsBuffer = GraphBuilder.CreateSRV(UpdateBoundsBuffer, PF_A32B32G32R32F);
							PassParameters->NumUpdateBounds = NumUpdateBounds;
							PassParameters->GridResolution = CullGridResolution;
							PassParameters->GridCoordToWorldCenterScale = CullGridCoordToWorldCenterScale;
							PassParameters->GridCoordToWorldCenterBias = CullGridCoordToWorldCenterBias;
							PassParameters->TileWorldExtent = CullTileWorldExtent;
							PassParameters->InfluenceRadiusSq = GlobalMaxSphereQueryRadius * GlobalMaxSphereQueryRadius;

							auto ComputeShader = View.ShaderMap->GetShader<FBuildGridTilesCS>();

							const FIntVector GroupSize = CullGridResolution;

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("BuildCullGridTiles %d", NumUpdateBounds),
								ComputeShader,
								PassParameters,
								GroupSize);
						}

						// Prepare Compose tiles which need to be updated for update regions
						{
							FBuildGridTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildGridTilesCS::FParameters>();
							PassParameters->RWGridTileBuffer = GraphBuilder.CreateUAV(ComposeTileBuffer, PF_R32_UINT);
							PassParameters->RWGridIndirectArgBuffer = GraphBuilder.CreateUAV(ComposeIndirectArgBuffer, PF_R32_UINT);
							PassParameters->UpdateBoundsBuffer = GraphBuilder.CreateSRV(UpdateBoundsBuffer, PF_A32B32G32R32F);
							PassParameters->NumUpdateBounds = NumUpdateBounds;
							PassParameters->GridResolution = ComposeGridResolution;
							PassParameters->GridCoordToWorldCenterScale = ComposeGridCoordToWorldCenterScale;
							PassParameters->GridCoordToWorldCenterBias = ComposeGridCoordToWorldCenterBias;
							PassParameters->TileWorldExtent = ComposeTileWorldExtent;
							PassParameters->InfluenceRadiusSq = GlobalMaxSphereQueryRadius * GlobalMaxSphereQueryRadius;

							auto ComputeShader = View.ShaderMap->GetShader<FBuildGridTilesCS>();

							const FIntVector GroupSize = ComposeGridResolution;

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("BuildComposeTiles %d", NumUpdateBounds),
								ComputeShader,
								PassParameters,
								GroupSize);
						}

						FRDGBufferRef CullGridAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("CullGridAllocator"));
						FRDGBufferRef CullGridObjectHeader = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 2 * CullGridSize), TEXT("CullGridObjectHeader"));
						FRDGBufferRef CullGridObjectArray = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), CullGridSize * GMaxGridCulledObjects), TEXT("CullGridObjectArray"));

						// Cull objects into a cull grid
						{
							AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CullGridAllocator, PF_R32_UINT), 0);
							AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CullGridObjectHeader, PF_R32_UINT), 0);

							FCullObjectsToGridCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullObjectsToGridCS::FParameters>();
							PassParameters->RWCullGridAllocator = GraphBuilder.CreateUAV(CullGridAllocator, PF_R32_UINT);
							PassParameters->RWCullGridObjectHeader = GraphBuilder.CreateUAV(CullGridObjectHeader, PF_R32_UINT);
							PassParameters->RWCullGridObjectArray = GraphBuilder.CreateUAV(CullGridObjectArray, PF_R32_UINT);
							PassParameters->CullGridIndirectArgBuffer = CullGridIndirectArgBuffer;
							PassParameters->CullGridTileBuffer = GraphBuilder.CreateSRV(CullGridTileBuffer, PF_R32_UINT);
							PassParameters->ObjectIndexBuffer = GraphBuilder.CreateSRV(ObjectIndexBuffer, PF_R32_UINT);
							PassParameters->ObjectIndexNumBuffer = GraphBuilder.CreateSRV(ObjectIndexNumBuffer, PF_R32_UINT);
							PassParameters->SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
							PassParameters->SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
							PassParameters->CullGridResolution = CullGridResolution;
							PassParameters->CullGridCoordToWorldCenterScale = CullGridCoordToWorldCenterScale;
							PassParameters->CullGridCoordToWorldCenterBias = CullGridCoordToWorldCenterBias;
							PassParameters->CullTileWorldExtent = CullTileWorldExtent;
							PassParameters->InfluenceRadiusSq = GlobalMaxSphereQueryRadius * GlobalMaxSphereQueryRadius;

							auto ComputeShader = View.ShaderMap->GetShader<FCullObjectsToGridCS>();

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("CullObjectsToGrid"),
								ComputeShader,
								PassParameters,
								CullGridIndirectArgBuffer,
								0);
						}

						// Compose the global distance field by computing the min distance from intersecting per-object distance fields
						{
							const FIntVector ClipmapResolution(GAOGlobalDFResolution, GAOGlobalDFResolution, GAOGlobalDFResolution);
							const FVector ClipmapWorldCenter = Clipmap.Bounds.GetCenter();
							const FVector ClipmapWorldExtent = Clipmap.Bounds.GetExtent();
							const FVector ClipmapVoxelSize = ClipmapSize / FVector(ClipmapResolution);
							const FVector ClipmapVoxelExtent = 0.5f * ClipmapVoxelSize;

							const FVector VoxelCoordToWorldVoxelCenterScale = ClipmapSize / FVector(ClipmapResolution);
							const FVector VoxelCoordToWorldVoxelCenterBias = Clipmap.Bounds.Min + ClipmapVoxelExtent;
							const FIntVector GlobalDistanceFieldScrollOffset = FIntVector((FVector(ClipmapResolution) * Clipmap.ScrollOffset) / ClipmapSize);

							FComposeObjectDistanceFieldsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeObjectDistanceFieldsCS::FParameters>();
							PassParameters->View = View.ViewUniformBuffer;
							PassParameters->RWGlobalDistanceFieldTexture = GraphBuilder.CreateUAV(GlobalDistanceFieldTexture);
							PassParameters->ComposeIndirectArgBuffer = ComposeIndirectArgBuffer;
							PassParameters->ComposeTileBuffer = GraphBuilder.CreateSRV(ComposeTileBuffer, PF_R32_UINT);
							PassParameters->ParentGlobalDistanceFieldTexture = ParentDistanceFieldTexture;
							PassParameters->DistanceFieldTexture = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
							PassParameters->DistanceFieldSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
							PassParameters->CullGridObjectHeader = GraphBuilder.CreateSRV(CullGridObjectHeader, PF_R32_UINT);
							PassParameters->CullGridObjectArray = GraphBuilder.CreateSRV(CullGridObjectArray, PF_R32_UINT);
							PassParameters->ObjectIndexBuffer = GraphBuilder.CreateSRV(ObjectIndexBuffer, PF_R32_UINT);
							PassParameters->ObjectIndexNumBuffer = GraphBuilder.CreateSRV(ObjectIndexNumBuffer, PF_R32_UINT);
							PassParameters->SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
							PassParameters->SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
							PassParameters->NumSceneObjects = DistanceFieldSceneData.NumObjectsInBuffer;
							PassParameters->InfluenceRadius = GlobalMaxSphereQueryRadius;
							PassParameters->InfluenceRadiusSq = GlobalMaxSphereQueryRadius * GlobalMaxSphereQueryRadius;
							PassParameters->CullGridResolution = CullGridResolution;
							PassParameters->GlobalDistanceFieldScrollOffset = GlobalDistanceFieldScrollOffset;
							PassParameters->ClipmapResolution = ClipmapResolution;
							PassParameters->VoxelCoordToWorldVoxelCenterScale = VoxelCoordToWorldVoxelCenterScale;
							PassParameters->VoxelCoordToWorldVoxelCenterBias = VoxelCoordToWorldVoxelCenterBias;
							PassParameters->ComposeGridCoordToWorldCenterScale = ComposeGridCoordToWorldCenterScale;
							PassParameters->ComposeGridCoordToWorldCenterBias = ComposeGridCoordToWorldCenterBias;
							PassParameters->ComposeTileWorldExtent = ComposeTileWorldExtent;

							FComposeObjectDistanceFieldsCS::FPermutationDomain PermutationVector;
							PermutationVector.Set<FComposeObjectDistanceFieldsCS::FComposeParentDistanceField>(ParentDistanceFieldTexture != nullptr);
							auto ComputeShader = View.ShaderMap->GetShader<FComposeObjectDistanceFieldsCS>(PermutationVector);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("ComposeObjects"),
								ComputeShader,
								PassParameters,
								ComposeIndirectArgBuffer,
								0);
						}
					}

					if (CacheType == GDF_MostlyStatic || !GAOGlobalDistanceFieldCacheMostlyStaticSeparately)
					{
						RDG_EVENT_SCOPE(GraphBuilder, "ComposeHeightfields");

						for (int32 UpdateRegionIndex = 0; UpdateRegionIndex < Clipmap.UpdateRegions.Num(); UpdateRegionIndex++)
						{
							const FVolumeUpdateRegion& UpdateRegion = Clipmap.UpdateRegions[UpdateRegionIndex];

							if (UpdateRegion.UpdateType & VUT_Heightfields)
							{
								View.HeightfieldLightingViewInfo.ComposeHeightfieldsIntoGlobalDistanceField(
									GraphBuilder,
									Scene,
									View,
									GlobalDistanceFieldTexture,
									GlobalMaxSphereQueryRadius,
									GlobalDistanceFieldInfo,
									Clipmap,
									ClipmapIndex,
									UpdateRegion);
							}
						}
					}
				}
			}

			// Extract GlobalDistanceFieldTexture
			for (int32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
			{
				TArray<FGlobalDistanceFieldClipmap>& Clipmaps = CacheType == GDF_MostlyStatic
					? GlobalDistanceFieldInfo.MostlyStaticClipmaps
					: GlobalDistanceFieldInfo.Clipmaps;

				for (int32 ClipmapIndex = 0; ClipmapIndex < Clipmaps.Num(); ClipmapIndex++)
				{
					FGlobalDistanceFieldClipmap& Clipmap = Clipmaps[ClipmapIndex];

					if (GlobalDistanceFieldTextures[CacheType][ClipmapIndex])
					{
						GraphBuilder.QueueTextureExtraction(GlobalDistanceFieldTextures[CacheType][ClipmapIndex], &Clipmap.RenderTarget);
					}
				}
			}
		}

		GraphBuilder.Execute();
	}

	if (GDFReadbackRequest && GlobalDistanceFieldInfo.Clipmaps.Num() > 0)
	{
		// Read back a clipmap
		ReadbackDistanceFieldClipmap(RHICmdList, GlobalDistanceFieldInfo);
	}

	if (GDFReadbackRequest && GlobalDistanceFieldInfo.Clipmaps.Num() > 0)
	{
		// Read back a clipmap
		ReadbackDistanceFieldClipmap(RHICmdList, GlobalDistanceFieldInfo);
	}
}
