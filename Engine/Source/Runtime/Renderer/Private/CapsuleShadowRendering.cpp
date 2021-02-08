// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CapsuleShadowRendering.cpp: Functionality for rendering shadows from capsules
=============================================================================*/

#include "CapsuleShadowRendering.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "ShadowRendering.h"
#include "DeferredShadingRenderer.h"
#include "MaterialShaderType.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "DistanceFieldLightingPost.h"
#include "DistanceFieldLightingShared.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"

DECLARE_GPU_STAT_NAMED(CapsuleShadows, TEXT("Capsule Shadows"));

int32 GCapsuleShadows = 1;
FAutoConsoleVariableRef CVarCapsuleShadows(
	TEXT("r.CapsuleShadows"),
	GCapsuleShadows,
	TEXT("Whether to allow capsule shadowing on skinned components with bCastCapsuleDirectShadow or bCastCapsuleIndirectShadow enabled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GCapsuleDirectShadows = 1;
FAutoConsoleVariableRef CVarCapsuleDirectShadows(
	TEXT("r.CapsuleDirectShadows"),
	GCapsuleDirectShadows,
	TEXT("Whether to allow capsule direct shadowing on skinned components with bCastCapsuleDirectShadow enabled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GCapsuleIndirectShadows = 1;
FAutoConsoleVariableRef CVarCapsuleIndirectShadows(
	TEXT("r.CapsuleIndirectShadows"),
	GCapsuleIndirectShadows,
	TEXT("Whether to allow capsule indirect shadowing on skinned components with bCastCapsuleIndirectShadow enabled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GCapsuleShadowsFullResolution = 0;
FAutoConsoleVariableRef CVarCapsuleShadowsFullResolution(
	TEXT("r.CapsuleShadowsFullResolution"),
	GCapsuleShadowsFullResolution,
	TEXT("Whether to compute capsule shadows at full resolution."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GCapsuleMaxDirectOcclusionDistance = 400;
FAutoConsoleVariableRef CVarCapsuleMaxDirectOcclusionDistance(
	TEXT("r.CapsuleMaxDirectOcclusionDistance"),
	GCapsuleMaxDirectOcclusionDistance,
	TEXT("Maximum cast distance for direct shadows from capsules.  This has a big impact on performance."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GCapsuleMaxIndirectOcclusionDistance = 200;
FAutoConsoleVariableRef CVarCapsuleMaxIndirectOcclusionDistance(
	TEXT("r.CapsuleMaxIndirectOcclusionDistance"),
	GCapsuleMaxIndirectOcclusionDistance,
	TEXT("Maximum cast distance for indirect shadows from capsules.  This has a big impact on performance."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GCapsuleShadowFadeAngleFromVertical = PI / 3;
FAutoConsoleVariableRef CVarCapsuleShadowFadeAngleFromVertical(
	TEXT("r.CapsuleShadowFadeAngleFromVertical"),
	GCapsuleShadowFadeAngleFromVertical,
	TEXT("Angle from vertical up to start fading out the indirect shadow, to avoid self shadowing artifacts."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GCapsuleIndirectConeAngle = PI / 8;
FAutoConsoleVariableRef CVarCapsuleIndirectConeAngle(
	TEXT("r.CapsuleIndirectConeAngle"),
	GCapsuleIndirectConeAngle,
	TEXT("Light source angle used when the indirect shadow direction is derived from precomputed indirect lighting (no stationary skylight present)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GCapsuleSkyAngleScale = .6f;
FAutoConsoleVariableRef CVarCapsuleSkyAngleScale(
	TEXT("r.CapsuleSkyAngleScale"),
	GCapsuleSkyAngleScale,
	TEXT("Scales the light source angle derived from the precomputed unoccluded sky vector (stationary skylight present)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GCapsuleMinSkyAngle = 15;
FAutoConsoleVariableRef CVarCapsuleMinSkyAngle(
	TEXT("r.CapsuleMinSkyAngle"),
	GCapsuleMinSkyAngle,
	TEXT("Minimum light source angle derived from the precomputed unoccluded sky vector (stationary skylight present)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

const int32 GComputeLightDirectionFromVolumetricLightmapGroupSize = 64;

class FComputeLightDirectionFromVolumetricLightmapCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FComputeLightDirectionFromVolumetricLightmapCS);
	SHADER_USE_PARAMETER_STRUCT(FComputeLightDirectionFromVolumetricLightmapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, NumLightDirectionData)
		SHADER_PARAMETER(uint32, SkyLightMode)
		SHADER_PARAMETER(float, CapsuleIndirectConeAngle)
		SHADER_PARAMETER(float, CapsuleSkyAngleScale)
		SHADER_PARAMETER(float, CapsuleMinSkyAngle)
		SHADER_PARAMETER_SRV(Buffer<float4>, LightDirectionData)
		SHADER_PARAMETER_UAV(RWBuffer<float4>, RWComputedLightDirectionData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportCapsuleShadows(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GComputeLightDirectionFromVolumetricLightmapGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), 1);
		OutEnvironment.SetDefine(TEXT("LIGHT_SOURCE_MODE"), TEXT("LIGHT_SOURCE_FROM_CAPSULE"));
	}
};

IMPLEMENT_GLOBAL_SHADER(FComputeLightDirectionFromVolumetricLightmapCS, "/Engine/Private/CapsuleShadowShaders.usf", "ComputeLightDirectionFromVolumetricLightmapCS", SF_Compute);

int32 GShadowShapeTileSize = 8;

int32 GetCapsuleShadowDownsampleFactor()
{
	return GCapsuleShadowsFullResolution ? 1 : 2;
}

FIntPoint GetBufferSizeForCapsuleShadows()
{
	return FIntPoint::DivideAndRoundDown(FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY(), GetCapsuleShadowDownsampleFactor());
}

enum ECapsuleShadowingType
{
	ShapeShadow_DirectionalLightTiledCulling,
	ShapeShadow_PointLightTiledCulling,
	ShapeShadow_IndirectTiledCulling,
	ShapeShadow_MovableSkylightTiledCulling,
	ShapeShadow_MovableSkylightTiledCullingGatherFromReceiverBentNormal
};

enum EIndirectShadowingPrimitiveTypes
{
	IPT_CapsuleShapes = 1,
	IPT_MeshDistanceFields = 2,
	IPT_CapsuleShapesAndMeshDistanceFields = IPT_CapsuleShapes | IPT_MeshDistanceFields
};

template<ECapsuleShadowingType ShadowingType>
class TCapsuleShadowingBaseCS : public FGlobalShader
{
	DECLARE_INLINE_TYPE_LAYOUT(TCapsuleShadowingBaseCS, NonVirtual);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportCapsuleShadows(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GShadowShapeTileSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GShadowShapeTileSize);
		OutEnvironment.SetDefine(TEXT("POINT_LIGHT"), ShadowingType == ShapeShadow_PointLightTiledCulling);
		uint32 LightSourceMode = 0;

		if (ShadowingType == ShapeShadow_DirectionalLightTiledCulling || ShadowingType == ShapeShadow_PointLightTiledCulling)
		{
			LightSourceMode = 0;
		}
		else if (ShadowingType == ShapeShadow_IndirectTiledCulling || ShadowingType == ShapeShadow_MovableSkylightTiledCulling)
		{
			LightSourceMode = 1;
		}
		else if (ShadowingType == ShapeShadow_MovableSkylightTiledCullingGatherFromReceiverBentNormal)
		{
			LightSourceMode = 2;
		}
		else
		{
			check(0);
		}

		OutEnvironment.SetDefine(TEXT("LIGHT_SOURCE_MODE"), LightSourceMode);
		const bool bApplyToBentNormal = ShadowingType == ShapeShadow_MovableSkylightTiledCulling || ShadowingType == ShapeShadow_MovableSkylightTiledCullingGatherFromReceiverBentNormal;
		OutEnvironment.SetDefine(TEXT("APPLY_TO_BENT_NORMAL"), bApplyToBentNormal);
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	/** Default constructor. */
	TCapsuleShadowingBaseCS() {}

	/** Initialization constructor. */
	TCapsuleShadowingBaseCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ShadowFactors.Bind(Initializer.ParameterMap, TEXT("ShadowFactors"));
		TileIntersectionCounts.Bind(Initializer.ParameterMap, TEXT("TileIntersectionCounts"));
		TileDimensions.Bind(Initializer.ParameterMap, TEXT("TileDimensions"));
		BentNormalTexture.Bind(Initializer.ParameterMap, TEXT("BentNormalTexture"));
		ReceiverBentNormalTexture.Bind(Initializer.ParameterMap, TEXT("ReceiverBentNormalTexture"));
		NumGroups.Bind(Initializer.ParameterMap, TEXT("NumGroups"));
		LightDirection.Bind(Initializer.ParameterMap, TEXT("LightDirection"));
		LightSourceRadius.Bind(Initializer.ParameterMap, TEXT("LightSourceRadius"));
		RayStartOffsetDepthScale.Bind(Initializer.ParameterMap, TEXT("RayStartOffsetDepthScale"));
		LightPositionAndInvRadius.Bind(Initializer.ParameterMap, TEXT("LightPositionAndInvRadius"));
		LightAngleAndNormalThreshold.Bind(Initializer.ParameterMap, TEXT("LightAngleAndNormalThreshold"));
		ScissorRectMinAndSize.Bind(Initializer.ParameterMap, TEXT("ScissorRectMinAndSize"));
		DownsampleFactor.Bind(Initializer.ParameterMap, TEXT("DownsampleFactor"));
		NumShadowCapsules.Bind(Initializer.ParameterMap, TEXT("NumShadowCapsules"));
		ShadowCapsuleShapes.Bind(Initializer.ParameterMap, TEXT("ShadowCapsuleShapes"));
		NumMeshDistanceFieldCasters.Bind(Initializer.ParameterMap, TEXT("NumMeshDistanceFieldCasters"));
		MeshDistanceFieldCasterIndices.Bind(Initializer.ParameterMap, TEXT("MeshDistanceFieldCasterIndices"));
		MaxOcclusionDistance.Bind(Initializer.ParameterMap, TEXT("MaxOcclusionDistance"));
		CosFadeStartAngle.Bind(Initializer.ParameterMap, TEXT("CosFadeStartAngle"));
		LightDirectionData.Bind(Initializer.ParameterMap, TEXT("LightDirectionData"));
		IndirectCapsuleSelfShadowingIntensity.Bind(Initializer.ParameterMap, TEXT("IndirectCapsuleSelfShadowingIntensity"));
		DistanceFieldObjectParameters.Bind(Initializer.ParameterMap);
	}

	void SetParameters(
		FRHIComputeCommandList& RHICmdList, 
		FScene* Scene,
		const FSceneView& View, 
		const FLightSceneInfo* LightSceneInfo,
		const FSceneRenderTargetItem& OutputTexture, 
		FIntPoint TileDimensionsValue,
		const FRWBuffer* TileIntersectionCountsBuffer,
		FVector2D NumGroupsValue,
		float MaxOcclusionDistanceValue,
		const FIntRect& ScissorRect,
		int32 DownsampleFactorValue,
		int32 NumShadowCapsulesValue,
		FRHIShaderResourceView* ShadowCapsuleShapesSRV,
		int32 NumMeshDistanceFieldCastersValue,
		FRHIShaderResourceView* MeshDistanceFieldCasterIndicesSRV,
		FRHIShaderResourceView* LightDirectionDataSRV,
		FRHITexture* ReceiverBentNormalTextureValue)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		if (TileIntersectionCountsBuffer)
		{
			RHICmdList.Transition(FRHITransitionInfo(TileIntersectionCountsBuffer->UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier));
		}

		if (ShadowingType == ShapeShadow_MovableSkylightTiledCulling)
		{
			check(!ShadowFactors.IsBound());
			BentNormalTexture.SetTexture(RHICmdList, ShaderRHI, OutputTexture.ShaderResourceTexture, OutputTexture.UAV);
		}
		else
		{
			check(!BentNormalTexture.IsBound());
			ShadowFactors.SetTexture(RHICmdList, ShaderRHI, OutputTexture.ShaderResourceTexture, OutputTexture.UAV);
		}
		
		if (TileIntersectionCountsBuffer)
		{
			TileIntersectionCounts.SetBuffer(RHICmdList, ShaderRHI, *TileIntersectionCountsBuffer);
		}
		else
		{
			check(!TileIntersectionCounts.IsBound());
		}

		SetShaderValue(RHICmdList, ShaderRHI, TileDimensions, TileDimensionsValue);

		if (ShadowingType == ShapeShadow_MovableSkylightTiledCulling)
		{
			check(ReceiverBentNormalTextureValue);
			SetTextureParameter(RHICmdList, ShaderRHI, ReceiverBentNormalTexture, ReceiverBentNormalTextureValue);
		}
		else
		{
			check(!ReceiverBentNormalTexture.IsBound());
		}

		SetShaderValue(RHICmdList, ShaderRHI, NumGroups, NumGroupsValue);

		if (LightSceneInfo)
		{
			check(ShadowingType == ShapeShadow_DirectionalLightTiledCulling || ShadowingType == ShapeShadow_PointLightTiledCulling);
			
			const FLightSceneProxy& LightProxy = *LightSceneInfo->Proxy;

			FLightShaderParameters LightParameters;
			LightProxy.GetLightShaderParameters(LightParameters);

			SetShaderValue(RHICmdList, ShaderRHI, LightDirection, LightParameters.Direction);
			FVector4 LightPositionAndInvRadiusValue(LightParameters.Position, LightParameters.InvRadius);
			SetShaderValue(RHICmdList, ShaderRHI, LightPositionAndInvRadius, LightPositionAndInvRadiusValue);
			// Default light source radius of 0 gives poor results
			SetShaderValue(RHICmdList, ShaderRHI, LightSourceRadius, LightParameters.SourceRadius == 0 ? 20 : FMath::Clamp(LightParameters.SourceRadius, .001f, 1.0f / (4 * LightParameters.InvRadius)));

			SetShaderValue(RHICmdList, ShaderRHI, RayStartOffsetDepthScale, LightProxy.GetRayStartOffsetDepthScale());

			const float LightSourceAngle = FMath::Clamp(LightProxy.GetLightSourceAngle() * 5, 1.0f, 30.0f) * PI / 180.0f;
			const FVector LightAngleAndNormalThresholdValue(LightSourceAngle, FMath::Cos(PI / 2 + LightSourceAngle), LightProxy.GetTraceDistance());
			SetShaderValue(RHICmdList, ShaderRHI, LightAngleAndNormalThreshold, LightAngleAndNormalThresholdValue);
		}
		else
		{
			check(ShadowingType == ShapeShadow_IndirectTiledCulling || ShadowingType == ShapeShadow_MovableSkylightTiledCulling || ShadowingType == ShapeShadow_MovableSkylightTiledCullingGatherFromReceiverBentNormal);
			check(!LightDirection.IsBound() && !LightPositionAndInvRadius.IsBound());
		}

		SetShaderValue(RHICmdList, ShaderRHI, ScissorRectMinAndSize, FIntRect(ScissorRect.Min, ScissorRect.Size()));
		SetShaderValue(RHICmdList, ShaderRHI, DownsampleFactor, DownsampleFactorValue);

		SetShaderValue(RHICmdList, ShaderRHI, NumShadowCapsules, NumShadowCapsulesValue);
		SetSRVParameter(RHICmdList, ShaderRHI, ShadowCapsuleShapes, ShadowCapsuleShapesSRV);

		SetShaderValue(RHICmdList, ShaderRHI, NumMeshDistanceFieldCasters, NumMeshDistanceFieldCastersValue);
		SetSRVParameter(RHICmdList, ShaderRHI, MeshDistanceFieldCasterIndices, MeshDistanceFieldCasterIndicesSRV);

		SetShaderValue(RHICmdList, ShaderRHI, MaxOcclusionDistance, MaxOcclusionDistanceValue);
		const float CosFadeStartAngleValue = FMath::Cos(GCapsuleShadowFadeAngleFromVertical);
		const FVector2D CosFadeStartAngleVector(CosFadeStartAngleValue, 1.0f / (1.0f - CosFadeStartAngleValue));
		SetShaderValue(RHICmdList, ShaderRHI, CosFadeStartAngle, CosFadeStartAngleVector);
		SetSRVParameter(RHICmdList, ShaderRHI, LightDirectionData, LightDirectionDataSRV);

		float IndirectCapsuleSelfShadowingIntensityValue = Scene->DynamicIndirectShadowsSelfShadowingIntensity;
		SetShaderValue(RHICmdList, ShaderRHI, IndirectCapsuleSelfShadowingIntensity, IndirectCapsuleSelfShadowingIntensityValue);

		if (Scene->DistanceFieldSceneData.GetCurrentObjectBuffers())
		{
			FRHITexture* TextureAtlas;
			int32 AtlasSizeX;
			int32 AtlasSizeY;
			int32 AtlasSizeZ;

			TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();

			DistanceFieldObjectParameters.Set(
				RHICmdList,
				ShaderRHI,
				*Scene->DistanceFieldSceneData.GetCurrentObjectBuffers(),
				Scene->DistanceFieldSceneData.NumObjectsInBuffer,
				TextureAtlas,
				AtlasSizeX,
				AtlasSizeY,
				AtlasSizeZ);
		}
		else
		{
			check(!DistanceFieldObjectParameters.AnyBound());
		}
	}

	void UnsetParameters(FRHIComputeCommandList& RHICmdList, const FRWBuffer* TileIntersectionCountsBuffer)
	{
		ShadowFactors.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());
		BentNormalTexture.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());
		TileIntersectionCounts.UnsetUAV(RHICmdList, RHICmdList.GetBoundComputeShader());

		if (TileIntersectionCountsBuffer)
		{
			RHICmdList.Transition(FRHITransitionInfo(TileIntersectionCountsBuffer->UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
		}
	}

private:

	LAYOUT_FIELD(FRWShaderParameter, ShadowFactors);
	LAYOUT_FIELD(FRWShaderParameter, TileIntersectionCounts);
	LAYOUT_FIELD(FShaderParameter, TileDimensions);
	LAYOUT_FIELD(FRWShaderParameter, BentNormalTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ReceiverBentNormalTexture);
	LAYOUT_FIELD(FShaderParameter, NumGroups);
	LAYOUT_FIELD(FShaderParameter, LightDirection);
	LAYOUT_FIELD(FShaderParameter, LightPositionAndInvRadius);
	LAYOUT_FIELD(FShaderParameter, LightSourceRadius);
	LAYOUT_FIELD(FShaderParameter, RayStartOffsetDepthScale);
	LAYOUT_FIELD(FShaderParameter, LightAngleAndNormalThreshold);
	LAYOUT_FIELD(FShaderParameter, ScissorRectMinAndSize);
	LAYOUT_FIELD(FShaderParameter, DownsampleFactor);
	LAYOUT_FIELD(FShaderParameter, NumShadowCapsules);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowCapsuleShapes);
	LAYOUT_FIELD(FShaderParameter, NumMeshDistanceFieldCasters);
	LAYOUT_FIELD(FShaderResourceParameter, MeshDistanceFieldCasterIndices);
	LAYOUT_FIELD(FShaderParameter, MaxOcclusionDistance);
	LAYOUT_FIELD(FShaderParameter, CosFadeStartAngle);
	LAYOUT_FIELD(FShaderResourceParameter, LightDirectionData);
	LAYOUT_FIELD(FShaderParameter, IndirectCapsuleSelfShadowingIntensity);
	LAYOUT_FIELD((TDistanceFieldObjectBufferParameters<DFPT_SignedDistanceField>), DistanceFieldObjectParameters);
};

template<ECapsuleShadowingType ShadowingType, EIndirectShadowingPrimitiveTypes PrimitiveTypes>
class TCapsuleShadowingCS : public TCapsuleShadowingBaseCS<ShadowingType>
{
	DECLARE_SHADER_TYPE(TCapsuleShadowingCS, Global);

	TCapsuleShadowingCS() {}

	/** Initialization constructor. */
	TCapsuleShadowingCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: TCapsuleShadowingBaseCS<ShadowingType>(Initializer)
	{}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TCapsuleShadowingBaseCS<ShadowingType>::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		if (PrimitiveTypes & IPT_CapsuleShapes)
		{
			OutEnvironment.SetDefine(TEXT("SUPPORT_CAPSULE_SHAPES"), 1);
		}
		
		if (PrimitiveTypes & IPT_MeshDistanceFields)
		{
			OutEnvironment.SetDefine(TEXT("SUPPORT_MESH_DISTANCE_FIELDS"), 1);
		}
	}
};

#define IMPLEMENT_CAPSULE_SHADOW_TYPE(ShadowType,PrimitiveType) \
	typedef TCapsuleShadowingCS<ShadowType,PrimitiveType> TCapsuleShadowingCS##ShadowType##PrimitiveType; \
	IMPLEMENT_SHADER_TYPE(template<>,TCapsuleShadowingCS##ShadowType##PrimitiveType,TEXT("/Engine/Private/CapsuleShadowShaders.usf"),TEXT("CapsuleShadowingCS"),SF_Compute);

IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_DirectionalLightTiledCulling, IPT_CapsuleShapes);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_PointLightTiledCulling, IPT_CapsuleShapes);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_IndirectTiledCulling, IPT_CapsuleShapes);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_MovableSkylightTiledCulling, IPT_CapsuleShapes);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_MovableSkylightTiledCullingGatherFromReceiverBentNormal, IPT_CapsuleShapes);

IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_DirectionalLightTiledCulling, IPT_MeshDistanceFields);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_PointLightTiledCulling, IPT_MeshDistanceFields);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_IndirectTiledCulling, IPT_MeshDistanceFields);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_MovableSkylightTiledCulling, IPT_MeshDistanceFields);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_MovableSkylightTiledCullingGatherFromReceiverBentNormal, IPT_MeshDistanceFields);

IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_DirectionalLightTiledCulling, IPT_CapsuleShapesAndMeshDistanceFields);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_PointLightTiledCulling, IPT_CapsuleShapesAndMeshDistanceFields);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_IndirectTiledCulling, IPT_CapsuleShapesAndMeshDistanceFields);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_MovableSkylightTiledCulling, IPT_CapsuleShapesAndMeshDistanceFields);
IMPLEMENT_CAPSULE_SHADOW_TYPE(ShapeShadow_MovableSkylightTiledCullingGatherFromReceiverBentNormal, IPT_CapsuleShapesAndMeshDistanceFields);

// Nvidia has lower vertex throughput when only processing a few verts per instance
// Disabled as it hasn't been tested
const int32 NumTileQuadsInBuffer = 1;

class FCapsuleShadowingUpsampleVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCapsuleShadowingUpsampleVS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportCapsuleShadows(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILES_PER_INSTANCE"), NumTileQuadsInBuffer);
	}

	/** Default constructor. */
	FCapsuleShadowingUpsampleVS() {}

	/** Initialization constructor. */
	FCapsuleShadowingUpsampleVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TileDimensions.Bind(Initializer.ParameterMap,TEXT("TileDimensions"));
		TileSize.Bind(Initializer.ParameterMap,TEXT("TileSize"));
		ScissorRectMinAndSize.Bind(Initializer.ParameterMap,TEXT("ScissorRectMinAndSize"));
		TileIntersectionCounts.Bind(Initializer.ParameterMap,TEXT("TileIntersectionCounts"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, FIntPoint TileDimensionsValue, const FIntRect& ScissorRect, const FRWBuffer& TileIntersectionCountsBuffer)
	{
		FRHIVertexShader* ShaderRHI = RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		SetShaderValue(RHICmdList, ShaderRHI, TileDimensions, TileDimensionsValue);
		SetShaderValue(RHICmdList, ShaderRHI, TileSize, FVector2D(
			GShadowShapeTileSize * GetCapsuleShadowDownsampleFactor(), 
			GShadowShapeTileSize * GetCapsuleShadowDownsampleFactor()));
		SetShaderValue(RHICmdList, ShaderRHI, ScissorRectMinAndSize, FIntRect(ScissorRect.Min, ScissorRect.Size()));
		SetSRVParameter(RHICmdList, ShaderRHI, TileIntersectionCounts, TileIntersectionCountsBuffer.SRV);
	}

private:

	LAYOUT_FIELD(FShaderParameter, TileDimensions);
	LAYOUT_FIELD(FShaderParameter, TileSize);
	LAYOUT_FIELD(FShaderParameter, ScissorRectMinAndSize);
	LAYOUT_FIELD(FShaderResourceParameter, TileIntersectionCounts);
};

IMPLEMENT_SHADER_TYPE(,FCapsuleShadowingUpsampleVS,TEXT("/Engine/Private/CapsuleShadowShaders.usf"),TEXT("CapsuleShadowingUpsampleVS"),SF_Vertex);


template<bool bUpsampleRequired, bool bApplyToSSAO>
class TCapsuleShadowingUpsamplePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TCapsuleShadowingUpsamplePS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportCapsuleShadows(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), 2);
		OutEnvironment.SetDefine(TEXT("UPSAMPLE_REQUIRED"), bUpsampleRequired);
		OutEnvironment.SetDefine(TEXT("APPLY_TO_SSAO"), bApplyToSSAO);
	}

	/** Default constructor. */
	TCapsuleShadowingUpsamplePS() {}

	/** Initialization constructor. */
	TCapsuleShadowingUpsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ShadowFactorsTexture.Bind(Initializer.ParameterMap,TEXT("ShadowFactorsTexture"));
		ShadowFactorsSampler.Bind(Initializer.ParameterMap,TEXT("ShadowFactorsSampler"));
		ScissorRectMinAndSize.Bind(Initializer.ParameterMap,TEXT("ScissorRectMinAndSize"));
		OutputtingToLightAttenuation.Bind(Initializer.ParameterMap,TEXT("OutputtingToLightAttenuation"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FIntRect& ScissorRect, IPooledRenderTarget* ShadowFactorsTextureValue, bool bOutputtingToLightAttenuation)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		SetTextureParameter(RHICmdList, ShaderRHI, ShadowFactorsTexture, ShadowFactorsSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), ShadowFactorsTextureValue->GetRenderTargetItem().ShaderResourceTexture);
	
		SetShaderValue(RHICmdList, ShaderRHI, ScissorRectMinAndSize, FIntRect(ScissorRect.Min, ScissorRect.Size()));
		SetShaderValue(RHICmdList, ShaderRHI, OutputtingToLightAttenuation, bOutputtingToLightAttenuation ? 1.0f : 0.0f);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ShadowFactorsTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowFactorsSampler);
	LAYOUT_FIELD(FShaderParameter, ScissorRectMinAndSize);
	LAYOUT_FIELD(FShaderParameter, OutputtingToLightAttenuation);
};

#define IMPLEMENT_CAPSULE_APPLY_SHADER_TYPE(bUpsampleRequired,bApplyToSSAO) \
	typedef TCapsuleShadowingUpsamplePS<bUpsampleRequired,bApplyToSSAO> TCapsuleShadowingUpsamplePS##bUpsampleRequired##bApplyToSSAO; \
	IMPLEMENT_SHADER_TYPE(template<>,TCapsuleShadowingUpsamplePS##bUpsampleRequired##bApplyToSSAO,TEXT("/Engine/Private/CapsuleShadowShaders.usf"),TEXT("CapsuleShadowingUpsamplePS"),SF_Pixel)

IMPLEMENT_CAPSULE_APPLY_SHADER_TYPE(true, true);
IMPLEMENT_CAPSULE_APPLY_SHADER_TYPE(true, false);
IMPLEMENT_CAPSULE_APPLY_SHADER_TYPE(false, true);
IMPLEMENT_CAPSULE_APPLY_SHADER_TYPE(false, false);

class FTileTexCoordVertexBuffer : public FVertexBuffer
{
public:
	virtual void InitRHI() override
	{
		const uint32 Size = sizeof(FVector2D) * 4 * NumTileQuadsInBuffer;
		FRHIResourceCreateInfo CreateInfo;
		void* BufferData = nullptr;
		VertexBufferRHI = RHICreateAndLockVertexBuffer(Size, BUF_Static, CreateInfo, BufferData);
		FVector2D* Vertices = (FVector2D*)BufferData;
		for (uint32 SpriteIndex = 0; SpriteIndex < NumTileQuadsInBuffer; ++SpriteIndex)
		{
			Vertices[SpriteIndex*4 + 0] = FVector2D(0.0f, 0.0f);
			Vertices[SpriteIndex*4 + 1] = FVector2D(0.0f, 1.0f);
			Vertices[SpriteIndex*4 + 2] = FVector2D(1.0f, 1.0f);
			Vertices[SpriteIndex*4 + 3] = FVector2D(1.0f, 0.0f);
		}
		RHIUnlockVertexBuffer( VertexBufferRHI );
	}
};

TGlobalResource<FTileTexCoordVertexBuffer> GTileTexCoordVertexBuffer;

class FTileIndexBuffer : public FIndexBuffer
{
public:
	/** Initialize the RHI for this rendering resource */
	void InitRHI() override
	{
		const uint32 Size = sizeof(uint16) * 6 * NumTileQuadsInBuffer;
		const uint32 Stride = sizeof(uint16);
		FRHIResourceCreateInfo CreateInfo;
		void* Buffer = nullptr;
		IndexBufferRHI = RHICreateAndLockIndexBuffer(Stride, Size, BUF_Static, CreateInfo, Buffer);
		uint16* Indices = (uint16*)Buffer;
		for (uint32 SpriteIndex = 0; SpriteIndex < NumTileQuadsInBuffer; ++SpriteIndex)
		{
			Indices[SpriteIndex * 6 + 0] = SpriteIndex * 4 + 0;
			Indices[SpriteIndex * 6 + 1] = SpriteIndex * 4 + 1;
			Indices[SpriteIndex * 6 + 2] = SpriteIndex * 4 + 2;
			Indices[SpriteIndex * 6 + 3] = SpriteIndex * 4 + 0;
			Indices[SpriteIndex * 6 + 4] = SpriteIndex * 4 + 2;
			Indices[SpriteIndex * 6 + 5] = SpriteIndex * 4 + 3;
		}
		RHIUnlockIndexBuffer(IndexBufferRHI);
	}
};

TGlobalResource<FTileIndexBuffer> GTileIndexBuffer;

void AllocateCapsuleTileIntersectionCountsBuffer(FIntPoint GroupSize, FSceneViewState* ViewState)
{
	EPixelFormat CapsuleTileIntersectionCountsBufferFormat = PF_R32_UINT;

	if (!IsValidRef(ViewState->CapsuleTileIntersectionCountsBuffer.Buffer) 
		|| (int32)ViewState->CapsuleTileIntersectionCountsBuffer.NumBytes < GroupSize.X * GroupSize.Y * GPixelFormats[CapsuleTileIntersectionCountsBufferFormat].BlockBytes)
	{
		ViewState->CapsuleTileIntersectionCountsBuffer.Release();
		ViewState->CapsuleTileIntersectionCountsBuffer.Initialize(GPixelFormats[CapsuleTileIntersectionCountsBufferFormat].BlockBytes, GroupSize.X * GroupSize.Y, CapsuleTileIntersectionCountsBufferFormat);
	}
}

// TODO(RDG) Move these into the shader FParameters.
BEGIN_SHADER_PARAMETER_STRUCT(FTiledCapsuleShadowParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RDG_TEXTURE_ACCESS(RayTracedShadows, ERHIAccess::UAVCompute)
END_SHADER_PARAMETER_STRUCT()

// TODO(RDG) Move these into the shader FParameters.
BEGIN_SHADER_PARAMETER_STRUCT(FUpsampleCapsuleShadowParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RDG_TEXTURE_ACCESS(RayTracedShadows, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

bool FDeferredShadingSceneRenderer::RenderCapsuleDirectShadows(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const FLightSceneInfo& LightSceneInfo,
	FRDGTextureRef ScreenShadowMaskTexture,
	TArrayView<const FProjectedShadowInfo* const> CapsuleShadows,
	bool bProjectingForForwardShading) const
{
	bool bAllViewsHaveViewState = true;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (!View.ViewState)
		{
			bAllViewsHaveViewState = false;
		}
	}

	if (!SupportsCapsuleDirectShadows(FeatureLevel, GShaderPlatformForFeatureLevel[FeatureLevel])
		|| CapsuleShadows.Num() == 0
		|| !ViewFamily.EngineShowFlags.CapsuleShadows
		|| !bAllViewsHaveViewState)
	{
		return false;
	}

	QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderCapsuleShadows);

	FRDGTextureRef RayTracedShadowsRT = nullptr;

	{
		const FIntPoint BufferSize = GetBufferSizeForCapsuleShadows();
		const FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(BufferSize, PF_G16R16F, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_UAV));
		RayTracedShadowsRT = GraphBuilder.CreateTexture(Desc, TEXT("RayTracedShadows"));
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE(GraphBuilder, "CapsuleShadows");
		RDG_GPU_STAT_SCOPE(GraphBuilder, CapsuleShadows);

		TArray<FCapsuleShape> CapsuleShapeData;

		for (int32 ShadowIndex = 0; ShadowIndex < CapsuleShadows.Num(); ShadowIndex++)
		{
			const FProjectedShadowInfo* Shadow = CapsuleShadows[ShadowIndex];

			int32 OriginalCapsuleIndex = CapsuleShapeData.Num();

			TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> ShadowGroupPrimitives;
			Shadow->GetParentSceneInfo()->GatherLightingAttachmentGroupPrimitives(ShadowGroupPrimitives);

			for (int32 ChildIndex = 0; ChildIndex < ShadowGroupPrimitives.Num(); ChildIndex++)
			{
				const FPrimitiveSceneInfo* PrimitiveSceneInfo = ShadowGroupPrimitives[ChildIndex];

				if (PrimitiveSceneInfo->Proxy->CastsDynamicShadow())
				{
					PrimitiveSceneInfo->Proxy->GetShadowShapes(CapsuleShapeData);
				}
			}

			const float FadeRadiusScale = Shadow->FadeAlphas[ViewIndex];

			for (int32 ShapeIndex = OriginalCapsuleIndex; ShapeIndex < CapsuleShapeData.Num(); ShapeIndex++)
			{
				CapsuleShapeData[ShapeIndex].Radius *= FadeRadiusScale;
			}
		}

		if (CapsuleShapeData.Num() > 0)
		{
			const bool bDirectionalLight = LightSceneInfo.Proxy->GetLightType() == LightType_Directional;
			FIntRect ScissorRect;

			if (!LightSceneInfo.Proxy->GetScissorRect(ScissorRect, View, View.ViewRect))
			{
				ScissorRect = View.ViewRect;
			}

			const FIntPoint GroupSize(
				FMath::DivideAndRoundUp(ScissorRect.Size().X / GetCapsuleShadowDownsampleFactor(), GShadowShapeTileSize),
				FMath::DivideAndRoundUp(ScissorRect.Size().Y / GetCapsuleShadowDownsampleFactor(), GShadowShapeTileSize));

			AllocateCapsuleTileIntersectionCountsBuffer(GroupSize, View.ViewState);
			int32 NumCapsuleShapeData = CapsuleShapeData.Num();
			AddPass(GraphBuilder, [&View, &LightSceneInfo, CapsuleShapeData = MoveTemp(CapsuleShapeData)](FRHICommandListImmediate& RHICmdList)
			{
				static_assert(sizeof(FCapsuleShape) == sizeof(FVector4) * 2, "FCapsuleShape has padding");
				const int32 DataSize = CapsuleShapeData.Num() * CapsuleShapeData.GetTypeSize();

				if (!IsValidRef(LightSceneInfo.ShadowCapsuleShapesVertexBuffer) || (int32)LightSceneInfo.ShadowCapsuleShapesVertexBuffer->GetSize() < DataSize)
				{
					LightSceneInfo.ShadowCapsuleShapesVertexBuffer.SafeRelease();
					LightSceneInfo.ShadowCapsuleShapesSRV.SafeRelease();
					FRHIResourceCreateInfo CreateInfo;
					LightSceneInfo.ShadowCapsuleShapesVertexBuffer = RHICreateVertexBuffer(DataSize, BUF_Volatile | BUF_ShaderResource, CreateInfo);
					LightSceneInfo.ShadowCapsuleShapesSRV = RHICreateShaderResourceView(LightSceneInfo.ShadowCapsuleShapesVertexBuffer, sizeof(FVector4), PF_A32B32G32R32F);
				}

				void* CapsuleShapeLockedData = RHILockVertexBuffer(LightSceneInfo.ShadowCapsuleShapesVertexBuffer, 0, DataSize, RLM_WriteOnly);
				FPlatformMemory::Memcpy(CapsuleShapeLockedData, CapsuleShapeData.GetData(), DataSize);
				RHIUnlockVertexBuffer(LightSceneInfo.ShadowCapsuleShapesVertexBuffer);

				RHICmdList.Transition(FRHITransitionInfo(View.ViewState->CapsuleTileIntersectionCountsBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier));
				RHICmdList.ClearUAVUint(View.ViewState->CapsuleTileIntersectionCountsBuffer.UAV, FUintVector4(0, 0, 0, 0));
			});

			{
				FTiledCapsuleShadowParameters* PassParameters = GraphBuilder.AllocParameters<FTiledCapsuleShadowParameters>();
				PassParameters->RayTracedShadows = RayTracedShadowsRT;
				PassParameters->SceneTextures = SceneTexturesUniformBuffer;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("TiledCapsuleShadowing"),
					PassParameters,
					ERDGPassFlags::Compute,
					[this, &View, &LightSceneInfo, RayTracedShadowsRT, GroupSize, ScissorRect, bDirectionalLight, NumCapsuleShapeData](FRHIComputeCommandList& RHICmdList)
				{
					if (bDirectionalLight)
					{
						TShaderMapRef<TCapsuleShadowingCS<ShapeShadow_DirectionalLightTiledCulling, IPT_CapsuleShapes> > ComputeShader(View.ShaderMap);
						RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

						ComputeShader->SetParameters(
							RHICmdList,
							Scene,
							View,
							&LightSceneInfo,
							RayTracedShadowsRT->GetPooledRenderTarget()->GetRenderTargetItem(),
							GroupSize,
							&View.ViewState->CapsuleTileIntersectionCountsBuffer,
							FVector2D(GroupSize.X, GroupSize.Y),
							GCapsuleMaxDirectOcclusionDistance,
							ScissorRect,
							GetCapsuleShadowDownsampleFactor(),
							NumCapsuleShapeData,
							LightSceneInfo.ShadowCapsuleShapesSRV.GetReference(),
							0,
							NULL,
							NULL,
							NULL);

						DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSize.X, GroupSize.Y, 1);
						ComputeShader->UnsetParameters(RHICmdList, &View.ViewState->CapsuleTileIntersectionCountsBuffer);
					}
					else
					{
						TShaderMapRef<TCapsuleShadowingCS<ShapeShadow_PointLightTiledCulling, IPT_CapsuleShapes> > ComputeShader(View.ShaderMap);
						RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

						ComputeShader->SetParameters(
							RHICmdList,
							Scene,
							View,
							&LightSceneInfo,
							RayTracedShadowsRT->GetPooledRenderTarget()->GetRenderTargetItem(),
							GroupSize,
							&View.ViewState->CapsuleTileIntersectionCountsBuffer,
							FVector2D(GroupSize.X, GroupSize.Y),
							GCapsuleMaxDirectOcclusionDistance,
							ScissorRect,
							GetCapsuleShadowDownsampleFactor(),
							NumCapsuleShapeData,
							LightSceneInfo.ShadowCapsuleShapesSRV.GetReference(),
							0,
							NULL,
							NULL,
							NULL);

						DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSize.X, GroupSize.Y, 1);
						ComputeShader->UnsetParameters(RHICmdList, &View.ViewState->CapsuleTileIntersectionCountsBuffer);
					}
				});
			}

			{
				FUpsampleCapsuleShadowParameters* PassParameters = GraphBuilder.AllocParameters<FUpsampleCapsuleShadowParameters>();
				PassParameters->RenderTargets[0] = FRenderTargetBinding(ScreenShadowMaskTexture, ERenderTargetLoadAction::ELoad);
				PassParameters->RayTracedShadows = RayTracedShadowsRT;
				PassParameters->SceneTextures = SceneTexturesUniformBuffer;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("UpsampleCapsuleShadow %dx%d", ScissorRect.Width(), ScissorRect.Height()),
					PassParameters,
					ERDGPassFlags::Raster,
					[this, &View, &LightSceneInfo, RayTracedShadowsRT, GroupSize, ScissorRect, bProjectingForForwardShading](FRHICommandList& RHICmdList)
				{
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

					RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

					GraphicsPSOInit.BlendState = FProjectedShadowInfo::GetBlendStateForProjection(
						LightSceneInfo.GetDynamicShadowMapChannel(),
						false,
						false,
						bProjectingForForwardShading,
						false);

					TShaderMapRef<FCapsuleShadowingUpsampleVS> VertexShader(View.ShaderMap);
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					if (GCapsuleShadowsFullResolution)
					{
						TShaderMapRef<TCapsuleShadowingUpsamplePS<false, false> > PixelShader(View.ShaderMap);

						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GTileVertexDeclaration.VertexDeclarationRHI;
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

						VertexShader->SetParameters(RHICmdList, View, GroupSize, ScissorRect, View.ViewState->CapsuleTileIntersectionCountsBuffer);
						PixelShader->SetParameters(RHICmdList, View, ScissorRect, RayTracedShadowsRT->GetPooledRenderTarget(), true);
					}
					else
					{
						TShaderMapRef<TCapsuleShadowingUpsamplePS<true, false> > PixelShader(View.ShaderMap);

						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GTileVertexDeclaration.VertexDeclarationRHI;
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

						VertexShader->SetParameters(RHICmdList, View, GroupSize, ScissorRect, View.ViewState->CapsuleTileIntersectionCountsBuffer);
						PixelShader->SetParameters(RHICmdList, View, ScissorRect, RayTracedShadowsRT->GetPooledRenderTarget(), true);
					}

					RHICmdList.SetStreamSource(0, GTileTexCoordVertexBuffer.VertexBufferRHI, 0);
					RHICmdList.DrawIndexedPrimitive(GTileIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2 * NumTileQuadsInBuffer, FMath::DivideAndRoundUp(GroupSize.X * GroupSize.Y, NumTileQuadsInBuffer));
				});
			}
		}
	}
	return true;
}

void FDeferredShadingSceneRenderer::CreateIndirectCapsuleShadows()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_CreateIndirectCapsuleShadows);

	for (int32 PrimitiveIndex = 0; PrimitiveIndex < Scene->DynamicIndirectCasterPrimitives.Num(); PrimitiveIndex++)
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->DynamicIndirectCasterPrimitives[PrimitiveIndex];
		FPrimitiveSceneProxy* PrimitiveProxy = PrimitiveSceneInfo->Proxy;

		if (PrimitiveProxy->CastsDynamicShadow() && PrimitiveProxy->CastsDynamicIndirectShadow())
		{
			TArray<FPrimitiveSceneInfo*, SceneRenderingAllocator> ShadowGroupPrimitives;
			PrimitiveSceneInfo->GatherLightingAttachmentGroupPrimitives(ShadowGroupPrimitives);

			// Compute the composite bounds of this group of shadow primitives.
			FBoxSphereBounds LightingGroupBounds = ShadowGroupPrimitives[0]->Proxy->GetBounds();

			for (int32 ChildIndex = 1; ChildIndex < ShadowGroupPrimitives.Num(); ChildIndex++)
			{
				const FPrimitiveSceneInfo* ShadowChild = ShadowGroupPrimitives[ChildIndex];

				if (ShadowChild->Proxy->CastsDynamicShadow())
				{
					LightingGroupBounds = LightingGroupBounds + ShadowChild->Proxy->GetBounds();
				}
			}

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				FViewInfo& View = Views[ViewIndex];

				float EffectiveMaxIndirectOcclusionDistance = GCapsuleMaxIndirectOcclusionDistance;

				if (PrimitiveProxy->HasDistanceFieldRepresentation())
				{
					// Increase max occlusion distance based on object size for distance field casters
					// This improves the solidness of the shadows, since the fadeout distance causes internal structure of objects to become visible
					EffectiveMaxIndirectOcclusionDistance += .5f * LightingGroupBounds.SphereRadius;
				}

				if (View.ViewFrustum.IntersectBox(LightingGroupBounds.Origin, LightingGroupBounds.BoxExtent + FVector(EffectiveMaxIndirectOcclusionDistance)))
				{
					View.IndirectShadowPrimitives.Add(PrimitiveSceneInfo);
				}
			}
		}
	}
}

void FDeferredShadingSceneRenderer::SetupIndirectCapsuleShadows(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View, 
	int32& NumCapsuleShapes, 
	int32& NumMeshesWithCapsules, 
	int32& NumMeshDistanceFieldCasters,
	FRHIShaderResourceView*& IndirectShadowLightDirectionSRV) const
{
	const float CosFadeStartAngle = FMath::Cos(GCapsuleShadowFadeAngleFromVertical);
	const FSkyLightSceneProxy* SkyLight = Scene ? Scene->SkyLight : NULL;

	static TArray<FCapsuleShape> CapsuleShapeData;
	static TArray<FVector4> CapsuleLightSourceData;
	static TArray<int32, TInlineAllocator<1>> MeshDistanceFieldCasterIndices;
	static TArray<FVector4> DistanceFieldCasterLightSourceData;

	CapsuleShapeData.Reset();
	MeshDistanceFieldCasterIndices.Reset();
	CapsuleLightSourceData.Reset();
	DistanceFieldCasterLightSourceData.Reset();
	IndirectShadowLightDirectionSRV = NULL;

	const bool bComputeLightDataFromVolumetricLightmapOrGpuSkyEnvMapIrradiance = Scene && (Scene->VolumetricLightmapSceneData.HasData() || (Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled));

	for (int32 PrimitiveIndex = 0; PrimitiveIndex < View.IndirectShadowPrimitives.Num(); PrimitiveIndex++)
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = View.IndirectShadowPrimitives[PrimitiveIndex];
		const FIndirectLightingCacheAllocation* Allocation = PrimitiveSceneInfo->IndirectLightingCacheAllocation;

		FVector4 PackedLightDirection(0, 0, 1, PI / 16);
		float ShapeFadeAlpha = 1;

		if (bComputeLightDataFromVolumetricLightmapOrGpuSkyEnvMapIrradiance)
		{
			// Encode object position for ComputeLightDirectionsFromVolumetricLightmapCS
			PackedLightDirection = FVector4(PrimitiveSceneInfo->Proxy->GetBounds().Origin, 0);
		}
		else if (SkyLight 
			&& !SkyLight->bHasStaticLighting
			&& SkyLight->bWantsStaticShadowing
			&& View.Family->EngineShowFlags.SkyLighting
			&& Allocation)
		{
			// Stationary sky light case
			// Get the indirect shadow direction from the unoccluded sky direction
			const float ConeAngle = FMath::Max(Allocation->CurrentSkyBentNormal.W * GCapsuleSkyAngleScale * .5f * PI, GCapsuleMinSkyAngle * PI / 180.0f);
			PackedLightDirection = FVector4(Allocation->CurrentSkyBentNormal, ConeAngle);
		}
		else if (SkyLight 
			&& !SkyLight->bHasStaticLighting 
			&& !SkyLight->bWantsStaticShadowing
			&& View.Family->EngineShowFlags.SkyLighting)
		{
			// Movable sky light case
			const FSHVector2 SkyLightingIntensity = FSHVectorRGB2(SkyLight->IrradianceEnvironmentMap).GetLuminance();
			const FVector ExtractedMaxDirection = SkyLightingIntensity.GetMaximumDirection();

			// Get the indirect shadow direction from the primary sky lighting direction
			PackedLightDirection = FVector4(ExtractedMaxDirection, GCapsuleIndirectConeAngle);
		}
		else if (Allocation)
		{
			// Static sky light or no sky light case
			FSHVectorRGB2 IndirectLighting;
			IndirectLighting.R = FSHVector2(Allocation->SingleSamplePacked0[0]);
			IndirectLighting.G = FSHVector2(Allocation->SingleSamplePacked0[1]);
			IndirectLighting.B = FSHVector2(Allocation->SingleSamplePacked0[2]);
			const FSHVector2 IndirectLightingIntensity = IndirectLighting.GetLuminance();
			const FVector ExtractedMaxDirection = IndirectLightingIntensity.GetMaximumDirection();

			// Get the indirect shadow direction from the primary indirect lighting direction
			PackedLightDirection = FVector4(ExtractedMaxDirection, GCapsuleIndirectConeAngle);
		}

		if (CosFadeStartAngle < 1 && !bComputeLightDataFromVolumetricLightmapOrGpuSkyEnvMapIrradiance)
		{
			// Fade out when nearly vertical up due to self shadowing artifacts
			ShapeFadeAlpha = 1 - FMath::Clamp(2 * (-PackedLightDirection.Z - CosFadeStartAngle) / (1 - CosFadeStartAngle), 0.0f, 1.0f);
		}
			
		if (ShapeFadeAlpha > 0)
		{
			const int32 OriginalNumCapsuleShapes = CapsuleShapeData.Num();
			const int32 OriginalNumMeshDistanceFieldCasters = MeshDistanceFieldCasterIndices.Num();

			TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> ShadowGroupPrimitives;
			PrimitiveSceneInfo->GatherLightingAttachmentGroupPrimitives(ShadowGroupPrimitives);

			for (int32 ChildIndex = 0; ChildIndex < ShadowGroupPrimitives.Num(); ChildIndex++)
			{
				const FPrimitiveSceneInfo* GroupPrimitiveSceneInfo = ShadowGroupPrimitives[ChildIndex];

				if (GroupPrimitiveSceneInfo->Proxy->CastsDynamicShadow())
				{
					GroupPrimitiveSceneInfo->Proxy->GetShadowShapes(CapsuleShapeData);
					
					if (GroupPrimitiveSceneInfo->Proxy->HasDistanceFieldRepresentation())
					{
						MeshDistanceFieldCasterIndices.Append(GroupPrimitiveSceneInfo->DistanceFieldInstanceIndices);
					}
				}
			}

			// Pack both values into a single float to keep float4 alignment
			const FFloat16 LightAngle16f = FFloat16(PackedLightDirection.W);
			const FFloat16 MinVisibility16f = FFloat16(PrimitiveSceneInfo->Proxy->GetDynamicIndirectShadowMinVisibility());
			const uint32 PackedWInt = ((uint32)LightAngle16f.Encoded) | ((uint32)MinVisibility16f.Encoded << 16);
			PackedLightDirection.W = *(float*)&PackedWInt;

			//@todo - remove entries with 0 fade alpha
			for (int32 ShapeIndex = OriginalNumCapsuleShapes; ShapeIndex < CapsuleShapeData.Num(); ShapeIndex++)
			{
				CapsuleLightSourceData.Add(PackedLightDirection);
			}

			for (int32 CasterIndex = OriginalNumMeshDistanceFieldCasters; CasterIndex < MeshDistanceFieldCasterIndices.Num(); CasterIndex++)
			{
				DistanceFieldCasterLightSourceData.Add(PackedLightDirection);
			}

			NumMeshesWithCapsules++;
		}
	}

	if (CapsuleShapeData.Num() > 0 || MeshDistanceFieldCasterIndices.Num() > 0)
	{
		static_assert(sizeof(FCapsuleShape) == sizeof(FVector4)* 2, "FCapsuleShape has padding");
		if (CapsuleShapeData.Num() > 0)
		{
			const int32 DataSize = CapsuleShapeData.Num() * CapsuleShapeData.GetTypeSize();

			if (!IsValidRef(View.ViewState->IndirectShadowCapsuleShapesVertexBuffer) || (int32)View.ViewState->IndirectShadowCapsuleShapesVertexBuffer->GetSize() < DataSize)
			{
				View.ViewState->IndirectShadowCapsuleShapesVertexBuffer.SafeRelease();
				View.ViewState->IndirectShadowCapsuleShapesSRV.SafeRelease();
				FRHIResourceCreateInfo CreateInfo;
				View.ViewState->IndirectShadowCapsuleShapesVertexBuffer = RHICreateVertexBuffer(DataSize, BUF_Volatile | BUF_ShaderResource, CreateInfo);
				View.ViewState->IndirectShadowCapsuleShapesSRV = RHICreateShaderResourceView(View.ViewState->IndirectShadowCapsuleShapesVertexBuffer, sizeof(FVector4), PF_A32B32G32R32F);
			}

			void* CapsuleShapeLockedData = RHILockVertexBuffer(View.ViewState->IndirectShadowCapsuleShapesVertexBuffer, 0, DataSize, RLM_WriteOnly);
			FPlatformMemory::Memcpy(CapsuleShapeLockedData, CapsuleShapeData.GetData(), DataSize);
			RHIUnlockVertexBuffer(View.ViewState->IndirectShadowCapsuleShapesVertexBuffer);
		}

		if (MeshDistanceFieldCasterIndices.Num() > 0)
		{
			const int32 DataSize = MeshDistanceFieldCasterIndices.Num() * MeshDistanceFieldCasterIndices.GetTypeSize();

			if (!IsValidRef(View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesVertexBuffer) || (int32)View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesVertexBuffer->GetSize() < DataSize)
			{
				View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesVertexBuffer.SafeRelease();
				View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesSRV.SafeRelease();
				FRHIResourceCreateInfo CreateInfo;
				View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesVertexBuffer = RHICreateVertexBuffer(DataSize, BUF_Volatile | BUF_ShaderResource, CreateInfo);
				View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesSRV = RHICreateShaderResourceView(View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesVertexBuffer, sizeof(uint32), PF_R32_UINT);
			}

			void* LockedData = RHILockVertexBuffer(View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesVertexBuffer, 0, DataSize, RLM_WriteOnly);
			FPlatformMemory::Memcpy(LockedData, MeshDistanceFieldCasterIndices.GetData(), DataSize);
			RHIUnlockVertexBuffer(View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesVertexBuffer);
		}

		EPixelFormat LightDirectionDataFormat = PF_A32B32G32R32F;

		{
			size_t CapsuleLightSourceDataSize = CapsuleLightSourceData.Num() * CapsuleLightSourceData.GetTypeSize();
			const int32 DataSize = CapsuleLightSourceDataSize + DistanceFieldCasterLightSourceData.Num() * DistanceFieldCasterLightSourceData.GetTypeSize();
			check(DataSize > 0);

			if (!IsValidRef(View.ViewState->IndirectShadowLightDirectionVertexBuffer) || (int32)View.ViewState->IndirectShadowLightDirectionVertexBuffer->GetSize() < DataSize)
			{
				View.ViewState->IndirectShadowLightDirectionVertexBuffer.SafeRelease();
				View.ViewState->IndirectShadowLightDirectionSRV.SafeRelease();
				FRHIResourceCreateInfo CreateInfo;
				View.ViewState->IndirectShadowLightDirectionVertexBuffer = RHICreateVertexBuffer(DataSize, BUF_Volatile | BUF_ShaderResource, CreateInfo);
				View.ViewState->IndirectShadowLightDirectionSRV = RHICreateShaderResourceView(View.ViewState->IndirectShadowLightDirectionVertexBuffer, sizeof(FVector4), LightDirectionDataFormat);
			}

			FVector4* LightDirectionLockedData = (FVector4*)RHILockVertexBuffer(View.ViewState->IndirectShadowLightDirectionVertexBuffer, 0, DataSize, RLM_WriteOnly);
			FPlatformMemory::Memcpy(LightDirectionLockedData, CapsuleLightSourceData.GetData(), CapsuleLightSourceDataSize);
			// Light data for distance fields is placed after capsule light data
			// This packing behavior must match GetLightDirectionData
			FPlatformMemory::Memcpy((char*)LightDirectionLockedData + CapsuleLightSourceDataSize, DistanceFieldCasterLightSourceData.GetData(), DistanceFieldCasterLightSourceData.Num() * DistanceFieldCasterLightSourceData.GetTypeSize());
			RHIUnlockVertexBuffer(View.ViewState->IndirectShadowLightDirectionVertexBuffer);

			IndirectShadowLightDirectionSRV = View.ViewState->IndirectShadowLightDirectionSRV;
		}

		if (bComputeLightDataFromVolumetricLightmapOrGpuSkyEnvMapIrradiance)
		{
			int32 NumLightDataElements = CapsuleLightSourceData.Num() + DistanceFieldCasterLightSourceData.Num();

			if (!IsValidRef(View.ViewState->IndirectShadowVolumetricLightmapDerivedLightDirection.Buffer) 
				|| (int32)View.ViewState->IndirectShadowVolumetricLightmapDerivedLightDirection.NumBytes != View.ViewState->IndirectShadowLightDirectionVertexBuffer->GetSize())
			{
				View.ViewState->IndirectShadowVolumetricLightmapDerivedLightDirection.Release();
				View.ViewState->IndirectShadowVolumetricLightmapDerivedLightDirection.Initialize(GPixelFormats[LightDirectionDataFormat].BlockBytes, NumLightDataElements, LightDirectionDataFormat);
			}

			IndirectShadowLightDirectionSRV = View.ViewState->IndirectShadowVolumetricLightmapDerivedLightDirection.SRV;

			TShaderMapRef<FComputeLightDirectionFromVolumetricLightmapCS> ComputeShader(View.ShaderMap);

			uint32 SkyLightMode = Scene->SkyLight && Scene->SkyLight->bWantsStaticShadowing ? 1 : 0;
			SkyLightMode = Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled ? 2 : SkyLightMode;

			const int32 GroupSize = FMath::DivideAndRoundUp(NumLightDataElements, GComputeLightDirectionFromVolumetricLightmapGroupSize);

			FRWBuffer& ComputedLightDirectionData = View.ViewState->IndirectShadowVolumetricLightmapDerivedLightDirection;

			AddPass(GraphBuilder, [&ComputedLightDirectionData](FRHIComputeCommandList& RHICmdList)
			{
				RHICmdList.Transition(FRHITransitionInfo(ComputedLightDirectionData.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			});

			FComputeLightDirectionFromVolumetricLightmapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeLightDirectionFromVolumetricLightmapCS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->NumLightDirectionData = NumLightDataElements;
			PassParameters->SkyLightMode = SkyLightMode;
			PassParameters->CapsuleIndirectConeAngle = GCapsuleSkyAngleScale;
			PassParameters->CapsuleSkyAngleScale = GCapsuleSkyAngleScale;
			PassParameters->CapsuleMinSkyAngle = GCapsuleMinSkyAngle;
			PassParameters->RWComputedLightDirectionData = ComputedLightDirectionData.UAV;
			PassParameters->LightDirectionData = View.ViewState->IndirectShadowLightDirectionSRV;

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("LightDirectionFromVolumetricLightmap"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				PassParameters,
				FIntVector(GroupSize, 1, 1));

			AddPass(GraphBuilder, [&ComputedLightDirectionData](FRHIComputeCommandList& RHICmdList)
			{
				RHICmdList.Transition(FRHITransitionInfo(ComputedLightDirectionData.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
			});
		}
	}

	NumCapsuleShapes = CapsuleShapeData.Num();
	NumMeshDistanceFieldCasters = MeshDistanceFieldCasterIndices.Num();
}

void FDeferredShadingSceneRenderer::RenderIndirectCapsuleShadows(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef ScreenSpaceAOTexture,
	bool& bScreenSpaceAOIsValid) const
{
	if (!SupportsCapsuleIndirectShadows(FeatureLevel, GShaderPlatformForFeatureLevel[FeatureLevel])
		|| !ViewFamily.EngineShowFlags.DynamicShadows
		|| !ViewFamily.EngineShowFlags.CapsuleShadows)
	{
		return;
	}

	check(ScreenSpaceAOTexture);
	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderIndirectCapsuleShadows);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderIndirectCapsuleShadows);

	bool bAnyViewsUseCapsuleShadows = false;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (View.IndirectShadowPrimitives.Num() > 0 && View.ViewState)
		{
			bAnyViewsUseCapsuleShadows = true;
		}
	}

	if (!bAnyViewsUseCapsuleShadows)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "IndirectCapsuleShadows");

	FRDGTextureRef RayTracedShadowsRT = nullptr;

	{
		const FIntPoint BufferSize = GetBufferSizeForCapsuleShadows();
		const FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(BufferSize, PF_G16R16F, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_UAV));
		// Reuse temporary target from RTDF shadows
		RayTracedShadowsRT = GraphBuilder.CreateTexture(Desc, TEXT("RayTracedShadows"));
	}

	TArray<FRDGTextureRef, TInlineAllocator<2>> RenderTargets;

	if (SceneColorTexture)
	{
		RenderTargets.Add(SceneColorTexture);
	}

	if (bScreenSpaceAOIsValid)
	{
		RenderTargets.Add(ScreenSpaceAOTexture);
	}
	else if (!SceneColorTexture)
	{
		bScreenSpaceAOIsValid = true;
		RenderTargets.Add(ScreenSpaceAOTexture);
		AddClearRenderTargetPass(GraphBuilder, ScreenSpaceAOTexture);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (View.IndirectShadowPrimitives.Num() > 0 && View.ViewState)
		{
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
			RDG_GPU_STAT_SCOPE(GraphBuilder, CapsuleShadows);

			int32 NumCapsuleShapes = 0;
			int32 NumMeshesWithCapsules = 0;
			int32 NumMeshDistanceFieldCasters = 0;
			FRHIShaderResourceView* IndirectShadowLightDirectionSRV = NULL;
			SetupIndirectCapsuleShadows(GraphBuilder, View, NumCapsuleShapes, NumMeshesWithCapsules, NumMeshDistanceFieldCasters, IndirectShadowLightDirectionSRV);

			if (NumCapsuleShapes == 0 && NumMeshDistanceFieldCasters == 0)
			{
				continue;
			}

			check(IndirectShadowLightDirectionSRV);

			const FIntRect ScissorRect = View.ViewRect;

			const FIntPoint GroupSize(
				FMath::DivideAndRoundUp(ScissorRect.Size().X / GetCapsuleShadowDownsampleFactor(), GShadowShapeTileSize),
				FMath::DivideAndRoundUp(ScissorRect.Size().Y / GetCapsuleShadowDownsampleFactor(), GShadowShapeTileSize));

			AllocateCapsuleTileIntersectionCountsBuffer(GroupSize, View.ViewState);

			AddPass(GraphBuilder, [&View](FRHIComputeCommandList& RHICmdList)
			{
				RHICmdList.Transition(FRHITransitionInfo(View.ViewState->CapsuleTileIntersectionCountsBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier));
				RHICmdList.ClearUAVUint(View.ViewState->CapsuleTileIntersectionCountsBuffer.UAV, FUintVector4(0, 0, 0, 0));
			});

			{
				FTiledCapsuleShadowParameters* PassParameters = GraphBuilder.AllocParameters<FTiledCapsuleShadowParameters>();
				PassParameters->RayTracedShadows = RayTracedShadowsRT;
				PassParameters->SceneTextures = SceneTexturesUniformBuffer;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("TiledCapsuleShadowing %u capsules among %u meshes", NumCapsuleShapes, NumMeshesWithCapsules),
					PassParameters,
					ERDGPassFlags::Compute,
					[this, &View, RayTracedShadowsRT, IndirectShadowLightDirectionSRV, GroupSize, ScissorRect, NumCapsuleShapes, NumMeshDistanceFieldCasters](FRHIComputeCommandList& RHICmdList)
				{
					TShaderRef<TCapsuleShadowingBaseCS<ShapeShadow_IndirectTiledCulling>> ComputeShaderBase;

					if (NumCapsuleShapes > 0 && NumMeshDistanceFieldCasters > 0)
					{
						TShaderMapRef<TCapsuleShadowingCS<ShapeShadow_IndirectTiledCulling, IPT_CapsuleShapesAndMeshDistanceFields> > ComputeShader(View.ShaderMap);
						ComputeShaderBase = ComputeShader;
					}
					else if (NumCapsuleShapes > 0)
					{
						TShaderMapRef<TCapsuleShadowingCS<ShapeShadow_IndirectTiledCulling, IPT_CapsuleShapes> > ComputeShader(View.ShaderMap);
						ComputeShaderBase = ComputeShader;
					}
					else
					{
						check(NumMeshDistanceFieldCasters > 0);
						TShaderMapRef<TCapsuleShadowingCS<ShapeShadow_IndirectTiledCulling, IPT_MeshDistanceFields> > ComputeShader(View.ShaderMap);
						ComputeShaderBase = ComputeShader;
					}

					RHICmdList.SetComputeShader(ComputeShaderBase.GetComputeShader());

					ComputeShaderBase->SetParameters(
						RHICmdList,
						Scene,
						View,
						NULL,
						RayTracedShadowsRT->GetPooledRenderTarget()->GetRenderTargetItem(),
						GroupSize,
						&View.ViewState->CapsuleTileIntersectionCountsBuffer,
						FVector2D(GroupSize.X, GroupSize.Y),
						GCapsuleMaxIndirectOcclusionDistance,
						ScissorRect,
						GetCapsuleShadowDownsampleFactor(),
						NumCapsuleShapes,
						View.ViewState->IndirectShadowCapsuleShapesSRV ? View.ViewState->IndirectShadowCapsuleShapesSRV.GetReference() : NULL,
						NumMeshDistanceFieldCasters,
						View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesSRV ? View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesSRV.GetReference() : NULL,
						IndirectShadowLightDirectionSRV,
						NULL);

					DispatchComputeShader(RHICmdList, ComputeShaderBase.GetShader(), GroupSize.X, GroupSize.Y, 1);
					ComputeShaderBase->UnsetParameters(RHICmdList, &View.ViewState->CapsuleTileIntersectionCountsBuffer);
				});
			}

			{
				const int32 RenderTargetCount = RenderTargets.Num();

				FUpsampleCapsuleShadowParameters* PassParameters = GraphBuilder.AllocParameters<FUpsampleCapsuleShadowParameters>();
				for (int32 Index = 0; Index < RenderTargetCount; ++Index)
				{
					PassParameters->RenderTargets[Index] = FRenderTargetBinding(RenderTargets[Index], ERenderTargetLoadAction::ELoad);
				}
				PassParameters->RayTracedShadows = RayTracedShadowsRT;
				PassParameters->SceneTextures = SceneTexturesUniformBuffer;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("UpsampleCapsuleShadow %dx%d", ScissorRect.Width(), ScissorRect.Height()),
					PassParameters,
					ERDGPassFlags::Raster,
					[this, &View, RayTracedShadowsRT, RenderTargetCount, GroupSize, ScissorRect](FRHICommandList& RHICmdList)
				{
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

					RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

					// Modulative blending against scene color for application to indirect diffuse
					if (RenderTargetCount > 1)
					{
						GraphicsPSOInit.BlendState = TStaticBlendState<
							CW_RGB, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_One,
							CW_RED, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_One>::GetRHI();
					}
					// Modulative blending against SSAO occlusion value for application to indirect specular, since Reflection Environment pass masks by AO
					else
					{
						GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI();
					}

					TShaderMapRef<FCapsuleShadowingUpsampleVS> VertexShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GTileVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					if (RenderTargetCount > 1)
					{
						if (GCapsuleShadowsFullResolution)
						{
							TShaderMapRef<TCapsuleShadowingUpsamplePS<false, true> > PixelShader(View.ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							VertexShader->SetParameters(RHICmdList, View, GroupSize, ScissorRect, View.ViewState->CapsuleTileIntersectionCountsBuffer);
							PixelShader->SetParameters(RHICmdList, View, ScissorRect, RayTracedShadowsRT->GetPooledRenderTarget(), false);
						}
						else
						{
							TShaderMapRef<TCapsuleShadowingUpsamplePS<true, true> > PixelShader(View.ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							VertexShader->SetParameters(RHICmdList, View, GroupSize, ScissorRect, View.ViewState->CapsuleTileIntersectionCountsBuffer);
							PixelShader->SetParameters(RHICmdList, View, ScissorRect, RayTracedShadowsRT->GetPooledRenderTarget(), false);
						}
					}
					else
					{
						if (GCapsuleShadowsFullResolution)
						{
							TShaderMapRef<TCapsuleShadowingUpsamplePS<false, false> > PixelShader(View.ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							VertexShader->SetParameters(RHICmdList, View, GroupSize, ScissorRect, View.ViewState->CapsuleTileIntersectionCountsBuffer);
							PixelShader->SetParameters(RHICmdList, View, ScissorRect, RayTracedShadowsRT->GetPooledRenderTarget(), false);
						}
						else
						{
							TShaderMapRef<TCapsuleShadowingUpsamplePS<true, false> > PixelShader(View.ShaderMap);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
							VertexShader->SetParameters(RHICmdList, View, GroupSize, ScissorRect, View.ViewState->CapsuleTileIntersectionCountsBuffer);
							PixelShader->SetParameters(RHICmdList, View, ScissorRect, RayTracedShadowsRT->GetPooledRenderTarget(), false);
						}
					}

					RHICmdList.SetStreamSource(0, GTileTexCoordVertexBuffer.VertexBufferRHI, 0);
					RHICmdList.DrawIndexedPrimitive(GTileIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2 * NumTileQuadsInBuffer, FMath::DivideAndRoundUp(GroupSize.X * GroupSize.Y, NumTileQuadsInBuffer));
				});
			}
		}
	}
}

bool FSceneRenderer::ShouldPrepareForDFInsetIndirectShadow() const
{
	bool bSceneHasInsetDFPrimitives = false;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		for (int32 PrimitiveIndex = 0; PrimitiveIndex < View.IndirectShadowPrimitives.Num(); PrimitiveIndex++)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = View.IndirectShadowPrimitives[PrimitiveIndex];
			TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> ShadowGroupPrimitives;
			PrimitiveSceneInfo->GatherLightingAttachmentGroupPrimitives(ShadowGroupPrimitives);

			for (int32 ChildIndex = 0; ChildIndex < ShadowGroupPrimitives.Num(); ChildIndex++)
			{
				const FPrimitiveSceneInfo* GroupPrimitiveSceneInfo = ShadowGroupPrimitives[ChildIndex];

				if (GroupPrimitiveSceneInfo->Proxy->CastsDynamicShadow() && GroupPrimitiveSceneInfo->Proxy->HasDistanceFieldRepresentation())
				{
					bSceneHasInsetDFPrimitives = true;
				}
			}
		}
	}

	return bSceneHasInsetDFPrimitives && SupportsCapsuleIndirectShadows(FeatureLevel, GShaderPlatformForFeatureLevel[FeatureLevel]) && ViewFamily.EngineShowFlags.CapsuleShadows;
}

BEGIN_SHADER_PARAMETER_STRUCT(FCapsuleShadowsForMovableSkylightParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RDG_TEXTURE_ACCESS(BentNormalInput, ERHIAccess::SRVCompute)
	RDG_TEXTURE_ACCESS(BentNormalOutput, ERHIAccess::UAVCompute)
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderCapsuleShadowsForMovableSkylight(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef& BentNormalOutput) const
{
	if (SupportsCapsuleIndirectShadows(FeatureLevel, GShaderPlatformForFeatureLevel[FeatureLevel])
		&& ViewFamily.EngineShowFlags.CapsuleShadows)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderCapsuleShadowsSkylight);

		bool bAnyViewsUseCapsuleShadows = false;

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];

			if (View.IndirectShadowPrimitives.Num() > 0 && View.ViewState)
			{
				bAnyViewsUseCapsuleShadows = true;
			}
		}

		if (bAnyViewsUseCapsuleShadows)
		{
			FRDGTextureRef NewBentNormal = nullptr;
			AllocateOrReuseAORenderTarget(GraphBuilder, NewBentNormal, TEXT("CapsuleBentNormal"), PF_FloatRGBA);

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];

				if (View.IndirectShadowPrimitives.Num() > 0 && View.ViewState)
				{
					RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
					RDG_EVENT_SCOPE(GraphBuilder, "IndirectCapsuleShadows");
					RDG_GPU_STAT_SCOPE(GraphBuilder, CapsuleShadows);

					FUniformBufferRHIRef PassUniformBuffer = CreateSceneTextureUniformBufferDependentOnShadingPath(GraphBuilder.RHICmdList, View.GetFeatureLevel(), ESceneTextureSetupMode::All);
					FUniformBufferStaticBindings GlobalUniformBuffers(PassUniformBuffer);
					SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(GraphBuilder.RHICmdList, GlobalUniformBuffers);

					int32 NumCapsuleShapes = 0;
					int32 NumMeshesWithCapsules = 0;
					int32 NumMeshDistanceFieldCasters = 0;
					FRHIShaderResourceView* IndirectShadowLightDirectionSRV = NULL;
					SetupIndirectCapsuleShadows(GraphBuilder, View, NumCapsuleShapes, NumMeshesWithCapsules, NumMeshDistanceFieldCasters, IndirectShadowLightDirectionSRV);

					// Don't render indirect occlusion from mesh distance fields when operating on a movable skylight,
					// DFAO is responsible for indirect occlusion from meshes with distance fields on a movable skylight.
					// A single mesh should only provide indirect occlusion for a given lighting component in one way.
					NumMeshDistanceFieldCasters = 0;

					if (NumCapsuleShapes > 0 || NumMeshDistanceFieldCasters > 0)
					{
						check(IndirectShadowLightDirectionSRV);

						FIntRect ScissorRect = View.ViewRect;

						{
							uint32 GroupSizeX = FMath::DivideAndRoundUp(ScissorRect.Size().X / GAODownsampleFactor, GShadowShapeTileSize);
							uint32 GroupSizeY = FMath::DivideAndRoundUp(ScissorRect.Size().Y / GAODownsampleFactor, GShadowShapeTileSize);

							auto* PassParameters = GraphBuilder.AllocParameters< FCapsuleShadowsForMovableSkylightParameters>();
							PassParameters->BentNormalInput = BentNormalOutput;
							PassParameters->BentNormalOutput = NewBentNormal;
							PassParameters->SceneTextures = SceneTexturesUniformBuffer;

							GraphBuilder.AddPass(
								RDG_EVENT_NAME("TiledCapsuleShadowing % u capsules among % u meshes", NumCapsuleShapes, NumMeshesWithCapsules),
								PassParameters,
								ERDGPassFlags::Compute,
								[this, &View, NewBentNormal, BentNormalOutput, GroupSizeX, GroupSizeY, NumCapsuleShapes, NumMeshDistanceFieldCasters, ScissorRect, IndirectShadowLightDirectionSRV]
								(FRHICommandList& RHICmdList)
							{
								FSceneRenderTargetItem& RayTracedShadowsRTI = NewBentNormal->GetPooledRenderTarget()->GetRenderTargetItem();

								{
									TShaderRef<TCapsuleShadowingBaseCS<ShapeShadow_MovableSkylightTiledCulling>> ComputeShaderBase;
									if (NumCapsuleShapes > 0 && NumMeshDistanceFieldCasters > 0)
									{
										TShaderMapRef<TCapsuleShadowingCS<ShapeShadow_MovableSkylightTiledCulling, IPT_CapsuleShapesAndMeshDistanceFields> > ComputeShader(View.ShaderMap);
										ComputeShaderBase = ComputeShader;
									}
									else if (NumCapsuleShapes > 0)
									{
										TShaderMapRef<TCapsuleShadowingCS<ShapeShadow_MovableSkylightTiledCulling, IPT_CapsuleShapes> > ComputeShader(View.ShaderMap);
										ComputeShaderBase = ComputeShader;
									}
									else
									{
										TShaderMapRef<TCapsuleShadowingCS<ShapeShadow_MovableSkylightTiledCulling, IPT_MeshDistanceFields> > ComputeShader(View.ShaderMap);
										ComputeShaderBase = ComputeShader;
									}

									RHICmdList.SetComputeShader(ComputeShaderBase.GetComputeShader());

									ComputeShaderBase->SetParameters(
										RHICmdList,
										Scene,
										View,
										NULL,
										RayTracedShadowsRTI,
										FIntPoint(GroupSizeX, GroupSizeY),
										NULL,
										FVector2D(GroupSizeX, GroupSizeY),
										GCapsuleMaxIndirectOcclusionDistance,
										ScissorRect,
										GAODownsampleFactor,
										NumCapsuleShapes,
										View.ViewState->IndirectShadowCapsuleShapesSRV ? View.ViewState->IndirectShadowCapsuleShapesSRV.GetReference() : NULL,
										NumMeshDistanceFieldCasters,
										View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesSRV ? View.ViewState->IndirectShadowMeshDistanceFieldCasterIndicesSRV.GetReference() : NULL,
										IndirectShadowLightDirectionSRV,
										BentNormalOutput->GetRHI());

									DispatchComputeShader(RHICmdList, ComputeShaderBase.GetShader(), GroupSizeX, GroupSizeY, 1);
									ComputeShaderBase->UnsetParameters(RHICmdList, nullptr);
								}
							});
						}

						// Replace the pipeline output with our output that has capsule shadows applied
						BentNormalOutput = NewBentNormal;
					}
				}
			}
		}
	}
}
