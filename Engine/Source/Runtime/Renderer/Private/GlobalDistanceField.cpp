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
#include "Lumen/Lumen.h"
#include "GlobalDistanceFieldHeightfields.h"

DECLARE_GPU_STAT(GlobalDistanceFieldUpdate);

int32 GAOGlobalDistanceField = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceField(
	TEXT("r.AOGlobalDistanceField"), 
	GAOGlobalDistanceField,
	TEXT("Whether to use a global distance field to optimize occlusion cone traces.\n")
	TEXT("The global distance field is created by compositing object distance fields into clipmaps as the viewer moves through the level."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GGlobalDistanceFieldOccupancyRatio = 0.3f;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldOccupancyRatio(
	TEXT("r.AOGlobalDistanceField.OccupancyRatio"),
	GGlobalDistanceFieldOccupancyRatio,
	TEXT("Expected sparse global distacne field occupancy for the page atlas allocation. 0.25 means 25% - filled and 75% - empty."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GAOGlobalDistanceFieldNumClipmaps = 4;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldNumClipmaps(
	TEXT("r.AOGlobalDistanceField.NumClipmaps"), 
	GAOGlobalDistanceFieldNumClipmaps,
	TEXT("Num clipmaps in the global distance field.  Setting this to anything other than 4 is currently only supported by Lumen."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAOUpdateGlobalDistanceField = 1;
FAutoConsoleVariableRef CVarAOUpdateGlobalDistanceField(
	TEXT("r.AOUpdateGlobalDistanceField"),
	GAOUpdateGlobalDistanceField,
	TEXT("Whether to update the global distance field, useful for debugging."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldCacheMostlyStaticSeparately = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldCacheMostlyStaticSeparately(
	TEXT("r.AOGlobalDistanceFieldCacheMostlyStaticSeparately"),
	GAOGlobalDistanceFieldCacheMostlyStaticSeparately,
	TEXT("Whether to cache mostly static primitives separately from movable primitives, which reduces global DF update cost when a movable primitive is modified.  Adds another 12Mb of volume textures."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldPartialUpdates = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldPartialUpdates(
	TEXT("r.AOGlobalDistanceFieldPartialUpdates"),
	GAOGlobalDistanceFieldPartialUpdates,
	TEXT("Whether to allow partial updates of the global distance field.  When profiling it's useful to disable this and get the worst case composition time that happens on camera cuts."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldStaggeredUpdates = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldStaggeredUpdatess(
	TEXT("r.AOGlobalDistanceFieldStaggeredUpdates"),
	GAOGlobalDistanceFieldStaggeredUpdates,
	TEXT("Whether to allow the larger clipmaps to be updated less frequently."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldClipmapUpdatesPerFrame = 2;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldClipmapUpdatesPerFrame(
	TEXT("r.AOGlobalDistanceFieldClipmapUpdatesPerFrame"),
	GAOGlobalDistanceFieldClipmapUpdatesPerFrame,
	TEXT("How many clipmaps to update each frame, only 1 or 2 supported.  With values less than 2, the first clipmap is only updated every other frame, which can cause incorrect self occlusion during movement."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldForceFullUpdate = 0;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldForceFullUpdate(
	TEXT("r.AOGlobalDistanceFieldForceFullUpdate"),
	GAOGlobalDistanceFieldForceFullUpdate,
	TEXT("Whether to force full global distance field update every frame."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GAOGlobalDistanceFieldForceMovementUpdate = 0;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldForceMovementUpdate(
	TEXT("r.AOGlobalDistanceFieldForceMovementUpdate"),
	GAOGlobalDistanceFieldForceMovementUpdate,
	TEXT("Whether to force N texel border on X, Y and Z update each frame."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GAOLogGlobalDistanceFieldModifiedPrimitives = 0;
FAutoConsoleVariableRef CVarAOLogGlobalDistanceFieldModifiedPrimitives(
	TEXT("r.AOGlobalDistanceFieldLogModifiedPrimitives"),
	GAOLogGlobalDistanceFieldModifiedPrimitives,
	TEXT("Whether to log primitive modifications (add, remove, updatetransform) that caused an update of the global distance field.\n")
	TEXT("This can be useful for tracking down why updating the global distance field is always costing a lot, since it should be mostly cached.\n")
	TEXT("Pass 2 to log only non movable object updates."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAODrawGlobalDistanceFieldModifiedPrimitives = 0;
FAutoConsoleVariableRef CVarAODrawGlobalDistanceFieldModifiedPrimitives(
	TEXT("r.AOGlobalDistanceFieldDrawModifiedPrimitives"),
	GAODrawGlobalDistanceFieldModifiedPrimitives,
	TEXT("Whether to lodrawg primitive modifications (add, remove, updatetransform) that caused an update of the global distance field.\n")
	TEXT("This can be useful for tracking down why updating the global distance field is always costing a lot, since it should be mostly cached."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GAOGlobalDFClipmapDistanceExponent = 2;
FAutoConsoleVariableRef CVarAOGlobalDFClipmapDistanceExponent(
	TEXT("r.AOGlobalDFClipmapDistanceExponent"),
	GAOGlobalDFClipmapDistanceExponent,
	TEXT("Exponent used to derive each clipmap's size, together with r.AOInnerGlobalDFClipmapDistance."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAOGlobalDFResolution = 128;
FAutoConsoleVariableRef CVarAOGlobalDFResolution(
	TEXT("r.AOGlobalDFResolution"),
	GAOGlobalDFResolution,
	TEXT("Resolution of the global distance field.  Higher values increase fidelity but also increase memory and composition cost."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GAOGlobalDFStartDistance = 100;
FAutoConsoleVariableRef CVarAOGlobalDFStartDistance(
	TEXT("r.AOGlobalDFStartDistance"),
	GAOGlobalDFStartDistance,
	TEXT("World space distance along a cone trace to switch to using the global distance field instead of the object distance fields.\n")
	TEXT("This has to be large enough to hide the low res nature of the global distance field, but smaller values result in faster cone tracing."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GAOGlobalDistanceFieldRepresentHeightfields = 1;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldRepresentHeightfields(
	TEXT("r.AOGlobalDistanceFieldRepresentHeightfields"),
	GAOGlobalDistanceFieldRepresentHeightfields,
	TEXT("Whether to put landscape in the global distance field.  Changing this won't propagate until the global distance field gets recached (fly away and back)."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GGlobalDistanceFieldHeightFieldThicknessScale = 4.0f;
FAutoConsoleVariableRef CVarGlobalDistanceFieldHeightFieldThicknessScale(
	TEXT("r.GlobalDistanceFieldHeightFieldThicknessScale"),
	GGlobalDistanceFieldHeightFieldThicknessScale,
	TEXT("Thickness of the height field when it's entered into the global distance field, measured in distance field voxels. Defaults to 4 which means 4x the voxel size as thickness."),
	ECVF_Scalability | ECVF_RenderThreadSafe
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
	ECVF_Scalability | ECVF_RenderThreadSafe
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

int32 GAOGlobalDistanceFieldAverageCulledObjectsPerPage = 512;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldAverageCulledObjectsPerPage(
	TEXT("r.AOGlobalDistanceField.AverageCulledObjectsPerPage"),
	GAOGlobalDistanceFieldAverageCulledObjectsPerPage,
	TEXT("Average expected number of objects per page, used to preallocate memory for the cull grid."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GAOGlobalDistanceFieldMipFactor = 4;
FAutoConsoleVariableRef CVarAOGlobalDistanceFieldMipFactor(
	TEXT("r.AOGlobalDistanceField.MipFactor"),
	GAOGlobalDistanceFieldMipFactor,
	TEXT("Resolution divider for the mip map of a distance field clipmap."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

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

// Global Distance Field Pages
// Must match GlobalDistanceFieldShared.ush
const int32 GGlobalDistanceFieldPageResolutionInAtlas = 16; // Includes 1 texel bilinear filter margin
const int32 GGlobalDistanceFieldPageResolution = GGlobalDistanceFieldPageResolutionInAtlas - 2;
const int32 GGlobalDistanceFieldPageAtlasSizeInPagesX = 32;
const int32 GGlobalDistanceFieldPageAtlasSizeInPagesY = 32;
const int32 GGlobalDistanceFieldInfluenceRangeInVoxels = 4;

namespace GlobalDistanceField
{
	int32 GetClipmapResolution();
	int32 GetMipFactor();
	int32 GetClipmapMipResolution();
	float GetClipmapExtent(int32 ClipmapIndex, const FScene* Scene);
	FIntVector GetPageAtlasSizeInPages();
	FIntVector GetPageAtlasSize();
	uint32 GetPageTableClipmapResolution();
	FIntVector GetPageTableTextureResolution();
	int32 GetMaxPageNum();
};

int32 GlobalDistanceField::GetClipmapResolution()
{
	int32 DFResolution = GAOGlobalDFResolution;

	if (Lumen::ShouldPrepareGlobalDistanceField(GMaxRHIShaderPlatform))
	{
		DFResolution = Lumen::GetGlobalDFResolution();
	}

	return FMath::DivideAndRoundUp(DFResolution, GGlobalDistanceFieldPageResolution) * GGlobalDistanceFieldPageResolution;
}

int32 GlobalDistanceField::GetMipFactor()
{
	return FMath::Clamp(GAOGlobalDistanceFieldMipFactor, 1, 8);
}

int32 GlobalDistanceField::GetClipmapMipResolution()
{
	const int32 ClipmapResolutution = GetClipmapResolution();
	return FMath::DivideAndRoundUp(GlobalDistanceField::GetClipmapResolution(), GetMipFactor());
}

float GlobalDistanceField::GetClipmapExtent(int32 ClipmapIndex, const FScene* Scene)
{
	if (Lumen::ShouldPrepareGlobalDistanceField(GMaxRHIShaderPlatform))
	{
		const float InnerClipmapDistance = Lumen::GetGlobalDFClipmapExtent();
		return InnerClipmapDistance * FMath::Pow(2, ClipmapIndex);
	}
	else
	{
		const float InnerClipmapDistance = Scene->GlobalDistanceFieldViewDistance / FMath::Pow(GAOGlobalDFClipmapDistanceExponent, 3);
		return InnerClipmapDistance * FMath::Pow(GAOGlobalDFClipmapDistanceExponent, ClipmapIndex);
	}
}

uint32 GlobalDistanceField::GetPageTableClipmapResolution()
{
	return FMath::DivideAndRoundUp(GlobalDistanceField::GetClipmapResolution(), GGlobalDistanceFieldPageResolution);
}

FIntVector GlobalDistanceField::GetPageTableTextureResolution()
{
	const int32 NumClipmaps = GetNumGlobalDistanceFieldClipmaps();
	const uint32 PageTableClipmapResolution = GetPageTableClipmapResolution();

	const FIntVector PageTableTextureResolution = FIntVector(
		PageTableClipmapResolution, 
		PageTableClipmapResolution, 
		PageTableClipmapResolution * NumClipmaps);

	return PageTableTextureResolution;
}

FIntVector GlobalDistanceField::GetPageAtlasSizeInPages()
{
	const FIntVector PageTableTextureResolution = GetPageTableTextureResolution();

	const int32 RequiredNumberOfPages = FMath::CeilToInt(
		PageTableTextureResolution.X * PageTableTextureResolution.Y * PageTableTextureResolution.Z 
		* (GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? 2 : 1)
		* FMath::Clamp(GGlobalDistanceFieldOccupancyRatio, 0.1f, 1.0f));

	const int32 RequiredNumberOfPagesInZ = FMath::DivideAndRoundUp(RequiredNumberOfPages, GGlobalDistanceFieldPageAtlasSizeInPagesX * GGlobalDistanceFieldPageAtlasSizeInPagesY);

	const FIntVector PageAtlasTextureSizeInPages = FIntVector(
		GGlobalDistanceFieldPageAtlasSizeInPagesX,
		GGlobalDistanceFieldPageAtlasSizeInPagesY,
		RequiredNumberOfPagesInZ);

	return PageAtlasTextureSizeInPages;
}

FIntVector GlobalDistanceField::GetPageAtlasSize()
{
	const FIntVector PageAtlasTextureSizeInPages = GlobalDistanceField::GetPageAtlasSizeInPages();
	return PageAtlasTextureSizeInPages * GGlobalDistanceFieldPageResolutionInAtlas;
}

int32 GlobalDistanceField::GetMaxPageNum()
{
	const FIntVector PageAtlasTextureSizeInPages = GlobalDistanceField::GetPageAtlasSizeInPages();
	int32 MaxPageNum = PageAtlasTextureSizeInPages.X * PageAtlasTextureSizeInPages.Y * PageAtlasTextureSizeInPages.Z;
	ensureMsgf(MaxPageNum < UINT16_MAX, TEXT("Page index is stored in a uint16, and 0xFFFF is reserved as invalid."));
	return MaxPageNum;
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
	ParameterData.PageTableTexture = nullptr;
	ParameterData.PageAtlasTexture = nullptr;
	ParameterData.MipTexture = nullptr;
	ParameterData.MaxPageNum = GlobalDistanceField::GetMaxPageNum();

	if (Clipmaps.Num() > 0)
	{
		if (PageAtlasTexture)
		{
			ParameterData.PageAtlasTexture = PageAtlasTexture->GetRenderTargetItem().ShaderResourceTexture;
		}

		if (PageTableCombinedTexture)
		{
			ParameterData.PageTableTexture = PageTableCombinedTexture->GetRenderTargetItem().ShaderResourceTexture;
		}

		if (MipTexture)
		{
			ParameterData.MipTexture = MipTexture->GetRenderTargetItem().ShaderResourceTexture;
		}

		for (int32 ClipmapIndex = 0; ClipmapIndex < GMaxGlobalDistanceFieldClipmaps; ClipmapIndex++)
		{
			if (ClipmapIndex < Clipmaps.Num())
			{
				const FGlobalDistanceFieldClipmap& Clipmap = Clipmaps[ClipmapIndex];
				ParameterData.CenterAndExtent[ClipmapIndex] = FVector4(Clipmap.Bounds.GetCenter(), Clipmap.Bounds.GetExtent().X);

				// GlobalUV = (WorldPosition - GlobalVolumeCenterAndExtent[ClipmapIndex].xyz + GlobalVolumeScollOffset[ClipmapIndex].xyz) / (GlobalVolumeCenterAndExtent[ClipmapIndex].w * 2) + .5f;
				// WorldToUVMul = 1.0f / (GlobalVolumeCenterAndExtent[ClipmapIndex].w * 2)
				// WorldToUVAdd = (GlobalVolumeScollOffset[ClipmapIndex].xyz - GlobalVolumeCenterAndExtent[ClipmapIndex].xyz) / (GlobalVolumeCenterAndExtent[ClipmapIndex].w * 2) + .5f
				const FVector WorldToUVAdd = (Clipmap.ScrollOffset - Clipmap.Bounds.GetCenter()) / (Clipmap.Bounds.GetExtent().X * 2) + FVector(.5f);
				ParameterData.WorldToUVAddAndMul[ClipmapIndex] = FVector4(WorldToUVAdd, 1.0f / (Clipmap.Bounds.GetExtent().X * 2));

				ParameterData.MipWorldToUVScale[ClipmapIndex] = FVector(1.0f) / (2.0f * Clipmap.Bounds.GetExtent());
				ParameterData.MipWorldToUVBias[ClipmapIndex] = (-Clipmap.Bounds.Min) / (2.0f * Clipmap.Bounds.GetExtent());

				ParameterData.MipWorldToUVScale[ClipmapIndex].Z = ParameterData.MipWorldToUVScale[ClipmapIndex].Z / Clipmaps.Num();
				ParameterData.MipWorldToUVBias[ClipmapIndex].Z = (ParameterData.MipWorldToUVBias[ClipmapIndex].Z + ClipmapIndex) / Clipmaps.Num();

				ParameterData.PageTableScrollOffset[ClipmapIndex] = Clipmap.ScrollOffset / Clipmap.Bounds.GetSize();
			}
			else
			{
				ParameterData.CenterAndExtent[ClipmapIndex] = FVector4(0);
				ParameterData.WorldToUVAddAndMul[ClipmapIndex] = FVector4(0);
				ParameterData.MipWorldToUVScale[ClipmapIndex] = FVector(0);
				ParameterData.MipWorldToUVBias[ClipmapIndex] = FVector(0);
				ParameterData.PageTableScrollOffset[ClipmapIndex] = FVector(0);
			}
		}

		ParameterData.MipFactor = GlobalDistanceField::GetMipFactor();
		ParameterData.MipTransition = (GGlobalDistanceFieldInfluenceRangeInVoxels + ParameterData.MipFactor / GGlobalDistanceFieldInfluenceRangeInVoxels) / (2.0f * GGlobalDistanceFieldInfluenceRangeInVoxels);
		ParameterData.ClipmapSizeInPages = GlobalDistanceField::GetPageTableTextureResolution().X;
		ParameterData.InvPageAtlasSize = FVector(1.0f) / FVector(GlobalDistanceField::GetPageAtlasSize());
		ParameterData.GlobalDFResolution = GlobalDistanceField::GetClipmapResolution();

		extern float GAOConeHalfAngle;
		const float MaxClipmapExtentX = Clipmaps[Clipmaps.Num() - 1].Bounds.GetExtent().X;
		const float MaxClipmapVoxelSize =  (2.0f * MaxClipmapExtentX) / GlobalDistanceField::GetClipmapResolution();
		float MaxClipmapInfluenceRadius = GGlobalDistanceFieldInfluenceRangeInVoxels * MaxClipmapVoxelSize;
		const float GlobalMaxSphereQueryRadius = FMath::Min(MaxOcclusionDistance / (1.0f + FMath::Tan(GAOConeHalfAngle)), MaxClipmapInfluenceRadius);
		ParameterData.MaxDFAOConeDistance = GlobalMaxSphereQueryRadius;
		ParameterData.NumGlobalSDFClipmaps = Clipmaps.Num();
	}
	else
	{
		FPlatformMemory::Memzero(&ParameterData, sizeof(ParameterData));
	}

	bInitialized = true;
}

/** Constructs and adds an update region based on camera movement for the given axis. */
static void AddUpdateBoundsForAxis(FIntVector MovementInPages,
	const FBox& ClipmapBounds,
	float ClipmapPageSize,
	int32 ComponentIndex, 
	TArray<FClipmapUpdateBounds, TInlineAllocator<64>>& UpdateBounds)
{
	FBox AxisUpdateBounds = ClipmapBounds;

	if (MovementInPages[ComponentIndex] > 0)
	{
		// Positive axis movement, set the min of that axis to contain the newly exposed area
		AxisUpdateBounds.Min[ComponentIndex] = FMath::Max(ClipmapBounds.Max[ComponentIndex] - MovementInPages[ComponentIndex] * ClipmapPageSize, ClipmapBounds.Min[ComponentIndex]);
	}
	else if (MovementInPages[ComponentIndex] < 0)
	{
		// Negative axis movement, set the max of that axis to contain the newly exposed area
		AxisUpdateBounds.Max[ComponentIndex] = FMath::Min(ClipmapBounds.Min[ComponentIndex] - MovementInPages[ComponentIndex] * ClipmapPageSize, ClipmapBounds.Max[ComponentIndex]);
	}

	if (FMath::Abs(MovementInPages[ComponentIndex]) > 0)
	{
		const FVector CellCenterAndBilinearFootprintBias = FVector((1.0f - 0.5f) * ClipmapPageSize);
		UpdateBounds.Add(FClipmapUpdateBounds(AxisUpdateBounds.GetCenter(), AxisUpdateBounds.GetExtent() + CellCenterAndBilinearFootprintBias, false));
	}
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
				const float LargestVoxelClipmapExtent = GlobalDistanceField::GetClipmapExtent(NumClipmaps - 1, Scene);
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
			const float ClipmapExtent = GlobalDistanceField::GetClipmapExtent(ClipmapIndex, Scene);
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

		FViewElementPDI ViewPDI(&View, nullptr, &View.DynamicPrimitiveCollector);

		bool bSharedDataReallocated = false;

		GlobalDistanceFieldInfo.PageFreeListAllocatorBuffer = nullptr;
		GlobalDistanceFieldInfo.PageFreeListBuffer = nullptr;
		GlobalDistanceFieldInfo.PageAtlasTexture = nullptr;

		if (View.ViewState)
		{
			FSceneViewState& ViewState = *View.ViewState;

			const int32 MaxPageNum = GlobalDistanceField::GetMaxPageNum();
			const FIntVector PageAtlasTextureSize = GlobalDistanceField::GetPageAtlasSize();

			if (!ViewState.GlobalDistanceFieldPageFreeListAllocatorBuffer)
			{
				GetPooledFreeBuffer(RHICmdList, FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), ViewState.GlobalDistanceFieldPageFreeListAllocatorBuffer, TEXT("PageFreeListAllocator"));
			}

			if (!ViewState.GlobalDistanceFieldPageFreeListBuffer
				|| ViewState.GlobalDistanceFieldPageFreeListBuffer->Desc.NumElements != MaxPageNum)
			{
				GetPooledFreeBuffer(RHICmdList, FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxPageNum), ViewState.GlobalDistanceFieldPageFreeListBuffer, TEXT("PageFreeList"));
			}

			if (!ViewState.GlobalDistanceFieldPageAtlasTexture
				|| ViewState.GlobalDistanceFieldPageAtlasTexture->GetDesc().Extent.X != PageAtlasTextureSize.X
				|| ViewState.GlobalDistanceFieldPageAtlasTexture->GetDesc().Extent.Y != PageAtlasTextureSize.Y
				|| ViewState.GlobalDistanceFieldPageAtlasTexture->GetDesc().Depth != PageAtlasTextureSize.Z)
			{
				FPooledRenderTargetDesc VolumeDesc = FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
					PageAtlasTextureSize.X,
					PageAtlasTextureSize.Y,
					PageAtlasTextureSize.Z,
					PF_R8,
					FClearValueBinding::None,
					TexCreate_None,
					// TexCreate_ReduceMemoryWithTilingMode used because 128^3 texture comes out 4x bigger on PS4 with recommended volume texture tiling modes
					TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_ReduceMemoryWithTilingMode | TexCreate_3DTiling,
					false));
				VolumeDesc.AutoWritable = false;

				GRenderTargetPool.FindFreeElement(
					RHICmdList,
					VolumeDesc,
					ViewState.GlobalDistanceFieldPageAtlasTexture,
					TEXT("GlobalDistanceFieldPageAtlas"),
					ERenderTargetTransience::NonTransient
				);

				bSharedDataReallocated = true;
			}

			GlobalDistanceFieldInfo.PageFreeListAllocatorBuffer = ViewState.GlobalDistanceFieldPageFreeListAllocatorBuffer;
			GlobalDistanceFieldInfo.PageFreeListBuffer = ViewState.GlobalDistanceFieldPageFreeListBuffer;
			GlobalDistanceFieldInfo.PageAtlasTexture = ViewState.GlobalDistanceFieldPageAtlasTexture;
		}

		{
			const FIntVector PageTableTextureResolution = GlobalDistanceField::GetPageTableTextureResolution();
			TRefCountPtr<IPooledRenderTarget>& PageTableTexture = View.ViewState->GlobalDistanceFieldPageTableCombinedTexture;

			if (!PageTableTexture
					|| PageTableTexture->GetDesc().Extent.X != PageTableTextureResolution.X
					|| PageTableTexture->GetDesc().Extent.Y != PageTableTextureResolution.Y
					|| PageTableTexture->GetDesc().Depth != PageTableTextureResolution.Z)
			{
					FPooledRenderTargetDesc VolumeDesc = FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
						PageTableTextureResolution.X,
						PageTableTextureResolution.Y,
						PageTableTextureResolution.Z,
						PF_R16_UINT,
						FClearValueBinding::None,
						TexCreate_None,
						TexCreate_ShaderResource | TexCreate_UAV | TexCreate_ReduceMemoryWithTilingMode | TexCreate_3DTiling,
						false));
					VolumeDesc.AutoWritable = false;

					GRenderTargetPool.FindFreeElement(
						RHICmdList,
						VolumeDesc,
						PageTableTexture,
						TEXT("DistanceFieldPageTableCombined"),
						ERenderTargetTransience::NonTransient
					);

					bSharedDataReallocated = true;
			}

			GlobalDistanceFieldInfo.PageTableCombinedTexture = PageTableTexture;
		}

		{
			const int32 ClipmapMipResolution = GlobalDistanceField::GetClipmapMipResolution();
			const FIntVector MipTextureResolution = FIntVector(ClipmapMipResolution, ClipmapMipResolution, ClipmapMipResolution * GetNumGlobalDistanceFieldClipmaps());
			TRefCountPtr<IPooledRenderTarget>& MipTexture = View.ViewState->GlobalDistanceFieldMipTexture;

			if (!MipTexture
				|| MipTexture->GetDesc().Extent.X != MipTextureResolution.X
				|| MipTexture->GetDesc().Extent.Y != MipTextureResolution.Y
				|| MipTexture->GetDesc().Depth != MipTextureResolution.Z)
			{
				FPooledRenderTargetDesc VolumeDesc = FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
					MipTextureResolution.X,
					MipTextureResolution.Y,
					MipTextureResolution.Z,
					PF_R8,
					FClearValueBinding::None,
					TexCreate_None,
					TexCreate_ShaderResource | TexCreate_UAV | TexCreate_ReduceMemoryWithTilingMode | TexCreate_3DTiling,
					false));
				VolumeDesc.AutoWritable = false;

				GRenderTargetPool.FindFreeElement(
					RHICmdList,
					VolumeDesc,
					MipTexture,
					TEXT("GlobalSDFMipTexture"),
					ERenderTargetTransience::NonTransient
				);

				bSharedDataReallocated = true;
			}

			GlobalDistanceFieldInfo.MipTexture = MipTexture;
		}

		for (uint32 CacheType = 0; CacheType < GDF_Num; CacheType++)
		{
			const FIntVector PageTableTextureResolution = GlobalDistanceField::GetPageTableTextureResolution();
			TRefCountPtr<IPooledRenderTarget>& PageTableTexture = View.ViewState->GlobalDistanceFieldPageTableLayerTextures[CacheType];

			if (CacheType == GDF_Full || GAOGlobalDistanceFieldCacheMostlyStaticSeparately)
			{
				if (!PageTableTexture
					|| PageTableTexture->GetDesc().Extent.X != PageTableTextureResolution.X
					|| PageTableTexture->GetDesc().Extent.Y != PageTableTextureResolution.Y
					|| PageTableTexture->GetDesc().Depth != PageTableTextureResolution.Z)
				{
					FPooledRenderTargetDesc VolumeDesc = FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
						PageTableTextureResolution.X,
						PageTableTextureResolution.Y,
						PageTableTextureResolution.Z,
						PF_R16_UINT,
						FClearValueBinding::None,
						TexCreate_None,
						TexCreate_ShaderResource | TexCreate_UAV | TexCreate_ReduceMemoryWithTilingMode | TexCreate_3DTiling,
						false));
					VolumeDesc.AutoWritable = false;

					GRenderTargetPool.FindFreeElement(
						RHICmdList,
						VolumeDesc,
						PageTableTexture,
						CacheType == GDF_MostlyStatic ? TEXT("GlobalDistanceFieldPageTableStationaryLayer") : TEXT("GlobalDistanceFieldPageTableMovableLayer"),
						ERenderTargetTransience::NonTransient
					);

					bSharedDataReallocated = true;
				}
			}

			GlobalDistanceFieldInfo.PageTableLayerTextures[CacheType] = PageTableTexture;
		}

		for (int32 ClipmapIndex = 0; ClipmapIndex < NumClipmaps; ClipmapIndex++)
		{
			FGlobalDistanceFieldClipmapState& ClipmapViewState = View.ViewState->GlobalDistanceFieldClipmapState[ClipmapIndex];

			const int32 ClipmapResolution = GlobalDistanceField::GetClipmapResolution();
			const float ClipmapExtent = GlobalDistanceField::GetClipmapExtent(ClipmapIndex, Scene);
			const float ClipmapVoxelSize = (2.0f * ClipmapExtent) / ClipmapResolution;
			const float ClipmapPageSize = GGlobalDistanceFieldPageResolution * ClipmapVoxelSize;
			const float ClipmapInfluenceRadius = GGlobalDistanceFieldInfluenceRangeInVoxels * ClipmapVoxelSize;

			// Accumulate primitive modifications in the viewstate in case we don't update the clipmap this frame
			for (uint32 CacheType = 0; CacheType < GDF_Num; CacheType++)
			{
				const uint32 SourceCacheType = GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? CacheType : GDF_Full;
				ClipmapViewState.Cache[CacheType].PrimitiveModifiedBounds.Append(Scene->DistanceFieldSceneData.PrimitiveModifiedBounds[SourceCacheType]);
			}

			const bool bForceFullUpdate = bSharedDataReallocated
				|| !View.ViewState->bInitializedGlobalDistanceFieldOrigins
				// Detect when max occlusion distance has changed
				|| ClipmapViewState.CachedClipmapExtent != ClipmapExtent
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

				// Snap to the global distance field page's size
				FIntVector PageGridCenter;
				PageGridCenter.X = FMath::RoundToInt(GlobalDistanceFieldViewOrigin.X / ClipmapPageSize);
				PageGridCenter.Y = FMath::RoundToInt(GlobalDistanceFieldViewOrigin.Y / ClipmapPageSize);
				PageGridCenter.Z = FMath::RoundToInt(GlobalDistanceFieldViewOrigin.Z / ClipmapPageSize);

				const FVector SnappedCenter = FVector(PageGridCenter) * ClipmapPageSize;
				const FBox ClipmapBounds(SnappedCenter - ClipmapExtent, SnappedCenter + ClipmapExtent);

				const bool bUsePartialUpdates = GAOGlobalDistanceFieldPartialUpdates && !bForceFullUpdate;

				if (!bUsePartialUpdates)
				{
					// Store the location of the full update
					ClipmapViewState.FullUpdateOriginInPages = PageGridCenter;
					View.ViewState->bInitializedGlobalDistanceFieldOrigins = true;
					View.ViewState->bGlobalDistanceFieldPendingReset = true;
					ClipmapViewState.LastUsedSceneDataForFullUpdate = &Scene->DistanceFieldSceneData;
				}

				const FGlobalDFCacheType StartCacheType = GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? GDF_MostlyStatic : GDF_Full;

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
						const FVector PrimWorldExtent = PrimBounds.GetExtent();
						const FBox ModifiedBounds(PrimWorldCenter - PrimWorldExtent, PrimWorldCenter + PrimWorldExtent);

						if (ModifiedBounds.ComputeSquaredDistanceToBox(ClipmapBounds) < ClipmapInfluenceRadius * ClipmapInfluenceRadius)
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
						FIntVector MovementInPages = PageGridCenter - ClipmapViewState.LastPartialUpdateOriginInPages;

						if (GAOGlobalDistanceFieldForceMovementUpdate != 0)
						{
							MovementInPages = FIntVector(GAOGlobalDistanceFieldForceMovementUpdate, GAOGlobalDistanceFieldForceMovementUpdate, GAOGlobalDistanceFieldForceMovementUpdate);
						}

						if (CacheType == GDF_MostlyStatic || !GAOGlobalDistanceFieldCacheMostlyStaticSeparately)
						{
							// Add an update region for each potential axis of camera movement
							AddUpdateBoundsForAxis(MovementInPages, ClipmapBounds, ClipmapPageSize, 0, Clipmap.UpdateBounds);
							AddUpdateBoundsForAxis(MovementInPages, ClipmapBounds, ClipmapPageSize, 1, Clipmap.UpdateBounds);
							AddUpdateBoundsForAxis(MovementInPages, ClipmapBounds, ClipmapPageSize, 2, Clipmap.UpdateBounds);
						}
						else
						{
							// Inherit from parent
							Clipmap.UpdateBounds.Append(GlobalDistanceFieldInfo.MostlyStaticClipmaps[ClipmapIndex].UpdateBounds);
						}
					}

					// Only use partial updates with small numbers of primitive modifications
					bool bUsePartialUpdatesForUpdateBounds = bUsePartialUpdates && CulledPrimitiveModifiedBounds.Num() < 1024;

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
					}
					else if (View.ViewState->DeferredGlobalDistanceFieldUpdates[CacheType].Remove(ClipmapIndex) > 0)
					{
						// Push full update
						Clipmap.UpdateBounds.Reset();
						Clipmap.UpdateBounds.Add(FClipmapUpdateBounds(ClipmapBounds.GetCenter(), ClipmapBounds.GetExtent(), false));
					}

					ClipmapViewState.Cache[CacheType].PrimitiveModifiedBounds.Reset();
				}

				ClipmapViewState.LastPartialUpdateOriginInPages = PageGridCenter;
			}

			const FVector SnappedCenter = FVector(ClipmapViewState.LastPartialUpdateOriginInPages) * ClipmapPageSize;
			const FGlobalDFCacheType StartCacheType = GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? GDF_MostlyStatic : GDF_Full;

			for (uint32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
			{
				FGlobalDistanceFieldClipmap& Clipmap = *(CacheType == GDF_MostlyStatic 
					? &GlobalDistanceFieldInfo.MostlyStaticClipmaps[ClipmapIndex] 
					: &GlobalDistanceFieldInfo.Clipmaps[ClipmapIndex]);

				// Setup clipmap properties from view state exclusively, so we can skip updating on some frames
				Clipmap.Bounds = FBox(SnappedCenter - ClipmapExtent, SnappedCenter + ClipmapExtent);

				// Scroll offset so the contents of the global distance field don't have to be moved as the camera moves around, only updated in slabs
				Clipmap.ScrollOffset = FVector(ClipmapViewState.LastPartialUpdateOriginInPages - ClipmapViewState.FullUpdateOriginInPages) * ClipmapPageSize;
			}

			ClipmapViewState.CachedClipmapExtent = ClipmapExtent;
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

				Clipmap.ScrollOffset = FVector(0);

				const int32 ClipmapResolution = GlobalDistanceField::GetClipmapResolution();
				const float Extent = GlobalDistanceField::GetClipmapExtent(ClipmapIndex, Scene);
				const float ClipmapVoxelSize = (2.0f * Extent) / ClipmapResolution;
				const float ClipmapPageSize = GGlobalDistanceFieldPageResolution * ClipmapVoxelSize;
				const FVector GlobalDistanceFieldViewOrigin = GetGlobalDistanceFieldViewOrigin(View, ClipmapIndex);

				FIntVector PageGridCenter;
				PageGridCenter.X = FMath::RoundToInt(GlobalDistanceFieldViewOrigin.X / ClipmapPageSize);
				PageGridCenter.Y = FMath::RoundToInt(GlobalDistanceFieldViewOrigin.Y / ClipmapPageSize);
				PageGridCenter.Z = FMath::RoundToInt(GlobalDistanceFieldViewOrigin.Z / ClipmapPageSize);

				FVector Center = FVector(PageGridCenter) * ClipmapPageSize;

				FBox ClipmapBounds(Center - Extent, Center + Extent);
				Clipmap.Bounds = ClipmapBounds;

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
		ViewUniformShaderParameters.GlobalDistanceFieldMipWorldToUVScale[Index] = FVector4(0);
		ViewUniformShaderParameters.GlobalDistanceFieldMipWorldToUVBias[Index] = FVector4(0);
	}
	ViewUniformShaderParameters.GlobalDistanceFieldMipFactor = 1.0f;
	ViewUniformShaderParameters.GlobalDistanceFieldMipTransition = 0.0f;
	ViewUniformShaderParameters.GlobalDistanceFieldClipmapSizeInPages = 1;
	ViewUniformShaderParameters.GlobalDistanceFieldInvPageAtlasSize = FVector(1.0f, 1.0f, 1.0f);
	ViewUniformShaderParameters.GlobalVolumeDimension = 0.0f;
	ViewUniformShaderParameters.GlobalVolumeTexelSize = 0.0f;
	ViewUniformShaderParameters.MaxGlobalDFAOConeDistance = 0.0f;
	ViewUniformShaderParameters.NumGlobalSDFClipmaps = 0;

	ViewUniformShaderParameters.GlobalDistanceFieldPageAtlasTexture = OrBlack3DIfNull(GBlackVolumeTexture->TextureRHI.GetReference());
	ViewUniformShaderParameters.GlobalDistanceFieldPageTableTexture = OrBlack3DIfNull(GBlackVolumeTexture->TextureRHI.GetReference());
	ViewUniformShaderParameters.GlobalDistanceFieldMipTexture = OrBlack3DIfNull(GBlackVolumeTexture->TextureRHI.GetReference());
}

void FViewInfo::SetupGlobalDistanceFieldUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const
{
	check(GlobalDistanceFieldInfo.bInitialized);

	for (int32 Index = 0; Index < GMaxGlobalDistanceFieldClipmaps; Index++)
	{
		ViewUniformShaderParameters.GlobalVolumeCenterAndExtent[Index] = GlobalDistanceFieldInfo.ParameterData.CenterAndExtent[Index];
		ViewUniformShaderParameters.GlobalVolumeWorldToUVAddAndMul[Index] = GlobalDistanceFieldInfo.ParameterData.WorldToUVAddAndMul[Index];
		ViewUniformShaderParameters.GlobalDistanceFieldMipWorldToUVScale[Index] = GlobalDistanceFieldInfo.ParameterData.MipWorldToUVScale[Index];
		ViewUniformShaderParameters.GlobalDistanceFieldMipWorldToUVBias[Index] = GlobalDistanceFieldInfo.ParameterData.MipWorldToUVBias[Index];
	}
	ViewUniformShaderParameters.GlobalDistanceFieldMipFactor = GlobalDistanceFieldInfo.ParameterData.MipFactor;
	ViewUniformShaderParameters.GlobalDistanceFieldMipTransition = GlobalDistanceFieldInfo.ParameterData.MipTransition;
	ViewUniformShaderParameters.GlobalDistanceFieldClipmapSizeInPages = GlobalDistanceFieldInfo.ParameterData.ClipmapSizeInPages;
	ViewUniformShaderParameters.GlobalDistanceFieldInvPageAtlasSize = GlobalDistanceFieldInfo.ParameterData.InvPageAtlasSize;
	ViewUniformShaderParameters.GlobalVolumeDimension = GlobalDistanceFieldInfo.ParameterData.GlobalDFResolution;
	ViewUniformShaderParameters.GlobalVolumeTexelSize = 1.0f / GlobalDistanceFieldInfo.ParameterData.GlobalDFResolution;
	ViewUniformShaderParameters.MaxGlobalDFAOConeDistance = GlobalDistanceFieldInfo.ParameterData.MaxDFAOConeDistance;
	ViewUniformShaderParameters.NumGlobalSDFClipmaps = GlobalDistanceFieldInfo.ParameterData.NumGlobalSDFClipmaps;

	ViewUniformShaderParameters.GlobalDistanceFieldPageAtlasTexture = OrBlack3DIfNull(GlobalDistanceFieldInfo.ParameterData.PageAtlasTexture);
	ViewUniformShaderParameters.GlobalDistanceFieldPageTableTexture = OrBlack3DIfNull(GlobalDistanceFieldInfo.ParameterData.PageTableTexture);
	ViewUniformShaderParameters.GlobalDistanceFieldMipTexture = OrBlack3DIfNull(GlobalDistanceFieldInfo.ParameterData.MipTexture);
}

void ReadbackDistanceFieldClipmap(FRHICommandListImmediate& RHICmdList, FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
{
	FGlobalDistanceFieldReadback* Readback = GDFReadbackRequest;
	GDFReadbackRequest = nullptr;

	//FGlobalDistanceFieldClipmap& ClipMap = GlobalDistanceFieldInfo.Clipmaps[0];
	//FTextureRHIRef SourceTexture = ClipMap.RenderTarget->GetRenderTargetItem().ShaderResourceTexture;
	//FIntVector Size = SourceTexture->GetSizeXYZ();
	
	//RHICmdList.Read3DSurfaceFloatData(SourceTexture, FIntRect(0, 0, Size.X, Size.Y), FIntPoint(0, Size.Z), Readback->ReadbackData);
	//Readback->Bounds = ClipMap.Bounds;
	//Readback->Size = Size;

	ensureMsgf(false, TEXT("#todo: Global DF readback requires a rewrite as global distance field is no longer stored in a continuos memory"));

	Readback->Bounds = FBox(FVector(0.0f), FVector(0.0f));
	Readback->Size = FIntVector(0);
	
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
	RDG_BUFFER_ACCESS(UpdateBoundsBuffer, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

class FCullObjectsToClipmapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullObjectsToClipmapCS);
	SHADER_USE_PARAMETER_STRUCT(FCullObjectsToClipmapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWObjectIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWObjectIndexNumBuffer)
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWPageUpdateIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWPageComposeIndirectArgBuffer)
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
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWGridTileBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWGridIndirectArgBuffer)
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCullGridAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCullGridObjectHeader)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCullGridObjectArray)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, CullGridIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CullGridTileBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ObjectIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ObjectIndexNumBuffer)
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
	}
};

IMPLEMENT_GLOBAL_SHADER(FCullObjectsToGridCS, "/Engine/Private/GlobalDistanceField.usf", "CullObjectsToGridCS", SF_Compute);

class FComposeObjectsIntoPagesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComposeObjectsIntoPagesCS);
	SHADER_USE_PARAMETER_STRUCT(FComposeObjectsIntoPagesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWPageAtlasTexture)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, ComposeIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ComposeTileBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, HeightfieldMarkedPageBuffer)
		SHADER_PARAMETER_TEXTURE(Texture3D, DistanceFieldTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DistanceFieldSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, PageTableLayerTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, ParentPageTableLayerTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CullGridObjectHeader)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CullGridObjectArray)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ObjectIndexNumBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ObjectIndexBuffer)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectBounds)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
		SHADER_PARAMETER(float, ClipmapVoxelExtent)
		SHADER_PARAMETER(float, InfluenceRadius)
		SHADER_PARAMETER(float, InfluenceRadiusSq)
		SHADER_PARAMETER(uint32, NumSceneObjects)
		SHADER_PARAMETER(FIntVector, CullGridResolution)
		SHADER_PARAMETER(FIntVector, GlobalDistanceFieldScrollOffset)
		SHADER_PARAMETER(FVector, GlobalDistanceFieldInvPageAtlasSize)
		SHADER_PARAMETER(FVector, InvPageGridResolution)
		SHADER_PARAMETER(FIntVector, PageGridResolution)
		SHADER_PARAMETER(FIntVector, ClipmapResolution)
		SHADER_PARAMETER(FVector, PageCoordToVoxelCenterScale)
		SHADER_PARAMETER(FVector, PageCoordToVoxelCenterBias)
		SHADER_PARAMETER(FVector, PageCoordToPageWorldCenterScale)
		SHADER_PARAMETER(FVector, PageCoordToPageWorldCenterBias)
		SHADER_PARAMETER(FVector4, ClipmapVolumeWorldToUVAddAndMul)
		SHADER_PARAMETER(FVector, ComposeTileWorldExtent)
		SHADER_PARAMETER(FVector, ClipmapMinBounds)
		SHADER_PARAMETER(uint32, PageTableClipmapOffsetZ)
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
		OutEnvironment.SetDefine(TEXT("COMPOSITE_THREADGROUP_SIZEX"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("COMPOSITE_THREADGROUP_SIZEY"), GetGroupSize().Y);
		OutEnvironment.SetDefine(TEXT("COMPOSITE_THREADGROUP_SIZEZ"), GetGroupSize().Z);
	}
};

IMPLEMENT_GLOBAL_SHADER(FComposeObjectsIntoPagesCS, "/Engine/Private/GlobalDistanceField.usf", "ComposeObjectsIntoPagesCS", SF_Compute);

class FInitPageFreeListCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FInitPageFreeListCS);
	SHADER_USE_PARAMETER_STRUCT(FInitPageFreeListCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPageFreeListBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int, RWPageFreeListAllocatorBuffer)
		SHADER_PARAMETER(uint32, GlobalDistanceFieldMaxPageNum)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static uint32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FInitPageFreeListCS, "/Engine/Private/GlobalDistanceField.usf", "InitPageFreeListCS", SF_Compute);

class FAllocatePagesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAllocatePagesCS);
	SHADER_USE_PARAMETER_STRUCT(FAllocatePagesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, PageUpdateIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageUpdateTileBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MarkedHeightfieldPageBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWPageTableCombinedTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWPageTableLayerTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, RWPageFreeListAllocatorBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageFreeListBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPageFreeListReturnAllocatorBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPageFreeListReturnBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPageComposeTileBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWPageComposeIndirectArgBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, ParentPageTableLayerTexture)
		SHADER_PARAMETER(FVector, InvPageGridResolution)
		SHADER_PARAMETER(FIntVector, PageGridResolution)
		SHADER_PARAMETER(uint32, GlobalDistanceFieldMaxPageNum)
		SHADER_PARAMETER(uint32, PageTableClipmapOffsetZ)
		SHADER_PARAMETER(FVector, PageWorldExtent)
		SHADER_PARAMETER(float, PageWorldRadius)
		SHADER_PARAMETER(float, ClipmapInfluenceRadius)
		SHADER_PARAMETER(FVector, PageCoordToPageWorldCenterScale)
		SHADER_PARAMETER(FVector, PageCoordToPageWorldCenterBias)
		SHADER_PARAMETER(FVector4, ClipmapVolumeWorldToUVAddAndMul)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CullGridObjectHeader)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CullGridObjectArray)
		SHADER_PARAMETER(FIntVector, CullGridResolution)
		SHADER_PARAMETER_TEXTURE(Texture3D, DistanceFieldTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DistanceFieldSampler)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectBounds)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
		SHADER_PARAMETER(uint32, NumSceneObjects)
	END_SHADER_PARAMETER_STRUCT()

	class FMarkedHeightfieldPageBuffer : SHADER_PERMUTATION_BOOL("MARKED_HEIGHTFIELD_PAGE_BUFFER");
	class FComposeParentDistanceField : SHADER_PERMUTATION_BOOL("COMPOSE_PARENT_DISTANCE_FIELD");
	using FPermutationDomain = TShaderPermutationDomain<FMarkedHeightfieldPageBuffer, FComposeParentDistanceField>;
	
	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FComposeParentDistanceField>())
		{
			PermutationVector.Set<FMarkedHeightfieldPageBuffer>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(64, 1, 1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), GetGroupSize().Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), GetGroupSize().Z);
	}
};

IMPLEMENT_GLOBAL_SHADER(FAllocatePagesCS, "/Engine/Private/GlobalDistanceField.usf", "AllocatePagesCS", SF_Compute);

class FPageFreeListReturnIndirectArgBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPageFreeListReturnIndirectArgBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FPageFreeListReturnIndirectArgBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWFreeListReturnIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int, RWPageFreeListAllocatorBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageFreeListReturnAllocatorBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPageFreeListReturnIndirectArgBufferCS, "/Engine/Private/GlobalDistanceField.usf", "PageFreeListReturnIndirectArgBufferCS", SF_Compute);

class FPageFreeListReturnCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPageFreeListReturnCS);
	SHADER_USE_PARAMETER_STRUCT(FPageFreeListReturnCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, FreeListReturnIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int, RWPageFreeListAllocatorBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPageFreeListBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageFreeListReturnAllocatorBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageFreeListReturnBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static uint32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPageFreeListReturnCS, "/Engine/Private/GlobalDistanceField.usf", "PageFreeListReturnCS", SF_Compute);

class FPropagateMipDistanceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPropagateMipDistanceCS);
	SHADER_USE_PARAMETER_STRUCT(FPropagateMipDistanceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWMipTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, PrevMipTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, PageTableCombinedTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, PageAtlasTexture)
		SHADER_PARAMETER(FVector, GlobalDistanceFieldInvPageAtlasSize)
		SHADER_PARAMETER(uint32, GlobalDistanceFieldClipmapSizeInPages)
		SHADER_PARAMETER(uint32, ClipmapMipResolution)
		SHADER_PARAMETER(uint32, ClipmapIndex)
		SHADER_PARAMETER(uint32, PrevClipmapOffsetZ)
		SHADER_PARAMETER(uint32, ClipmapOffsetZ)
		SHADER_PARAMETER(FVector, ClipmapUVScrollOffset)
		SHADER_PARAMETER(float, CoarseDistanceFieldValueScale)
		SHADER_PARAMETER(float, CoarseDistanceFieldValueBias)
	END_SHADER_PARAMETER_STRUCT()

	class FReadPages : SHADER_PERMUTATION_BOOL("READ_PAGES");
	using FPermutationDomain = TShaderPermutationDomain<FReadPages>;

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
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), GetGroupSize().Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), GetGroupSize().Z);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPropagateMipDistanceCS, "/Engine/Private/GlobalDistanceFieldMip.usf", "PropagateMipDistanceCS", SF_Compute);

/** 
 * Updates the global distance field for a view.  
 * Typically issues updates for just the newly exposed regions of the volume due to camera movement.
 * In the worst case of a camera cut or large distance field scene changes, a full update of the global distance field will be done.
 **/
void UpdateGlobalDistanceFieldVolume(
	FRDGBuilder& GraphBuilder,
	FViewInfo& View,
	FScene* Scene,
	float MaxOcclusionDistance,
	FGlobalDistanceFieldInfo& GlobalDistanceFieldInfo)
{
	RDG_GPU_STAT_SCOPE(GraphBuilder, GlobalDistanceFieldUpdate);
	SCOPED_GPU_STAT(GraphBuilder.RHICmdList, GlobalDistanceFieldUpdate);

	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	UpdateGlobalDistanceFieldViewOrigin(View);

	if (Scene->DistanceFieldSceneData.NumObjectsInBuffer > 0)
	{
		const int32 NumClipmaps = FMath::Clamp<int32>(GetNumGlobalDistanceFieldClipmaps(), 0, GMaxGlobalDistanceFieldClipmaps);
		ComputeUpdateRegionsAndUpdateViewState(GraphBuilder.RHICmdList, View, Scene, GlobalDistanceFieldInfo, NumClipmaps, MaxOcclusionDistance);

		// Recreate the view uniform buffer now that we have updated GlobalDistanceFieldInfo
		View.SetupGlobalDistanceFieldUniformBufferParameters(*View.CachedViewUniformShaderParameters);
		View.ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*View.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);

		bool bHasUpdateBounds = false;

		for (int32 ClipmapIndex = 0; ClipmapIndex < GlobalDistanceFieldInfo.Clipmaps.Num(); ClipmapIndex++)
		{
			bHasUpdateBounds = bHasUpdateBounds || GlobalDistanceFieldInfo.Clipmaps[ClipmapIndex].UpdateBounds.Num() > 0;
		}

		for (int32 ClipmapIndex = 0; ClipmapIndex < GlobalDistanceFieldInfo.MostlyStaticClipmaps.Num(); ClipmapIndex++)
		{
			bHasUpdateBounds = bHasUpdateBounds || GlobalDistanceFieldInfo.MostlyStaticClipmaps[ClipmapIndex].UpdateBounds.Num() > 0;
		}

		if (bHasUpdateBounds && GAOUpdateGlobalDistanceField)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "UpdateGlobalDistanceField");

			const FGlobalDFCacheType StartCacheType = GAOGlobalDistanceFieldCacheMostlyStaticSeparately ? GDF_MostlyStatic : GDF_Full;

			FRDGBufferRef PageFreeListAllocatorBuffer = nullptr;
			if (GlobalDistanceFieldInfo.PageFreeListAllocatorBuffer)
			{
				PageFreeListAllocatorBuffer = GraphBuilder.RegisterExternalBuffer(GlobalDistanceFieldInfo.PageFreeListAllocatorBuffer, TEXT("PageFreeListAllocator"));
			}

			FRDGBufferRef PageFreeListBuffer = nullptr;
			if (GlobalDistanceFieldInfo.PageFreeListBuffer)
			{
				PageFreeListBuffer = GraphBuilder.RegisterExternalBuffer(GlobalDistanceFieldInfo.PageFreeListBuffer, TEXT("PageFreeList"));
			}

			FRDGTextureRef PageAtlasTexture = nullptr;
			if (GlobalDistanceFieldInfo.PageAtlasTexture)
			{
				PageAtlasTexture = GraphBuilder.RegisterExternalTexture(GlobalDistanceFieldInfo.PageAtlasTexture, TEXT("PageAtlas"));
			}

			FRDGTextureRef PageTableCombinedTexture = nullptr;
			if (GlobalDistanceFieldInfo.PageTableCombinedTexture)
			{
				PageTableCombinedTexture = GraphBuilder.RegisterExternalTexture(GlobalDistanceFieldInfo.PageTableCombinedTexture, TEXT("PageTableCombined"));
			}

			FRDGTextureRef MipTexture = nullptr;
			if (GlobalDistanceFieldInfo.MipTexture)
			{
				MipTexture = GraphBuilder.RegisterExternalTexture(GlobalDistanceFieldInfo.MipTexture, TEXT("GlobalSDFMips"));
			}

			FRDGTextureRef TempMipTexture = nullptr;
			{
				const int32 ClipmapMipResolution = GlobalDistanceField::GetClipmapMipResolution();
				FRDGTextureDesc TempMipDesc(FRDGTextureDesc::Create3D(
					FIntVector(ClipmapMipResolution),
					PF_R8,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling));

				TempMipTexture = GraphBuilder.CreateTexture(TempMipDesc, TEXT("TempMip"));
			}

			FRDGTextureRef PageTableLayerTextures[GDF_Num] = {};
			for (int32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
			{
				if (GlobalDistanceFieldInfo.PageTableLayerTextures[CacheType])
				{
					PageTableLayerTextures[CacheType] = GraphBuilder.RegisterExternalTexture(GlobalDistanceFieldInfo.PageTableLayerTextures[CacheType], TEXT("GlobalDistanceFieldPageTableLayer"));
				}
			}

			if (View.ViewState && View.ViewState->bGlobalDistanceFieldPendingReset)
			{
				// Reset all allocators to default

				const uint32 PageTableClearValue[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

				if (PageTableCombinedTexture)
				{
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageTableCombinedTexture), PageTableClearValue);
				}

				for (int32 CacheType = StartCacheType; CacheType < GDF_Num; ++CacheType)
				{
					if (PageTableLayerTextures[CacheType])
					{
						AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageTableLayerTextures[CacheType]), PageTableClearValue);
					}
				}

				const int32 MaxPageNum = GlobalDistanceField::GetMaxPageNum();

				if (PageFreeListAllocatorBuffer)
				{
					FInitPageFreeListCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitPageFreeListCS::FParameters>();
					PassParameters->RWPageFreeListBuffer = GraphBuilder.CreateUAV(PageFreeListBuffer, PF_R32_UINT);
					PassParameters->RWPageFreeListAllocatorBuffer = GraphBuilder.CreateUAV(PageFreeListAllocatorBuffer, PF_R32_SINT);
					PassParameters->GlobalDistanceFieldMaxPageNum = MaxPageNum;

					auto ComputeShader = View.ShaderMap->GetShader<FInitPageFreeListCS>();

					const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(MaxPageNum, FInitPageFreeListCS::GetGroupSize());

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("InitPageFreeList"),
						ComputeShader,
						PassParameters,
						GroupSize);
				}

				View.ViewState->bGlobalDistanceFieldPendingReset = false;
			}

			for (int32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
			{
				FRDGTextureRef PageTableLayerTexture = PageTableLayerTextures[CacheType];
				FRDGTextureRef ParentPageTableLayerTexture = nullptr;

				if (CacheType == GDF_Full && GAOGlobalDistanceFieldCacheMostlyStaticSeparately && PageTableLayerTextures[GDF_MostlyStatic])
				{
					ParentPageTableLayerTexture = PageTableLayerTextures[GDF_MostlyStatic];
				}

				TArray<FGlobalDistanceFieldClipmap>& Clipmaps = CacheType == GDF_MostlyStatic
					? GlobalDistanceFieldInfo.MostlyStaticClipmaps
					: GlobalDistanceFieldInfo.Clipmaps;

				for (int32 ClipmapIndex = 0; ClipmapIndex < Clipmaps.Num(); ClipmapIndex++)
				{
					RDG_EVENT_SCOPE(GraphBuilder, "Clipmap:%d CacheType:%s", ClipmapIndex, CacheType == GDF_MostlyStatic ? TEXT("MostlyStatic") : TEXT("Movable"));

					FGlobalDistanceFieldClipmap& Clipmap = Clipmaps[ClipmapIndex];

					const int32 ClipmapResolution = GlobalDistanceField::GetClipmapResolution();
					const FVector ClipmapWorldCenter = Clipmap.Bounds.GetCenter();
					const FVector ClipmapWorldExtent = Clipmap.Bounds.GetExtent();
					const FVector ClipmapSize = Clipmap.Bounds.GetSize();
					const FVector ClipmapVoxelSize = ClipmapSize / FVector(ClipmapResolution);
					const FVector ClipmapVoxelExtent = 0.5f * ClipmapVoxelSize;
					const float ClipmapVoxelRadius = ClipmapVoxelExtent.Size();
					const float ClipmapInfluenceRadius = (GGlobalDistanceFieldInfluenceRangeInVoxels * ClipmapSize.X) / ClipmapResolution;

					FVector4 ClipmapVolumeWorldToUVAddAndMul;
					const FVector WorldToUVAdd = (Clipmap.ScrollOffset - Clipmap.Bounds.GetCenter()) / (Clipmap.Bounds.GetExtent().X * 2.0f) + FVector(0.5f);
					ClipmapVolumeWorldToUVAddAndMul = FVector4(WorldToUVAdd, 1.0f / (Clipmap.Bounds.GetExtent().X * 2.0f));

					int32 MaxSDFMeshObjects = FMath::RoundUpToPowerOfTwo(DistanceFieldSceneData.NumObjectsInBuffer);
					FRDGBufferRef ObjectIndexBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxSDFMeshObjects), TEXT("ObjectIndices"));
					FRDGBufferRef ObjectIndexNumBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("ObjectIndexNum"));

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
									void* DestCardIdPtr = RHILockVertexBuffer(PassParameters->UpdateBoundsBuffer->GetRHI(), 0, UploadBytes, RLM_WriteOnly);
									FPlatformMemory::Memcpy(DestCardIdPtr, UploadPtr, UploadBytes);
									RHIUnlockVertexBuffer(PassParameters->UpdateBoundsBuffer->GetRHI());
								}
							});
					}


					FHeightfieldDescription UpdateRegionHeightfield;

					// Update heightfield descriptors
					{
						const int32 NumHeightfieldPrimitives = DistanceFieldSceneData.HeightfieldPrimitives.Num();
						if ((CacheType == GDF_MostlyStatic || !GAOGlobalDistanceFieldCacheMostlyStaticSeparately)
							&& NumUpdateBounds > 0
							&& NumHeightfieldPrimitives > 0
							&& GAOGlobalDistanceFieldRepresentHeightfields
							&& SupportsDistanceFieldAO(Scene->GetFeatureLevel(), Scene->GetShaderPlatform())
							&& !IsMetalPlatform(Scene->GetShaderPlatform())
							&& !IsVulkanMobileSM5Platform(Scene->GetShaderPlatform()))
						{
							for (int32 HeightfieldPrimitiveIndex = 0; HeightfieldPrimitiveIndex < NumHeightfieldPrimitives; HeightfieldPrimitiveIndex++)
							{
								const FPrimitiveSceneInfo* HeightfieldPrimitive = Scene->DistanceFieldSceneData.HeightfieldPrimitives[HeightfieldPrimitiveIndex];
								const FBoxSphereBounds& PrimitiveBounds = HeightfieldPrimitive->Proxy->GetBounds();

								// Expand bounding box by a SDF max influence distance (only in local Z axis, as distance is computed from a top down projected heightmap point).
								const FVector QueryInfluenceExpand = HeightfieldPrimitive->Proxy->GetLocalToWorld().GetUnitAxis(EAxis::Z) * FVector(0.0f, 0.0f, ClipmapInfluenceRadius);
								const FBox HeightfieldInfluenceBox = PrimitiveBounds.GetBox().ExpandBy(QueryInfluenceExpand, QueryInfluenceExpand);

								if (Clipmap.Bounds.Intersect(HeightfieldInfluenceBox))
								{
									UTexture2D* HeightfieldTexture = nullptr;
									UTexture2D* DiffuseColorTexture = nullptr;
									UTexture2D* VisibilityTexture = nullptr;
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
						}
					}

					if (NumUpdateBounds > 0 && PageAtlasTexture)
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
							PassParameters->MeshSDFRadiusThreshold = GetMinMeshSDFRadius(ClipmapVoxelSize.X);
							PassParameters->InfluenceRadiusSq = ClipmapInfluenceRadius * ClipmapInfluenceRadius;

							auto ComputeShader = View.ShaderMap->GetShader<FCullObjectsToClipmapCS>();
							const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(DistanceFieldSceneData.NumObjectsInBuffer, FCullObjectsToClipmapCS::GetGroupSize());

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("CullToClipmap"),
								ComputeShader,
								PassParameters,
								GroupSize);
						}

						const uint32 GGlobalDistanceFieldMaxPageNum = GlobalDistanceField::GetMaxPageNum();

						const uint32 PageGridDim = FMath::DivideAndRoundUp(ClipmapResolution, GGlobalDistanceFieldPageResolution);
						const uint32 PageGridSize = PageGridDim * PageGridDim * PageGridDim;
						const FIntVector PageGridResolution(PageGridDim, PageGridDim, PageGridDim);

						const FVector PageTileWorldExtent = ClipmapVoxelExtent * GGlobalDistanceFieldPageResolutionInAtlas;
						const FVector PageTileWorldExtentWithoutBorders = ClipmapVoxelExtent * GGlobalDistanceFieldPageResolution;
						const FVector PageGridCoordToWorldCenterScale = ClipmapSize / FVector(PageGridResolution);
						const FVector PageGridCoordToWorldCenterBias = Clipmap.Bounds.Min + 0.5f * PageGridCoordToWorldCenterScale;

						FRDGBufferRef PageUpdateTileBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PageGridSize), TEXT("PageUpdateTiles"));
						FRDGBufferRef PageComposeTileBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PageGridSize), TEXT("PageComposeTiles"));
						FRDGBufferRef PageComposeHeightfieldTileBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PageGridSize), TEXT("PageComposeHeightfieldTiles"));

						FRDGBufferRef PageUpdateIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("PageUpdateIndirectArgs"));
						FRDGBufferRef PageComposeIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("PageComposeIndirectArgs"));
						FRDGBufferRef PageComposeHeightfieldIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("PageComposeHeightfieldIndirectArgs"));

						// Clear indirect dispatch arguments
						{
							FClearIndirectArgBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearIndirectArgBufferCS::FParameters>();
							PassParameters->RWPageUpdateIndirectArgBuffer = GraphBuilder.CreateUAV(PageUpdateIndirectArgBuffer, PF_R32_UINT);
							PassParameters->RWPageComposeIndirectArgBuffer = GraphBuilder.CreateUAV(PageComposeIndirectArgBuffer, PF_R32_UINT);

							auto ComputeShader = View.ShaderMap->GetShader<FClearIndirectArgBufferCS>();

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("ClearIndirectArgBuffer"),
								ComputeShader,
								PassParameters,
								FIntVector(1, 1, 1));
						}

						// Prepare page tiles which need to be updated for update regions
						{
							FBuildGridTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildGridTilesCS::FParameters>();
							PassParameters->View = View.ViewUniformBuffer;
							PassParameters->RWGridTileBuffer = GraphBuilder.CreateUAV(PageUpdateTileBuffer, PF_R32_UINT);
							PassParameters->RWGridIndirectArgBuffer = GraphBuilder.CreateUAV(PageUpdateIndirectArgBuffer, PF_R32_UINT);
							PassParameters->UpdateBoundsBuffer = GraphBuilder.CreateSRV(UpdateBoundsBuffer, PF_A32B32G32R32F);
							PassParameters->NumUpdateBounds = NumUpdateBounds;
							PassParameters->GridResolution = PageGridResolution;
							PassParameters->GridCoordToWorldCenterScale = PageGridCoordToWorldCenterScale;
							PassParameters->GridCoordToWorldCenterBias = PageGridCoordToWorldCenterBias;
							PassParameters->TileWorldExtent = PageTileWorldExtent;
							PassParameters->InfluenceRadiusSq = ClipmapInfluenceRadius * ClipmapInfluenceRadius;

							auto ComputeShader = View.ShaderMap->GetShader<FBuildGridTilesCS>();

							const FIntVector GroupSize = PageGridResolution;

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("BuildPageUpdateTiles %d", NumUpdateBounds),
								ComputeShader,
								PassParameters,
								GroupSize);
						}

						// Mark pages which contain a heightfield
						FRDGBufferRef MarkedHeightfieldPageBuffer = nullptr;
						if (UpdateRegionHeightfield.ComponentDescriptions.Num() > 0)
						{
							RDG_EVENT_SCOPE(GraphBuilder, "HeightfieldPageAllocation");

							MarkedHeightfieldPageBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PageGridSize), TEXT("MarkedHeightfieldPages"));
							AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MarkedHeightfieldPageBuffer, PF_R32_UINT), 0);

							const FVector PageVoxelExtent = 0.5f * ClipmapSize / FVector(ClipmapResolution);
							const FVector PageCoordToVoxelCenterScale = ClipmapSize / FVector(ClipmapResolution);
							const FVector PageCoordToVoxelCenterBias = Clipmap.Bounds.Min + PageVoxelExtent;

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

									FMarkHeightfieldPagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMarkHeightfieldPagesCS::FParameters>();
									PassParameters->View = View.ViewUniformBuffer;
									PassParameters->RWMarkedHeightfieldPageBuffer = GraphBuilder.CreateUAV(MarkedHeightfieldPageBuffer, PF_R32_UINT);
									PassParameters->PageUpdateIndirectArgBuffer = PageUpdateIndirectArgBuffer;
									PassParameters->PageUpdateTileBuffer = GraphBuilder.CreateSRV(PageUpdateTileBuffer, PF_R32_UINT);
									PassParameters->InfluenceRadius = ClipmapInfluenceRadius;
									PassParameters->PageCoordToPageWorldCenterScale = PageGridCoordToWorldCenterScale;
									PassParameters->PageCoordToPageWorldCenterBias = PageGridCoordToWorldCenterBias;
									PassParameters->PageWorldExtent = PageTileWorldExtentWithoutBorders;
									PassParameters->ClipmapVoxelExtent = ClipmapVoxelExtent.X;
									PassParameters->PageGridResolution = PageGridResolution;
									PassParameters->NumHeightfields = HeightfieldDescriptions.Num();
									PassParameters->InfluenceRadius = ClipmapInfluenceRadius;
									PassParameters->HeightfieldThickness = ClipmapVoxelSize.X * GGlobalDistanceFieldHeightFieldThicknessScale;
									PassParameters->HeightfieldTexture = HeightfieldTexture->Resource->TextureRHI;
									PassParameters->HeightfieldSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
									PassParameters->VisibilityTexture = VisibilityTexture ? VisibilityTexture->Resource->TextureRHI : GBlackTexture->TextureRHI;
									PassParameters->VisibilitySampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
									PassParameters->HeightfieldDescriptions = GraphBuilder.CreateSRV(HeightfieldDescriptionBuffer, EPixelFormat::PF_A32B32G32R32F);

									auto ComputeShader = View.ShaderMap->GetShader<FMarkHeightfieldPagesCS>();

									FComputeShaderUtils::AddPass(
										GraphBuilder,
										RDG_EVENT_NAME("MarkHeightfieldPages"),
										ComputeShader,
										PassParameters,
										PageUpdateIndirectArgBuffer,
										0);
								}
							}

							// Build heightfield page compose tile buffer
							{
								FRDGBufferRef BuildHeightfieldComposeTilesIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("BuildHeightfieldComposeTilesIndirectArgs"));

								{
									FBuildHeightfieldComposeTilesIndirectArgBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildHeightfieldComposeTilesIndirectArgBufferCS::FParameters>();
									PassParameters->RWBuildHeightfieldComposeTilesIndirectArgBuffer = GraphBuilder.CreateUAV(BuildHeightfieldComposeTilesIndirectArgBuffer, PF_R32_UINT);
									PassParameters->RWPageComposeHeightfieldIndirectArgBuffer = GraphBuilder.CreateUAV(PageComposeHeightfieldIndirectArgBuffer, PF_R32_UINT);
									PassParameters->PageUpdateIndirectArgBuffer = GraphBuilder.CreateSRV(PageUpdateIndirectArgBuffer, PF_R32_UINT);

									auto ComputeShader = View.ShaderMap->GetShader<FBuildHeightfieldComposeTilesIndirectArgBufferCS>();

									FComputeShaderUtils::AddPass(
										GraphBuilder,
										RDG_EVENT_NAME("BuildHeightfieldComposeTilesIndirectArgs"),
										ComputeShader,
										PassParameters,
										FIntVector(1, 1, 1));
								}

								{
									FBuildHeightfieldComposeTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildHeightfieldComposeTilesCS::FParameters>();
									PassParameters->View = View.ViewUniformBuffer;
									PassParameters->RWPageComposeHeightfieldIndirectArgBuffer = GraphBuilder.CreateUAV(PageComposeHeightfieldIndirectArgBuffer, PF_R32_UINT);
									PassParameters->RWPageComposeHeightfieldTileBuffer = GraphBuilder.CreateUAV(PageComposeHeightfieldTileBuffer, PF_R32_UINT);;
									PassParameters->PageUpdateTileBuffer = GraphBuilder.CreateSRV(PageUpdateTileBuffer, PF_R32_UINT);
									PassParameters->MarkedHeightfieldPageBuffer = GraphBuilder.CreateSRV(MarkedHeightfieldPageBuffer, PF_R32_UINT);
									PassParameters->PageUpdateIndirectArgBuffer = GraphBuilder.CreateSRV(PageUpdateIndirectArgBuffer, PF_R32_UINT);
									PassParameters->BuildHeightfieldComposeTilesIndirectArgBuffer = BuildHeightfieldComposeTilesIndirectArgBuffer;

									auto ComputeShader = View.ShaderMap->GetShader<FBuildHeightfieldComposeTilesCS>();

									FComputeShaderUtils::AddPass(
										GraphBuilder,
										RDG_EVENT_NAME("BuildHeightfieldComposeTiles"),
										ComputeShader,
										PassParameters,
										BuildHeightfieldComposeTilesIndirectArgBuffer,
										0);
								}
							}
						}

						const uint32 AverageCulledObjectsPerPage = FMath::Clamp(GAOGlobalDistanceFieldAverageCulledObjectsPerPage, 1, 8192);
						FRDGBufferRef CullGridAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("CullGridAllocator"));
						FRDGBufferRef CullGridObjectHeader = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 2 * PageGridSize), TEXT("CullGridObjectHeader"));
						FRDGBufferRef CullGridObjectArray = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PageGridSize * AverageCulledObjectsPerPage), TEXT("CullGridObjectArray"));

						// Cull objects into a cull grid
						{
							AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CullGridAllocator, PF_R32_UINT), 0);
							AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CullGridObjectHeader, PF_R32_UINT), 0);

							FCullObjectsToGridCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullObjectsToGridCS::FParameters>();
							PassParameters->RWCullGridAllocator = GraphBuilder.CreateUAV(CullGridAllocator, PF_R32_UINT);
							PassParameters->RWCullGridObjectHeader = GraphBuilder.CreateUAV(CullGridObjectHeader, PF_R32_UINT);
							PassParameters->RWCullGridObjectArray = GraphBuilder.CreateUAV(CullGridObjectArray, PF_R32_UINT);
							PassParameters->CullGridIndirectArgBuffer = PageUpdateIndirectArgBuffer;
							PassParameters->CullGridTileBuffer = GraphBuilder.CreateSRV(PageUpdateTileBuffer, PF_R32_UINT);
							PassParameters->ObjectIndexBuffer = GraphBuilder.CreateSRV(ObjectIndexBuffer, PF_R32_UINT);
							PassParameters->ObjectIndexNumBuffer = GraphBuilder.CreateSRV(ObjectIndexNumBuffer, PF_R32_UINT);
							PassParameters->SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
							PassParameters->SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
							PassParameters->CullGridResolution = PageGridResolution;
							PassParameters->CullGridCoordToWorldCenterScale = PageGridCoordToWorldCenterScale;
							PassParameters->CullGridCoordToWorldCenterBias = PageGridCoordToWorldCenterBias;
							PassParameters->CullTileWorldExtent = PageTileWorldExtent;
							PassParameters->InfluenceRadiusSq = ClipmapInfluenceRadius * ClipmapInfluenceRadius;

							auto ComputeShader = View.ShaderMap->GetShader<FCullObjectsToGridCS>();

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("CullObjectsToGrid"),
								ComputeShader,
								PassParameters,
								PageUpdateIndirectArgBuffer,
								0);
						}

						// Allocate and build page lists
						{
							FRDGBufferRef PageFreeListReturnAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("PageFreeListReturnAllocator"));
							FRDGBufferRef PageFreeListReturnBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), GlobalDistanceField::GetMaxPageNum()), TEXT("PageFreeListReturn"));

							AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageFreeListReturnAllocatorBuffer, PF_R32_UINT), 0);

							// Allocate pages for objects
							{
								FAllocatePagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAllocatePagesCS::FParameters>();
								PassParameters->View = View.ViewUniformBuffer;
								PassParameters->PageUpdateIndirectArgBuffer = PageUpdateIndirectArgBuffer;
								PassParameters->PageUpdateTileBuffer = GraphBuilder.CreateSRV(PageUpdateTileBuffer, PF_R32_UINT);
								PassParameters->MarkedHeightfieldPageBuffer = MarkedHeightfieldPageBuffer ? GraphBuilder.CreateSRV(MarkedHeightfieldPageBuffer, PF_R32_UINT) : nullptr;

								PassParameters->RWPageTableCombinedTexture = GraphBuilder.CreateUAV(PageTableCombinedTexture);
								PassParameters->RWPageTableLayerTexture = GraphBuilder.CreateUAV(PageTableLayerTexture);
								PassParameters->RWPageFreeListAllocatorBuffer = GraphBuilder.CreateUAV(PageFreeListAllocatorBuffer, PF_R32_SINT);
								PassParameters->PageFreeListBuffer = GraphBuilder.CreateSRV(PageFreeListBuffer, PF_R32_UINT);
								PassParameters->RWPageFreeListReturnAllocatorBuffer = GraphBuilder.CreateUAV(PageFreeListReturnAllocatorBuffer, PF_R32_UINT);
								PassParameters->RWPageFreeListReturnBuffer = GraphBuilder.CreateUAV(PageFreeListReturnBuffer, PF_R32_UINT);
								PassParameters->RWPageComposeTileBuffer = GraphBuilder.CreateUAV(PageComposeTileBuffer, PF_R32_UINT);
								PassParameters->RWPageComposeIndirectArgBuffer = GraphBuilder.CreateUAV(PageComposeIndirectArgBuffer, PF_R32_UINT);

								PassParameters->ParentPageTableLayerTexture = ParentPageTableLayerTexture;
								PassParameters->PageWorldExtent = PageTileWorldExtentWithoutBorders;
								PassParameters->PageWorldRadius = PageTileWorldExtentWithoutBorders.Size();
								PassParameters->ClipmapInfluenceRadius = ClipmapInfluenceRadius;
								PassParameters->PageGridResolution = PageGridResolution;
								PassParameters->InvPageGridResolution = FVector(1.0f) / FVector(PageGridResolution);
								PassParameters->GlobalDistanceFieldMaxPageNum = GGlobalDistanceFieldMaxPageNum;
								PassParameters->PageCoordToPageWorldCenterScale = PageGridCoordToWorldCenterScale;
								PassParameters->PageCoordToPageWorldCenterBias = PageGridCoordToWorldCenterBias;
								PassParameters->ClipmapVolumeWorldToUVAddAndMul = ClipmapVolumeWorldToUVAddAndMul;
								PassParameters->PageTableClipmapOffsetZ = ClipmapIndex * PageGridResolution.Z;

								PassParameters->CullGridObjectHeader = GraphBuilder.CreateSRV(CullGridObjectHeader, PF_R32_UINT);
								PassParameters->CullGridObjectArray = GraphBuilder.CreateSRV(CullGridObjectArray, PF_R32_UINT);
								PassParameters->CullGridResolution = PageGridResolution;

								PassParameters->DistanceFieldTexture = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
								PassParameters->DistanceFieldSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
								PassParameters->SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
								PassParameters->SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
								PassParameters->NumSceneObjects = DistanceFieldSceneData.NumObjectsInBuffer;

								FAllocatePagesCS::FPermutationDomain PermutationVector;
								PermutationVector.Set<FAllocatePagesCS::FMarkedHeightfieldPageBuffer>(MarkedHeightfieldPageBuffer != nullptr);
								PermutationVector.Set<FAllocatePagesCS::FComposeParentDistanceField>(ParentPageTableLayerTexture != nullptr);
								auto ComputeShader = View.ShaderMap->GetShader<FAllocatePagesCS>(PermutationVector);

								FComputeShaderUtils::AddPass(
									GraphBuilder,
									RDG_EVENT_NAME("AllocatePages"),
									ComputeShader,
									PassParameters,
									PageUpdateIndirectArgBuffer,
									0);
							}

							FRDGBufferRef FreeListReturnIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("FreeListReturnIndirectArgs"));

							// Setup free list return indirect dispatch arguments
							{
								FPageFreeListReturnIndirectArgBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPageFreeListReturnIndirectArgBufferCS::FParameters>();
								PassParameters->RWFreeListReturnIndirectArgBuffer = GraphBuilder.CreateUAV(FreeListReturnIndirectArgBuffer, PF_R32_UINT);
								PassParameters->RWPageFreeListAllocatorBuffer = GraphBuilder.CreateUAV(PageFreeListAllocatorBuffer, PF_R32_SINT);
								PassParameters->PageFreeListReturnAllocatorBuffer = GraphBuilder.CreateSRV(PageFreeListReturnAllocatorBuffer, PF_R32_UINT);

								auto ComputeShader = View.ShaderMap->GetShader<FPageFreeListReturnIndirectArgBufferCS>();

								FComputeShaderUtils::AddPass(
									GraphBuilder,
									RDG_EVENT_NAME("SetupPageFreeListRetunIndirectArgs"),
									ComputeShader,
									PassParameters,
									FIntVector(1, 1, 1));
							}

							// Return to the free list
							{
								FPageFreeListReturnCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPageFreeListReturnCS::FParameters>();
								PassParameters->FreeListReturnIndirectArgBuffer = FreeListReturnIndirectArgBuffer;
								PassParameters->RWPageFreeListAllocatorBuffer = GraphBuilder.CreateUAV(PageFreeListAllocatorBuffer, PF_R32_SINT);
								PassParameters->RWPageFreeListBuffer = GraphBuilder.CreateUAV(PageFreeListBuffer, PF_R32_UINT);
								PassParameters->PageFreeListReturnAllocatorBuffer = GraphBuilder.CreateSRV(PageFreeListReturnAllocatorBuffer, PF_R32_UINT);
								PassParameters->PageFreeListReturnBuffer = GraphBuilder.CreateSRV(PageFreeListReturnBuffer, PF_R32_UINT);

								auto ComputeShader = View.ShaderMap->GetShader<FPageFreeListReturnCS>();

								FComputeShaderUtils::AddPass(
									GraphBuilder,
									RDG_EVENT_NAME("ReturnToPageFreeList"),
									ComputeShader,
									PassParameters,
									FreeListReturnIndirectArgBuffer,
									0);
							}
						}

						// Compose the mesh SDFs into allocated pages
						{
							const FVector PageVoxelExtent = 0.5f * ClipmapSize / FVector(ClipmapResolution);
							const FVector PageCoordToVoxelCenterScale = ClipmapSize / FVector(ClipmapResolution);
							const FVector PageCoordToVoxelCenterBias = Clipmap.Bounds.Min + PageVoxelExtent;

							const uint32 PageComposeTileSize = 4;
							const FVector PageComposeTileWorldExtent = ClipmapVoxelExtent * PageComposeTileSize;

							FComposeObjectsIntoPagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeObjectsIntoPagesCS::FParameters>();
							PassParameters->View = View.ViewUniformBuffer;
							PassParameters->RWPageAtlasTexture = GraphBuilder.CreateUAV(PageAtlasTexture);
							PassParameters->ComposeIndirectArgBuffer = PageComposeIndirectArgBuffer;
							PassParameters->ComposeTileBuffer = GraphBuilder.CreateSRV(PageComposeTileBuffer, PF_R32_UINT);
							PassParameters->PageTableLayerTexture = PageTableLayerTexture;
							PassParameters->ParentPageTableLayerTexture = ParentPageTableLayerTexture;
							PassParameters->DistanceFieldTexture = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
							PassParameters->DistanceFieldSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
							PassParameters->CullGridObjectHeader = GraphBuilder.CreateSRV(CullGridObjectHeader, PF_R32_UINT);
							PassParameters->CullGridObjectArray = GraphBuilder.CreateSRV(CullGridObjectArray, PF_R32_UINT);
							PassParameters->ObjectIndexBuffer = GraphBuilder.CreateSRV(ObjectIndexBuffer, PF_R32_UINT);
							PassParameters->ObjectIndexNumBuffer = GraphBuilder.CreateSRV(ObjectIndexNumBuffer, PF_R32_UINT);
							PassParameters->SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
							PassParameters->SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
							PassParameters->NumSceneObjects = DistanceFieldSceneData.NumObjectsInBuffer;
							PassParameters->InfluenceRadius = ClipmapInfluenceRadius;
							PassParameters->InfluenceRadiusSq = ClipmapInfluenceRadius * ClipmapInfluenceRadius;
							PassParameters->ClipmapVoxelExtent = ClipmapVoxelExtent.X;
							PassParameters->CullGridResolution = PageGridResolution;
							PassParameters->PageGridResolution = PageGridResolution;
							PassParameters->InvPageGridResolution = FVector(1.0f) / FVector(PageGridResolution);
							PassParameters->ClipmapResolution = FIntVector(ClipmapResolution);
							PassParameters->PageCoordToVoxelCenterScale = PageCoordToVoxelCenterScale;
							PassParameters->PageCoordToVoxelCenterBias = PageCoordToVoxelCenterBias;
							PassParameters->ComposeTileWorldExtent = PageComposeTileWorldExtent;
							PassParameters->ClipmapMinBounds = Clipmap.Bounds.Min;
							PassParameters->PageCoordToPageWorldCenterScale = PageGridCoordToWorldCenterScale;
							PassParameters->PageCoordToPageWorldCenterBias = PageGridCoordToWorldCenterBias;
							PassParameters->ClipmapVolumeWorldToUVAddAndMul = ClipmapVolumeWorldToUVAddAndMul;
							PassParameters->PageTableClipmapOffsetZ = ClipmapIndex * PageGridResolution.Z;

							FComposeObjectsIntoPagesCS::FPermutationDomain PermutationVector;
							PermutationVector.Set<FComposeObjectsIntoPagesCS::FComposeParentDistanceField>(ParentPageTableLayerTexture != nullptr);
							auto ComputeShader = View.ShaderMap->GetShader<FComposeObjectsIntoPagesCS>(PermutationVector);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("ComposeObjectsIntoPages"),
								ComputeShader,
								PassParameters,
								PageComposeIndirectArgBuffer,
								0);
						}

						// Compose heightfields into global SDF pages
						if (UpdateRegionHeightfield.ComponentDescriptions.Num() > 0)
						{
							RDG_EVENT_SCOPE(GraphBuilder, "ComposeHeightfieldsIntoPages");

							const FVector PageVoxelExtent = 0.5f * ClipmapSize / FVector(ClipmapResolution);
							const FVector PageCoordToVoxelCenterScale = ClipmapSize / FVector(ClipmapResolution);
							const FVector PageCoordToVoxelCenterBias = Clipmap.Bounds.Min + PageVoxelExtent;

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

									FComposeHeightfieldsIntoPagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeHeightfieldsIntoPagesCS::FParameters>();
									PassParameters->View = View.ViewUniformBuffer;
									PassParameters->RWPageAtlasTexture = GraphBuilder.CreateUAV(PageAtlasTexture);
									PassParameters->ComposeIndirectArgBuffer = PageComposeHeightfieldIndirectArgBuffer;
									PassParameters->ComposeTileBuffer = GraphBuilder.CreateSRV(PageComposeHeightfieldTileBuffer, PF_R32_UINT);
									PassParameters->PageTableLayerTexture = PageTableLayerTexture;
									PassParameters->InfluenceRadius = ClipmapInfluenceRadius;
									PassParameters->PageCoordToVoxelCenterScale = PageCoordToVoxelCenterScale;
									PassParameters->PageCoordToVoxelCenterBias = PageCoordToVoxelCenterBias;
									PassParameters->ClipmapVoxelExtent = ClipmapVoxelExtent.X;
									PassParameters->PageGridResolution = PageGridResolution;
									PassParameters->InvPageGridResolution = FVector(1.0f) / FVector(PageGridResolution);
									PassParameters->PageCoordToPageWorldCenterScale = PageGridCoordToWorldCenterScale;
									PassParameters->PageCoordToPageWorldCenterBias = PageGridCoordToWorldCenterBias;
									PassParameters->ClipmapVolumeWorldToUVAddAndMul = ClipmapVolumeWorldToUVAddAndMul;
									PassParameters->PageTableClipmapOffsetZ = ClipmapIndex * PageGridResolution.Z;
									PassParameters->NumHeightfields = HeightfieldDescriptions.Num();
									PassParameters->InfluenceRadius = ClipmapInfluenceRadius;
									PassParameters->HeightfieldThickness = ClipmapVoxelSize.X * GGlobalDistanceFieldHeightFieldThicknessScale;
									PassParameters->HeightfieldTexture = HeightfieldTexture->Resource->TextureRHI;
									PassParameters->HeightfieldSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
									PassParameters->VisibilityTexture = VisibilityTexture ? VisibilityTexture->Resource->TextureRHI : GBlackTexture->TextureRHI;
									PassParameters->VisibilitySampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
									PassParameters->HeightfieldDescriptions = GraphBuilder.CreateSRV(HeightfieldDescriptionBuffer, EPixelFormat::PF_A32B32G32R32F);

									auto ComputeShader = View.ShaderMap->GetShader<FComposeHeightfieldsIntoPagesCS>();

									FComputeShaderUtils::AddPass(
										GraphBuilder,
										RDG_EVENT_NAME("ComposeHeightfield"),
										ComputeShader,
										PassParameters,
										PageComposeHeightfieldIndirectArgBuffer,
										0);
								}
							}
						}

						if (MipTexture && CacheType == GDF_Full)
						{
							RDG_EVENT_SCOPE(GraphBuilder, "Coarse Clipmap");

							const int32 ClipmapMipResolution = GlobalDistanceField::GetClipmapMipResolution();

							// Propagate distance field
							const int32 NumPropagationSteps = 5;
							for (int32 StepIndex = 0; StepIndex < NumPropagationSteps; ++StepIndex)
							{
								FRDGTextureRef PrevTexture = TempMipTexture;
								FRDGTextureRef NextTexture = MipTexture;
								uint32 PrevClipmapOffsetZ = 0;
								uint32 NextClipmapOffsetZ = ClipmapIndex * ClipmapMipResolution;

								if (StepIndex % 2 == 0)
								{
									Swap(PrevTexture, NextTexture);
									Swap(PrevClipmapOffsetZ, NextClipmapOffsetZ);
								}

								FPropagateMipDistanceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPropagateMipDistanceCS::FParameters>();
								PassParameters->View = View.ViewUniformBuffer;
								PassParameters->RWMipTexture = GraphBuilder.CreateUAV(NextTexture);
								PassParameters->PageTableCombinedTexture = PageTableCombinedTexture;
								PassParameters->PageAtlasTexture = PageAtlasTexture;
								PassParameters->GlobalDistanceFieldInvPageAtlasSize = FVector(1.0f) / FVector(GlobalDistanceField::GetPageAtlasSize());
								PassParameters->GlobalDistanceFieldClipmapSizeInPages = GlobalDistanceField::GetPageTableTextureResolution().X;
								PassParameters->PrevMipTexture = PrevTexture;
								PassParameters->ClipmapMipResolution = ClipmapMipResolution;
								PassParameters->ClipmapIndex = ClipmapIndex;
								PassParameters->PrevClipmapOffsetZ = PrevClipmapOffsetZ;
								PassParameters->ClipmapOffsetZ = NextClipmapOffsetZ;
								PassParameters->ClipmapUVScrollOffset = Clipmap.ScrollOffset / ClipmapSize;
								PassParameters->CoarseDistanceFieldValueScale = 1.0f / GlobalDistanceField::GetMipFactor();
								PassParameters->CoarseDistanceFieldValueBias = 0.5f - 0.5f / GlobalDistanceField::GetMipFactor();

								FPropagateMipDistanceCS::FPermutationDomain PermutationVector;
								PermutationVector.Set<FPropagateMipDistanceCS::FReadPages>(StepIndex == 0);
								auto ComputeShader = View.ShaderMap->GetShader<FPropagateMipDistanceCS>(PermutationVector);

								FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(FIntVector(ClipmapMipResolution), FPropagateMipDistanceCS::GetGroupSize());

								FComputeShaderUtils::AddPass(
									GraphBuilder,
									RDG_EVENT_NAME("Propagate step %d", StepIndex),
									ComputeShader,
									PassParameters,
									GroupSize);
							}
						}
					}
				}
			}

			for (int32 CacheType = StartCacheType; CacheType < GDF_Num; CacheType++)
			{
				if (PageTableLayerTextures[CacheType])
				{
					ConvertToUntrackedExternalTexture(GraphBuilder, PageTableLayerTextures[CacheType], GlobalDistanceFieldInfo.PageTableLayerTextures[CacheType], ERHIAccess::SRVMask);
				}
			}

			if (PageFreeListAllocatorBuffer)
			{
				ConvertToUntrackedExternalBuffer(GraphBuilder, PageFreeListAllocatorBuffer, GlobalDistanceFieldInfo.PageFreeListAllocatorBuffer, ERHIAccess::SRVMask);
			}

			if (PageFreeListBuffer)
			{
				ConvertToUntrackedExternalBuffer(GraphBuilder, PageFreeListBuffer, GlobalDistanceFieldInfo.PageFreeListBuffer, ERHIAccess::SRVMask);
			}

			if (PageAtlasTexture)
			{
				ConvertToUntrackedExternalTexture(GraphBuilder, PageAtlasTexture, GlobalDistanceFieldInfo.PageAtlasTexture, ERHIAccess::SRVMask);
			}

			if (PageTableCombinedTexture)
			{
				ConvertToUntrackedExternalTexture(GraphBuilder, PageTableCombinedTexture, GlobalDistanceFieldInfo.PageTableCombinedTexture, ERHIAccess::SRVMask);
			}

			if (MipTexture)
			{
				ConvertToUntrackedExternalTexture(GraphBuilder, MipTexture, GlobalDistanceFieldInfo.MipTexture, ERHIAccess::SRVMask);
			}
		}
	}

	if (GDFReadbackRequest && GlobalDistanceFieldInfo.Clipmaps.Num() > 0)
	{
		// Read back a clipmap
		ReadbackDistanceFieldClipmap(GraphBuilder.RHICmdList, GlobalDistanceFieldInfo);
	}

	if (GDFReadbackRequest && GlobalDistanceFieldInfo.Clipmaps.Num() > 0)
	{
		// Read back a clipmap
		ReadbackDistanceFieldClipmap(GraphBuilder.RHICmdList, GlobalDistanceFieldInfo);
	}
}
