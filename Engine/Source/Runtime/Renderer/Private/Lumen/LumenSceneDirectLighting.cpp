// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneDirectLighting.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenSceneUtils.h"
#include "DistanceFieldLightingShared.h"

int32 GLumenDirectLighting = 1;
FAutoConsoleVariableRef CVarLumenDirectLighting(
	TEXT("r.Lumen.DirectLighting"),
	GLumenDirectLighting,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenDirectLightingForceOffscreenShadowing = 0;
FAutoConsoleVariableRef CVarLumenDirectLightingForceOffscreenShadowing(
	TEXT("r.Lumen.DirectLighting.ForceOffscreenShadowing"),
	GLumenDirectLightingForceOffscreenShadowing,
	TEXT("Use offscreen shadowing for all lights casting shadows."),
	ECVF_RenderThreadSafe
);

int32 GLumenDirectLightingOffscreenShadowingTraceMeshSDFs = 1;
FAutoConsoleVariableRef CVarLumenDirectLightingOffscreenShadowingTraceMeshSDFs(
	TEXT("r.Lumen.DirectLighting.OffscreenShadowing.TraceMeshSDFs"),
	GLumenDirectLightingOffscreenShadowingTraceMeshSDFs,
	TEXT("Whether to trace against Mesh Signed Distance Fields for offscreen shadowing, or to trace against the lower resolution Global SDF."),
	ECVF_RenderThreadSafe
);

int32 GLumenDirectLightingBatchSize = 16;
FAutoConsoleVariableRef CVarLumenDirectLightingBatchSize(
	TEXT("r.Lumen.DirectLighting.BatchSize"),
	GLumenDirectLightingBatchSize,
	TEXT(""),
	ECVF_RenderThreadSafe
);

float GOffscreenShadowingMaxTraceDistance = 10000;
FAutoConsoleVariableRef CVarOffscreenShadowingMaxTraceDistance(
	TEXT("r.Lumen.DirectLighting.OffscreenShadowingMaxTraceDistance"),
	GOffscreenShadowingMaxTraceDistance,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GOffscreenShadowingTraceStepFactor = 5;
FAutoConsoleVariableRef CVarOffscreenShadowingTraceStepFactor(
	TEXT("r.Lumen.DirectLighting.OffscreenShadowingTraceStepFactor"),
	GOffscreenShadowingTraceStepFactor,
	TEXT(""),
	ECVF_RenderThreadSafe
	);


float GOffscreenShadowingSDFSurfaceBiasScale = 6;
FAutoConsoleVariableRef CVarOffscreenShadowingSDFSurfaceBiasScale(
	TEXT("r.Lumen.DirectLighting.OffscreenShadowingSDFSurfaceBiasScale"),
	GOffscreenShadowingSDFSurfaceBiasScale,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GShadowingSurfaceBias = 2;
FAutoConsoleVariableRef CVarShadowingSurfaceBias(
	TEXT("r.Lumen.DirectLighting.ShadowingSurfaceBias"),
	GShadowingSurfaceBias,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GShadowingSlopeScaledSurfaceBias = 2;
FAutoConsoleVariableRef CVarShadowingSlopeScaledSurfaceBias(
	TEXT("r.Lumen.DirectLighting.ShadowingSlopeScaledSurfaceBias"),
	GShadowingSlopeScaledSurfaceBias,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GLumenSceneCardDirectLightingUpdateFrequencyScale = .003f;
FAutoConsoleVariableRef CVarLumenSceneCardDirectLightingUpdateFrequencyScale(
	TEXT("r.Lumen.DirectLighting.CardUpdateFrequencyScale"),
	GLumenSceneCardDirectLightingUpdateFrequencyScale,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

BEGIN_SHADER_PARAMETER_STRUCT(FLightFunctionParameters, )
	SHADER_PARAMETER(FVector4, LightFunctionParameters)
	SHADER_PARAMETER(FMatrix, LightFunctionWorldToLight)
	SHADER_PARAMETER(FVector, LightFunctionParameters2)
END_SHADER_PARAMETER_STRUCT()

class FLumenCardDirectLightingPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FLumenCardDirectLightingPS, Material);

	FLumenCardDirectLightingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) 
		: FMaterialShader(Initializer) 
	{ 
		Bindings.BindForLegacyShaderParameters( 
			this, 
			Initializer.PermutationId, 
			Initializer.ParameterMap,
			*FParameters::FTypeInfo::GetStructMetadata(), 
			// Don't require full bindings, we use FMaterialShader::SetParameters
			false); 
	} 
	
	FLumenCardDirectLightingPS() {}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParameters, VolumeShadowingShaderParameters)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)	
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightFunctionParameters, LightFunctionParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, ObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldCulledObjectBufferParameters, CulledObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightTileIntersectionParameters, LightTileIntersectionParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldAtlasParameters, DistanceFieldAtlasParameters)
		SHADER_PARAMETER(FMatrix, WorldToShadow)
		SHADER_PARAMETER(float, TwoSidedMeshDistanceBias)
		SHADER_PARAMETER(float, StepFactor)
		SHADER_PARAMETER(float, TanLightSourceAngle)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, SlopeScaledSurfaceBias)
		SHADER_PARAMETER(float, SDFSurfaceBiasScale)
		SHADER_PARAMETER(uint32, ForceOffscreenShadowing)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>,ShadowMaskAtlas)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicallyShadowed	: SHADER_PERMUTATION_BOOL("DYNAMICALLY_SHADOWED");
	class FShadowed	: SHADER_PERMUTATION_BOOL("SHADOWED_LIGHT");
	class FTraceMeshSDFs	: SHADER_PERMUTATION_BOOL("OFFSCREEN_SHADOWING_TRACE_MESH_SDF");
	class FLightFunction : SHADER_PERMUTATION_BOOL("LIGHT_FUNCTION");
	class FLightType : SHADER_PERMUTATION_ENUM_CLASS("LIGHT_TYPE", ELumenLightType);
	class FRayTracingShadowPassCombine : SHADER_PERMUTATION_BOOL("HARDWARE_RAYTRACING_SHADOW_PASS_COMBINE");
	using FPermutationDomain = TShaderPermutationDomain<FLightType, FDynamicallyShadowed, FShadowed, FTraceMeshSDFs,
								FLightFunction, FRayTracingShadowPassCombine>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (!PermutationVector.Get<FShadowed>())
		{
			PermutationVector.Set<FDynamicallyShadowed>(false);
			PermutationVector.Set<FRayTracingShadowPassCombine>(false);
			PermutationVector.Set<FTraceMeshSDFs>(false);
		}

		if (PermutationVector.Get<FRayTracingShadowPassCombine>() || PermutationVector.Get<FLightType>() != ELumenLightType::Directional)
		{
			PermutationVector.Set<FTraceMeshSDFs>(false);
		}

		if (PermutationVector.Get<FRayTracingShadowPassCombine>())
		{
			PermutationVector.Set<FDynamicallyShadowed>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return Parameters.MaterialParameters.MaterialDomain == EMaterialDomain::MD_LightFunction && DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	FORCENOINLINE static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) 
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FLumenCardDirectLightingPS, TEXT("/Engine/Private/Lumen/LumenSceneDirectLighting.usf"), TEXT("LumenCardDirectLightingPS"), SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardDirectLighting, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardDirectLightingPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void SetupLightFunctionParameters(const FLightSceneInfo* LightSceneInfo, float ShadowFadeFraction, FLightFunctionParameters& OutParameters)
{
	const bool bIsSpotLight = LightSceneInfo->Proxy->GetLightType() == LightType_Spot;
	const bool bIsPointLight = LightSceneInfo->Proxy->GetLightType() == LightType_Point;
	const float TanOuterAngle = bIsSpotLight ? FMath::Tan(LightSceneInfo->Proxy->GetOuterConeAngle()) : 1.0f;

	OutParameters.LightFunctionParameters = FVector4(TanOuterAngle, ShadowFadeFraction, bIsSpotLight ? 1.0f : 0.0f, bIsPointLight ? 1.0f : 0.0f);

	const FVector Scale = LightSceneInfo->Proxy->GetLightFunctionScale();
	// Switch x and z so that z of the user specified scale affects the distance along the light direction
	const FVector InverseScale = FVector( 1.f / Scale.Z, 1.f / Scale.Y, 1.f / Scale.X );
	const FMatrix WorldToLight = LightSceneInfo->Proxy->GetWorldToLight() * FScaleMatrix(FVector(InverseScale));	

	OutParameters.LightFunctionWorldToLight = WorldToLight;

	const float PreviewShadowsMask = 0.0f;
	OutParameters.LightFunctionParameters2 = FVector(
		LightSceneInfo->Proxy->GetLightFunctionFadeDistance(),
		LightSceneInfo->Proxy->GetLightFunctionDisabledBrightness(),
		PreviewShadowsMask);
}

void SetupMeshSDFShadowInitializer(
	const FLightSceneInfo* LightSceneInfo,
	const FBox& LumenSceneBounds, 
	FSphere& OutShadowBounds,
	FWholeSceneProjectedShadowInitializer& OutInitializer)
{	
	FSphere Bounds;

	{
		// Get the 8 corners of the cascade's camera frustum, in world space
		FVector CascadeFrustumVerts[8];
		const FVector LumenSceneCenter = LumenSceneBounds.GetCenter();
		const FVector LumenSceneExtent = LumenSceneBounds.GetExtent();
		CascadeFrustumVerts[0] = LumenSceneCenter + FVector(LumenSceneExtent.X, LumenSceneExtent.Y, LumenSceneExtent.Z);  
		CascadeFrustumVerts[1] = LumenSceneCenter + FVector(LumenSceneExtent.X, LumenSceneExtent.Y, -LumenSceneExtent.Z); 
		CascadeFrustumVerts[2] = LumenSceneCenter + FVector(LumenSceneExtent.X, -LumenSceneExtent.Y, LumenSceneExtent.Z);    
		CascadeFrustumVerts[3] = LumenSceneCenter + FVector(LumenSceneExtent.X, -LumenSceneExtent.Y, -LumenSceneExtent.Z);  
		CascadeFrustumVerts[4] = LumenSceneCenter + FVector(-LumenSceneExtent.X, LumenSceneExtent.Y, LumenSceneExtent.Z);     
		CascadeFrustumVerts[5] = LumenSceneCenter + FVector(-LumenSceneExtent.X, LumenSceneExtent.Y, -LumenSceneExtent.Z);   
		CascadeFrustumVerts[6] = LumenSceneCenter + FVector(-LumenSceneExtent.X, -LumenSceneExtent.Y, LumenSceneExtent.Z);      
		CascadeFrustumVerts[7] = LumenSceneCenter + FVector(-LumenSceneExtent.X, -LumenSceneExtent.Y, -LumenSceneExtent.Z);   

		Bounds = FSphere(LumenSceneCenter, 0);
		for (int32 Index = 0; Index < 8; Index++)
		{
			Bounds.W = FMath::Max(Bounds.W, FVector::DistSquared(CascadeFrustumVerts[Index], Bounds.Center));
		}

		Bounds.W = FMath::Max(FMath::Sqrt(Bounds.W), 1.0f);

		ComputeShadowCullingVolume(true, CascadeFrustumVerts, -LightSceneInfo->Proxy->GetDirection(), OutInitializer.CascadeSettings.ShadowBoundsAccurate, OutInitializer.CascadeSettings.NearFrustumPlane, OutInitializer.CascadeSettings.FarFrustumPlane);
	}

	OutInitializer.CascadeSettings.ShadowSplitIndex = 0;

	const float ShadowExtent = Bounds.W / FMath::Sqrt(3.0f);
	const FBoxSphereBounds SubjectBounds(Bounds.Center, FVector(ShadowExtent, ShadowExtent, ShadowExtent), Bounds.W);
	OutInitializer.PreShadowTranslation = -Bounds.Center;
	OutInitializer.WorldToLight = FInverseRotationMatrix(LightSceneInfo->Proxy->GetDirection().GetSafeNormal().Rotation());
	OutInitializer.Scales = FVector2D(1.0f / Bounds.W, 1.0f / Bounds.W);
	OutInitializer.SubjectBounds = FBoxSphereBounds(FVector::ZeroVector, SubjectBounds.BoxExtent, SubjectBounds.SphereRadius);
	OutInitializer.WAxis = FVector4(0, 0, 0, 1);
	OutInitializer.MinLightW = FMath::Min<float>(-HALF_WORLD_MAX, -SubjectBounds.SphereRadius);
	const float MaxLightW = SubjectBounds.SphereRadius;
	OutInitializer.MaxDistanceToCastInLightW = MaxLightW - OutInitializer.MinLightW;
	OutInitializer.bRayTracedDistanceField = true;
	OutInitializer.CascadeSettings.bFarShadowCascade = false;

	const float SplitNear = -Bounds.W;
	const float SplitFar = Bounds.W;

	OutInitializer.CascadeSettings.SplitFarFadeRegion = 0.0f;
	OutInitializer.CascadeSettings.SplitNearFadeRegion = 0.0f;
	OutInitializer.CascadeSettings.SplitFar = SplitFar;
	OutInitializer.CascadeSettings.SplitNear = SplitNear;
	OutInitializer.CascadeSettings.FadePlaneOffset = SplitFar;
	OutInitializer.CascadeSettings.FadePlaneLength = 0;
	OutInitializer.CascadeSettings.CascadeBiasDistribution = 0;
	OutInitializer.CascadeSettings.ShadowSplitIndex = 0;

	OutShadowBounds = Bounds;
}

void CullMeshSDFsForLightCards(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	const FDistanceFieldObjectBufferParameters& ObjectBufferParameters,
	FMatrix& WorldToMeshSDFShadowValue,
	FDistanceFieldCulledObjectBufferParameters& CulledObjectBufferParameters,
	FLightTileIntersectionParameters& LightTileIntersectionParameters)
{
	const FVector LumenSceneViewOrigin = GetLumenSceneViewOrigin(View, GetNumLumenVoxelClipmaps() - 1);
	const FVector LumenSceneExtent = FVector(ComputeMaxCardUpdateDistanceFromCamera());
	const FBox LumenSceneBounds(LumenSceneViewOrigin - LumenSceneExtent, LumenSceneViewOrigin + LumenSceneExtent);

	FSphere MeshSDFShadowBounds;
	FWholeSceneProjectedShadowInitializer MeshSDFShadowInitializer;
	SetupMeshSDFShadowInitializer(LightSceneInfo, LumenSceneBounds, MeshSDFShadowBounds, MeshSDFShadowInitializer);

	const FMatrix FaceMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(0, 1, 0, 0),
		FPlane(-1, 0, 0, 0),
		FPlane(0, 0, 0, 1));

	const FMatrix TranslatedWorldToView = MeshSDFShadowInitializer.WorldToLight * FaceMatrix;

	float MaxSubjectZ = TranslatedWorldToView.TransformPosition(MeshSDFShadowInitializer.SubjectBounds.Origin).Z + MeshSDFShadowInitializer.SubjectBounds.SphereRadius;
	MaxSubjectZ = FMath::Min(MaxSubjectZ, MeshSDFShadowInitializer.MaxDistanceToCastInLightW);
	const float MinSubjectZ = FMath::Max(MaxSubjectZ - MeshSDFShadowInitializer.SubjectBounds.SphereRadius * 2, MeshSDFShadowInitializer.MinLightW);

	const FMatrix ScaleMatrix = FScaleMatrix( FVector( MeshSDFShadowInitializer.Scales.X, MeshSDFShadowInitializer.Scales.Y, 1.0f ) );
	const FMatrix ViewToClip = ScaleMatrix * FShadowProjectionMatrix(MinSubjectZ, MaxSubjectZ, MeshSDFShadowInitializer.WAxis);
	const FMatrix SubjectAndReceiverMatrix = TranslatedWorldToView * ViewToClip;

	int32 NumPlanes = MeshSDFShadowInitializer.CascadeSettings.ShadowBoundsAccurate.Planes.Num();
	const FPlane* PlaneData = MeshSDFShadowInitializer.CascadeSettings.ShadowBoundsAccurate.Planes.GetData();
	FVector4 LocalLightShadowBoundingSphereValue(0, 0, 0, 0);

	WorldToMeshSDFShadowValue = FTranslationMatrix(MeshSDFShadowInitializer.PreShadowTranslation) * SubjectAndReceiverMatrix;

	CullDistanceFieldObjectsForLight(
		GraphBuilder,
		View,
		LightSceneInfo->Proxy,
		DFPT_SignedDistanceField,
		WorldToMeshSDFShadowValue,
		NumPlanes,
		PlaneData,
		LocalLightShadowBoundingSphereValue,
		MeshSDFShadowBounds.W,
		ObjectBufferParameters,
		CulledObjectBufferParameters,
		LightTileIntersectionParameters);
}

void RenderDirectLightIntoLumenCards(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FEngineShowFlags& EngineShowFlags,
	TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	FRDGTextureRef FinalLightingAtlas,
	FRDGTextureRef OpacityAtlas,
	const FLightSceneInfo* LightSceneInfo,
	const FString& LightName,
	const FLumenCardScatterContext& CardScatterContext,
	int32 ScatterInstanceIndex,
	FLumenDirectLightingHardwareRayTracingData& LumenDirectLightingHardwareRayTracingData)
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
	const ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();
	bool bDynamicallyShadowed = false;
	bool bShadowed = LightSceneInfo->Proxy->CastsDynamicShadow();

	ELumenLightType LumenLightType = ELumenLightType::MAX;
	{
		switch (LightType)
		{
		case LightType_Directional: LumenLightType = ELumenLightType::Directional;	break;
		case LightType_Point:		LumenLightType = ELumenLightType::Point;		break;
		case LightType_Spot:		LumenLightType = ELumenLightType::Spot;			break;
		case LightType_Rect:		LumenLightType = ELumenLightType::Rect;			break;
		}
		check(LumenLightType != ELumenLightType::MAX);
	}

	extern const FProjectedShadowInfo* GetShadowForInjectionIntoVolumetricFog(FVisibleLightInfo& VisibleLightInfo);
	const FProjectedShadowInfo* ProjectedShadowInfo = GetShadowForInjectionIntoVolumetricFog(VisibleLightInfos[LightSceneInfo->Id]);

	bDynamicallyShadowed = ProjectedShadowInfo != nullptr;

	FDistanceFieldObjectBufferParameters ObjectBufferParameters;
	ObjectBufferParameters.SceneObjectBounds = Scene->DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
	ObjectBufferParameters.SceneObjectData = Scene->DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
	ObjectBufferParameters.NumSceneObjects = Scene->DistanceFieldSceneData.NumObjectsInBuffer;

	FLightTileIntersectionParameters LightTileIntersectionParameters;
	FDistanceFieldCulledObjectBufferParameters CulledObjectBufferParameters;
	FMatrix WorldToMeshSDFShadowValue = FMatrix::Identity;

	const bool bLumenUseHardwareRayTracedShadow = Lumen::UseHardwareRayTracedShadows(View) && bShadowed;
	const bool bTraceMeshSDFs = bShadowed 
		&& LumenLightType == ELumenLightType::Directional 
		&& DoesPlatformSupportDistanceFieldShadowing(View.GetShaderPlatform())
		&& GLumenDirectLightingOffscreenShadowingTraceMeshSDFs != 0
		&& Scene->DistanceFieldSceneData.NumObjectsInBuffer > 0;

	if (bLumenUseHardwareRayTracedShadow)
	{
		RenderHardwareRayTracedShadowIntoLumenCards(
			GraphBuilder, Scene, View, OpacityAtlas, LightSceneInfo, 
			LightName,CardScatterContext, ScatterInstanceIndex, 
			LumenDirectLightingHardwareRayTracingData, bDynamicallyShadowed, LumenLightType);
	}
	else if (bTraceMeshSDFs)
	{
		CullMeshSDFsForLightCards(GraphBuilder, Scene, View, LightSceneInfo, ObjectBufferParameters, WorldToMeshSDFShadowValue, CulledObjectBufferParameters, LightTileIntersectionParameters);
	}

	FLumenCardDirectLighting* PassParameters = GraphBuilder.AllocParameters<FLumenCardDirectLighting>();
	{
		PassParameters->RenderTargets[0] = FRenderTargetBinding(FinalLightingAtlas, ERenderTargetLoadAction::ELoad);
		PassParameters->VS.InfluenceSphere = FVector4(LightBounds.Center, LightBounds.W);
		PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->VS.CardScatterParameters = CardScatterContext.Parameters;
		PassParameters->VS.ScatterInstanceIndex = ScatterInstanceIndex;
		PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;

		GetVolumeShadowingShaderParameters(
			View,
			LightSceneInfo,
			ProjectedShadowInfo,
			0,
			bDynamicallyShadowed,
			PassParameters->PS.VolumeShadowingShaderParameters);

		FDeferredLightUniformStruct DeferredLightUniforms = GetDeferredLightParameters(View, *LightSceneInfo);

		if (LightSceneInfo->Proxy->IsInverseSquared())
		{
			DeferredLightUniforms.LightParameters.FalloffExponent = 0;
		}

		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->PS.OpacityAtlas = OpacityAtlas;
		DeferredLightUniforms.LightParameters.Color *= LightSceneInfo->Proxy->GetIndirectLightingScale();
		PassParameters->PS.DeferredLightUniforms = CreateUniformBufferImmediate(DeferredLightUniforms, UniformBuffer_SingleDraw);
		PassParameters->PS.ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
		SetupLightFunctionParameters(LightSceneInfo, 1.0f, PassParameters->PS.LightFunctionParameters);
		
		PassParameters->PS.ObjectBufferParameters = ObjectBufferParameters;
		PassParameters->PS.CulledObjectBufferParameters = CulledObjectBufferParameters;
		PassParameters->PS.LightTileIntersectionParameters = LightTileIntersectionParameters;

		FDistanceFieldAtlasParameters DistanceFieldAtlasParameters;
		DistanceFieldAtlasParameters.DistanceFieldTexture = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
		DistanceFieldAtlasParameters.DistanceFieldSampler = TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
		DistanceFieldAtlasParameters.DistanceFieldAtlasTexelSize = FVector(1.0f / GDistanceFieldVolumeTextureAtlas.GetSizeX(), 1.0f / GDistanceFieldVolumeTextureAtlas.GetSizeY(), 1.0f / GDistanceFieldVolumeTextureAtlas.GetSizeZ());

		PassParameters->PS.DistanceFieldAtlasParameters = DistanceFieldAtlasParameters;
		PassParameters->PS.WorldToShadow = WorldToMeshSDFShadowValue;
		extern float GTwoSidedMeshDistanceBias;
		PassParameters->PS.TwoSidedMeshDistanceBias = GTwoSidedMeshDistanceBias;

		PassParameters->PS.TanLightSourceAngle = FMath::Tan(LightSceneInfo->Proxy->GetLightSourceAngle());
		PassParameters->PS.MaxTraceDistance = GOffscreenShadowingMaxTraceDistance;
		PassParameters->PS.StepFactor = FMath::Clamp(GOffscreenShadowingTraceStepFactor, .1f, 10.0f);
		PassParameters->PS.SurfaceBias = FMath::Clamp(GShadowingSurfaceBias, .01f, 100.0f);
		PassParameters->PS.SlopeScaledSurfaceBias = FMath::Clamp(GShadowingSlopeScaledSurfaceBias, .01f, 100.0f);
		PassParameters->PS.SDFSurfaceBiasScale = FMath::Clamp(GOffscreenShadowingSDFSurfaceBiasScale, .01f, 100.0f);
		PassParameters->PS.ForceOffscreenShadowing = GLumenDirectLightingForceOffscreenShadowing;

		if (bLumenUseHardwareRayTracedShadow)
		{
			PassParameters->PS.ShadowMaskAtlas = LumenDirectLightingHardwareRayTracingData.ShadowMaskAtlas;
		}
	}

	FRasterizeToCardsVS::FPermutationDomain VSPermutationVector;
	VSPermutationVector.Set< FRasterizeToCardsVS::FClampToInfluenceSphere >(LightType != LightType_Directional);
	auto VertexShader = View.ShaderMap->GetShader<FRasterizeToCardsVS>(VSPermutationVector);
	const FMaterialRenderProxy* LightFunctionMaterialProxy = LightSceneInfo->Proxy->GetLightFunctionMaterial();
	bool bUseLightFunction = true;

	if (!LightFunctionMaterialProxy
		|| !LightFunctionMaterialProxy->GetIncompleteMaterialWithFallback(Scene->GetFeatureLevel()).IsLightFunction()
		|| !EngineShowFlags.LightFunctions)
	{
		bUseLightFunction = false;
		LightFunctionMaterialProxy = UMaterial::GetDefaultMaterial(MD_LightFunction)->GetRenderProxy();
	}

	FLumenCardDirectLightingPS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FLumenCardDirectLightingPS::FLightType >(LumenLightType);
	PermutationVector.Set< FLumenCardDirectLightingPS::FDynamicallyShadowed >(bDynamicallyShadowed);
	PermutationVector.Set< FLumenCardDirectLightingPS::FShadowed >(bShadowed);
	PermutationVector.Set< FLumenCardDirectLightingPS::FTraceMeshSDFs >(bTraceMeshSDFs);
	PermutationVector.Set< FLumenCardDirectLightingPS::FLightFunction >(bUseLightFunction);
	PermutationVector.Set< FLumenCardDirectLightingPS::FRayTracingShadowPassCombine>(bLumenUseHardwareRayTracedShadow);
	
	PermutationVector = FLumenCardDirectLightingPS::RemapPermutation(PermutationVector);

	const FMaterial& Material = LightFunctionMaterialProxy->GetMaterialWithFallback(Scene->GetFeatureLevel(), LightFunctionMaterialProxy);
	const FMaterialShaderMap* MaterialShaderMap = Material.GetRenderingThreadShaderMap();
	auto PixelShader = MaterialShaderMap->GetShader<FLumenCardDirectLightingPS>(PermutationVector);

	ClearUnusedGraphResources(PixelShader, &PassParameters->PS);

	const uint32 CardIndirectArgOffset = CardScatterContext.GetIndirectArgOffset(ScatterInstanceIndex);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("%s %s", *LightName, bDynamicallyShadowed ? TEXT("Shadowmap") : TEXT("")),
		PassParameters,
		ERDGPassFlags::Raster,
		[MaxAtlasSize = LumenSceneData.MaxAtlasSize, PassParameters, LightSceneInfo, VertexShader, PixelShader, GlobalShaderMap = View.ShaderMap, LightFunctionMaterialProxy, &Material, &View, CardIndirectArgOffset](FRHICommandListImmediate& RHICmdList)
		{
			DrawQuadsToAtlas(
				MaxAtlasSize,
				VertexShader,
				PixelShader,
				PassParameters,
				GlobalShaderMap,
				TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One>::GetRHI(),
				RHICmdList,
				[LightFunctionMaterialProxy, &Material, &View](FRHICommandListImmediate& RHICmdList, TShaderRefBase<FLumenCardDirectLightingPS, FShaderMapPointerTable> Shader, FRHIPixelShader* ShaderRHI, const FLumenCardDirectLightingPS::FParameters& Parameters)
				{
					Shader->SetParameters(RHICmdList, ShaderRHI, LightFunctionMaterialProxy, Material, View);
				},
				CardIndirectArgOffset);
		});
}

void FDeferredShadingSceneRenderer::RenderDirectLightingForLumenScene(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef FinalLightingAtlas,
	FRDGTextureRef OpacityAtlas,
	FGlobalShaderMap* GlobalShaderMap,
	const FLumenCardScatterContext& VisibleCardScatterContext)
{
	LLM_SCOPE_BYTAG(Lumen);

	if (GLumenDirectLighting)
	{
		check(Lumen::ShouldPrepareGlobalDistanceField(ShaderPlatform));
		RDG_EVENT_SCOPE(GraphBuilder, "DirectLighting");
		QUICK_SCOPE_CYCLE_COUNTER(RenderDirectLightingForLumenScene);

		const FViewInfo& MainView = Views[0];
		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
		const bool bLumenUseHardwareRayTracedShadow = Lumen::UseHardwareRayTracedShadows(MainView);
		FLumenDirectLightingHardwareRayTracingData LumenDirectLightingHardwareRayTracingData;
		
		if(bLumenUseHardwareRayTracedShadow)
		{
			LumenDirectLightingHardwareRayTracingData.Initialize(GraphBuilder, Scene);
		}

		TArray<const FLightSceneInfo*, TInlineAllocator<64>> GatheredLocalLights;

		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
			const FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

			if (LightSceneInfo->ShouldRenderLightViewIndependent()
				&& LightSceneInfo->ShouldRenderLight(MainView, true)
				&& LightSceneInfo->Proxy->GetIndirectLightingScale() > 0.0f)
			{
				const ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();

				if (LightType == LightType_Directional)
				{
					// Doesn't require culling, just draw immediately

					FString LightNameWithLevel;
					FSceneRenderer::GetLightNameForDrawEvent(LightSceneInfo->Proxy, LightNameWithLevel);

					RenderDirectLightIntoLumenCards(
						GraphBuilder,
						Scene,
						MainView,
						ViewFamily.EngineShowFlags,
						VisibleLightInfos,
						FinalLightingAtlas,
						OpacityAtlas,
						LightSceneInfo,
						LightNameWithLevel,
						VisibleCardScatterContext,
						0,
						LumenDirectLightingHardwareRayTracingData);
				}
				else
				{
					GatheredLocalLights.Add(LightSceneInfo);
				}
			}
		}

		const int32 LightBatchSize = FMath::Clamp(GLumenDirectLightingBatchSize, 1, 256);

		// Batched light culling and drawing
		for (int32 LightBatchIndex = 0; LightBatchIndex * LightBatchSize < GatheredLocalLights.Num(); ++LightBatchIndex)
		{
			const int32 FirstLightIndex = LightBatchIndex * LightBatchSize;
			const int32 LastLightIndex = FMath::Min((LightBatchIndex + 1) * LightBatchSize, GatheredLocalLights.Num());

			FLumenCardScatterContext CardScatterContext;

			{
				RDG_EVENT_SCOPE(GraphBuilder, "Cull Cards %d Lights", LastLightIndex - FirstLightIndex);

				CardScatterContext.Init(
					GraphBuilder,
					MainView,
					LumenSceneData,
					LumenCardRenderer,
					ECullCardsMode::OperateOnSceneForceUpdateForCardsToRender,
					LightBatchSize);

				for (int32 LightIndex = FirstLightIndex; LightIndex < LastLightIndex; ++LightIndex)
				{
					const int32 ScatterInstanceIndex = LightIndex - FirstLightIndex;
					const FLightSceneInfo* LightSceneInfo = GatheredLocalLights[LightIndex];
					const ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();
					const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();

					ECullCardsShapeType ShapeType = ECullCardsShapeType::None;

					if (LightType == LightType_Point)
					{
						ShapeType = ECullCardsShapeType::PointLight;
					}
					else if (LightType == LightType_Spot)
					{
						ShapeType = ECullCardsShapeType::SpotLight;
					}
					else if (LightType == LightType_Rect)
					{
						ShapeType = ECullCardsShapeType::RectLight;
					}
					else
					{
						ensureMsgf(false, TEXT("Need Lumen card culling for new light type"));
					}

					FCullCardsShapeParameters ShapeParameters;
					ShapeParameters.InfluenceSphere = FVector4(LightBounds.Center, LightBounds.W);
					ShapeParameters.LightPosition = LightSceneInfo->Proxy->GetPosition();
					ShapeParameters.LightDirection = LightSceneInfo->Proxy->GetDirection();
					ShapeParameters.LightRadius = LightSceneInfo->Proxy->GetRadius();
					ShapeParameters.CosConeAngle = FMath::Cos(LightSceneInfo->Proxy->GetOuterConeAngle());
					ShapeParameters.SinConeAngle = FMath::Sin(LightSceneInfo->Proxy->GetOuterConeAngle());

					CardScatterContext.CullCardsToShape(
						GraphBuilder,
						MainView,
						LumenSceneData,
						LumenCardRenderer,
						ShapeType,
						ShapeParameters,
						GLumenSceneCardDirectLightingUpdateFrequencyScale,
						ScatterInstanceIndex);
				}

				CardScatterContext.BuildScatterIndirectArgs(
					GraphBuilder,
					MainView);
			}

			{
				RDG_EVENT_SCOPE(GraphBuilder, "Draw %d Lights", LastLightIndex - FirstLightIndex);

				for (int32 LightIndex = FirstLightIndex; LightIndex < LastLightIndex; ++LightIndex)
				{
					const int32 ScatterInstanceIndex = LightIndex - FirstLightIndex;
					const FLightSceneInfo* LightSceneInfo = GatheredLocalLights[LightIndex];

					FString LightNameWithLevel;
					FSceneRenderer::GetLightNameForDrawEvent(LightSceneInfo->Proxy, LightNameWithLevel);

					RenderDirectLightIntoLumenCards(
						GraphBuilder,
						Scene,
						MainView,
						ViewFamily.EngineShowFlags,
						VisibleLightInfos,
						FinalLightingAtlas,
						OpacityAtlas,
						LightSceneInfo,
						LightNameWithLevel,
						CardScatterContext,
						ScatterInstanceIndex,
						LumenDirectLightingHardwareRayTracingData);
				}
			}
		}
	}
}
