// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScreenProbeHardwareRayTracing.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"
#include "LumenRadianceCache.h"
#include "LumenScreenProbeGather.h"

#if RHI_RAYTRACING

#include "ClearQuad.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "RHIResources.h"
#include "PostProcess/PostProcessing.h"
#include "RayTracing/RaytracingOptions.h"
#include "Raytracing/RaytracingLighting.h"
#include <BuiltInRayTracingShaders.h>

int32 GLumenScreenProbeGatherHardwareTraceCards = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHardwareTraceCards(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing"),
	GLumenScreenProbeGatherHardwareTraceCards,
	TEXT("0. Software raytracing of diffuse indirect from Lumen cubemap tree. (Default)")
	TEXT("1. Enable hardware ray tracing of diffuse indirect.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherHardwareTraceMode = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHardwareTraceMode(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.Mode"),
	GLumenScreenProbeGatherHardwareTraceMode,
	TEXT("0. Hardware raytraced diffuse indirect gathering from Lumen cubemap tree projected with geometry normal.")
	TEXT("1. Hardware raytraced diffuse indirect gathering from Lumen cubemap tree projected with SDF normal.")
	TEXT("2. Hardware raytraced diffuse indirect gathering from Lumen cubemap tree projected with geometry/material normal. However, in two passes")
	TEXT("3. Brutal-force hardware raytracing of one bounce diffuse indirect.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherHardwareTraceCardsGeometryNormalOnly = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHardwareTraceCardsGeometryNormalOnly(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.GeometryNormal"),
	GLumenScreenProbeGatherHardwareTraceCardsGeometryNormalOnly,
	TEXT("0. Evaluate full materials to use material normal in two pass mode")
	TEXT("1. Evaluate geometry normal only in two pass mode"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenVisualizeHardwareTracing = 0;
FAutoConsoleVariableRef GVarLumenVisualizeHardwareTracing(
	TEXT("r.Lumen.Visualize.HardwareRayTracing"),
	GLumenVisualizeHardwareTracing,
	TEXT("0. Disable ray visualization.")
	TEXT("1. Enable ray visualization. (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenVisualizeHardwareTracingMode = 1;
FAutoConsoleVariableRef GVarLumenVisualizeHardwareTracingMode(
	TEXT("r.Lumen.Visualize.HardwareRayTracing.Mode"),
	GLumenVisualizeHardwareTracingMode,
	TEXT("0. Ray starts from viewpoint and traces to the center pixel.")
	TEXT("1. Ray starts from the center pixel, and then traces to a given screen point project world position. Fallback to Mode 1, when no object is hit in the target coordinate.")
	TEXT("2. Ray starts from the center pixel, and then traces along the default sampling direction."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenVisualizeHardwareTracingX = 200;
FAutoConsoleVariableRef GVarLumenVisualizeHardwareTracingX(
	TEXT("r.Lumen.Visualize.HardwareRayTracing.X"),
	GLumenVisualizeHardwareTracingX,
	TEXT("The x coordinate of the visualization source screen pixel. Visualization mode must be 1."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenVisualizeHardwareTracingY = 200;
FAutoConsoleVariableRef GVarLumenVisualizeHardwareTracingY(
	TEXT("r.Lumen.Visualize.HardwareRayTracing.Y"),
	GLumenVisualizeHardwareTracingY,
	TEXT("The y coordinate of the visualization source screen pixel. Visualization mode must be 1."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenVisualizeHardwareTracingFreeze = 0;
FAutoConsoleVariableRef GVarLumenVisualizeHardwareTracingFreeze(
	TEXT("r.Lumen.Visualize.HardwareRayTracing.Freeze"),
	GLumenVisualizeHardwareTracingFreeze,
	TEXT("Set to 1 to freeze the capture of new traces. Will use the captured ray origin and direction for tracing. The users can zoom in to debug the bias parameter."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

#endif

namespace Lumen
{
	bool UseHardwareRayTracedScreenProbeGather()
	{
	#if RHI_RAYTRACING
		return (GLumenScreenProbeGatherHardwareTraceCards != 0) && IsRayTracingEnabled();
	#else
		return false;
	#endif
	}
}

bool ShouldVisualizeLumenHardwareRayTracingPrimaryRay()
{
	bool bVisualizePrimaryRay = false;
#if RHI_RAYTRACING
	bVisualizePrimaryRay = GLumenScreenProbeGatherHardwareTraceCards == 1 && GLumenVisualizeHardwareTracing == 1 && IsRayTracingEnabled();
#endif
	return bVisualizePrimaryRay;
}

#if RHI_RAYTRACING

enum class ERayHitMaterialEvaluationType
{

	// Mode 0: use single pass dynamic branching in the default material CHS to query surface normal and InstanceID
	NormalFromGeometryLightingFromSurfaceCache,

	// Mode 1: After hardware ray hit, use the normal derived from SDF.
	NormalFromSDFLightingFromSurfaceCache,

	// Mode 2: Use surface normal.

	// First pass fetches the surface normal with WPO included. Direct fetching the geometry normal would change the 
	// RayTracingMaterialHitShaders.cpp too much for just reference mode.
	NormalFromGeometryOrMaterial,

	// Second pass direct reuse the surface normal after fetching the InstanceID. Note that the instance ID and normal 
	// are not in the same Payload struct, which makes it a two pass instead of one pass
	LightingFromSurfaceCache,

	// mode 3: Direct evaluate the material on hardware ray hit.
	NormalFromMaterialLightingFromHit,

	MAX
};

// A temporary hack for RGS to access array declaration.
// Workaround for error "subscripted value is not an array, matrix, or vector" in DXC when SHADER_PARAMETER_ARRAY is used in RGS
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FRGSRadianceCacheParameters,)
	SHADER_PARAMETER_ARRAY(float, RadianceProbeClipmapTMin, [LumenRadianceCache::MaxClipmaps])
	SHADER_PARAMETER_ARRAY(float, RadianceProbeClipmapSamplingJitter, [LumenRadianceCache::MaxClipmaps])
	SHADER_PARAMETER_ARRAY(float, WorldPositionToRadianceProbeCoordScale, [LumenRadianceCache::MaxClipmaps])
	SHADER_PARAMETER_ARRAY(FVector, WorldPositionToRadianceProbeCoordBias, [LumenRadianceCache::MaxClipmaps])
	SHADER_PARAMETER_ARRAY(float, RadianceProbeCoordToWorldPositionScale, [LumenRadianceCache::MaxClipmaps])
	SHADER_PARAMETER_ARRAY(FVector, RadianceProbeCoordToWorldPositionBias, [LumenRadianceCache::MaxClipmaps])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FRGSRadianceCacheParameters, "RGSRadianceCacheParameters");

void SetupRGSRadianceCacheParameters(const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	FRGSRadianceCacheParameters & RGSRadianceCacheParameters)
{
	for (int i = 0; i < LumenRadianceCache::MaxClipmaps; ++i)
	{
		RGSRadianceCacheParameters.RadianceProbeClipmapTMin[i] = RadianceCacheParameters.RadianceProbeClipmapTMin[i];
		RGSRadianceCacheParameters.RadianceProbeClipmapSamplingJitter[i] = RadianceCacheParameters.RadianceProbeClipmapSamplingJitter[i];
		RGSRadianceCacheParameters.WorldPositionToRadianceProbeCoordScale[i] = RadianceCacheParameters.WorldPositionToRadianceProbeCoordScale[i];
		RGSRadianceCacheParameters.WorldPositionToRadianceProbeCoordBias[i] = RadianceCacheParameters.WorldPositionToRadianceProbeCoordBias[i];
		RGSRadianceCacheParameters.RadianceProbeCoordToWorldPositionScale[i] = RadianceCacheParameters.RadianceProbeCoordToWorldPositionScale[i];
		RGSRadianceCacheParameters.RadianceProbeCoordToWorldPositionBias[i] = RadianceCacheParameters.RadianceProbeCoordToWorldPositionBias[i];
	}
}


class FLumenCardHardwareRayTracingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenCardHardwareRayTracingRGS, FGlobalShader)

	//Ray tracing permutation
	class FEnableTwoSidedGeometryForShadowDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FMissShaderLighting : SHADER_PERMUTATION_BOOL("DIM_MISS_SHADER_LIGHTING");

	//Lumen permutation
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");
	//Pass permutation
	class  FMaterialEvaluationType : SHADER_PERMUTATION_ENUM_CLASS("MATERIAL_EVALUATION_TYPE", ERayHitMaterialEvaluationType);

	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryForShadowDim,
		FMissShaderLighting, FStructuredImportanceSampling, FRadianceCache, FMaterialEvaluationType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		//----------------------------------------------------------------------------------------
		//Ray Racing Parameters
		SHADER_PARAMETER(int32, ReflectedShadowsType)
		SHADER_PARAMETER(uint32, PrimaryRayFlags)
		SHADER_PARAMETER(float, MaxNormalBias)
		SHADER_PARAMETER(int32, GeometryNormalOnlyForCHS)

		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRTLightingData>, LightDataBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture) //required for primary ray shader.
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)

		//----------------------------------------------------------------------------------------
		//Lumen Parameters

		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFGridParameters, MeshSDFGridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_REF(FRGSRadianceCacheParameters, RGSRadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedTraceParameters, CompactedTraceParameters)

		//-----------------------------------------------------------------------------------------
		//Pass Parameters
		//When we use the geometry normal reference mode, this cache is fetched in the first pass, 
		//and used in the second pass. 
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, GeometryNormalCache)

		//-----------------------------------------------------------------------------------------
		//Visualization Parameters
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>,RWHardwareDetailTracingVisualizationBuffer)
		SHADER_PARAMETER(FVector4, HardwareTracesVisualizationParams)

		END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);//DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static bool UseFullMaterialPayloadSetup(ERayHitMaterialEvaluationType MaterialEvaluationType)
	{
		return MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromMaterialLightingFromHit ||
			MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromGeometryOrMaterial ||
			MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromGeometryLightingFromSurfaceCache;
	}
	
	static int32 GetHardwareRayTracingModeCount()
	{
		return static_cast<int32>(ERayHitMaterialEvaluationType::MAX) - 1;
	}

	static FVector4 GetHardwareTracesVisualizationParams()
	{
		return FVector4(GLumenVisualizeHardwareTracing==1? 
			FMath::Clamp(GLumenVisualizeHardwareTracingMode,0,2):-1,
			GLumenVisualizeHardwareTracingX,
			GLumenVisualizeHardwareTracingY,
			GLumenVisualizeHardwareTracingFreeze);
	}

	static int32 GetVisualizationNumBufferElements()
	{
		return 10;// consistent with that in LumenScreenProbeHardwareRayTracing.usf VISUALIZATION_BUFFER_ELEMENTS
	}

	static int32 GetVisualizationBytesPerElement()
	{
		return sizeof(FVector4);
	}
	static int32 GetVivualizationBufferSizeInBytes()
	{
		return GetVisualizationBytesPerElement() * GetVisualizationNumBufferElements();
	}

	static int32 GetNumOfPrimitives()
	{
		//mode 0 trace only from the surface. only need 2 primitive, the surface normal colored by SDNormal , and the surface colored by InstanceID
		FVector4 VisualizationParams = GetHardwareTracesVisualizationParams();
		if (VisualizationParams.X == 0)
		{
			return 2;
		}
		else if (VisualizationParams.X == 1 || VisualizationParams.X == 2)
		{
			// source normal, 
			// bias vector, 
			// tracing vector, 
			// target surface normal (shading with DFObjectIndex), 
			// target surface shading with InstanceID).
			return 5;
		}

		return 2;
	}

	static ERayHitMaterialEvaluationType GetRayHitMaterialEvaluationType()
	{
		int ClampedMode = FMath::Clamp<int32>(GLumenScreenProbeGatherHardwareTraceMode, 0, GetHardwareRayTracingModeCount() - 1);
		
		ERayHitMaterialEvaluationType EvaluationType = ERayHitMaterialEvaluationType::NormalFromGeometryLightingFromSurfaceCache;
		
		switch (ClampedMode)
		{
		case 0:
			EvaluationType = ERayHitMaterialEvaluationType::NormalFromGeometryLightingFromSurfaceCache;
			break;
		case 1:
			EvaluationType = ERayHitMaterialEvaluationType::NormalFromSDFLightingFromSurfaceCache;
			break;
		case 2:
			EvaluationType = ERayHitMaterialEvaluationType::NormalFromGeometryOrMaterial;
			break;
		case 3:
			EvaluationType = ERayHitMaterialEvaluationType::NormalFromMaterialLightingFromHit;
			break;
		}

		return EvaluationType;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
		OutEnvironment.SetDefine(TEXT("LUMEN_HARDWARE_RAYTRACING"),1);

		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DYNAMIC_ANY_HIT_SHADER"), 0);
		
		OutEnvironment.SetDefine(TEXT("VISUALIZATION_NUM_BUFFER_ELEMENTS"), GetVisualizationNumBufferElements());
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "LumenCardHardwareRayTracingRGS", SF_RayGen);

class FLumenCardHardwarePrimaryRayTracingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardHardwarePrimaryRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenCardHardwarePrimaryRayTracingRGS, FGlobalShader)

		//Ray tracing permutation
		class FEnableTwoSidedGeometryForShadowDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FMissShaderLighting : SHADER_PERMUTATION_BOOL("DIM_MISS_SHADER_LIGHTING");

	//Lumen permutation
	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");
	//Pass permutation
	class  FMaterialEvaluationType : SHADER_PERMUTATION_ENUM_CLASS("MATERIAL_EVALUATION_TYPE", ERayHitMaterialEvaluationType);

	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryForShadowDim,
		FMissShaderLighting, FRadianceCache, FMaterialEvaluationType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		//----------------------------------------------------------------------------------------
		//Ray Racing Parameters
		SHADER_PARAMETER(int32, ReflectedShadowsType)
		SHADER_PARAMETER(uint32, PrimaryRayFlags)
		SHADER_PARAMETER(float, MaxNormalBias)
		SHADER_PARAMETER(int32, GeometryNormalOnlyForCHS)

		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRTLightingData>, LightDataBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture) //required for primary ray shader.
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)

		//----------------------------------------------------------------------------------------
		//Lumen Parameters

		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFGridParameters, MeshSDFGridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_REF(FRGSRadianceCacheParameters, RGSRadianceCacheParameters)

		//-----------------------------------------------------------------------------------------
		//Pass Parameters
		//When we use the geometry normal reference mode, this cache is fetched in the first pass, 
		//and used in the second pass. 
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, GeometryNormalCache)

		//-----------------------------------------------------------------------------------------
		//Visualization Parameters
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, RWHardwareDetailTracingVisualizationBuffer)
		SHADER_PARAMETER(FVector4, HardwareTracesVisualizationParams)
		SHADER_PARAMETER(FIntRect, ViewDimensions)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSceneColor)
		END_SHADER_PARAMETER_STRUCT()

		static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);//DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static bool UseFullMaterialPayloadSetup(ERayHitMaterialEvaluationType MaterialEvaluationType)
	{
		return MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromMaterialLightingFromHit ||
			MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromGeometryOrMaterial ||
			MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromGeometryLightingFromSurfaceCache;
	}

	static int32 GetHardwareRayTracingModeCount()
	{
		return static_cast<int32>(ERayHitMaterialEvaluationType::MAX) - 1;
	}

	static FVector4 GetHardwareTracesVisualizationParams()
	{
		return FVector4(GLumenVisualizeHardwareTracing == 1 ?
			FMath::Clamp(GLumenVisualizeHardwareTracingMode, 0, 2) : -1,
			GLumenVisualizeHardwareTracingX,
			GLumenVisualizeHardwareTracingY,
			GLumenVisualizeHardwareTracingFreeze);
	}

	static int32 GetVisualizationNumBufferElements()
	{
		return 10;// consistent with that in LumenScreenProbeHardwareRayTracing.usf VISUALIZATION_BUFFER_ELEMENTS
	}

	static int32 GetVisualizationBytesPerElement()
	{
		return sizeof(FVector4);
	}
	static int32 GetVivualizationBufferSizeInBytes()
	{
		return GetVisualizationBytesPerElement() * GetVisualizationNumBufferElements();
	}

	static int32 GetNumOfPrimitives()
	{
		//mode 0 trace only from the surface. only need 2 primitive, the surface normal colored by SDNormal , and the surface colored by InstanceID
		FVector4 VisualizationParams = GetHardwareTracesVisualizationParams();
		if (VisualizationParams.X == 0)
		{
			return 2;
		}
		else if (VisualizationParams.X == 1 || VisualizationParams.X == 2)
		{
			// source normal, 
			// bias vector, 
			// tracing vector, 
			// target surface normal (shading with DFObjectIndex), 
			// target surface shading with InstanceID).
			return 5;
		}

		return 2;
	}

	static ERayHitMaterialEvaluationType GetRayHitMaterialEvaluationType()
	{
		int ClampedMode = FMath::Clamp<int32>(GLumenScreenProbeGatherHardwareTraceMode, 0, GetHardwareRayTracingModeCount() - 1);

		ERayHitMaterialEvaluationType EvaluationType = ERayHitMaterialEvaluationType::NormalFromGeometryLightingFromSurfaceCache;

		switch (ClampedMode)
		{
		case 0:
			EvaluationType = ERayHitMaterialEvaluationType::NormalFromGeometryLightingFromSurfaceCache;
			break;
		case 1:
			EvaluationType = ERayHitMaterialEvaluationType::NormalFromSDFLightingFromSurfaceCache;
			break;
		case 2:
			EvaluationType = ERayHitMaterialEvaluationType::NormalFromGeometryOrMaterial;
			break;
		case 3:
			EvaluationType = ERayHitMaterialEvaluationType::NormalFromMaterialLightingFromHit;
			break;
		}

		return EvaluationType;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
		OutEnvironment.SetDefine(TEXT("LUMEN_HARDWARE_RAYTRACING"), 1);
		OutEnvironment.SetDefine(TEXT("PRIMARY_VISUALIZATION_MODE"), 1);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DYNAMIC_ANY_HIT_SHADER"), 0);

		OutEnvironment.SetDefine(TEXT("VISUALIZATION_NUM_BUFFER_ELEMENTS"), GetVisualizationNumBufferElements());
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardHardwarePrimaryRayTracingRGS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "LumenCardHardwarePrimaryRayTracingRGS", SF_RayGen);


class FVisualizeHardwareTracesVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeHardwareTracesVS)
	SHADER_USE_PARAMETER_STRUCT(FVisualizeHardwareTracesVS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, HardwareDetailTracingVisualizationBuffer)
	END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LUMEN_HARDWARE_RAYTRACING"), 0);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeHardwareTracesVS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "VisualizeHardwareTracesVS", SF_Vertex);


class FVisualizeHardwareTracesPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeHardwareTracesPS)
	SHADER_USE_PARAMETER_STRUCT(FVisualizeHardwareTracesPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, HardwareDetailTracingVisualizationBuffer)
	END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LUMEN_HARDWARE_RAYTRACING"), 0);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeHardwareTracesPS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "VisualizeHardwareTracesPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FVisualizeHardwareTraces, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeHardwareTracesVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeHardwareTracesPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FVisualizeHardwareTracesVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	/** Destructor. */
	virtual ~FVisualizeHardwareTracesVertexDeclaration() {}

	virtual void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

TGlobalResource<FVisualizeHardwareTracesVertexDeclaration> GVisualizeHardwareTracesVertexDeclaration;

TRefCountPtr<FRDGPooledBuffer> GVisualizeHardwareTracesBuffer;

FRDGBufferRef  SetupVisualizationBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name)
{
	FRDGBufferRef VisualizeHardwareTraces = nullptr;
	if (GVisualizeHardwareTracesBuffer.IsValid())
	{
		VisualizeHardwareTraces = GraphBuilder.RegisterExternalBuffer(GVisualizeHardwareTracesBuffer);
	}

	typedef FLumenCardHardwareRayTracingRGS SHADER;

	if (!GVisualizeHardwareTracesBuffer || GVisualizeHardwareTracesBuffer->Desc.NumElements != SHADER::GetVisualizationNumBufferElements())
	{
		VisualizeHardwareTraces = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(
				SHADER::GetVisualizationBytesPerElement(), SHADER::GetVisualizationNumBufferElements()), Name);
	}
	return VisualizeHardwareTraces;
}

void FDeferredShadingSceneRenderer::PrepareRayTracingScreenProbeGather(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	const bool bLightingMissShader = CanUseRayTracingLightingMissShader(View.GetShaderPlatform());

	// Prepare the shader permutation FLumenCardHardwareRayTracingRGS
	for (int32 UseRadianceCache = 0; UseRadianceCache < 2; ++UseRadianceCache)
	{
		for (int32 TwoSided = 0; TwoSided < 2; ++TwoSided)
		{
			for (int32 StructuredImportanceSampling = 0; StructuredImportanceSampling < 2; ++StructuredImportanceSampling)
			{
				for (int32 RayHitMaterialEvaluationType = 0;
					RayHitMaterialEvaluationType < static_cast<int32>(ERayHitMaterialEvaluationType::MAX);++RayHitMaterialEvaluationType)
				{
					typedef FLumenCardHardwareRayTracingRGS RGShader;

					RGShader::FPermutationDomain PermutationVector;

					PermutationVector.Set<RGShader::FMissShaderLighting>(bLightingMissShader);
					PermutationVector.Set<RGShader::FStructuredImportanceSampling>(StructuredImportanceSampling == 1);
					PermutationVector.Set<RGShader::FEnableTwoSidedGeometryForShadowDim>(TwoSided == 1);
					PermutationVector.Set<RGShader::FRadianceCache>(UseRadianceCache == 1);
					PermutationVector.Set<RGShader::FMaterialEvaluationType>(
						static_cast<ERayHitMaterialEvaluationType>(RayHitMaterialEvaluationType));

					auto RayGenShader = View.ShaderMap->GetShader<RGShader>(PermutationVector);
					OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
				}
			}
		}
	}

	// Prepare the shader permutation for FLumenCardHardwarePrimaryRayTracingRGS

	for (int32 UseRadianceCache = 0; UseRadianceCache < 2; ++UseRadianceCache)
	{
		for (int32 TwoSided = 0; TwoSided < 2; ++TwoSided)
		{
			for (int32 RayHitMaterialEvaluationType = 0;
				RayHitMaterialEvaluationType < static_cast<int32>(ERayHitMaterialEvaluationType::MAX);++RayHitMaterialEvaluationType)
			{
				typedef FLumenCardHardwarePrimaryRayTracingRGS RGShader;

				RGShader::FPermutationDomain PermutationVector;

				PermutationVector.Set<RGShader::FMissShaderLighting>(bLightingMissShader);
				PermutationVector.Set<RGShader::FEnableTwoSidedGeometryForShadowDim>(TwoSided == 1);
				PermutationVector.Set<RGShader::FRadianceCache>(UseRadianceCache == 1);
				PermutationVector.Set<RGShader::FMaterialEvaluationType>(
					static_cast<ERayHitMaterialEvaluationType>(RayHitMaterialEvaluationType));

				auto RayGenShader = View.ShaderMap->GetShader<RGShader>(PermutationVector);
				OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
			}
		}
	}
}

void RenderHardwareRayTracingScreenProbeSubpass(FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	FScreenProbeParameters& ScreenProbeParameters,
	const FViewInfo& View,
	ERayTracingPrimaryRaysFlag Flags,
	ERayHitMaterialEvaluationType MaterialEvaluationType,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	const FCompactedTraceParameters& CompactedTraceParameters,
	bool bUseNormalCache,
	FRDGTextureRef SurfaceNormalCacheTexture = nullptr)
{
	const bool bVisualizeHardwareTracing= GLumenVisualizeHardwareTracing == 1;
	FRDGBufferRef VisualizeHardwareTraces = nullptr;

	typedef FLumenCardHardwareRayTracingRGS SHADER;
	SHADER::FParameters* PassParameters =
		GraphBuilder.AllocParameters<SHADER::FParameters>();
	{
		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

		FRayTracingPrimaryRaysOptions TranslucencyOptions = GetRayTracingTranslucencyOptions();
		PassParameters->ReflectedShadowsType = TranslucencyOptions.EnableShadows > -1 ? TranslucencyOptions.EnableShadows : (int32)View.FinalPostProcessSettings.RayTracingTranslucencyShadows;
		PassParameters->PrimaryRayFlags = (uint32)Flags;
		PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
		PassParameters->LightDataPacked = View.RayTracingLightData.UniformBuffer;
		PassParameters->LightDataBuffer = View.RayTracingLightData.LightBufferSRV;
		PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);
		PassParameters->GeometryNormalOnlyForCHS = GLumenScreenProbeGatherHardwareTraceCardsGeometryNormalOnly != 0;
		
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;
		PassParameters->CompactedTraceParameters = CompactedTraceParameters;

		FRGSRadianceCacheParameters RGSRadianceCacheParameters;
		SetupRGSRadianceCacheParameters(RadianceCacheParameters, RGSRadianceCacheParameters);
		PassParameters->RGSRadianceCacheParameters = CreateUniformBufferImmediate(RGSRadianceCacheParameters, UniformBuffer_SingleFrame);

		PassParameters->MaxNormalBias = GetRaytracingMaxNormalBias();

		if (bUseNormalCache)
		{
			check(SurfaceNormalCacheTexture);
			PassParameters->GeometryNormalCache = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SurfaceNormalCacheTexture));
		}

		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);

		PassParameters->IndirectTracingParameters = IndirectTracingParameters;
		PassParameters->MeshSDFGridParameters = MeshSDFGridParameters;

		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->SceneTextures = SceneTextures;

		//This parameter should be set always as dynamic branch is chosen to reduce permutation counts. 
		// The dynamic branch is coherent. Only set up the buffer when trace visualization is enabled.
		PassParameters->HardwareTracesVisualizationParams = SHADER::GetHardwareTracesVisualizationParams();
		{	
			VisualizeHardwareTraces = SetupVisualizationBuffer(GraphBuilder, TEXT("VisualizeHardwareTracesBuffer")); 
			PassParameters->RWHardwareDetailTracingVisualizationBuffer = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(VisualizeHardwareTraces, PF_A32B32G32R32F));
		}
	}

	SHADER::FPermutationDomain PermutationVector;

	const bool bMissShaderLighting = CanUseRayTracingLightingMissShader(View.GetShaderPlatform());
	PermutationVector.Set<SHADER::FEnableTwoSidedGeometryForShadowDim>(EnableRayTracingShadowTwoSidedGeometry());
	PermutationVector.Set< SHADER::FMissShaderLighting>(bMissShaderLighting);
	PermutationVector.Set< SHADER::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling());
	PermutationVector.Set< SHADER::FRadianceCache>(LumenScreenProbeGather::UseRadianceCache(View));
	PermutationVector.Set< SHADER::FMaterialEvaluationType>(MaterialEvaluationType);

	PermutationVector = SHADER::RemapPermutation(PermutationVector);

	auto RayGenShader = View.ShaderMap->GetShader<SHADER>(PermutationVector);

	ClearUnusedGraphResources(RayGenShader, PassParameters);

	//@TODO Add indirect dispatch mode when DXR 1.1 is integrated
	auto RayTracingResolution = ScreenProbeParameters.ScreenProbeAtlasViewSize * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution;
	bool bReferenceDirectMaterialTracing = MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromMaterialLightingFromHit;
	
	GraphBuilder.AddPass(
		bReferenceDirectMaterialTracing ? 
			RDG_EVENT_NAME("HardwareRayTracing %ux%u (direct material evaluation)", RayTracingResolution.X, RayTracingResolution.Y) :
			RDG_EVENT_NAME("HardwareRayTracing %ux%u", RayTracingResolution.X, RayTracingResolution.Y),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, RayGenShader, RayTracingResolution, MaterialEvaluationType](FRHICommandList& RHICmdList)
		{
			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			FRayTracingShaderBindingsWriter GlobalResources;

			SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);
			const uint32 NumTracingThreads = RayTracingResolution.X * RayTracingResolution.Y;

			if (SHADER::UseFullMaterialPayloadSetup(MaterialEvaluationType))
			{
				FRayTracingPipelineState* Pipeline = View.RayTracingMaterialPipeline;

				RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, NumTracingThreads, 1);
			}
			else
			{
				FRayTracingPipelineStateInitializer Initializer;

				Initializer.MaxPayloadSizeInBytes = 24; // sizeof FDefaultPayload declared in RayTracingCommon.ush

				FRHIRayTracingShader* RayGenShaderTable[] = { RayGenShader.GetRayTracingShader() };
				Initializer.SetRayGenShaderTable(RayGenShaderTable);

				FRHIRayTracingShader* HitGroupTable[] = { View.ShaderMap->GetShader<FDefaultMainCHSOpaqueAHS>().GetRayTracingShader() };
				Initializer.SetHitGroupTable(HitGroupTable);

				Initializer.bAllowHitGroupIndexing = false; // Use the same hit shader for all geometry in the scene by disabling SBT indexing.

				FRayTracingPipelineState* Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);
				RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, NumTracingThreads, 1);
			}
		});

	if (bVisualizeHardwareTracing)
	{
		ConvertToExternalBuffer(GraphBuilder, VisualizeHardwareTraces, GVisualizeHardwareTracesBuffer);
	}
}

#endif

void RenderHardwareRayTracingScreenProbe(FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	FScreenProbeParameters& ScreenProbeParameters,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	const FCompactedTraceParameters& CompactedTraceParameters)
#if RHI_RAYTRACING
{
	ERayTracingPrimaryRaysFlag Flags = ERayTracingPrimaryRaysFlag::AllowSkipSkySample | ERayTracingPrimaryRaysFlag::ConsiderSurfaceScatter;
	ERayHitMaterialEvaluationType MaterialEvaluationType = FLumenCardHardwareRayTracingRGS::GetRayHitMaterialEvaluationType();

	if (MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromMaterialLightingFromHit ||
		MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromSDFLightingFromSurfaceCache ||
		MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromGeometryLightingFromSurfaceCache)
	{
		RenderHardwareRayTracingScreenProbeSubpass(GraphBuilder, Scene, SceneTextures, ScreenProbeParameters, View,
			Flags,
			MaterialEvaluationType,
			TracingInputs,
			MeshSDFGridParameters,
			IndirectTracingParameters,
			RadianceCacheParameters,
			CompactedTraceParameters,
			false);
	}
	else
	{
		// Hardware ray tracing performance reference pass.
		ERayHitMaterialEvaluationType EvaluationTypePasses[] = { ERayHitMaterialEvaluationType::NormalFromGeometryOrMaterial,
												ERayHitMaterialEvaluationType::LightingFromSurfaceCache };
		FIntPoint AllRayStorageExtent = ScreenProbeParameters.ScreenProbeAtlasViewSize *
										ScreenProbeParameters.ScreenProbeTracingOctahedronResolution;

		FRDGTextureRef FaceNormalCacheTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				AllRayStorageExtent,
				PF_FloatRGBA,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("FaceNormalCacheTexture"));

		for (int i = 0; i < 2; ++i)
		{
			RenderHardwareRayTracingScreenProbeSubpass(GraphBuilder, Scene, SceneTextures, ScreenProbeParameters, View,
				Flags,
				EvaluationTypePasses[i],
				TracingInputs,
				MeshSDFGridParameters,
				IndirectTracingParameters,
				RadianceCacheParameters,
				CompactedTraceParameters,
				true,
				FaceNormalCacheTexture);
		}

	}
}
#else // !RHI_RAYTRACING
{
	unimplemented();
}
#endif

#if RHI_RAYTRACING
void VisualizeLumenHardwareRayTracingPrimaryRaySubpass(FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	FRDGTextureRef SceneColor,
	const FViewInfo& View,
	ERayTracingPrimaryRaysFlag Flags,
	ERayHitMaterialEvaluationType MaterialEvaluationType,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	bool bUseNormalCache,
	FRDGTextureRef SurfaceNormalCacheTexture = nullptr)
{
	const bool bVisualizeHardwareTracing = GLumenVisualizeHardwareTracing == 1;
	FRDGBufferRef VisualizeHardwareTraces = nullptr;

	typedef FLumenCardHardwarePrimaryRayTracingRGS SHADER;
	SHADER::FParameters* PassParameters =
		GraphBuilder.AllocParameters<SHADER::FParameters>();
	{
		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

		FRayTracingPrimaryRaysOptions TranslucencyOptions = GetRayTracingTranslucencyOptions();
		PassParameters->ReflectedShadowsType = TranslucencyOptions.EnableShadows > -1 ? TranslucencyOptions.EnableShadows : (int32)View.FinalPostProcessSettings.RayTracingTranslucencyShadows;
		PassParameters->PrimaryRayFlags = (uint32)Flags;
		PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
		PassParameters->LightDataPacked = View.RayTracingLightData.UniformBuffer;
		PassParameters->LightDataBuffer = View.RayTracingLightData.LightBufferSRV;
		PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);
		PassParameters->GeometryNormalOnlyForCHS = GLumenScreenProbeGatherHardwareTraceCardsGeometryNormalOnly != 0;

		PassParameters->RadianceCacheParameters = RadianceCacheParameters;

		FRGSRadianceCacheParameters RGSRadianceCacheParameters;
		SetupRGSRadianceCacheParameters(RadianceCacheParameters, RGSRadianceCacheParameters);
		PassParameters->RGSRadianceCacheParameters = CreateUniformBufferImmediate(RGSRadianceCacheParameters, UniformBuffer_SingleFrame);

		PassParameters->MaxNormalBias = GetRaytracingMaxNormalBias();

		if (bUseNormalCache)
		{
			check(SurfaceNormalCacheTexture);
			PassParameters->GeometryNormalCache = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SurfaceNormalCacheTexture));
		}

		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);

		PassParameters->IndirectTracingParameters = IndirectTracingParameters;
		PassParameters->MeshSDFGridParameters = MeshSDFGridParameters;

		//PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder);
		
		//This parameter should be set always as dynamic branch is chosen to reduce permutation counts. 
		// The dynamic branch is coherent. Only set up the buffer when trace visualization is enabled.
		PassParameters->HardwareTracesVisualizationParams = SHADER::GetHardwareTracesVisualizationParams();
		{
			VisualizeHardwareTraces = SetupVisualizationBuffer(GraphBuilder, TEXT("VisualizeHardwareTracesBuffer"));
			PassParameters->RWHardwareDetailTracingVisualizationBuffer = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(VisualizeHardwareTraces, PF_A32B32G32R32F));
		}
		PassParameters->ViewDimensions = View.ViewRect;
		PassParameters->RWSceneColor = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SceneColor));
	}

	SHADER::FPermutationDomain PermutationVector;

	const bool bMissShaderLighting = CanUseRayTracingLightingMissShader(View.GetShaderPlatform());
	PermutationVector.Set<SHADER::FEnableTwoSidedGeometryForShadowDim>(EnableRayTracingShadowTwoSidedGeometry());
	PermutationVector.Set< SHADER::FMissShaderLighting>(bMissShaderLighting);
	PermutationVector.Set< SHADER::FRadianceCache>(LumenScreenProbeGather::UseRadianceCache(View));
	PermutationVector.Set< SHADER::FMaterialEvaluationType>(MaterialEvaluationType);

	PermutationVector = SHADER::RemapPermutation(PermutationVector);

	auto RayGenShader = View.ShaderMap->GetShader<SHADER>(PermutationVector);

	ClearUnusedGraphResources(RayGenShader, PassParameters);

	//@TODO Add indirect dispatch mode when DXR 1.1 is integrated
	//auto RayTracingResolution = FIntPoint(View.ViewRect.Width(), View.ViewRect.Height());
	FIntPoint RayTracingResolution(FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), 8) * 8);
	bool bReferenceDirectMaterialTracing = MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromMaterialLightingFromHit;

	GraphBuilder.AddPass(
		bReferenceDirectMaterialTracing ? 
			RDG_EVENT_NAME("HardwareRayTracing %ux%u (direct material evaluation)", RayTracingResolution.X, RayTracingResolution.Y) :
			RDG_EVENT_NAME("HardwareRayTracing %ux%u", RayTracingResolution.X, RayTracingResolution.Y),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, &View, RayGenShader, RayTracingResolution, MaterialEvaluationType](FRHICommandList& RHICmdList)
		{
			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			FRayTracingShaderBindingsWriter GlobalResources;

			SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

			if (SHADER::UseFullMaterialPayloadSetup(MaterialEvaluationType))
			{
				FRayTracingPipelineState* Pipeline = View.RayTracingMaterialPipeline;

				RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
			}
			else
			{
				FRayTracingPipelineStateInitializer Initializer;

				Initializer.MaxPayloadSizeInBytes = 24; // sizeof FDefaultPayload declared in RayTracingCommon.ush

				FRHIRayTracingShader* RayGenShaderTable[] = { RayGenShader.GetRayTracingShader() };
				Initializer.SetRayGenShaderTable(RayGenShaderTable);

				FRHIRayTracingShader* HitGroupTable[] = { View.ShaderMap->GetShader<FDefaultMainCHSOpaqueAHS>().GetRayTracingShader() };
				Initializer.SetHitGroupTable(HitGroupTable);

				Initializer.bAllowHitGroupIndexing = false; // Use the same hit shader for all geometry in the scene by disabling SBT indexing.

				FRayTracingPipelineState* Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);
				RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
			}
		});
}

#endif

void VisualizeLumenHardwareRayTracingPrimaryRay(FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	FRDGTextureRef SceneColor)
#if RHI_RAYTRACING
{
	ERayTracingPrimaryRaysFlag Flags = ERayTracingPrimaryRaysFlag::AllowSkipSkySample | ERayTracingPrimaryRaysFlag::ConsiderSurfaceScatter;
	ERayHitMaterialEvaluationType MaterialEvaluationType = FLumenCardHardwareRayTracingRGS::GetRayHitMaterialEvaluationType();

	if (MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromMaterialLightingFromHit ||
		MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromSDFLightingFromSurfaceCache ||
		MaterialEvaluationType == ERayHitMaterialEvaluationType::NormalFromGeometryLightingFromSurfaceCache)
	{
		VisualizeLumenHardwareRayTracingPrimaryRaySubpass(GraphBuilder, Scene, SceneColor, View,
			Flags,
			MaterialEvaluationType,
			TracingInputs,
			MeshSDFGridParameters,
			IndirectTracingParameters,
			RadianceCacheParameters,
			false);
	}
	else
	{
		// Hardware ray tracing performance reference pass.
		ERayHitMaterialEvaluationType EvaluationTypePasses[] = { ERayHitMaterialEvaluationType::NormalFromGeometryOrMaterial,
												ERayHitMaterialEvaluationType::LightingFromSurfaceCache };

		FIntPoint AllRayStorageExtent(FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), 8) * 8) ;

		FRDGTextureRef FaceNormalCacheTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				AllRayStorageExtent,
				PF_FloatRGBA,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("FaceNormalCacheTexture"));

		for (int i = 0; i < 2; ++i)
		{
			VisualizeLumenHardwareRayTracingPrimaryRaySubpass(GraphBuilder, Scene, SceneColor, View,
				Flags,
				EvaluationTypePasses[i],
				TracingInputs,
				MeshSDFGridParameters,
				IndirectTracingParameters,
				RadianceCacheParameters,
				true,
				FaceNormalCacheTexture);
		}
	}
}
#else // !RHI_RAYTRACING
{
	//no need to check
}
#endif


void FDeferredShadingSceneRenderer::RenderScreenProbeGatherVisualizeHardwareTraces(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef SceneColor
)
#if RHI_RAYTRACING
{
	bool bVisualizeHardwareTraces = GLumenScreenProbeGatherHardwareTraceCards == 1 && GLumenVisualizeHardwareTracing == 1 
									&& GVisualizeHardwareTracesBuffer.IsValid();
	bool bIsLumenHardwareDetailTracingEnabled = Lumen::UseHardwareRayTracedScreenProbeGather();
	if (!bVisualizeHardwareTraces || !bIsLumenHardwareDetailTracingEnabled)
	{
		return;
	}

	FRDGBufferRef VisualizeHardwareTracesBuffer = GraphBuilder.RegisterExternalBuffer(GVisualizeHardwareTracesBuffer);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();

	FVisualizeHardwareTraces* PassParameters = GraphBuilder.AllocParameters<FVisualizeHardwareTraces>();
	{
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ), ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop);
		PassParameters->VS.View = View.ViewUniformBuffer;

		auto HardwareTracesBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(VisualizeHardwareTracesBuffer, PF_A32B32G32R32F));
		PassParameters->VS.HardwareDetailTracingVisualizationBuffer = HardwareTracesBufferSRV;
		PassParameters->PS.HardwareDetailTracingVisualizationBuffer = HardwareTracesBufferSRV;
	}

	auto VertexShader = View.ShaderMap->GetShader<FVisualizeHardwareTracesVS>();
	auto PixelShader = View.ShaderMap->GetShader<FVisualizeHardwareTracesPS>();

	const int32 NumPrimitives = FLumenCardHardwareRayTracingRGS::GetNumOfPrimitives();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("VisualizeHardwareTraces"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, VertexShader, PixelShader, &View, NumPrimitives](FRHICommandListImmediate& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNear>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB>::GetRHI();

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GVisualizeHardwareTracesVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

			RHICmdList.SetStreamSource(0, nullptr, 0);

			RHICmdList.DrawPrimitive(0, NumPrimitives * 2, 1); // Use two triangles to draw one quad.
		});
}
#else // !RHI_RAYTRACING
{
	//no need to check
}
#endif
