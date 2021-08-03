// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneDirectLighting.cpp
=============================================================================*/

#include "LumenSceneDirectLighting.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenSceneUtils.h"
#include "DistanceFieldLightingShared.h"
#include "VirtualShadowMaps/VirtualShadowMapClipmap.h"
#include "VolumetricCloudRendering.h"

int32 GLumenDirectLighting = 1;
FAutoConsoleVariableRef CVarLumenDirectLighting(
	TEXT("r.LumenScene.DirectLighting"),
	GLumenDirectLighting,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenDirectLightingForceForceShadowMaps = 0;
FAutoConsoleVariableRef CVarLumenDirectLightingForceShadowMaps(
	TEXT("r.LumenScene.DirectLighting.ForceShadowMaps"),
	GLumenDirectLightingForceForceShadowMaps,
	TEXT("Use shadow maps for all lights casting shadows."),
	ECVF_RenderThreadSafe
);

int32 GLumenDirectLightingForceOffscreenShadowing = 0;
FAutoConsoleVariableRef CVarLumenDirectLightingForceOffscreenShadowing(
	TEXT("r.LumenScene.DirectLighting.ForceOffscreenShadowing"),
	GLumenDirectLightingForceOffscreenShadowing,
	TEXT("Use offscreen shadowing for all lights casting shadows."),
	ECVF_RenderThreadSafe
);

int32 GLumenDirectLightingOffscreenShadowingTraceMeshSDFs = 1;
FAutoConsoleVariableRef CVarLumenDirectLightingOffscreenShadowingTraceMeshSDFs(
	TEXT("r.LumenScene.DirectLighting.OffscreenShadowing.TraceMeshSDFs"),
	GLumenDirectLightingOffscreenShadowingTraceMeshSDFs,
	TEXT("Whether to trace against Mesh Signed Distance Fields for offscreen shadowing, or to trace against the lower resolution Global SDF."),
	ECVF_RenderThreadSafe
);

int32 GLumenDirectLightingBatchSize = 16;
FAutoConsoleVariableRef CVarLumenDirectLightingBatchSize(
	TEXT("r.LumenScene.DirectLighting.BatchSize"),
	GLumenDirectLightingBatchSize,
	TEXT(""),
	ECVF_RenderThreadSafe
);

float GOffscreenShadowingMaxTraceDistance = 15000;
FAutoConsoleVariableRef CVarOffscreenShadowingMaxTraceDistance(
	TEXT("r.LumenScene.DirectLighting.OffscreenShadowingMaxTraceDistance"),
	GOffscreenShadowingMaxTraceDistance,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GOffscreenShadowingTraceStepFactor = 5;
FAutoConsoleVariableRef CVarOffscreenShadowingTraceStepFactor(
	TEXT("r.LumenScene.DirectLighting.OffscreenShadowingTraceStepFactor"),
	GOffscreenShadowingTraceStepFactor,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GOffscreenShadowingSDFSurfaceBiasScale = 6;
FAutoConsoleVariableRef CVarOffscreenShadowingSDFSurfaceBiasScale(
	TEXT("r.LumenScene.DirectLighting.OffscreenShadowingSDFSurfaceBiasScale"),
	GOffscreenShadowingSDFSurfaceBiasScale,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GShadowingSurfaceBias = 2;
FAutoConsoleVariableRef CVarShadowingSurfaceBias(
	TEXT("r.LumenScene.DirectLighting.ShadowingSurfaceBias"),
	GShadowingSurfaceBias,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GShadowingSlopeScaledSurfaceBias = 4.0f;
FAutoConsoleVariableRef CVarShadowingSlopeScaledSurfaceBias(
	TEXT("r.LumenScene.DirectLighting.ShadowingSlopeScaledSurfaceBias"),
	GShadowingSlopeScaledSurfaceBias,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenDirectLightingCloudTransmittance = 1;
FAutoConsoleVariableRef CVarLumenDirectLightingCloudTransmittance(
	TEXT("r.LumenScene.DirectLighting.CloudTransmittance"),
	GLumenDirectLightingCloudTransmittance,
	TEXT("Whether to sample cloud shadows when avaible."),
	ECVF_RenderThreadSafe
);

int32 GLumenDirectLightingVirtualShadowMap = 1;
FAutoConsoleVariableRef CVarLumenDirectLightingVirtualShadowMap(
	TEXT("r.LumenScene.DirectLighting.VirtualShadowMap"),
	GLumenDirectLightingVirtualShadowMap,
	TEXT("Whether to sample virtual shadow when avaible."),
	ECVF_RenderThreadSafe
);

float GLumenDirectLightingVirtualShadowMapBias = 7.0f;
FAutoConsoleVariableRef CVarLumenDirectLightingVirtualShadowMapBias(
	TEXT("r.LumenScene.DirectLighting.VirtualShadowMapBias"),
	GLumenDirectLightingVirtualShadowMapBias,
	TEXT("Bias for sampling virtual shadow maps."),
	ECVF_RenderThreadSafe
);

float GLumenSceneCardDirectLightingUpdateFrequencyScale = .003f;
FAutoConsoleVariableRef CVarLumenSceneCardDirectLightingUpdateFrequencyScale(
	TEXT("r.LumenScene.DirectLighting.CardUpdateFrequencyScale"),
	GLumenSceneCardDirectLightingUpdateFrequencyScale,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

bool Lumen::UseVirtualShadowMaps()
{
	return GLumenDirectLightingVirtualShadowMap != 0;
}

float Lumen::GetSurfaceCacheOffscreenShadowingMaxTraceDistance()
{
	return FMath::Max(GOffscreenShadowingMaxTraceDistance, 0.0f);
}

void Lumen::SetDirectLightingDeferredLightUniformBuffer(
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	TUniformBufferBinding<FDeferredLightUniformStruct>& UniformBuffer)
{
	FDeferredLightUniformStruct DeferredLightUniforms = GetDeferredLightParameters(View, *LightSceneInfo);
	if (LightSceneInfo->Proxy->IsInverseSquared())
	{
		DeferredLightUniforms.LightParameters.FalloffExponent = 0;
	}
	DeferredLightUniforms.LightParameters.Color *= LightSceneInfo->Proxy->GetIndirectLightingScale();

	UniformBuffer = CreateUniformBufferImmediate(DeferredLightUniforms, UniformBuffer_SingleDraw);
}

BEGIN_SHADER_PARAMETER_STRUCT(FLightFunctionParameters, )
	SHADER_PARAMETER(FVector4, LightFunctionParameters)
	SHADER_PARAMETER(FMatrix44f, LightFunctionWorldToLight)
	SHADER_PARAMETER(FVector3f, LightFunctionParameters2)
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
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTileScatterParameters, CardScatterParameters)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParameters, VolumeShadowingShaderParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightFunctionParameters, LightFunctionParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightCloudTransmittanceParameters, LightCloudTransmittanceParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ShadowMaskTiles)
		SHADER_PARAMETER(float, TwoSidedMeshDistanceBias)
		SHADER_PARAMETER(float, StepFactor)
		SHADER_PARAMETER(float, TanLightSourceAngle)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, SlopeScaledSurfaceBias)
		SHADER_PARAMETER(float, SDFSurfaceBiasScale)
		SHADER_PARAMETER(float, VirtualShadowMapSurfaceBias)
		SHADER_PARAMETER(int32, VirtualShadowMapId)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ShadowMaskAtlas)
		SHADER_PARAMETER(int, ShadowMaskIndex)
		SHADER_PARAMETER(uint32, UseIESProfile)
		SHADER_PARAMETER_TEXTURE(Texture2D,IESTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState,IESTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	class FShadowMask : SHADER_PERMUTATION_BOOL("SHADOW_MASK");
	class FLightFunction : SHADER_PERMUTATION_BOOL("LIGHT_FUNCTION");
	class FCloudTransmittance : SHADER_PERMUTATION_BOOL("USE_CLOUD_TRANSMITTANCE");
	class FLightType : SHADER_PERMUTATION_ENUM_CLASS("LIGHT_TYPE", ELumenLightType);
	using FPermutationDomain = TShaderPermutationDomain<FLightType, FShadowMask, FLightFunction, FCloudTransmittance>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (!PermutationVector.Get<FShadowMask>())
		{
			PermutationVector.Set<FCloudTransmittance>(false);
		}

		if (PermutationVector.Get<FLightType>() != ELumenLightType::Directional)
		{
			PermutationVector.Set<FCloudTransmittance>(false);
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
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FLumenCardDirectLightingPS, TEXT("/Engine/Private/Lumen/LumenSceneDirectLighting.usf"), TEXT("LumenCardDirectLightingPS"), SF_Pixel);

class FSampleShadowMapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSampleShadowMapCS)
	SHADER_USE_PARAMETER_STRUCT(FSampleShadowMapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWShadowMaskTiles)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTileScatterParameters, CardScatterParameters)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParameters, VolumeShadowingShaderParameters)
		SHADER_PARAMETER(FMatrix44f, WorldToShadow)
		SHADER_PARAMETER(float, TwoSidedMeshDistanceBias)
		SHADER_PARAMETER(float, StepFactor)
		SHADER_PARAMETER(float, TanLightSourceAngle)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, SlopeScaledSurfaceBias)
		SHADER_PARAMETER(float, SDFSurfaceBiasScale)
		SHADER_PARAMETER(float, VirtualShadowMapSurfaceBias)
		SHADER_PARAMETER(int32, VirtualShadowMapId)
		SHADER_PARAMETER(uint32, ForceShadowMaps)
		SHADER_PARAMETER(uint32, ForceOffscreenShadowing)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicallyShadowed : SHADER_PERMUTATION_BOOL("DYNAMICALLY_SHADOWED");
	class FVirtualShadowMap : SHADER_PERMUTATION_BOOL("VIRTUAL_SHADOW_MAP");
	class FLightType : SHADER_PERMUTATION_ENUM_CLASS("LIGHT_TYPE", ELumenLightType);
	using FPermutationDomain = TShaderPermutationDomain<FLightType, FDynamicallyShadowed, FVirtualShadowMap>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FSampleShadowMapCS, "/Engine/Private/Lumen/LumenSceneDirectLightingShadowMask.usf", "SampleShadowMapCS", SF_Compute);

class FTraceDistanceFieldShadowsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTraceDistanceFieldShadowsCS)
	SHADER_USE_PARAMETER_STRUCT(FTraceDistanceFieldShadowsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWShadowMaskTiles)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTileScatterParameters, CardScatterParameters)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, ObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldCulledObjectBufferParameters, CulledObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightTileIntersectionParameters, LightTileIntersectionParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldAtlasParameters, DistanceFieldAtlasParameters)
		SHADER_PARAMETER(FMatrix44f, WorldToShadow)
		SHADER_PARAMETER(float, TwoSidedMeshDistanceBias)
		SHADER_PARAMETER(float, StepFactor)
		SHADER_PARAMETER(float, TanLightSourceAngle)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, SlopeScaledSurfaceBias)
		SHADER_PARAMETER(float, SDFSurfaceBiasScale)
	END_SHADER_PARAMETER_STRUCT()

	class FTraceMeshSDFs : SHADER_PERMUTATION_BOOL("OFFSCREEN_SHADOWING_TRACE_MESH_SDF");
	class FLightType : SHADER_PERMUTATION_ENUM_CLASS("LIGHT_TYPE", ELumenLightType);
	using FPermutationDomain = TShaderPermutationDomain<FLightType, FTraceMeshSDFs>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	FORCENOINLINE static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) 
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FTraceDistanceFieldShadowsCS, "/Engine/Private/Lumen/LumenSceneDirectLightingShadowMask.usf", "TraceDistanceFieldShadowsCS", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardDirectLighting, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardTilesVS::FParameters, VS)
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

FLumenShadowSetup GetShadowForLumenDirectLighting(FVisibleLightInfo& VisibleLightInfo)
{
	FLumenShadowSetup ShadowSetup;
	ShadowSetup.VirtualShadowMap = nullptr;
	ShadowSetup.DenseShadowMap = nullptr;

	for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.ShadowsToProject.Num(); ShadowIndex++)
	{
		FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo.ShadowsToProject[ShadowIndex];
		if (ProjectedShadowInfo->bIncludeInScreenSpaceShadowMask 
			&& ProjectedShadowInfo->bWholeSceneShadow 
			&& !ProjectedShadowInfo->bRayTracedDistanceField)
		{
			if (ProjectedShadowInfo->HasVirtualShadowMap())
			{
				ShadowSetup.VirtualShadowMap = ProjectedShadowInfo;
			}
			else if (ProjectedShadowInfo->bAllocated)
			{
				ShadowSetup.DenseShadowMap = ProjectedShadowInfo;
			}
		}
	}

	return ShadowSetup;
}

const FProjectedShadowInfo* GetShadowForInjectionIntoVolumetricFog(const FVisibleLightInfo & VisibleLightInfo);

void SetupLumenLight(FRDGBuilder& GraphBuilder, const FLumenSceneData& LumenSceneData, const FLightSceneInfo* LightSceneInfo, FLumenLight& LumenLight)
{
	LumenLight.LightSceneInfo = LightSceneInfo;
	FSceneRenderer::GetLightNameForDrawEvent(LightSceneInfo->Proxy, LumenLight.Name);

	const ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();
	LumenLight.Type = ELumenLightType::MAX;
	{
		switch (LightType)
		{
		case LightType_Directional: LumenLight.Type = ELumenLightType::Directional;	break;
		case LightType_Point:		LumenLight.Type = ELumenLightType::Point;		break;
		case LightType_Spot:		LumenLight.Type = ELumenLightType::Spot;		break;
		case LightType_Rect:		LumenLight.Type = ELumenLightType::Rect;		break;
		}
		check(LumenLight.Type != ELumenLightType::MAX);
	}

	// 2 bits per shadow mask texel
	const int32 ShadowMaskTileSize = Lumen::CardTileSize;
	const uint32 MaxShadowMaskX = FMath::DivideAndRoundUp(LumenSceneData.GetPhysicalAtlasSize().X, ShadowMaskTileSize);
	const uint32 MaxShadowMaskY = FMath::DivideAndRoundUp(LumenSceneData.GetPhysicalAtlasSize().Y, ShadowMaskTileSize);
	const uint32 MaxShadowMaskTiles = 4 * MaxShadowMaskX * MaxShadowMaskY;

	if (LightSceneInfo->Proxy->CastsDynamicShadow())
	{
		LumenLight.ShadowMaskTiles = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxShadowMaskTiles), TEXT("Lumen.ShadowMaskTiles"));
	}
	else
	{
		LumenLight.ShadowMaskTiles = nullptr;
	}
}

void RenderDirectLightIntoLumenCards(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenCardRenderer& LumenCardRenderer,
	const FEngineShowFlags& EngineShowFlags,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	FRDGTextureRef FinalLightingAtlas,
	const FLumenLight& LumenLight,
	const FLumenCardScatterContext& CardScatterContext)
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	FLumenCardDirectLighting* PassParameters = GraphBuilder.AllocParameters<FLumenCardDirectLighting>();
	{
		PassParameters->RenderTargets[0] = FRenderTargetBinding(FinalLightingAtlas, ERenderTargetLoadAction::ELoad);
		PassParameters->VS.LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->VS.CardScatterParameters = CardScatterContext.CardTileParameters;

		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->PS.CardScatterParameters = CardScatterContext.CardTileParameters;
		Lumen::SetDirectLightingDeferredLightUniformBuffer(View, LumenLight.LightSceneInfo, PassParameters->PS.DeferredLightUniforms);

		SetupLightFunctionParameters(LumenLight.LightSceneInfo, 1.0f, PassParameters->PS.LightFunctionParameters);

		PassParameters->PS.ShadowMaskTiles = nullptr;
		if (LumenLight.ShadowMaskTiles)
		{
			PassParameters->PS.ShadowMaskTiles = GraphBuilder.CreateSRV(LumenLight.ShadowMaskTiles);
		}

		extern float GTwoSidedMeshDistanceBias;
		PassParameters->PS.TwoSidedMeshDistanceBias = GTwoSidedMeshDistanceBias;
		PassParameters->PS.TanLightSourceAngle = FMath::Tan(LumenLight.LightSceneInfo->Proxy->GetLightSourceAngle());
		PassParameters->PS.MaxTraceDistance = Lumen::GetSurfaceCacheOffscreenShadowingMaxTraceDistance();
		PassParameters->PS.StepFactor = FMath::Clamp(GOffscreenShadowingTraceStepFactor, .1f, 10.0f);
		PassParameters->PS.SurfaceBias = FMath::Clamp(GShadowingSurfaceBias, .01f, 100.0f);
		PassParameters->PS.SlopeScaledSurfaceBias = FMath::Clamp(GShadowingSlopeScaledSurfaceBias, .01f, 100.0f);
		PassParameters->PS.SDFSurfaceBiasScale = FMath::Clamp(GOffscreenShadowingSDFSurfaceBiasScale, .01f, 100.0f);
		PassParameters->PS.VirtualShadowMapSurfaceBias = FMath::Clamp(GLumenDirectLightingVirtualShadowMapBias, .01f, 100.0f);

		// IES profile
		{
			FTexture* IESTextureResource = LumenLight.LightSceneInfo->Proxy->GetIESTextureResource();

			if (View.Family->EngineShowFlags.TexturedLightProfiles && IESTextureResource)
			{
				PassParameters->PS.UseIESProfile = 1;
				PassParameters->PS.IESTexture = IESTextureResource->TextureRHI;
			}
			else
			{
				PassParameters->PS.UseIESProfile = 0;
				PassParameters->PS.IESTexture = GWhiteTexture->TextureRHI;
			}

			PassParameters->PS.IESTextureSampler = TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
		}
	}

	auto VertexShader = View.ShaderMap->GetShader<FRasterizeToCardTilesVS>();
	const FMaterialRenderProxy* LightFunctionMaterialProxy = LumenLight.LightSceneInfo->Proxy->GetLightFunctionMaterial();
	bool bUseLightFunction = true;

	if (!LightFunctionMaterialProxy
		|| !LightFunctionMaterialProxy->GetIncompleteMaterialWithFallback(Scene->GetFeatureLevel()).IsLightFunction()
		|| !EngineShowFlags.LightFunctions)
	{
		bUseLightFunction = false;
		LightFunctionMaterialProxy = UMaterial::GetDefaultMaterial(MD_LightFunction)->GetRenderProxy();
	}

	const bool bUseCloudTransmittance = SetupLightCloudTransmittanceParameters(Scene, View, GLumenDirectLightingCloudTransmittance != 0 ? LumenLight.LightSceneInfo : nullptr, PassParameters->PS.LightCloudTransmittanceParameters);

	FLumenCardDirectLightingPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLumenCardDirectLightingPS::FLightType>(LumenLight.Type);
	PermutationVector.Set<FLumenCardDirectLightingPS::FShadowMask>(LumenLight.ShadowMaskTiles != nullptr);
	PermutationVector.Set<FLumenCardDirectLightingPS::FLightFunction>(bUseLightFunction);
	PermutationVector.Set<FLumenCardDirectLightingPS::FCloudTransmittance>(bUseCloudTransmittance);
	
	PermutationVector = FLumenCardDirectLightingPS::RemapPermutation(PermutationVector);

	const FMaterial& Material = LightFunctionMaterialProxy->GetMaterialWithFallback(Scene->GetFeatureLevel(), LightFunctionMaterialProxy);
	const FMaterialShaderMap* MaterialShaderMap = Material.GetRenderingThreadShaderMap();
	auto PixelShader = MaterialShaderMap->GetShader<FLumenCardDirectLightingPS>(PermutationVector);

	ClearUnusedGraphResources(PixelShader, &PassParameters->PS);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("%s %s", *LumenLight.Name, LumenLight.ShadowMaskTiles != nullptr ? TEXT("ShadowMask") : TEXT("")),
		PassParameters,
		ERDGPassFlags::Raster,
		[MaxAtlasSize = LumenSceneData.GetPhysicalAtlasSize(), PassParameters, VertexShader, PixelShader, GlobalShaderMap = View.ShaderMap, LightFunctionMaterialProxy, &Material, &View](FRHICommandList& RHICmdList)
		{
			DrawQuadsToAtlas(
				MaxAtlasSize,
				VertexShader,
				PixelShader,
				PassParameters,
				GlobalShaderMap,
				TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One>::GetRHI(),
				RHICmdList,
				[LightFunctionMaterialProxy, &Material, &View](FRHICommandList& RHICmdList, TShaderRefBase<FLumenCardDirectLightingPS, FShaderMapPointerTable> Shader, FRHIPixelShader* ShaderRHI, const FLumenCardDirectLightingPS::FParameters& Parameters)
				{
					Shader->SetParameters(RHICmdList, ShaderRHI, LightFunctionMaterialProxy, Material, View);
				});
		});
}

void SampleShadowMap(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	const FVirtualShadowMapArray& VirtualShadowMapArray,
	const FLumenLight& LumenLight,
	const FLumenCardScatterContext& CardScatterContext)
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const bool bShadowed = LumenLight.LightSceneInfo->Proxy->CastsDynamicShadow();
	check(bShadowed);

	FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LumenLight.LightSceneInfo->Id];
	FLumenShadowSetup ShadowSetup = GetShadowForLumenDirectLighting(VisibleLightInfo);

	const bool bDynamicallyShadowed = ShadowSetup.DenseShadowMap != nullptr;

	int32 VirtualShadowMapId = -1;
	if (bDynamicallyShadowed
		&& Lumen::UseVirtualShadowMaps()
		&& VirtualShadowMapArray.IsAllocated())
	{
		if (LumenLight.Type == ELumenLightType::Directional)
		{
			VirtualShadowMapId = VisibleLightInfo.VirtualShadowMapClipmaps[0]->GetVirtualShadowMap()->ID;
		}
		else if (ShadowSetup.VirtualShadowMap)
		{
			VirtualShadowMapId = ShadowSetup.VirtualShadowMap->VirtualShadowMaps[0]->ID;
		}
	}

	const bool bUseVirtualShadowMap = VirtualShadowMapId >= 0;
	if (!bUseVirtualShadowMap)
	{
		// Fallback to a complete shadow map
		ShadowSetup.VirtualShadowMap = nullptr;
		ShadowSetup.DenseShadowMap = GetShadowForInjectionIntoVolumetricFog(VisibleLightInfo);
	}

	FSampleShadowMapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSampleShadowMapCS::FParameters>();
	{
		PassParameters->IndirectArgBuffer = CardScatterContext.CardTileParameters.DispatchIndirectArgs;
		PassParameters->RWShadowMaskTiles = GraphBuilder.CreateUAV(LumenLight.ShadowMaskTiles);

		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->CardScatterParameters = CardScatterContext.CardTileParameters;
		Lumen::SetDirectLightingDeferredLightUniformBuffer(View, LumenLight.LightSceneInfo, PassParameters->DeferredLightUniforms);
		PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;

		GetVolumeShadowingShaderParameters(
			GraphBuilder,
			View,
			LumenLight.LightSceneInfo,
			ShadowSetup.DenseShadowMap,
			0,
			bDynamicallyShadowed,
			PassParameters->VolumeShadowingShaderParameters);
		
		PassParameters->VirtualShadowMapId = VirtualShadowMapId;
		if (bUseVirtualShadowMap)
		{
			PassParameters->VirtualShadowMapSamplingParameters = VirtualShadowMapArray.GetSamplingParameters(GraphBuilder);
		}

		PassParameters->TanLightSourceAngle = FMath::Tan(LumenLight.LightSceneInfo->Proxy->GetLightSourceAngle());
		PassParameters->MaxTraceDistance = Lumen::GetSurfaceCacheOffscreenShadowingMaxTraceDistance();
		PassParameters->StepFactor = FMath::Clamp(GOffscreenShadowingTraceStepFactor, .1f, 10.0f);
		PassParameters->SurfaceBias = FMath::Clamp(GShadowingSurfaceBias, .01f, 100.0f);
		PassParameters->SlopeScaledSurfaceBias = FMath::Clamp(GShadowingSlopeScaledSurfaceBias, .01f, 100.0f);
		PassParameters->SDFSurfaceBiasScale = FMath::Clamp(GOffscreenShadowingSDFSurfaceBiasScale, .01f, 100.0f);
		PassParameters->VirtualShadowMapSurfaceBias = FMath::Clamp(GLumenDirectLightingVirtualShadowMapBias, .01f, 100.0f);
		PassParameters->ForceOffscreenShadowing = GLumenDirectLightingForceOffscreenShadowing;
		PassParameters->ForceShadowMaps = GLumenDirectLightingForceForceShadowMaps;
	}

	FSampleShadowMapCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FSampleShadowMapCS::FLightType>(LumenLight.Type);
	PermutationVector.Set<FSampleShadowMapCS::FDynamicallyShadowed>(bDynamicallyShadowed);
	PermutationVector.Set<FSampleShadowMapCS::FVirtualShadowMap>(bUseVirtualShadowMap);
	TShaderRef<FSampleShadowMapCS> ComputeShader = View.ShaderMap->GetShader<FSampleShadowMapCS>(PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ShadowMapPass %s", *LumenLight.Name),
		ComputeShader,
		PassParameters,
		CardScatterContext.CardTileParameters.DispatchIndirectArgs,
		0);
}

void TraceDistanceFieldShadows(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	const FLumenLight& LumenLight,
	const FLumenCardScatterContext& CardScatterContext)
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const bool bShadowed = LumenLight.LightSceneInfo->Proxy->CastsDynamicShadow();
	check(bShadowed);

	FDistanceFieldObjectBufferParameters ObjectBufferParameters = DistanceField::SetupObjectBufferParameters(Scene->DistanceFieldSceneData);

	FLightTileIntersectionParameters LightTileIntersectionParameters;
	FDistanceFieldCulledObjectBufferParameters CulledObjectBufferParameters;
	FMatrix WorldToMeshSDFShadowValue = FMatrix::Identity;

	const bool bLumenUseHardwareRayTracedDirectLighting = Lumen::UseHardwareRayTracedDirectLighting();
	const bool bTraceMeshSDFs = bShadowed
		&& LumenLight.Type == ELumenLightType::Directional
		&& DoesPlatformSupportDistanceFieldShadowing(View.GetShaderPlatform())
		&& GLumenDirectLightingOffscreenShadowingTraceMeshSDFs != 0
		&& Lumen::UseMeshSDFTracing()
		&& ObjectBufferParameters.NumSceneObjects > 0;

	if (bTraceMeshSDFs)
	{
		CullMeshSDFsForLightCards(GraphBuilder, Scene, View, LumenLight.LightSceneInfo, ObjectBufferParameters, WorldToMeshSDFShadowValue, CulledObjectBufferParameters, LightTileIntersectionParameters);
	}

	FTraceDistanceFieldShadowsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTraceDistanceFieldShadowsCS::FParameters>();
	{
		PassParameters->IndirectArgBuffer = CardScatterContext.CardTileParameters.DispatchIndirectArgs;
		PassParameters->RWShadowMaskTiles = GraphBuilder.CreateUAV(LumenLight.ShadowMaskTiles);

		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->CardScatterParameters = CardScatterContext.CardTileParameters;
		Lumen::SetDirectLightingDeferredLightUniformBuffer(View, LumenLight.LightSceneInfo, PassParameters->DeferredLightUniforms);

		PassParameters->ObjectBufferParameters = ObjectBufferParameters;
		PassParameters->CulledObjectBufferParameters = CulledObjectBufferParameters;
		PassParameters->LightTileIntersectionParameters = LightTileIntersectionParameters;

		FDistanceFieldAtlasParameters DistanceFieldAtlasParameters = DistanceField::SetupAtlasParameters(Scene->DistanceFieldSceneData);

		PassParameters->DistanceFieldAtlasParameters = DistanceFieldAtlasParameters;
		PassParameters->WorldToShadow = WorldToMeshSDFShadowValue;
		extern float GTwoSidedMeshDistanceBias;
		PassParameters->TwoSidedMeshDistanceBias = GTwoSidedMeshDistanceBias;

		PassParameters->TanLightSourceAngle = FMath::Tan(LumenLight.LightSceneInfo->Proxy->GetLightSourceAngle());
		PassParameters->MaxTraceDistance = Lumen::GetSurfaceCacheOffscreenShadowingMaxTraceDistance();
		PassParameters->StepFactor = FMath::Clamp(GOffscreenShadowingTraceStepFactor, .1f, 10.0f);
		PassParameters->SurfaceBias = FMath::Clamp(GShadowingSurfaceBias, .01f, 100.0f);
		PassParameters->SlopeScaledSurfaceBias = FMath::Clamp(GShadowingSlopeScaledSurfaceBias, .01f, 100.0f);
		PassParameters->SDFSurfaceBiasScale = FMath::Clamp(GOffscreenShadowingSDFSurfaceBiasScale, .01f, 100.0f);
	}

	FTraceDistanceFieldShadowsCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FTraceDistanceFieldShadowsCS::FLightType>(LumenLight.Type);
	PermutationVector.Set<FTraceDistanceFieldShadowsCS::FTraceMeshSDFs>(bTraceMeshSDFs);
	PermutationVector = FTraceDistanceFieldShadowsCS::RemapPermutation(PermutationVector);

	TShaderRef<FTraceDistanceFieldShadowsCS> ComputeShader = View.ShaderMap->GetShader<FTraceDistanceFieldShadowsCS>(PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("DistanceFieldShadowPass %s", *LumenLight.Name),
		ComputeShader,
		PassParameters,
		CardScatterContext.CardTileParameters.DispatchIndirectArgs,
		0);
}

void FDeferredShadingSceneRenderer::RenderDirectLightingForLumenScene(
	FRDGBuilder& GraphBuilder,
	const FLumenCardTracingInputs& TracingInputs,
	FGlobalShaderMap* GlobalShaderMap,
	const FLumenCardScatterContext& VisibleCardScatterContext)
{
	LLM_SCOPE_BYTAG(Lumen);

	if (GLumenDirectLighting)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "DirectLighting");
		QUICK_SCOPE_CYCLE_COUNTER(RenderDirectLightingForLumenScene);

		const FViewInfo& View = Views[0];
		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

		TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer = TracingInputs.LumenCardSceneUniformBuffer;
		FRDGTextureRef FinalLightingAtlas = TracingInputs.FinalLightingAtlas;

		TArray<const FLightSceneInfo*, TInlineAllocator<64>> GatheredLights;

		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
			const FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

			if (LightSceneInfo->ShouldRenderLightViewIndependent()
				&& LightSceneInfo->ShouldRenderLight(View, true)
				&& LightSceneInfo->Proxy->GetIndirectLightingScale() > 0.0f)
			{
				GatheredLights.Add(LightSceneInfo);
			}
		}

		const int32 LightBatchSize = FMath::Clamp(GLumenDirectLightingBatchSize, 1, 64);

		struct FLightBatchEntry
		{
			FLumenLight LumenLight;
			FLumenCardScatterContext ScatterContext;
		};

		TArray<FLightBatchEntry, SceneRenderingAllocator> LightBatchEntries;
		LightBatchEntries.SetNum(LightBatchSize);

		// Batched light culling and drawing
		for (int32 LightBatchIndex = 0; LightBatchIndex * LightBatchSize < GatheredLights.Num(); ++LightBatchIndex)
		{
			const int32 FirstLightIndex = LightBatchIndex * LightBatchSize;
			const int32 LastLightIndex = FMath::Min((LightBatchIndex + 1) * LightBatchSize, GatheredLights.Num());
			RDG_EVENT_SCOPE(GraphBuilder, "Batch draw %d lights", LastLightIndex - FirstLightIndex);

			// Build card tiles and setup Lumen lights
			for (int32 LightIndex = FirstLightIndex; LightIndex < LastLightIndex; ++LightIndex)
			{
				const int32 LightIndexInBatch = LightIndex - FirstLightIndex;
				const FLightSceneInfo* LightSceneInfo = GatheredLights[LightIndex];
				FLightBatchEntry& LightBatchEntry = LightBatchEntries[LightIndexInBatch];

				SetupLumenLight(GraphBuilder, LumenSceneData, LightSceneInfo, LightBatchEntry.LumenLight);

				const ELightComponentType LightType = (ELightComponentType)LightSceneInfo->Proxy->GetLightType();
				if (LightType != LightType_Directional)
				{
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

					LightBatchEntry.ScatterContext.Build(
						GraphBuilder,
						View,
						LumenSceneData,
						LumenCardRenderer,
						LumenCardSceneUniformBuffer,
						/*bBuildCardTiles*/ true,
						ECullCardsMode::OperateOnSceneForceUpdateForCardPagesToRender,
						GLumenSceneCardDirectLightingUpdateFrequencyScale,
						ShapeParameters,
						ShapeType);
				}
			}

			// Shadow map pass
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Shadow map");

				for (int32 LightIndex = FirstLightIndex; LightIndex < LastLightIndex; ++LightIndex)
				{
					const int32 LightIndexInBatch = LightIndex - FirstLightIndex;
					const FLightBatchEntry& LightBatchEntry = LightBatchEntries[LightIndexInBatch];

					if (LightBatchEntry.LumenLight.ShadowMaskTiles)
					{
						SampleShadowMap(
							GraphBuilder,
							Scene,
							View,
							LumenCardSceneUniformBuffer,
							VisibleLightInfos,
							VirtualShadowMapArray,
							LightBatchEntry.LumenLight,
							LightBatchEntry.LumenLight.Type == ELumenLightType::Directional ? VisibleCardScatterContext : LightBatchEntry.ScatterContext);
					}
				}
			}

			// Offscreen shadow pass
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Offscreen shadows");

				const bool bLumenUseHardwareRayTracedDirectLighting = Lumen::UseHardwareRayTracedDirectLighting();

				for (int32 LightIndex = FirstLightIndex; LightIndex < LastLightIndex; ++LightIndex)
				{
					const int32 LightIndexInBatch = LightIndex - FirstLightIndex;
					const FLightBatchEntry& LightBatchEntry = LightBatchEntries[LightIndexInBatch];

					if (LightBatchEntry.LumenLight.ShadowMaskTiles)
					{
						if (bLumenUseHardwareRayTracedDirectLighting)
						{
							TraceLumenHardwareRayTracedDirectLightingShadows(
								GraphBuilder,
								Scene,
								View,
								TracingInputs,
								LightBatchEntry.LumenLight,
								LightBatchEntry.LumenLight.Type == ELumenLightType::Directional ? VisibleCardScatterContext : LightBatchEntry.ScatterContext);
						}
						else
						{
							TraceDistanceFieldShadows(
								GraphBuilder,
								Scene,
								View,
								LumenCardSceneUniformBuffer,
								LightBatchEntry.LumenLight,
								LightBatchEntry.LumenLight.Type == ELumenLightType::Directional ? VisibleCardScatterContext : LightBatchEntry.ScatterContext);
						}
					}
				}
			}

			// Apply lights pass
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Lights");

				for (int32 LightIndex = FirstLightIndex; LightIndex < LastLightIndex; ++LightIndex)
				{
					const int32 LightIndexInBatch = LightIndex - FirstLightIndex;
					const FLightBatchEntry& LightBatchEntry = LightBatchEntries[LightIndexInBatch];

					RenderDirectLightIntoLumenCards(
						GraphBuilder,
						Scene,
						View,
						TracingInputs,
						LumenCardRenderer,
						ViewFamily.EngineShowFlags,
						LumenCardSceneUniformBuffer,
						FinalLightingAtlas,
						LightBatchEntry.LumenLight,
						LightBatchEntry.LumenLight.Type == ELumenLightType::Directional ? VisibleCardScatterContext : LightBatchEntry.ScatterContext);
				}
			}
		}
	}
}
