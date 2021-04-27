// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsVisibility.h"
#include "HairStrandsCluster.h"
#include "HairStrandsUtils.h"
#include "HairStrandsInterface.h"
#include "HairStrandsTile.h"

#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"
#include "MeshPassProcessor.inl"
#include "ScenePrivate.h"

DECLARE_GPU_STAT(HairStrandsVisibility);

/////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairStrandsViewTransmittancePassEnable = 1;
static FAutoConsoleVariableRef CVarHairStrandsCoveragePassEnable(TEXT("r.HairStrands.ViewTransmittancePass"), GHairStrandsViewTransmittancePassEnable, TEXT("Enable accurate transmittance pass for better rendering of small scale hair strand."));

static int32 GHairStrandsMaterialCompactionEnable = 0;
static FAutoConsoleVariableRef CVarHairStrandsMaterialCompactionEnable(TEXT("r.HairStrands.MaterialCompaction"), GHairStrandsMaterialCompactionEnable, TEXT("Enable extra compaction based on material properties in order to reduce sample per pixel and improve performance."));

static float GHairStrandsMaterialCompactionDepthThreshold = 1.f;
static float GHairStrandsMaterialCompactionTangentThreshold = 10.f;
static FAutoConsoleVariableRef CVarHairStrandsMaterialCompactionDepthThreshold(TEXT("r.HairStrands.MaterialCompaction.DepthThreshold"), GHairStrandsMaterialCompactionDepthThreshold, TEXT("Compaction threshold for depth value for material compaction (in centimeters). Default 1 cm."));
static FAutoConsoleVariableRef CVarHairStrandsMaterialCompactionTangentThreshold(TEXT("r.HairStrands.MaterialCompaction.TangentThreshold"), GHairStrandsMaterialCompactionTangentThreshold, TEXT("Compaciton threshold for tangent value for material compaction (in degrees). Default 10 deg."));

static int32 GHairVisibilityMSAA_MaxSamplePerPixel = 8;
static float GHairVisibilityMSAA_MeanSamplePerPixel = 0.75f;
static FAutoConsoleVariableRef CVarHairVisibilityMSAA_MaxSamplePerPixel(TEXT("r.HairStrands.Visibility.MSAA.SamplePerPixel"), GHairVisibilityMSAA_MaxSamplePerPixel, TEXT("Hair strands visibility sample count (2, 4, or 8)"), ECVF_Scalability | ECVF_RenderThreadSafe);
static FAutoConsoleVariableRef CVarHairVisibilityMSAA_MeanSamplePerPixel(TEXT("r.HairStrands.Visibility.MSAA.MeanSamplePerPixel"), GHairVisibilityMSAA_MeanSamplePerPixel, TEXT("Scale the numer of sampler per pixel for limiting memory allocation (0..1, default 0.5f)"));

static int32 GHairClearVisibilityBuffer = 0;
static FAutoConsoleVariableRef CVarHairClearVisibilityBuffer(TEXT("r.HairStrands.Visibility.Clear"), GHairClearVisibilityBuffer, TEXT("Clear hair strands visibility buffer"));

static TAutoConsoleVariable<int32> CVarHairVelocityMagnitudeScale(
	TEXT("r.HairStrands.VelocityMagnitudeScale"),
	100,  // Tuned by eye, based on heavy motion (strong head shack)
	TEXT("Velocity magnitude (in pixel) at which a hair will reach its pic velocity-rasterization-scale under motion to reduce aliasing. Default is 100."));

static int32 GHairVelocityType = 1; // default is 
static FAutoConsoleVariableRef CVarHairVelocityType(TEXT("r.HairStrands.VelocityType"), GHairVelocityType, TEXT("Type of velocity filtering (0:avg, 1:closest, 2:max). Default is 1."));

static int32 GHairVisibilityPPLL = 0;
static int32 GHairVisibilityPPLL_MaxSamplePerPixel = 16;
static float GHairVisibilityPPLL_MeanSamplePerPixel = 1;
static FAutoConsoleVariableRef CVarGHairVisibilityPPLL(TEXT("r.HairStrands.Visibility.PPLL"), GHairVisibilityPPLL, TEXT("Hair Visibility uses per pixel linked list"), ECVF_Scalability | ECVF_RenderThreadSafe);
static FAutoConsoleVariableRef CVarGHairVisibilityPPLL_MeanNodeCountPerPixel(TEXT("r.HairStrands.Visibility.PPLL.SamplePerPixel"), GHairVisibilityPPLL_MaxSamplePerPixel, TEXT("The maximum number of node allowed to be independently shaded and composited per pixel. Total amount of node will be width*height*VisibilityPPLLMaxRenderNodePerPixel. The last node is used to aggregate all furthest strands to shade into a single one."));
static FAutoConsoleVariableRef CVarGHairVisibilityPPLL_MeanSamplePerPixel(TEXT("r.HairStrands.Visibility.PPLL.MeanSamplePerPixel"), GHairVisibilityPPLL_MeanSamplePerPixel, TEXT("Scale the maximum number of node allowed for all linked list element (0..1, default 1). It will be width*height*SamplerPerPixel*Scale."));

static float GHairStrandsViewHairCountDepthDistanceThreshold = 30.f;
static FAutoConsoleVariableRef CVarHairStrandsViewHairCountDepthDistanceThreshold(TEXT("r.HairStrands.Visibility.HairCount.DistanceThreshold"), GHairStrandsViewHairCountDepthDistanceThreshold, TEXT("Distance threshold defining if opaque depth get injected into the 'view-hair-count' buffer."));

static int32 GHairVisibilityComputeRaster = 0;
static int32 GHairVisibilityComputeRaster_MaxSamplePerPixel = 1;
static float GHairVisibilityComputeRaster_MeanSamplePerPixel = 1;
static int32 GHairVisibilityComputeRaster_MaxPixelCount = 64;
static int32 GHairVisibilityComputeRaster_Stochastic = 0;
static FAutoConsoleVariableRef CVarHairStrandsVisibilityComputeRaster(TEXT("r.HairStrands.Visibility.ComputeRaster"), GHairVisibilityComputeRaster, TEXT("Hair Visiblity uses raster compute."), ECVF_Scalability | ECVF_RenderThreadSafe);
static FAutoConsoleVariableRef CVarHairStrandsVisibilityComputeRaster_MaxSamplePerPixel(TEXT("r.HairStrands.Visibility.ComputeRaster.SamplePerPixel"), GHairVisibilityComputeRaster_MaxSamplePerPixel, TEXT("Define the number of sampler per pixel using raster compute."));
static FAutoConsoleVariableRef CVarHairStrandsVisibilityComputeRaster_MaxPixelCount(TEXT("r.HairStrands.Visibility.ComputeRaster.MaxPixelCount"), GHairVisibilityComputeRaster_MaxPixelCount, TEXT("Define the maximal length rasterize in compute."));
static FAutoConsoleVariableRef CVarHairVisibilityComputeRaster_Stochastic(TEXT("r.HairStrands.Visibility.ComputeRaster.Stochastic"), GHairVisibilityComputeRaster_Stochastic, TEXT("Enable stochastic compute rasterization (faster, but more prone to aliasting). Experimental."));

static float GHairStrandsFullCoverageThreshold = 0.98f;
static FAutoConsoleVariableRef CVarHairStrandsFullCoverageThreshold(TEXT("r.HairStrands.Visibility.FullCoverageThreshold"), GHairStrandsFullCoverageThreshold, TEXT("Define the coverage threshold at which a pixel is considered fully covered."));

static float GHairStrandsWriteVelocityCoverageThreshold = 0.f;
static FAutoConsoleVariableRef CVarHairStrandsWriteVelocityCoverageThreshold(TEXT("r.HairStrands.Visibility.WriteVelocityCoverageThreshold"), GHairStrandsWriteVelocityCoverageThreshold, TEXT("Define the coverage threshold at which a pixel write its hair velocity (default: 0, i.e., write for all pixel)"));

static int32 GHairStrandsSortHairSampleByDepth = 0;
static FAutoConsoleVariableRef CVarHairStrandsSortHairSampleByDepth(TEXT("r.HairStrands.Visibility.SortByDepth"), GHairStrandsSortHairSampleByDepth, TEXT("Sort hair fragment by depth and update their coverage based on ordered transmittance."));

static int32 GHairStrandsHairCountToTransmittance = 0;
static FAutoConsoleVariableRef CVarHairStrandsHairCountToTransmittance(TEXT("r.HairStrands.Visibility.UseCoverageMappping"), GHairStrandsHairCountToTransmittance, TEXT("Use hair count to coverage transfer function."));

static int32 GHairStrandsVisibility_UseFastPath = 0;
static FAutoConsoleVariableRef CVarHairStrandsVisibility_UseFastPath(TEXT("r.HairStrands.Visibility.UseFastPath"), GHairStrandsVisibility_UseFastPath, TEXT("Use fast path writing hair data into Gbuffer."));

static int32 GHairStrandsVisibility_OutputEmissiveData = 0;
static FAutoConsoleVariableRef CVarHairStrandsVisibility_OutputEmissiveData(TEXT("r.HairStrands.Visibility.Emissive"), GHairStrandsVisibility_OutputEmissiveData, TEXT("Enable emissive data during the material pass."));

static int32 GHairStrandsDebugPPLL = 0;
static FAutoConsoleVariableRef CVarHairStrandsDebugPPLL(TEXT("r.HairStrands.Visibility.PPLL.Debug"), GHairStrandsDebugPPLL, TEXT("Draw debug per pixel light list rendering."));

static int32 GHairStrandsTile = 0;
static FAutoConsoleVariableRef CVarHairStrandsTile(TEXT("r.HairStrands.Tile"), GHairStrandsTile, TEXT("Enable tile generation & usage for hair strands."));


/////////////////////////////////////////////////////////////////////////////////////////

namespace HairStrandsVisibilityInternal
{
	struct NodeData
	{
		uint32 Depth;
		uint32 PrimitiveId_MacroGroupId;
		uint32 Tangent_Coverage;
		uint32 BaseColor_Roughness;
		uint32 Specular;
	};

	// 128 bit alignment
	struct NodeVis
	{
		uint32 Depth;
		uint32 PrimitiveId_MacroGroupId;
		uint32 Coverage_MacroGroupId_Pad;
		uint32 Pad;
	};
}

enum EHairVisibilityRenderMode
{
	HairVisibilityRenderMode_Transmittance,
	HairVisibilityRenderMode_PPLL,
	HairVisibilityRenderMode_MSAA_Visibility,
	HairVisibilityRenderMode_TransmittanceAndHairCount,
	HairVisibilityRenderMode_ComputeRaster,
	HairVisibilityRenderModeCount
};

inline bool DoesSupportRasterCompute()
{
#if PLATFORM_WINDOWS
	return (IsRHIDeviceNVIDIA() || IsRHIDeviceAMD()) && GRHISupportsAtomicUInt64;
#else
	return GRHISupportsAtomicUInt64;
#endif
}

inline EHairVisibilityRenderMode GetHairVisibilityRenderMode()
{
	if (GHairVisibilityPPLL > 0)
	{
		return HairVisibilityRenderMode_PPLL;
	}
	else if (GHairVisibilityComputeRaster > 0 && DoesSupportRasterCompute())
	{
		return HairVisibilityRenderMode_ComputeRaster;
	}
	else
	{
		return HairVisibilityRenderMode_MSAA_Visibility;
	}
}

inline bool IsMsaaEnabled()
{
	const EHairVisibilityRenderMode Mode = GetHairVisibilityRenderMode();
	return Mode == HairVisibilityRenderMode_MSAA_Visibility;
}

static uint32 GetMaxSamplePerPixel()
{
	switch (GetHairVisibilityRenderMode())
	{
		case HairVisibilityRenderMode_ComputeRaster:
		{
			if (GHairVisibilityComputeRaster_MaxSamplePerPixel <= 1)
			{
				return 1;
			}
			else if (GHairVisibilityComputeRaster_MaxSamplePerPixel < 4)
			{
				return 2;
			}
			else
			{
				return 4;
			}
		}
		case HairVisibilityRenderMode_MSAA_Visibility:
		{
			if (GHairVisibilityMSAA_MaxSamplePerPixel <= 1)
			{
				return 1;
			}
			else if (GHairVisibilityMSAA_MaxSamplePerPixel == 2)
			{
				return 2;
			}
			else if (GHairVisibilityMSAA_MaxSamplePerPixel <= 4)
			{
				return 4;
			}
			else
			{
				return 8;
			}
		}
		case HairVisibilityRenderMode_PPLL:
		{
			// The following must match the FPPLL permutation of FHairVisibilityPrimitiveIdCompactionCS.
			if (GHairVisibilityPPLL_MaxSamplePerPixel == 0)
			{
				return 0;
			}
			else if (GHairVisibilityPPLL_MaxSamplePerPixel <= 8)
			{
				return 8;
			}
			else if (GHairVisibilityPPLL_MaxSamplePerPixel <= 16)
			{
				return 16;
			}
			else //if (GHairVisibilityPPLL_MaxSamplePerPixel <= 32)
			{
				return 32;
			}
			// If more is needed: please check out EncodeNodeDesc from HairStrandsVisibilityCommon.ush to verify node count representation limitations.
		}
	}
	return 1;
}

inline uint32 GetMeanSamplePerPixel()
{
	const uint32 SamplePerPixel = GetMaxSamplePerPixel();
	switch (GetHairVisibilityRenderMode())
	{
	case HairVisibilityRenderMode_ComputeRaster:
		return FMath::Max(1, FMath::FloorToInt(SamplePerPixel * FMath::Clamp(GHairVisibilityComputeRaster_MeanSamplePerPixel, 0.f, 1.f)));
	case HairVisibilityRenderMode_MSAA_Visibility:
		return FMath::Max(1, FMath::FloorToInt(SamplePerPixel * FMath::Clamp(GHairVisibilityMSAA_MeanSamplePerPixel, 0.f, 1.f)));
	case HairVisibilityRenderMode_PPLL:
		return FMath::Max(1, FMath::FloorToInt(SamplePerPixel * FMath::Clamp(GHairVisibilityPPLL_MeanSamplePerPixel, 0.f, 10.f)));
	case HairVisibilityRenderMode_Transmittance:
	case HairVisibilityRenderMode_TransmittanceAndHairCount:
		return 1;
	}
	return 1;
}

uint32 GetHairStrandsMeanSamplePerPixel()
{
	return GetMeanSamplePerPixel();
}

struct FRasterComputeOutput
{
	FIntPoint BaseResolution;
	FIntPoint SuperResolution;
	uint32 ResolutionMultiplier = 1;

	FRDGTextureRef HairCountTexture = nullptr;
	FRDGTextureRef DepthTexture = nullptr;

	FRDGTextureRef VisibilityTexture0 = nullptr;
	FRDGTextureRef VisibilityTexture1 = nullptr;
	FRDGTextureRef VisibilityTexture2 = nullptr;
	FRDGTextureRef VisibilityTexture3 = nullptr;
};

static uint32 GetTotalSampleCountForAllocation(FIntPoint Resolution)
{
	return Resolution.X * Resolution.Y * GetMeanSamplePerPixel();
}

static void SetUpViewHairRenderInfo(const FViewInfo& ViewInfo, bool bEnableMSAA, FVector4& OutHairRenderInfo, uint32& OutHairRenderInfoBits, uint32& OutHairComponents)
{
	FVector2D PixelVelocity(1.f / (ViewInfo.ViewRect.Width() * 2), 1.f / (ViewInfo.ViewRect.Height() * 2));
	const float VelocityMagnitudeScale = FMath::Clamp(CVarHairVelocityMagnitudeScale.GetValueOnAnyThread(), 0, 512) * FMath::Min(PixelVelocity.X, PixelVelocity.Y);

	// In the case we render coverage, we need to override some view uniform shader parameters to account for the change in MSAA sample count.
	const uint32 HairVisibilitySampleCount = bEnableMSAA ? GetMaxSamplePerPixel() : 1;	// The coverage pass does not use MSAA
	const float RasterizationScaleOverride = 0.0f;	// no override
	FMinHairRadiusAtDepth1 MinHairRadius = ComputeMinStrandRadiusAtDepth1(
		FIntPoint(ViewInfo.UnconstrainedViewRect.Width(), ViewInfo.UnconstrainedViewRect.Height()), ViewInfo.FOV, HairVisibilitySampleCount, RasterizationScaleOverride);

	OutHairRenderInfo = PackHairRenderInfo(MinHairRadius.Primary, MinHairRadius.Stable, MinHairRadius.Velocity, VelocityMagnitudeScale);
	OutHairRenderInfoBits = PackHairRenderInfoBits(!ViewInfo.IsPerspectiveProjection(), false);
	OutHairComponents = ToBitfield(GetHairComponents());
}

void SetUpViewHairRenderInfo(const FViewInfo& ViewInfo, FVector4& OutHairRenderInfo, uint32& OutHairRenderInfoBits, uint32& OutHairComponents)
{
	SetUpViewHairRenderInfo(ViewInfo, IsMsaaEnabled(), OutHairRenderInfo, OutHairRenderInfoBits, OutHairComponents);
}

static bool IsCompatibleWithHairVisibility(const FMeshMaterialShaderPermutationParameters& Parameters)
{
	return IsCompatibleWithHairStrands(Parameters.Platform, Parameters.MaterialParameters);
}

bool IsHairStrandsComplexLightingEnabled()
{
	return GHairStrandsVisibility_UseFastPath == 0 || GetMeanSamplePerPixel() > 1 || GetHairVisibilityRenderMode() == HairVisibilityRenderMode_PPLL;
}

float GetHairWriteVelocityCoverageThreshold()
{
	return FMath::Clamp(GHairStrandsWriteVelocityCoverageThreshold, 0.f, 1.f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairLightSampleClearVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairLightSampleClearVS);
	SHADER_USE_PARAMETER_STRUCT(FHairLightSampleClearVS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, MaxViewportResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairNodeCountTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_VERTEX"), 1);
	}
};

class FHairLightSampleClearPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairLightSampleClearPS);
	SHADER_USE_PARAMETER_STRUCT(FHairLightSampleClearPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, MaxViewportResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairNodeCountTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLEAR"), 1);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_FloatRGBA);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairLightSampleClearVS, "/Engine/Private/HairStrands/HairStrandsLightSample.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairLightSampleClearPS, "/Engine/Private/HairStrands/HairStrandsLightSample.usf", "ClearPS", SF_Pixel);

static FRDGTextureRef AddClearLightSamplePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const uint32 MaxNodeCount,
	const FRDGTextureRef NodeCounter)
{	
	const uint32 SampleTextureResolution = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(MaxNodeCount)));
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(SampleTextureResolution, SampleTextureResolution), PF_FloatRGBA, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable);
	FRDGTextureRef Output = GraphBuilder.CreateTexture(Desc, TEXT("Hair.LightSample"));

	FHairLightSampleClearPS::FParameters* ParametersPS = GraphBuilder.AllocParameters<FHairLightSampleClearPS::FParameters>();
	ParametersPS->MaxViewportResolution = Desc.Extent;
	ParametersPS->HairNodeCountTexture = NodeCounter;
	
	const FIntPoint ViewportResolution = Desc.Extent;
	TShaderMapRef<FHairLightSampleClearVS> VertexShader(View->ShaderMap);
	TShaderMapRef<FHairLightSampleClearPS> PixelShader(View->ShaderMap);

	ParametersPS->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ENoAction);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairLightSampleClearPS"),
		ParametersPS,
		ERDGPassFlags::Raster,
		[ParametersPS, VertexShader, PixelShader, ViewportResolution](FRHICommandList& RHICmdList)
	{
		FHairLightSampleClearVS::FParameters ParametersVS;
		ParametersVS.MaxViewportResolution = ParametersPS->MaxViewportResolution;
		ParametersVS.HairNodeCountTexture = ParametersPS->HairNodeCountTexture;

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);

		RHICmdList.SetViewport(0, 0, 0.0f, ViewportResolution.X, ViewportResolution.Y, 1.0f);
		RHICmdList.SetStreamSource(0, nullptr, 0);
		RHICmdList.DrawPrimitive(0, 1, 1);
	});

	return Output;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Material pass which write value directly into the GBuffer. This is a fast (low-quality) 
// pass, which dither raster result into GBuffer, and works only with 1spp
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMaterialGBufferPassUniformParameters, )
	SHADER_PARAMETER(FIntPoint, MaxResolution)
	SHADER_PARAMETER(uint32, InputType)
	SHADER_PARAMETER(float, CoverageThreshold)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InTransmittanceTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>,  InRasterOutputVisibilityTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>,  InMSAAIDTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InMSAADepthTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FMaterialGBufferPassUniformParameters, "MaterialGBufferPassParameters", SceneTextures);

class FHairMaterialGBufferVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairMaterialGBufferVS, MeshMaterial);

protected:
	FHairMaterialGBufferVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
	}

	FHairMaterialGBufferVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsCompatibleWithHairVisibility(Parameters) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FHairMaterialGBufferVS, TEXT("/Engine/Private/HairStrands/HairStrandsMaterialGBufferVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////

class FHairMaterialGBufferShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FHairMaterialGBufferShaderElementData(int32 MacroGroupId, int32 MaterialId, int32 PrimitiveId, uint32 LightChannelMask) : MaterialPass_MacroGroupId(MacroGroupId), MaterialPass_MaterialId(MaterialId), MaterialPass_PrimitiveId(PrimitiveId), MaterialPass_LightChannelMask(LightChannelMask) { }
	uint32 MaterialPass_MacroGroupId;
	uint32 MaterialPass_MaterialId;
	uint32 MaterialPass_PrimitiveId;
	uint32 MaterialPass_LightChannelMask;
};

class FHairMaterialGBufferPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairMaterialGBufferPS, MeshMaterial);

public:
	FHairMaterialGBufferPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		MaterialPass_MacroGroupId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_MacroGroupId"));
		MaterialPass_MaterialId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_MaterialId"));
		MaterialPass_PrimitiveId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_PrimitiveId"));
		MaterialPass_LightChannelMask.Bind(Initializer.ParameterMap, TEXT("MaterialPass_LightChannelMask"));
	}

	FHairMaterialGBufferPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsCompatibleWithHairStrands(Parameters.Platform, Parameters.MaterialParameters) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		//OutEnvironment.SetDefine(TEXT("HAIR_MATERIAL_DEBUG_OUTPUT"), bPlatformRequireRenderTarget ? 1 : 0);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FHairMaterialGBufferShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
		ShaderBindings.Add(MaterialPass_MacroGroupId, ShaderElementData.MaterialPass_MacroGroupId);
		ShaderBindings.Add(MaterialPass_MaterialId, ShaderElementData.MaterialPass_MaterialId);
		ShaderBindings.Add(MaterialPass_PrimitiveId, ShaderElementData.MaterialPass_PrimitiveId);
		ShaderBindings.Add(MaterialPass_LightChannelMask, ShaderElementData.MaterialPass_LightChannelMask);
	}

private:
	LAYOUT_FIELD(FShaderParameter, MaterialPass_MacroGroupId);
	LAYOUT_FIELD(FShaderParameter, MaterialPass_MaterialId);
	LAYOUT_FIELD(FShaderParameter, MaterialPass_PrimitiveId);
	LAYOUT_FIELD(FShaderParameter, MaterialPass_LightChannelMask);
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FHairMaterialGBufferPS, TEXT("/Engine/Private/HairStrands/HairStrandsMaterialGBufferPS.usf"), TEXT("Main"), SF_Pixel);
/////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FVisibilityMaterialGBufferPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMaterialGBufferPassUniformParameters, UniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

/////////////////////////////////////////////////////////////////////////////////////////

class FHairMaterialGBufferProcessor : public FMeshPassProcessor
{
public:
	FHairMaterialGBufferProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FDynamicPassMeshDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;
	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, int32 MacroGroupId, int32 HairMaterialId);

private:
	bool TryAddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		int32 MacroGroupId,
		int32 HairMaterialId,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material);

	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		const int32 MacroGroupId,
		const int32 HairMaterialId,
		const int32 HairPrimitiveId,
		const uint32 HairPrimitiveLightChannelMask);

	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FHairMaterialGBufferProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, 0, 0);
}

void FHairMaterialGBufferProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, int32 MacroGroupId, int32 HairMaterialId)
{
	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	while (MaterialRenderProxy)
	{
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
		if (Material)
		{
			if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MacroGroupId, HairMaterialId, *MaterialRenderProxy, *Material))
			{
				break;
			}
		}

		MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
	}
}

bool FHairMaterialGBufferProcessor::TryAddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	int32 MacroGroupId,
	int32 HairMaterialId,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName();
	const bool bShouldRender = (!PrimitiveSceneProxy && MeshBatch.Elements.Num() > 0) || (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass());

	if (bIsCompatible
		&& bIsHairStrandsFactory
		&& bShouldRender
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		// For the mesh patch to be rendered a single triangle triangle to spawn the necessary amount of thread
		FMeshBatch MeshBatchCopy = MeshBatch;
		for (uint32 ElementIt = 0, ElementCount = uint32(MeshBatch.Elements.Num()); ElementIt < ElementCount; ++ElementIt)
		{
			MeshBatchCopy.Elements[ElementIt].FirstIndex = 0;
			MeshBatchCopy.Elements[ElementIt].NumPrimitives = 1;
			MeshBatchCopy.Elements[ElementIt].NumInstances = 1;
			MeshBatchCopy.Elements[ElementIt].IndirectArgsBuffer = nullptr;
			MeshBatchCopy.Elements[ElementIt].IndirectArgsOffset = 0;
		}

		int32 PrimitiveId = 0;
		int32 ScenePrimitiveId = 0;
		FPrimitiveSceneInfo* SceneInfo = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetPrimitiveSceneInfo() : nullptr;
		GetDrawCommandPrimitiveId(SceneInfo, MeshBatch.Elements[0], PrimitiveId, ScenePrimitiveId);
		uint32 LightChannelMask = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetLightingChannelMask() : 0;

		return Process(MeshBatchCopy, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MacroGroupId, HairMaterialId, PrimitiveId, LightChannelMask);
	}

	return true;
}

bool FHairMaterialGBufferProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	const int32 MacroGroupId,
	const int32 HairMaterialId,
	const int32 HairPrimitiveId,
	const uint32 HairPrimitiveLightChannelMask)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FHairMaterialGBufferVS,
		FHairMaterialGBufferPS> PassShaders;
	{
		FMaterialShaderTypes ShaderTypes;
		ShaderTypes.AddShaderType<FHairMaterialGBufferVS>();
		ShaderTypes.AddShaderType<FHairMaterialGBufferPS>();

		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

		FMaterialShaders Shaders;
		if (!MaterialResource.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
		{
			return false;
		}

		Shaders.TryGetVertexShader(PassShaders.VertexShader);
		Shaders.TryGetPixelShader(PassShaders.PixelShader);
	}

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	FHairMaterialGBufferShaderElementData ShaderElementData(MacroGroupId, HairMaterialId, HairPrimitiveId, HairPrimitiveLightChannelMask);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		PassShaders,
		ERasterizerFillMode::FM_Solid,
		ERasterizerCullMode::CM_CCW,
		FMeshDrawCommandSortKey::Default,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

FHairMaterialGBufferProcessor::FHairMaterialGBufferProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,
	FDynamicPassMeshDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void AddHairMaterialGBufferPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	FInstanceCullingManager& InstanceCullingManager,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,

	FRDGTextureRef InTransmittanceTexture,
	FRDGTextureRef InRasterOutputVisibilityTexture,
	FRDGTextureRef InMSAAIDTexture,
	FRDGTextureRef InMSAADepthTexture,

	FRDGTextureRef OutBufferATexture,
	FRDGTextureRef OutBufferBTexture,
	FRDGTextureRef OutBufferCTexture,
	FRDGTextureRef OutBufferDTexture,
	FRDGTextureRef OutBufferETexture,

	FRDGTextureRef OutColorTexture,
	FRDGTextureRef OutDepthTexture,
	FRDGTextureRef OutVelocityTexture)
{
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	// Add resources reference to the pass parameters, in order to get the resource lifetime extended to this pass
	FVisibilityMaterialGBufferPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityMaterialGBufferPassParameters>();

	const FIntPoint Resolution = OutBufferATexture->Desc.Extent;

	{
		FMaterialGBufferPassUniformParameters* UniformParameters = GraphBuilder.AllocParameters<FMaterialGBufferPassUniformParameters>();
		UniformParameters->InputType = InRasterOutputVisibilityTexture != nullptr ? 1 : 0;
		UniformParameters->MaxResolution = Resolution;
		UniformParameters->CoverageThreshold = FMath::Clamp(GHairStrandsFullCoverageThreshold, 0.1f, 1.f);
		UniformParameters->InTransmittanceTexture = InTransmittanceTexture;
		UniformParameters->InMSAAIDTexture = SystemTextures.Black;
		UniformParameters->InMSAADepthTexture = SystemTextures.Black;
		UniformParameters->InRasterOutputVisibilityTexture = SystemTextures.Black;
		if (UniformParameters->InputType == 0)
		{
			check(InMSAAIDTexture);
			check(InMSAADepthTexture);
			UniformParameters->InMSAAIDTexture = InMSAAIDTexture;
			UniformParameters->InMSAADepthTexture = InMSAADepthTexture;
		}
		else if (UniformParameters->InputType == 1)
		{
			check(InRasterOutputVisibilityTexture);
			UniformParameters->InRasterOutputVisibilityTexture = InRasterOutputVisibilityTexture;
		}

		PassParameters->UniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);
	}

	{
		const bool bEnableMSAA = false;
		SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfoBits, ViewInfo->CachedViewUniformShaderParameters->HairComponents);
		PassParameters->View = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
	}

	// If there is velocity texture, we recreate a dummy one
	bool bIsVelocityDummy = false;
	if (OutVelocityTexture == nullptr)
	{
		FRDGTextureDesc VelocityDesc = FVelocityRendering::GetRenderTargetDesc(ViewInfo->GetShaderPlatform(), OutDepthTexture->Desc.Extent);
		OutVelocityTexture = GraphBuilder.CreateTexture(VelocityDesc, TEXT("Hair.DummyVelocity"));
		bIsVelocityDummy = true;
	}
	bool bIsBufferEDummy = false;
	if (OutBufferETexture == nullptr)
	{
		FRDGTextureDesc Desc = FVelocityRendering::GetRenderTargetDesc(ViewInfo->GetShaderPlatform(), OutBufferDTexture->Desc.Extent);
		OutBufferETexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.DummyGBufferE"));
		bIsBufferEDummy = true;
	}
	PassParameters->RenderTargets[0] = FRenderTargetBinding(OutBufferATexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(OutBufferBTexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[2] = FRenderTargetBinding(OutBufferCTexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[3] = FRenderTargetBinding(OutBufferDTexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[4] = FRenderTargetBinding(OutBufferETexture, bIsBufferEDummy ? ERenderTargetLoadAction::ENoAction : ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[5] = FRenderTargetBinding(OutVelocityTexture, bIsVelocityDummy ? ERenderTargetLoadAction::ENoAction : ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[6] = FRenderTargetBinding(OutColorTexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthWrite_StencilNop);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsMaterialGBufferPass"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Scene = Scene, ViewInfo, &MacroGroupDatas, Resolution](FRHICommandListImmediate& RHICmdList)
		{
			FMeshPassProcessorRenderState DrawRenderState;

			{
				RHICmdList.SetViewport(0, 0, 0.0f, Resolution.X, Resolution.Y, 1.0f);
				DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState <true, CF_Always> ::GetRHI());

				FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage;
				FMeshCommandOneFrameArray VisibleMeshDrawCommands;
				FGraphicsMinimalPipelineStateSet PipelineStateSet;
				// @todo loadtime arnes: do we need to pass this along to somewhere?
				bool NeedsShaderInitialization;
				FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, PipelineStateSet, NeedsShaderInitialization);
				FHairMaterialGBufferProcessor MeshProcessor(Scene, ViewInfo, DrawRenderState, &ShadowContext);

				for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas)
				{
					for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroupData.PrimitivesInfos)
					{
						const FMeshBatch& MeshBatch = *PrimitiveInfo.Mesh;
						const uint64 BatchElementMask = ~0ull;
						MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.PrimitiveSceneProxy, -1, MacroGroupData.MacroGroupId, PrimitiveInfo.MaterialId);
					}
				}

				if (VisibleMeshDrawCommands.Num() > 0)
				{
					FRHIBuffer* PrimitiveIdVertexBuffer = nullptr;
					SortAndMergeDynamicPassMeshDrawCommands(ViewInfo->GetFeatureLevel(), VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1, ViewInfo->DynamicPrimitiveCollector.GetPrimitiveIdRange());
					SubmitMeshDrawCommands(VisibleMeshDrawCommands, PipelineStateSet, PrimitiveIdVertexBuffer, 0, false, 1, RHICmdList);
				}
			}
		});
}

/////////////////////////////////////////////////////////////////////////////////////////

class FHairMaterialVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairMaterialVS, MeshMaterial);

protected:
	FHairMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
	}

	FHairMaterialVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsCompatibleWithHairVisibility(Parameters) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FHairMaterialVS, TEXT("/Engine/Private/HairStrands/HairStrandsMaterialVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////


static bool IsHairStrandsEmissiveEnable()
{
	return GHairStrandsVisibility_OutputEmissiveData > 0;
}

class FHairMaterialShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FHairMaterialShaderElementData(int32 MacroGroupId, int32 MaterialId, int32 PrimitiveId, uint32 LightChannelMask) : MaterialPass_MacroGroupId(MacroGroupId), MaterialPass_MaterialId(MaterialId), MaterialPass_PrimitiveId(PrimitiveId), MaterialPass_LightChannelMask(LightChannelMask){ }
	uint32 MaterialPass_MacroGroupId;
	uint32 MaterialPass_MaterialId;
	uint32 MaterialPass_PrimitiveId;
	uint32 MaterialPass_LightChannelMask;
};

#define HAIR_MATERIAL_DEBUG_OUTPUT 0
static bool IsPlatformRequiringRenderTargetForMaterialPass(EShaderPlatform Platform)
{
	return HAIR_MATERIAL_DEBUG_OUTPUT || Platform == SP_VULKAN_SM5 || FDataDrivenShaderPlatformInfo::GetRequiresRenderTargetDuringRaster(Platform); //#hair_todo: change to a proper RHI(Platform) function
}

class FHairMaterialPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairMaterialPS, MeshMaterial);

public:
	FHairMaterialPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		MaterialPass_MacroGroupId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_MacroGroupId"));
		MaterialPass_MaterialId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_MaterialId"));
		MaterialPass_PrimitiveId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_PrimitiveId"));
		MaterialPass_LightChannelMask.Bind(Initializer.ParameterMap, TEXT("MaterialPass_LightChannelMask"));
	}

	FHairMaterialPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsCompatibleWithHairStrands(Parameters.Platform, Parameters.MaterialParameters) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		const bool bPlatformRequireRenderTarget = IsPlatformRequiringRenderTargetForMaterialPass(Parameters.Platform);
		OutEnvironment.SetDefine(TEXT("HAIR_MATERIAL_DEBUG_OR_EMISSIVE_OUTPUT"), (IsHairStrandsEmissiveEnable() || bPlatformRequireRenderTarget) ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("HAIRSTRANDS_HAS_NORMAL_CONNECTED"), Parameters.MaterialParameters.bHasNormalConnected ? 1 : 0);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FHairMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
		ShaderBindings.Add(MaterialPass_MacroGroupId, ShaderElementData.MaterialPass_MacroGroupId);
		ShaderBindings.Add(MaterialPass_MaterialId, ShaderElementData.MaterialPass_MaterialId);
		ShaderBindings.Add(MaterialPass_PrimitiveId, ShaderElementData.MaterialPass_PrimitiveId);
		ShaderBindings.Add(MaterialPass_LightChannelMask, ShaderElementData.MaterialPass_LightChannelMask);
	}

private:
	LAYOUT_FIELD(FShaderParameter, MaterialPass_MacroGroupId);
	LAYOUT_FIELD(FShaderParameter, MaterialPass_MaterialId);
	LAYOUT_FIELD(FShaderParameter, MaterialPass_PrimitiveId);
	LAYOUT_FIELD(FShaderParameter, MaterialPass_LightChannelMask);
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FHairMaterialPS, TEXT("/Engine/Private/HairStrands/HairStrandsMaterialPS.usf"), TEXT("Main"), SF_Pixel);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairMaterialProcessor : public FMeshPassProcessor
{
public:
	FHairMaterialProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FDynamicPassMeshDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;
	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, int32 MacroGroupId, int32 HairMaterialId);

private:
	bool TryAddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		uint32 MacroGroupId,
		uint32 HairMaterialId,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material);

	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		const int32 MacroGroupId,
		const int32 HairMaterialId,
		const int32 HairPrimitiveId,
		const uint32 HairPrimitiveLightChannelMask);

	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FHairMaterialProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, 0, 0);
}

void FHairMaterialProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, int32 MacroGroupId, int32 HairMaterialId)
{
	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	while (MaterialRenderProxy)
	{
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
		if (Material)
		{
			if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MacroGroupId, HairMaterialId, *MaterialRenderProxy, *Material))
			{
				break;
			}
		}

		MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
	}
}

bool FHairMaterialProcessor::TryAddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	uint32 MacroGroupId,
	uint32 HairMaterialId,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName();
	const bool bShouldRender = (!PrimitiveSceneProxy && MeshBatch.Elements.Num() > 0) || (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass());

	if (bIsCompatible
		&& bIsHairStrandsFactory
		&& bShouldRender
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		// For the mesh patch to be rendered a single triangle triangle to spawn the necessary amount of thread
		FMeshBatch MeshBatchCopy = MeshBatch;
		for (uint32 ElementIt = 0, ElementCount = uint32(MeshBatch.Elements.Num()); ElementIt < ElementCount; ++ElementIt)
		{
			MeshBatchCopy.Elements[ElementIt].FirstIndex = 0;
			MeshBatchCopy.Elements[ElementIt].NumPrimitives = 1;
			MeshBatchCopy.Elements[ElementIt].NumInstances = 1;
			MeshBatchCopy.Elements[ElementIt].IndirectArgsBuffer = nullptr;
			MeshBatchCopy.Elements[ElementIt].IndirectArgsOffset = 0;
		}

		int32 PrimitiveId = 0;
		int32 ScenePrimitiveId = 0;
		FPrimitiveSceneInfo* SceneInfo = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetPrimitiveSceneInfo() : nullptr;
		GetDrawCommandPrimitiveId(SceneInfo, MeshBatch.Elements[0], PrimitiveId, ScenePrimitiveId);
		uint32 LightChannelMask = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetLightingChannelMask() : 0;

		return Process(MeshBatchCopy, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MacroGroupId, HairMaterialId, PrimitiveId, LightChannelMask);
	}

	return true;
}

bool FHairMaterialProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	const int32 MacroGroupId,
	const int32 HairMaterialId,
	const int32 HairPrimitiveId,
	const uint32 HairPrimitiveLightChannelMask)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FHairMaterialVS,
		FHairMaterialPS> PassShaders;
	{
		FMaterialShaderTypes ShaderTypes;
		ShaderTypes.AddShaderType<FHairMaterialVS>();
		ShaderTypes.AddShaderType<FHairMaterialPS>();

		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

		FMaterialShaders Shaders;
		if (!MaterialResource.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
		{
			return false;
		}

		Shaders.TryGetVertexShader(PassShaders.VertexShader);
		Shaders.TryGetPixelShader(PassShaders.PixelShader);
	}

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	FHairMaterialShaderElementData ShaderElementData(MacroGroupId, HairMaterialId, HairPrimitiveId, HairPrimitiveLightChannelMask);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		PassShaders,
		ERasterizerFillMode::FM_Solid,
		ERasterizerCullMode::CM_CCW,
		FMeshDrawCommandSortKey::Default,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

FHairMaterialProcessor::FHairMaterialProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,
	FDynamicPassMeshDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{
}

/////////////////////////////////////////////////////////////////////////////////////////

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVisibilityMaterialPassUniformParameters, )
	SHADER_PARAMETER(FIntPoint, MaxResolution)
	SHADER_PARAMETER(uint32, MaxSampleCount)
	SHADER_PARAMETER(uint32, NodeGroupSize)
	SHADER_PARAMETER(uint32, bUpdateSampleCoverage)
	SHADER_PARAMETER(uint32, bOutputEmissive)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NodeIndex)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, TotalNodeCounter)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, NodeCoord)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, IndirectArgs)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPackedHairSample>, OutNodeData)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float2>, OutNodeVelocity)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FVisibilityMaterialPassUniformParameters, "MaterialPassParameters", SceneTextures);

BEGIN_SHADER_PARAMETER_STRUCT(FVisibilityMaterialPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVisibilityMaterialPassUniformParameters, UniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

///////////////////////////////////////////////////////////////////////////////////////////////////
// Patch sample coverage
class FUpdateSampleCoverageCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FUpdateSampleCoverageCS);
	SHADER_USE_PARAMETER_STRUCT(FUpdateSampleCoverageCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NodeIndexAndOffset)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedHairSample>,  InNodeDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPackedHairSample>, OutNodeDataBuffer)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FUpdateSampleCoverageCS, "/Engine/Private/HairStrands/HairStrandsVisibilityComputeSampleCoverage.usf", "MainCS", SF_Compute);

static FRDGBufferRef AddUpdateSampleCoveragePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const FRDGTextureRef NodeIndexAndOffset,
	const FRDGBufferRef InNodeDataBuffer)
{
	FRDGBufferRef OutNodeDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(InNodeDataBuffer->Desc.BytesPerElement, InNodeDataBuffer->Desc.NumElements), TEXT("Hair.CompactNodeData"));

	FUpdateSampleCoverageCS::FParameters* Parameters = GraphBuilder.AllocParameters<FUpdateSampleCoverageCS::FParameters>();
	Parameters->Resolution = NodeIndexAndOffset->Desc.Extent;
	Parameters->NodeIndexAndOffset = NodeIndexAndOffset;
	Parameters->InNodeDataBuffer = GraphBuilder.CreateSRV(InNodeDataBuffer);
	Parameters->OutNodeDataBuffer = GraphBuilder.CreateUAV(OutNodeDataBuffer);

	TShaderMapRef<FUpdateSampleCoverageCS> ComputeShader(View->ShaderMap);

	// Add 64 threads permutation
	const uint32 GroupSizeX = 8;
	const uint32 GroupSizeY = 4;
	const FIntVector DispatchCount = FIntVector(
		(Parameters->Resolution.X + GroupSizeX-1) / GroupSizeX, 
		(Parameters->Resolution.Y + GroupSizeY-1) / GroupSizeY, 
		1);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVisbilityUpdateCoverage"),
		ComputeShader,
		Parameters,
		DispatchCount);

	return OutNodeDataBuffer;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
struct FMaterialPassOutput
{
	static const EPixelFormat VelocityFormat = PF_G16R16;
	FRDGBufferRef NodeData = nullptr;
	FRDGBufferRef NodeVelocity = nullptr;
	FRDGTextureRef EmissiveTexture = nullptr;
};

static FMaterialPassOutput AddHairMaterialPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const bool bUpdateSampleCoverage,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	FInstanceCullingManager& InstanceCullingManager,
	const uint32 NodeGroupSize,
	FRDGTextureRef CompactNodeIndex,
	FRDGBufferRef CompactNodeVis,
	FRDGBufferRef CompactNodeCoord,
	FRDGTextureRef CompactNodeCounter,
	FRDGBufferRef IndirectArgBuffer)
{
	if (!CompactNodeVis || !CompactNodeIndex)
		return FMaterialPassOutput();

	const uint32 MaxNodeCount = CompactNodeVis->Desc.NumElements;

	FMaterialPassOutput Output;
	Output.NodeData		= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(HairStrandsVisibilityInternal::NodeData), MaxNodeCount), TEXT("Hair.CompactNodeData"));
	Output.NodeVelocity = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, CompactNodeVis->Desc.NumElements), TEXT("Hair.CompactNodeVelocity"));

	const uint32 ResolutionDim = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(MaxNodeCount)));
	const FIntPoint Resolution(ResolutionDim, ResolutionDim);

	const bool bOutputEmissive = IsHairStrandsEmissiveEnable();
	const bool bIsPlatformRequireRenderTarget = IsPlatformRequiringRenderTargetForMaterialPass(Scene->GetShaderPlatform()) || GRHIRequiresRenderTargetForPixelShaderUAVs;

	// Add resources reference to the pass parameters, in order to get the resource lifetime extended to this pass
	FVisibilityMaterialPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityMaterialPassParameters>();

	{
		FVisibilityMaterialPassUniformParameters* UniformParameters = GraphBuilder.AllocParameters<FVisibilityMaterialPassUniformParameters>();

		UniformParameters->bUpdateSampleCoverage = bUpdateSampleCoverage ? 1 : 0;
		UniformParameters->bOutputEmissive = bOutputEmissive ? 1 : 0;
		UniformParameters->MaxResolution = Resolution;
		UniformParameters->NodeGroupSize = NodeGroupSize;
		UniformParameters->MaxSampleCount = MaxNodeCount;
		UniformParameters->TotalNodeCounter = CompactNodeCounter;
		UniformParameters->NodeIndex = CompactNodeIndex;
		UniformParameters->NodeVis = GraphBuilder.CreateSRV(CompactNodeVis);
		UniformParameters->NodeCoord = GraphBuilder.CreateSRV(CompactNodeCoord, FHairStrandsVisibilityData::NodeCoordFormat);
		UniformParameters->IndirectArgs = GraphBuilder.CreateSRV(IndirectArgBuffer);
		UniformParameters->OutNodeData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.NodeData));
		UniformParameters->OutNodeVelocity = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.NodeVelocity, FMaterialPassOutput::VelocityFormat));

		PassParameters->UniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);
	}

	{
		const bool bEnableMSAA = false;
		SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfoBits, ViewInfo->CachedViewUniformShaderParameters->HairComponents);
		PassParameters->View = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
	}

	// For debug purpose only
	if (bOutputEmissive)
	{
		FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(Resolution, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_RenderTargetable);
		Output.EmissiveTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("Hair.MaterialEmissiveOutput"));
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Output.EmissiveTexture, ERenderTargetLoadAction::EClear, 0);
		
	}
	else if (bIsPlatformRequireRenderTarget)
	{
		FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(Resolution, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_RenderTargetable);
		FRDGTextureRef OutDummyTexture0 = GraphBuilder.CreateTexture(OutputDesc, TEXT("Hair.MaterialDummyOutput"));
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutDummyTexture0, ERenderTargetLoadAction::EClear, 0);
	}
	

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsMaterialPass"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Scene = Scene, ViewInfo, &MacroGroupDatas, MaxNodeCount, Resolution, NodeGroupSize, bUpdateSampleCoverage, bOutputEmissive](FRHICommandListImmediate& RHICmdList)
	{
		FMeshPassProcessorRenderState DrawRenderState;

		{
			RHICmdList.SetViewport(0, 0, 0.0f, Resolution.X, Resolution.Y, 1.0f);
			if (bOutputEmissive)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_Zero>::GetRHI());
			}
			else
			{
				DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
			}
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState <false, CF_Always> ::GetRHI());
			
			FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage;
			FMeshCommandOneFrameArray VisibleMeshDrawCommands;
			FGraphicsMinimalPipelineStateSet PipelineStateSet;
			// @todo loadtime arnes: do we need to pass this along to somewhere?
			bool NeedsShaderInitialization;
			FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, PipelineStateSet, NeedsShaderInitialization);
			FHairMaterialProcessor MeshProcessor(Scene, ViewInfo, DrawRenderState, &ShadowContext);

			for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas)
			{
				for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroupData.PrimitivesInfos)
				{
					const FMeshBatch& MeshBatch = *PrimitiveInfo.Mesh;
					const uint64 BatchElementMask = ~0ull;
					MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.PrimitiveSceneProxy, -1, MacroGroupData.MacroGroupId, PrimitiveInfo.MaterialId);
				}
			}

			if (VisibleMeshDrawCommands.Num() > 0)
			{
				FRHIBuffer* PrimitiveIdVertexBuffer = nullptr;
				SortAndMergeDynamicPassMeshDrawCommands(ViewInfo->GetFeatureLevel(), VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1, ViewInfo->DynamicPrimitiveCollector.GetPrimitiveIdRange());
				SubmitMeshDrawCommands(VisibleMeshDrawCommands, PipelineStateSet, PrimitiveIdVertexBuffer, 0, false, 1, RHICmdList);
			}
		}
	});

	return Output;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVelocityCS: public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FHairVelocityCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_GROUPSIZE", 32, 64);
	class FVelocity : SHADER_PERMUTATION_INT("PERMUTATION_VELOCITY", 4);
	class FOuputFormat : SHADER_PERMUTATION_INT("PERMUTATION_OUTPUT_FORMAT", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FVelocity, FOuputFormat>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, ResolutionOffset)
		SHADER_PARAMETER(float, VelocityThreshold)
		SHADER_PARAMETER(float, CoverageThreshold)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NodeIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, NodeVelocity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutVelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutResolveMaskTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVelocityCS, "/Engine/Private/HairStrands/HairStrandsVelocity.usf", "MainCS", SF_Compute);

float GetHairFastResolveVelocityThreshold(const FIntPoint& Resolution);

static void AddHairVelocityPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	FRDGTextureRef& CategorizationTexture,
	FRDGTextureRef& NodeIndex,
	FRDGBufferRef& NodeVis,
	FRDGBufferRef& NodeVelocity,
	FRDGTextureRef& OutVelocityTexture,
	FRDGTextureRef& OutResolveMaskTexture)
{
	const bool bWriteOutVelocity = OutVelocityTexture != nullptr;
	if (!bWriteOutVelocity)
		return;

	const FIntPoint Resolution = OutVelocityTexture->Desc.Extent;
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV);
		OutResolveMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.VelocityResolveMaskTexture"));
	}

	check(OutVelocityTexture->Desc.Format == PF_G16R16 || OutVelocityTexture->Desc.Format == PF_A16B16G16R16);
	const bool bTwoChannelsOutput = OutVelocityTexture->Desc.Format == PF_G16R16;

	FHairVelocityCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVelocityCS::FGroupSize>(GetVendorOptimalGroupSize1D());
	PermutationVector.Set<FHairVelocityCS::FVelocity>(bWriteOutVelocity ? FMath::Clamp(GHairVelocityType + 1, 0, 3) : 0);
	PermutationVector.Set<FHairVelocityCS::FOuputFormat>(bTwoChannelsOutput ? 0 : 1);

	FHairVelocityCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVelocityCS::FParameters>();
	PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->VelocityThreshold = GetHairFastResolveVelocityThreshold(Resolution);
	PassParameters->CoverageThreshold = GetHairWriteVelocityCoverageThreshold();
	PassParameters->NodeIndex = NodeIndex;
	PassParameters->NodeVis = GraphBuilder.CreateSRV(NodeVis);
	PassParameters->NodeVelocity = GraphBuilder.CreateSRV(NodeVelocity, FMaterialPassOutput::VelocityFormat);
	PassParameters->CategorizationTexture = CategorizationTexture;
	PassParameters->OutVelocityTexture = GraphBuilder.CreateUAV(OutVelocityTexture);
	PassParameters->OutResolveMaskTexture = GraphBuilder.CreateUAV(OutResolveMaskTexture);

	const FIntPoint GroupSize = GetVendorOptimalGroupSize2D();
	// We don't use the CPU screen projection for running the velocity pass, as we need to clear the entire 
	// velocity mask through the UAV write, otherwise the mask will be partially invalid.
#if 1
	FIntRect TotalRect = View.ViewRect; 
#else
	// Snap the rect onto thread group boundary
	FIntRect TotalRect = ComputeVisibleHairStrandsMacroGroupsRect(View.ViewRect, MacroGroupDatas);
	TotalRect.Min.X = FMath::FloorToInt(float(TotalRect.Min.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Min.Y = FMath::FloorToInt(float(TotalRect.Min.Y) / float(GroupSize.Y)) * GroupSize.Y;
	TotalRect.Max.X = FMath::CeilToInt(float(TotalRect.Max.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Max.Y = FMath::CeilToInt(float(TotalRect.Max.Y) / float(GroupSize.Y)) * GroupSize.Y;
#endif
	FIntPoint RectResolution(TotalRect.Width(), TotalRect.Height());
	PassParameters->ResolutionOffset = FIntPoint(TotalRect.Min.X, TotalRect.Min.Y);

	TShaderMapRef<FHairVelocityCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVelocity"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(RectResolution, GroupSize));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairLightChannelMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairLightChannelMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FHairLightChannelMaskCS, FGlobalShader);

	class FVendor : SHADER_PERMUTATION_INT("PERMUTATION_VENDOR", HairVisibilityVendorCount);
	using FPermutationDomain = TShaderPermutationDomain<FVendor>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, NodeData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NodeOffsetAndCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutLightChannelMaskTexture)
	END_SHADER_PARAMETER_STRUCT()
		
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairLightChannelMaskCS, "/Engine/Private/HairStrands/HairStrandsLightChannelMask.usf", "MainCS", SF_Compute);

static FRDGTextureRef AddHairLightChannelMaskPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FIntPoint Resolution,
	FRDGBufferRef NodeData,
	FRDGTextureRef NodeOffsetAndCount)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
	FRDGTextureRef OutLightChannelMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.LightChannelMask"));

	FHairLightChannelMaskCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairLightChannelMaskCS::FVendor>(GetVendor());

	FHairLightChannelMaskCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairLightChannelMaskCS::FParameters>();
	PassParameters->OutputResolution = Resolution;
	PassParameters->NodeData = GraphBuilder.CreateSRV(NodeData);
	PassParameters->NodeOffsetAndCount = NodeOffsetAndCount;
	PassParameters->OutLightChannelMaskTexture = GraphBuilder.CreateUAV(OutLightChannelMaskTexture);
	
	const FIntPoint GroupSize = GetVendorOptimalGroupSize2D();
	TShaderMapRef<FHairLightChannelMaskCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairLightChannelMask"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(Resolution, GroupSize));

	return OutLightChannelMaskTexture;
}

/////////////////////////////////////////////////////////////////////////////////////////
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVisibilityPassUniformParameters, )
	SHADER_PARAMETER(uint32, MaxPPLLNodeCount)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, PPLLCounter)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, PPLLNodeIndex)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPPLLNodeData>, PPLLNodeData)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FVisibilityPassUniformParameters, "HairVisibilityPass", SceneTextures);

BEGIN_SHADER_PARAMETER_STRUCT(FVisibilityPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVisibilityPassUniformParameters, UniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

// Example: 28bytes * 8spp = 224bytes per pixel = 442Mb @ 1080p
struct PPLLNodeData
{
	uint32 Depth;
	uint32 PrimitiveId_MacroGroupId;
	uint32 Tangent_Coverage;
	uint32 BaseColor_Roughness;
	uint32 Specular;
	uint32 NextNodeIndex;
	uint32 PackedVelocity;
};

TRDGUniformBufferRef<FVisibilityPassUniformParameters> CreatePassDummyTextures(FRDGBuilder& GraphBuilder)
{
	FVisibilityPassUniformParameters* UniformParameters = GraphBuilder.AllocParameters<FVisibilityPassUniformParameters>();

	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(1,1), PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
	UniformParameters->PPLLCounter		= GraphBuilder.CreateUAV(GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityPPLLNodeIndex")));
	UniformParameters->PPLLNodeIndex	= GraphBuilder.CreateUAV(GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityPPLLNodeIndex")));
	UniformParameters->PPLLNodeData		= GraphBuilder.CreateUAV(GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(PPLLNodeData), 1), TEXT("Hair.DummyPPLLNodeData")));

	return GraphBuilder.CreateUniformBuffer(UniformParameters);
}

template<EHairVisibilityRenderMode RenderMode, bool bCullingEnable>
class FHairVisibilityVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairVisibilityVS, MeshMaterial);

protected:

	FHairVisibilityVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
	}

	FHairVisibilityVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairVisibility(Parameters) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		const uint32 RenderModeValue = uint32(RenderMode);
		OutEnvironment.SetDefine(TEXT("HAIR_RENDER_MODE"), RenderModeValue);
		OutEnvironment.SetDefine(TEXT("USE_CULLED_CLUSTER"), bCullingEnable ? 1 : 0);
	}
};

typedef FHairVisibilityVS<HairVisibilityRenderMode_MSAA_Visibility, false >				THairVisiblityVS_MSAAVisibility_NoCulling;
typedef FHairVisibilityVS<HairVisibilityRenderMode_MSAA_Visibility, true >				THairVisiblityVS_MSAAVisibility_Culling;
typedef FHairVisibilityVS<HairVisibilityRenderMode_Transmittance, true >				THairVisiblityVS_Transmittance;
typedef FHairVisibilityVS<HairVisibilityRenderMode_TransmittanceAndHairCount, true >	THairVisiblityVS_TransmittanceAndHairCount;
typedef FHairVisibilityVS<HairVisibilityRenderMode_PPLL, true >							THairVisiblityVS_PPLL;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_MSAAVisibility_NoCulling,	TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_MSAAVisibility_Culling,		TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_Transmittance,				TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_TransmittanceAndHairCount,	TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_PPLL,						TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FHairVisibilityShaderElementData(uint32 InHairMacroGroupId, uint32 InHairMaterialId, uint32 InLightChannelMask) : HairMacroGroupId(InHairMacroGroupId), HairMaterialId(InHairMaterialId), LightChannelMask(InLightChannelMask) { }
	uint32 HairMacroGroupId;
	uint32 HairMaterialId;
	uint32 LightChannelMask;
};

template<EHairVisibilityRenderMode RenderMode>
class FHairVisibilityPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairVisibilityPS, MeshMaterial);

public:

	FHairVisibilityPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:	FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		HairVisibilityPass_HairMacroGroupIndex.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_HairMacroGroupIndex"));
		HairVisibilityPass_HairMaterialId.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_HairMaterialId"));
		HairVisibilityPass_LightChannelMask.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_LightChannelMask"));
	}

	FHairVisibilityPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		if (Parameters.VertexFactoryType->GetFName() != FName(TEXT("FHairStrandsVertexFactory")))
		{
			return false;
		}

		// Disable PPLL rendering for non-PC platform
		if (RenderMode == HairVisibilityRenderMode_PPLL)
		{
			return IsCompatibleWithHairStrands(Parameters.Platform, Parameters.MaterialParameters) && IsPCPlatform(Parameters.Platform) && !IsMobilePlatform(Parameters.Platform);
		}
		else
		{
			return IsCompatibleWithHairStrands(Parameters.Platform, Parameters.MaterialParameters);
		}
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)	
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		const uint32 RenderModeValue = uint32(RenderMode);
		OutEnvironment.SetDefine(TEXT("HAIR_RENDER_MODE"), RenderModeValue);

		if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_UINT);
		}
		else if (RenderMode == HairVisibilityRenderMode_Transmittance)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_FLOAT);
		}
		else if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_FLOAT);
			OutEnvironment.SetRenderTargetOutputFormat(1, PF_R32G32_UINT);
		}
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FHairVisibilityShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
		ShaderBindings.Add(HairVisibilityPass_HairMacroGroupIndex, ShaderElementData.HairMacroGroupId);
		ShaderBindings.Add(HairVisibilityPass_HairMaterialId, ShaderElementData.HairMaterialId);
		ShaderBindings.Add(HairVisibilityPass_LightChannelMask, ShaderElementData.LightChannelMask);
	}

	LAYOUT_FIELD(FShaderParameter, HairVisibilityPass_HairMacroGroupIndex);
	LAYOUT_FIELD(FShaderParameter, HairVisibilityPass_HairMaterialId);
	LAYOUT_FIELD(FShaderParameter, HairVisibilityPass_LightChannelMask);
};
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_MSAA_Visibility>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_Transmittance>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_TransmittanceAndHairCount>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_PPLL>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityProcessor : public FMeshPassProcessor
{
public:
	FHairVisibilityProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		const EHairVisibilityRenderMode InRenderMode,
		FDynamicPassMeshDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;
	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, uint32 HairMacroGroupId, uint32 HairMaterialId, bool bCullingEnable);

private:
	bool TryAddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		uint32 HairMacroGroupId,
		uint32 HairMaterialId,
		bool bCullingEnable,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material);

	template<EHairVisibilityRenderMode RenderMode, bool bCullingEnable=true>
	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		const uint32 HairMacroGroupId,
		const uint32 HairMaterialId,
		const uint32 LightChannelMask,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	const EHairVisibilityRenderMode RenderMode;
	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FHairVisibilityProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, 0, 0, false);
}

void FHairVisibilityProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, uint32 HairMacroGroupId, uint32 HairMaterialId, bool bCullingEnable)
{
	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	while (MaterialRenderProxy)
	{
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
		if (Material)
		{
			if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, HairMacroGroupId, HairMaterialId, bCullingEnable, *MaterialRenderProxy, *Material))
			{
				break;
			}
		}

		MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
	}
}

bool FHairVisibilityProcessor::TryAddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	uint32 HairMacroGroupId,
	uint32 HairMaterialId,
	bool bCullingEnable,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName();
	const bool bShouldRender = (!PrimitiveSceneProxy && MeshBatch.Elements.Num() > 0) || (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass());
	const uint32 LightChannelMask = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetLightingChannelMask() : 0;

	if (bIsCompatible
		&& bIsHairStrandsFactory
		&& bShouldRender
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility && bCullingEnable)
			return Process<HairVisibilityRenderMode_MSAA_Visibility, true>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility && !bCullingEnable)
			return Process<HairVisibilityRenderMode_MSAA_Visibility, false>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_Transmittance)
			return Process<HairVisibilityRenderMode_Transmittance>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
			return Process<HairVisibilityRenderMode_TransmittanceAndHairCount>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_PPLL)
			return Process<HairVisibilityRenderMode_PPLL>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
	}

	return true;
}

template<EHairVisibilityRenderMode TRenderMode, bool bCullingEnable>
bool FHairVisibilityProcessor::Process(
	const FMeshBatch& MeshBatch, 
	uint64 BatchElementMask, 
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	const uint32 HairMacroGroupId,
	const uint32 HairMaterialId,
	const uint32 LightChannelMask,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FHairVisibilityVS<TRenderMode, bCullingEnable>,
		FHairVisibilityPS<TRenderMode>> PassShaders;
	{
		FMaterialShaderTypes ShaderTypes;
		ShaderTypes.AddShaderType<FHairVisibilityVS<TRenderMode, bCullingEnable>>();
		ShaderTypes.AddShaderType<FHairVisibilityPS<TRenderMode>>();

		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

		FMaterialShaders Shaders;
		if (!MaterialResource.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
		{
			return false;
		}

		Shaders.TryGetVertexShader(PassShaders.VertexShader);
		Shaders.TryGetPixelShader(PassShaders.PixelShader);
	}

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	FHairVisibilityShaderElementData ShaderElementData(HairMacroGroupId, HairMaterialId, LightChannelMask);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		PassShaders,
		MeshFillMode,
		MeshCullMode,
		FMeshDrawCommandSortKey::Default,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

FHairVisibilityProcessor::FHairVisibilityProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,
	const EHairVisibilityRenderMode InRenderMode,
	FDynamicPassMeshDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, RenderMode(InRenderMode)
	, PassDrawRenderState(InPassDrawRenderState)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Clear uint texture
class FClearUIntGraphicPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearUIntGraphicPS);
	SHADER_USE_PARAMETER_STRUCT(FClearUIntGraphicPS, FGlobalShader);

	class FOutputFormat : SHADER_PERMUTATION_INT("PERMUTATION_OUTPUT_FORMAT", 2);
	using FPermutationDomain = TShaderPermutationDomain<FOutputFormat>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ClearValue)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FOutputFormat>() == 0)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_UINT);
		}
		else if (PermutationVector.Get<FOutputFormat>() == 1)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32G32_UINT);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearUIntGraphicPS, "/Engine/Private/HairStrands/HairStrandsVisibilityClearPS.usf", "ClearPS", SF_Pixel);

// Opaque mask
static void AddClearGraphicPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo* View,
	const uint32 ClearValue,
	FRDGTextureRef& OutTarget)
{
	check(OutTarget);

	FClearUIntGraphicPS::FParameters* Parameters = GraphBuilder.AllocParameters<FClearUIntGraphicPS::FParameters>();
	Parameters->ClearValue = ClearValue;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTarget, ERenderTargetLoadAction::ENoAction, 0);

	FClearUIntGraphicPS::FPermutationDomain PermutationVector;
	if (OutTarget->Desc.Format == PF_R32_UINT)
	{
		PermutationVector.Set<FClearUIntGraphicPS::FOutputFormat>(0);
	}
	else if (OutTarget->Desc.Format == PF_R32G32_UINT)
	{
		PermutationVector.Set<FClearUIntGraphicPS::FOutputFormat>(1);
	}

	TShaderMapRef<FPostProcessVS> VertexShader(View->ShaderMap);
	TShaderMapRef<FClearUIntGraphicPS> PixelShader(View->ShaderMap, PermutationVector);
	const FIntRect Viewport = FIntRect(FIntPoint(0, 0), OutTarget->Desc.Extent);// View->ViewRect;
	const FIntPoint Resolution = OutTarget->Desc.Extent;

	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		Forward<FRDGEventName>(PassName),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);

		DrawRectangle(
			RHICmdList,
			0, 0,
			Viewport.Width(),Viewport.Height(),
			Viewport.Min.X,  Viewport.Min.Y,
			Viewport.Width(),Viewport.Height(),
			Viewport.Size(),
			Resolution,
			VertexShader,
			EDRF_UseTriangleOptimization);
	});
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Copy dispatch count into an indirect buffer 
class FCopyIndirectBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCopyIndirectBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FCopyIndirectBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ThreadGroupSize)
		SHADER_PARAMETER(uint32, ItemCountPerGroup)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CounterTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutArgBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FCopyIndirectBufferCS, "/Engine/Private/HairStrands/HairStrandsVisibilityCopyIndirectArg.usf", "CopyCS", SF_Compute);

static FRDGBufferRef AddCopyIndirectArgPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const uint32 ThreadGroupSize,
	const uint32 ItemCountPerGroup,
	FRDGTextureRef CounterTexture)
{
	check(CounterTexture);

	FRDGBufferRef OutBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("Hair.VisibilityIndirectArgBuffer"));

	FCopyIndirectBufferCS::FParameters* Parameters = GraphBuilder.AllocParameters<FCopyIndirectBufferCS::FParameters>();
	Parameters->ThreadGroupSize = ThreadGroupSize;
	Parameters->ItemCountPerGroup = ItemCountPerGroup;
	Parameters->CounterTexture = CounterTexture;
	Parameters->OutArgBuffer = GraphBuilder.CreateUAV(OutBuffer);

	TShaderMapRef<FCopyIndirectBufferCS> ComputeShader(View->ShaderMap);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVisbilityCopyIndirectArgs"),
		ComputeShader,
		Parameters,
		FIntVector(1,1,1));

	return OutBuffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityPrimitiveIdCompactionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityPrimitiveIdCompactionCS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityPrimitiveIdCompactionCS, FGlobalShader);

	class FGroupSize	: SHADER_PERMUTATION_INT("PERMUTATION_GROUPSIZE", 2);
	class FVelocity		: SHADER_PERMUTATION_INT("PERMUTATION_VELOCITY", 2);
	class FViewTransmittance : SHADER_PERMUTATION_INT("PERMUTATION_VIEWTRANSMITTANCE", 2);
	class FMaterial 	: SHADER_PERMUTATION_INT("PERMUTATION_MATERIAL_COMPACTION", 2);
	class FPPLL 		: SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_PPLL", 0, 8, 16, 32); // See GetPPLLMaxRenderNodePerPixel
	class FMSAACount 	: SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_MSAACOUNT", 1, 2, 4, 8);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FVelocity, FViewTransmittance, FMaterial, FPPLL, FMSAACount>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(FIntPoint, ResolutionOffset)
		SHADER_PARAMETER(uint32, MaxNodeCount)
		SHADER_PARAMETER(uint32, bSortSampleByDepth)
		SHADER_PARAMETER(float, DepthTheshold)
		SHADER_PARAMETER(float, CosTangentThreshold)
		SHADER_PARAMETER(float, CoverageThreshold)
		SHADER_PARAMETER(uint32, VelocityType)

		// Available for the MSAA path
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_IDTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_MaterialTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_AttributeTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_VelocityTexture)
		// Available for the PPLL path
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PPLLCounter)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PPLLNodeIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, PPLLNodeData)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ViewTransmittanceTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCompactNodeCounter)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCompactNodeIndex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCategorizationTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, OutCompactNodeData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutCompactNodeCoord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutVelocityTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	END_SHADER_PARAMETER_STRUCT()

public:

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FPPLL>() > 0)
		{
			PermutationVector.Set<FViewTransmittance>(0);
			PermutationVector.Set<FMSAACount>(4);
		}
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FPPLL>() > 0 && PermutationVector.Get<FViewTransmittance>() > 0)
		{
			return false;
		}
		if (PermutationVector.Get<FPPLL>() > 0 && PermutationVector.Get<FMSAACount>() == 8)
		{
			return false;
		}
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityPrimitiveIdCompactionCS, "/Engine/Private/HairStrands/HairStrandsVisibilityCompaction.usf", "MainCS", SF_Compute);

static void AddHairVisibilityPrimitiveIdCompactionPass(
	const bool bUsePPLL,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const uint32 NodeGroupSize,
	FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters,
	FRDGTextureRef& OutCompactCounter,
	FRDGTextureRef& OutCompactNodeIndex,
	FRDGBufferRef& OutCompactNodeData,
	FRDGBufferRef& OutCompactNodeCoord,
	FRDGTextureRef& OutCategorizationTexture,
	FRDGTextureRef& OutVelocityTexture,
	FRDGBufferRef& OutIndirectArgsBuffer,
	uint32& OutMaxRenderNodeCount)
{
	FIntPoint Resolution;
	if (bUsePPLL)
	{
		check(PassParameters->PPLLCounter);
		check(PassParameters->PPLLNodeIndex);
		check(PassParameters->PPLLNodeData);
		Resolution = PassParameters->PPLLNodeIndex->Desc.Extent;
	}
	else
	{
		check(PassParameters->MSAA_DepthTexture->Desc.NumSamples == GetMaxSamplePerPixel());
		check(PassParameters->MSAA_DepthTexture);
		check(PassParameters->MSAA_IDTexture);
		Resolution = PassParameters->MSAA_DepthTexture->Desc.Extent;
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCompactCounter = GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityCompactCounter"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCompactNodeIndex = GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityCompactNodeIndex"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R16G16B16A16_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCategorizationTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.CategorizationTexture"));
	}

	const uint32 ClearValues[4] = { 0u,0u,0u,0u };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactCounter), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactNodeIndex), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCategorizationTexture), ClearValues);

	// Adapt the buffer allocation based on the bounding box of the hair macro groups. This allows to reduce the overall allocation size
	const FIntRect HairRect = ComputeVisibleHairStrandsMacroGroupsRect(View.ViewRect, MacroGroupDatas);
	const FIntPoint EffectiveResolution(HairRect.Width(), HairRect.Height());

	// Select render node count according to current mode
	const uint32 MSAASampleCount = GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA_Visibility ? GetMaxSamplePerPixel() : 1;
	const uint32 PPLLMaxRenderNodePerPixel = GetMaxSamplePerPixel();
	const uint32 MaxRenderNodeCount = GetTotalSampleCountForAllocation(EffectiveResolution);

	OutCompactNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(HairStrandsVisibilityInternal::NodeVis), MaxRenderNodeCount), TEXT("Hair.VisibilityPrimitiveIdCompactNodeData"));

	{
		// Pixel coord of the node. Stored as 2*uint16, packed into a single uint32
		OutCompactNodeCoord = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxRenderNodeCount), TEXT("Hair.VisibilityPrimitiveIdCompactNodeCoord"));
	}

	const bool bWriteOutVelocity = OutVelocityTexture != nullptr;
	const uint32 VelocityPermutation = bWriteOutVelocity ? FMath::Clamp(GHairVelocityType + 1, 0, 3) : 0;
	FHairVisibilityPrimitiveIdCompactionCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FGroupSize>(GetVendor() == HairVisibilityVendor_NVIDIA ? 0 : 1);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FVelocity>(VelocityPermutation > 0 ? 1 : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FViewTransmittance>(PassParameters->ViewTransmittanceTexture ? 1 : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FMaterial>(GHairStrandsMaterialCompactionEnable ? 1 : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FPPLL>(bUsePPLL ? PPLLMaxRenderNodePerPixel : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FMSAACount>(MSAASampleCount);
	PermutationVector = FHairVisibilityPrimitiveIdCompactionCS::RemapPermutation(PermutationVector);

	PassParameters->OutputResolution = Resolution;
	PassParameters->VelocityType = VelocityPermutation;
	PassParameters->MaxNodeCount = MaxRenderNodeCount;
	PassParameters->bSortSampleByDepth = GHairStrandsSortHairSampleByDepth > 0 ? 1 : 0;
	PassParameters->CoverageThreshold = FMath::Clamp(GHairStrandsFullCoverageThreshold, 0.1f, 1.f);
	PassParameters->DepthTheshold = FMath::Clamp(GHairStrandsMaterialCompactionDepthThreshold, 0.f, 100.f);
	PassParameters->CosTangentThreshold = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(GHairStrandsMaterialCompactionTangentThreshold, 0.f, 90.f)));
	PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->OutCompactNodeCounter = GraphBuilder.CreateUAV(OutCompactCounter);
	PassParameters->OutCompactNodeIndex = GraphBuilder.CreateUAV(OutCompactNodeIndex);
	PassParameters->OutCompactNodeData = GraphBuilder.CreateUAV(OutCompactNodeData);
	PassParameters->OutCompactNodeCoord = GraphBuilder.CreateUAV(OutCompactNodeCoord, FHairStrandsVisibilityData::NodeCoordFormat);
	PassParameters->OutCategorizationTexture = GraphBuilder.CreateUAV(OutCategorizationTexture);

	if (bWriteOutVelocity)
	{
		PassParameters->OutVelocityTexture = GraphBuilder.CreateUAV(OutVelocityTexture);
	}
 
	FIntRect TotalRect = ComputeVisibleHairStrandsMacroGroupsRect(View.ViewRect, MacroGroupDatas);

	// Snap the rect onto thread group boundary
	const FIntPoint GroupSize = GetVendorOptimalGroupSize2D();
	TotalRect.Min.X = FMath::FloorToInt(float(TotalRect.Min.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Min.Y = FMath::FloorToInt(float(TotalRect.Min.Y) / float(GroupSize.Y)) * GroupSize.Y;
	TotalRect.Max.X = FMath::CeilToInt(float(TotalRect.Max.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Max.Y = FMath::CeilToInt(float(TotalRect.Max.Y) / float(GroupSize.Y)) * GroupSize.Y;

	FIntPoint RectResolution(TotalRect.Width(), TotalRect.Height());
	PassParameters->ResolutionOffset = FIntPoint(TotalRect.Min.X, TotalRect.Min.Y);

	TShaderMapRef<FHairVisibilityPrimitiveIdCompactionCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVisibilityCompaction"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(RectResolution, GroupSize));

	OutIndirectArgsBuffer = AddCopyIndirectArgPass(GraphBuilder, &View, NodeGroupSize, 1, OutCompactCounter);
	OutMaxRenderNodeCount = MaxRenderNodeCount;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityCompactionComputeRasterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityCompactionComputeRasterCS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityCompactionComputeRasterCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUPSIZE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(uint32, MaxNodeCount)
		SHADER_PARAMETER(uint32, SamplerPerPixel)
		SHADER_PARAMETER(float, CoverageThreshold)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisibilityTexture0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisibilityTexture1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisibilityTexture2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisibilityTexture3)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ViewTransmittanceTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCompactNodeCounter)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCompactNodeIndex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutCategorizationTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, OutCompactNodeData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutCompactNodeCoord)

		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityCompactionComputeRasterCS, "/Engine/Private/HairStrands/HairStrandsVisibilityCompactionComputeRaster.usf", "MainCS", SF_Compute);

static void AddHairVisibilityCompactionComputeRasterPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const uint32 NodeGroupSize,
	const uint32 SamplerPerPixel,
	const FRasterComputeOutput& RasterComputeData,
	FRDGTextureRef& InTransmittanceTexture,
	FRDGTextureRef& OutCompactCounter,
	FRDGTextureRef& OutCompactNodeIndex,
	FRDGBufferRef&  OutCompactNodeData,
	FRDGBufferRef&  OutCompactNodeCoord,
	FRDGTextureRef& OutCategorizationTexture,
	FRDGTextureRef& OutVelocityTexture,
	FRDGBufferRef&  OutIndirectArgsBuffer,
	uint32& OutMaxRenderNodeCount)
{	
	FIntPoint Resolution = RasterComputeData.VisibilityTexture0->Desc.Extent;

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(1,1), PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV);
		OutCompactCounter = GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityCompactCounter"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCompactNodeIndex = GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityCompactNodeIndex"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R16G16B16A16_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCategorizationTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.CategorizationTexture"));
	}

	const uint32 ClearValues[4] = { 0u,0u,0u,0u };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactCounter), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactNodeIndex), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCategorizationTexture), ClearValues);

	// Select render node count according to current mode
	const uint32 MSAASampleCount = GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA_Visibility ? GetMaxSamplePerPixel() : 1;
	const uint32 PPLLMaxRenderNodePerPixel = GetMaxSamplePerPixel();
	const uint32 MaxRenderNodeCount = GetTotalSampleCountForAllocation(Resolution);
	OutCompactNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(HairStrandsVisibilityInternal::NodeVis), MaxRenderNodeCount), TEXT("Hair.VisibilityPrimitiveIdCompactNodeData"));
	OutCompactNodeCoord = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxRenderNodeCount), TEXT("Hair.VisibilityPrimitiveIdCompactNodeCoord"));

	FRDGTextureRef DefaultTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
	FHairVisibilityCompactionComputeRasterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityCompactionComputeRasterCS::FParameters>();
	PassParameters->VisibilityTexture0		= RasterComputeData.VisibilityTexture0;
	PassParameters->VisibilityTexture1		= SamplerPerPixel > 1 ? RasterComputeData.VisibilityTexture1 : DefaultTexture;
	PassParameters->VisibilityTexture2		= SamplerPerPixel > 2 ? RasterComputeData.VisibilityTexture2 : DefaultTexture;
	PassParameters->VisibilityTexture3		= SamplerPerPixel > 3 ? RasterComputeData.VisibilityTexture3 : DefaultTexture;
	PassParameters->SamplerPerPixel			= SamplerPerPixel;
	PassParameters->ViewTransmittanceTexture= InTransmittanceTexture;
	PassParameters->OutputResolution		= Resolution;
	PassParameters->MaxNodeCount			= MaxRenderNodeCount;
	PassParameters->CoverageThreshold		= FMath::Clamp(GHairStrandsFullCoverageThreshold, 0.1f, 1.f);
	PassParameters->ViewUniformBuffer		= View.ViewUniformBuffer;
	PassParameters->SceneTexturesStruct		= CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
	PassParameters->OutCompactNodeCounter	= GraphBuilder.CreateUAV(OutCompactCounter);
	PassParameters->OutCompactNodeIndex		= GraphBuilder.CreateUAV(OutCompactNodeIndex);
	PassParameters->OutCompactNodeData		= GraphBuilder.CreateUAV(OutCompactNodeData);
	PassParameters->OutCompactNodeCoord		= GraphBuilder.CreateUAV(OutCompactNodeCoord, FHairStrandsVisibilityData::NodeCoordFormat);
	PassParameters->OutCategorizationTexture= GraphBuilder.CreateUAV(OutCategorizationTexture);

	const FIntPoint GroupSize = GetVendorOptimalGroupSize2D();
	FHairVisibilityCompactionComputeRasterCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityCompactionComputeRasterCS::FGroupSize>(GetVendor() == HairVisibilityVendor_NVIDIA ? 0 : 1);
	TShaderMapRef<FHairVisibilityCompactionComputeRasterCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVisibilityCompaction"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(Resolution, GroupSize));

	OutIndirectArgsBuffer = AddCopyIndirectArgPass(GraphBuilder, &View, NodeGroupSize, 1, OutCompactCounter);
	OutMaxRenderNodeCount = MaxRenderNodeCount;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityFillOpaqueDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityFillOpaqueDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityFillOpaqueDepthPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VisibilityDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VisibilityIDTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityFillOpaqueDepthPS, "/Engine/Private/HairStrands/HairStrandsVisibilityFillOpaqueDepthPS.usf", "MainPS", SF_Pixel);

static FRDGTextureRef AddHairVisibilityFillOpaqueDepth(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FIntPoint& Resolution,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FRDGTextureRef& SceneDepthTexture)
{
	FRDGTextureRef OutVisibilityDepthTexture;
	{
		check(GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA_Visibility);

		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, 1, GetMaxSamplePerPixel());
		OutVisibilityDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityDepthTexture"));
	}

	FHairVisibilityFillOpaqueDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityFillOpaqueDepthPS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneDepthTexture = SceneDepthTexture;
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutVisibilityDepthTexture,
		ERenderTargetLoadAction::EClear,
		ERenderTargetLoadAction::ENoAction,
		FExclusiveDepthStencil::DepthWrite_StencilNop);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairVisibilityFillOpaqueDepthPS> PixelShader(View.ShaderMap);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;

	TArray<FIntRect> MacroGroupRects;
	if (IsHairStrandsViewRectOptimEnable())
	{
		for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas)
		{
			MacroGroupRects.Add(MacroGroupData.ScreenRect);
		}
	}
	else
	{
		MacroGroupRects.Add(Viewport);
	}

	{
		ClearUnusedGraphResources(PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVisibilityFillOpaqueDepth"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, MacroGroupRects, Viewport, Resolution](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);

			for (const FIntRect& ViewRect : MacroGroupRects)
			{
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
				DrawRectangle(
					RHICmdList,
					0, 0,
					Viewport.Width(), Viewport.Height(),
					Viewport.Min.X,   Viewport.Min.Y,
					Viewport.Width(), Viewport.Height(),
					Viewport.Size(),
					Resolution,
					VertexShader,
					EDRF_UseTriangleOptimization);
			}
		});
	}


	return OutVisibilityDepthTexture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void AddHairCulledVertexResourcesTransitionPass(
	FRDGBuilder& GraphBuilder,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas)
{
	FBufferTransitionQueue TransitionQueue;
	for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas)
	{
		for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroupData.PrimitivesInfos)
		{
			if (PrimitiveInfo.PublicDataPtr)
			{
				if (FUnorderedAccessViewRHIRef UAV = PrimitiveInfo.PublicDataPtr->CulledVertexIdBuffer.UAV)
				{
					TransitionQueue.Add(UAV);
				}

				if (FUnorderedAccessViewRHIRef UAV = PrimitiveInfo.PublicDataPtr->CulledVertexRadiusScaleBuffer.UAV)
				{
					TransitionQueue.Add(UAV);
				}
			}
		}
	}
	TransitBufferToReadable(GraphBuilder, TransitionQueue);
}

static void AddHairVisibilityCommonPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const EHairVisibilityRenderMode RenderMode,
	FVisibilityPassParameters* PassParameters,
	FInstanceCullingManager& InstanceCullingManager)
{
	auto GetPassName = [RenderMode]()
	{
		switch (RenderMode)
		{
		case HairVisibilityRenderMode_PPLL:						return RDG_EVENT_NAME("HairStrandsVisibilityPPLLPass");
		case HairVisibilityRenderMode_MSAA_Visibility:			return RDG_EVENT_NAME("HairStrandsVisibilityMSAAVisPass");
		case HairVisibilityRenderMode_Transmittance:			return RDG_EVENT_NAME("HairStrandsTransmittancePass");
		case HairVisibilityRenderMode_TransmittanceAndHairCount:return RDG_EVENT_NAME("HairStrandsTransmittanceAndHairCountPass");
		default:												return RDG_EVENT_NAME("Noname");
		}
	};

	AddHairCulledVertexResourcesTransitionPass(GraphBuilder, MacroGroupDatas);

	// Note: this reference needs to persistent until SubmitMeshDrawCommands() is called, as DrawRenderState does not ref count 
	// the view uniform buffer (raw pointer). It is only within the MeshProcessor that the uniform buffer get reference
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
	if (RenderMode == HairVisibilityRenderMode_Transmittance || RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount || RenderMode == HairVisibilityRenderMode_PPLL)
	{
		const bool bEnableMSAA = false;
		SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfoBits, ViewInfo->CachedViewUniformShaderParameters->HairComponents);

		// Create and set the uniform buffer
		PassParameters->View = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
	}
	else
	{
		PassParameters->View = ViewInfo->ViewUniformBuffer;
	}

	GraphBuilder.AddPass(
		GetPassName(),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Scene = Scene, ViewInfo, &MacroGroupDatas, RenderMode](FRHICommandListImmediate& RHICmdList)
	{
		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		FMeshPassProcessorRenderState DrawRenderState;

		{
			RHICmdList.SetViewport(0, 0, 0.0f, ViewInfo->ViewRect.Width(), ViewInfo->ViewRect.Height(), 1.0f);
			if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
			}
			else if (RenderMode == HairVisibilityRenderMode_Transmittance)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RED, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_Zero>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			}
			else if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<
					CW_RED, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_Zero,
					CW_RG, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_Zero>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			}
			else if (RenderMode == HairVisibilityRenderMode_PPLL)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			}

			FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage;
			FMeshCommandOneFrameArray VisibleMeshDrawCommands;
			FGraphicsMinimalPipelineStateSet PipelineStateSet;
			bool NeedsShaderInitialization;
			FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, PipelineStateSet, NeedsShaderInitialization);
			FHairVisibilityProcessor MeshProcessor(Scene, ViewInfo, DrawRenderState, RenderMode, &ShadowContext);
			
			for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas)
			{
				for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroupData.PrimitivesInfos)
				{
					const FMeshBatch& MeshBatch = *PrimitiveInfo.Mesh;
					const uint64 BatchElementMask = ~0ull;
					MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.PrimitiveSceneProxy, -1, MacroGroupData.MacroGroupId, PrimitiveInfo.MaterialId, PrimitiveInfo.IsCullingEnable());
				}
			}

			if (VisibleMeshDrawCommands.Num() > 0)
			{
				FRHIBuffer* PrimitiveIdVertexBuffer = nullptr;
				SortAndMergeDynamicPassMeshDrawCommands(ViewInfo->GetFeatureLevel(), VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1, ViewInfo->DynamicPrimitiveCollector.GetPrimitiveIdRange());
				SubmitMeshDrawCommands(VisibleMeshDrawCommands, PipelineStateSet, PrimitiveIdVertexBuffer, 0, false, 1, RHICmdList);
			}
		}
	});
}

static void AddHairVisibilityMSAAPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& Resolution,
	FInstanceCullingManager& InstanceCullingManager,
	FRDGTextureRef& OutVisibilityIdTexture,
	FRDGTextureRef& OutVisibilityDepthTexture)
{
	const uint32 MSAASampleCount = GetMaxSamplePerPixel();
	{
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding(EClearBinding::ENoneBound), TexCreate_NoFastClear | TexCreate_RenderTargetable | TexCreate_ShaderResource, 1, MSAASampleCount);
			OutVisibilityIdTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityIDTexture"));
		}

		AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAIdTexture"), ViewInfo, 0xFFFFFFFF, OutVisibilityIdTexture);

		FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
		PassParameters->UniformBuffer = CreatePassDummyTextures(GraphBuilder);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutVisibilityIdTexture, ERenderTargetLoadAction::ELoad, 0);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			OutVisibilityDepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
		AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, HairVisibilityRenderMode_MSAA_Visibility, PassParameters, InstanceCullingManager);
	}
}

static void AddHairVisibilityPPLLPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& Resolution,
	FInstanceCullingManager& InstanceCullingManager,
	FRDGTextureRef& InViewZDepthTexture,
	FRDGTextureRef& OutVisibilityPPLLNodeCounter,
	FRDGTextureRef& OutVisibilityPPLLNodeIndex,
	FRDGBufferRef&  OutVisibilityPPLLNodeData)
{
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(1,1), PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutVisibilityPPLLNodeCounter = GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityPPLLCounter"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutVisibilityPPLLNodeIndex = GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityPPLLNodeIndex"));
	}

	const FIntRect HairRect = ComputeVisibleHairStrandsMacroGroupsRect(ViewInfo->ViewRect, MacroGroupDatas);
	const FIntPoint EffectiveResolution(HairRect.Width(), HairRect.Height());

	const uint32 PPLLMaxTotalListElementCount = GetTotalSampleCountForAllocation(EffectiveResolution);
	{
		OutVisibilityPPLLNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(PPLLNodeData), PPLLMaxTotalListElementCount), TEXT("Hair.VisibilityPPLLNodeData"));
	}
	const uint32 ClearValue0[4] = { 0,0,0,0 };
	const uint32 ClearValueInvalid[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutVisibilityPPLLNodeCounter), ClearValue0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutVisibilityPPLLNodeIndex), ClearValueInvalid);

	FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();

	{
		FVisibilityPassUniformParameters* UniformParameters = GraphBuilder.AllocParameters<FVisibilityPassUniformParameters>();

		UniformParameters->PPLLCounter = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutVisibilityPPLLNodeCounter, 0));
		UniformParameters->PPLLNodeIndex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutVisibilityPPLLNodeIndex, 0));
		UniformParameters->PPLLNodeData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutVisibilityPPLLNodeData));
		UniformParameters->MaxPPLLNodeCount = PPLLMaxTotalListElementCount;

		PassParameters->UniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);
	}

	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(InViewZDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, HairVisibilityRenderMode_PPLL, PassParameters, InstanceCullingManager);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

struct FHairPrimaryTransmittance
{
	FRDGTextureRef TransmittanceTexture = nullptr;
	FRDGTextureRef HairCountTexture = nullptr;

	FRDGTextureRef HairCountTextureUint = nullptr;
	FRDGTextureRef DepthTextureUint = nullptr;
};

static FHairPrimaryTransmittance AddHairViewTransmittancePass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& Resolution,
	const bool bOutputHairCount,
	FRDGTextureRef SceneDepthTexture,
	FInstanceCullingManager& InstanceCullingManager)
{
	check(SceneDepthTexture->Desc.Extent == Resolution);
	const EHairVisibilityRenderMode RenderMode = bOutputHairCount ? HairVisibilityRenderMode_TransmittanceAndHairCount : HairVisibilityRenderMode_Transmittance;

	// Clear to transmittance 1
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_FLOAT, FClearValueBinding(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)), TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
	PassParameters->UniformBuffer = CreatePassDummyTextures(GraphBuilder);
	FHairPrimaryTransmittance Out;

	Out.TransmittanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.ViewTransmittanceTexture"));
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Out.TransmittanceTexture, ERenderTargetLoadAction::EClear, 0);

	if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
	{
		Desc.Format = PF_G32R32F;
		Desc.ClearValue = FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		Out.HairCountTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.ViewHairCountTexture"));
		PassParameters->RenderTargets[1] = FRenderTargetBinding(Out.HairCountTexture, ERenderTargetLoadAction::EClear, 0);
	}

	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		SceneDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ENoAction,
		FExclusiveDepthStencil::DepthRead_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, RenderMode, PassParameters, InstanceCullingManager);

	return Out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Inject depth information into the view hair count texture, to block opaque occluder
class FHairViewTransmittanceDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairViewTransmittanceDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FHairViewTransmittanceDepthPS, FGlobalShader);

	class FOutputFormat : SHADER_PERMUTATION_INT("PERMUTATION_OUTPUT_FORMAT", 2);
	using FPermutationDomain = TShaderPermutationDomain<FOutputFormat>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, DistanceThreshold)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FOutputFormat>() == 0)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_FLOAT);
		}
		else if (PermutationVector.Get<FOutputFormat>() == 1)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_G32R32F);
		}

	}
};

IMPLEMENT_GLOBAL_SHADER(FHairViewTransmittanceDepthPS, "/Engine/Private/HairStrands/HairStrandsVisibilityTransmittanceDepthPS.usf", "MainPS", SF_Pixel);

static void AddHairViewTransmittanceDepthPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& CategorizationTexture,
	const FRDGTextureRef& SceneDepthTexture,
	FRDGTextureRef& HairCountTexture)
{
	FHairViewTransmittanceDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairViewTransmittanceDepthPS::FParameters>();
	Parameters->DistanceThreshold = FMath::Max(1.f, GHairStrandsViewHairCountDepthDistanceThreshold);
	Parameters->CategorizationTexture = CategorizationTexture;
	Parameters->SceneDepthTexture = SceneDepthTexture;
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(HairCountTexture, ERenderTargetLoadAction::ELoad);

	FHairViewTransmittanceDepthPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairViewTransmittanceDepthPS::FOutputFormat>(HairCountTexture->Desc.Format == PF_G32R32F ? 1 : 0);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairViewTransmittanceDepthPS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = HairCountTexture->Desc.Extent;
	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsViewTransmittanceDepth"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
		DrawRectangle(
			RHICmdList,
			0, 0,
			Viewport.Width(), Viewport.Height(),
			Viewport.Min.X, Viewport.Min.Y,
			Viewport.Width(), Viewport.Height(),
			Viewport.Size(),
			Resolution,
			VertexShader,
			EDRF_UseTriangleOptimization);
	});
}


///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityDepthPS, FGlobalShader);

	class FOutputType : SHADER_PERMUTATION_INT("PERMUTATION_OUTPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FOutputType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairStrandsTilePassVS::FParameters, TileData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorisationTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_B8G8R8A8);
		OutEnvironment.SetRenderTargetOutputFormat(1, PF_B8G8R8A8);
		OutEnvironment.SetRenderTargetOutputFormat(2, PF_FloatRGBA);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityDepthPS, "/Engine/Private/HairStrands/HairStrandsVisibilityDepthPS.usf", "MainPS", SF_Pixel);

static void AddHairVisibilityCommonPatchPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsTiles& TileData,
	const bool bDepthOnly,
	const FRDGTextureRef& CategorisationTexture,
	FRDGTextureRef OutGBufferBTexture,
	FRDGTextureRef OutGBufferCTexture,
	FRDGTextureRef OutColorTexture,
	FRDGTextureRef OutDepthTexture)
{
	FHairVisibilityDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityDepthPS::FParameters>();
	Parameters->TileData = GetHairStrandsTileParameters(TileData);
	Parameters->CategorisationTexture = CategorisationTexture;
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthWrite_StencilNop);
	if (!bDepthOnly)
	{
		check(OutGBufferBTexture && OutGBufferCTexture && OutColorTexture);
		Parameters->RenderTargets[0] = FRenderTargetBinding(OutGBufferBTexture, ERenderTargetLoadAction::ELoad);
		Parameters->RenderTargets[1] = FRenderTargetBinding(OutGBufferCTexture, ERenderTargetLoadAction::ELoad);
		Parameters->RenderTargets[2] = FRenderTargetBinding(OutColorTexture, ERenderTargetLoadAction::ELoad);
	}

	TShaderMapRef<FPostProcessVS> ScreenVertexShader(View.ShaderMap);
	TShaderMapRef<FHairStrandsTilePassVS> TileVertexShader(View.ShaderMap);

	FHairVisibilityDepthPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityDepthPS::FOutputType>(bDepthOnly ? 1 : 0);
	TShaderMapRef<FHairVisibilityDepthPS> PixelShader(View.ShaderMap, PermutationVector);
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutDepthTexture->Desc.Extent;
	const bool bUseTile = TileData.IsValid();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("%s(%s)", bDepthOnly ? TEXT("HairStrandsVisibilityHairOnlyDepth") : TEXT("HairStrandsVisibilityWriteColorAndDepth"), bUseTile ? TEXT("Tile") : TEXT("Screen")),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, ScreenVertexShader, TileVertexShader, PixelShader, Viewport, Resolution, bUseTile](FRHICommandList& RHICmdList)
	{
		FHairStrandsTilePassVS::FParameters ParametersVS = Parameters->TileData;

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = bUseTile ? TileVertexShader.GetVertexShader() : ScreenVertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = Parameters->TileData.bRectPrimitive > 0 ? PT_RectList : PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);

		if (bUseTile)
		{
			SetShaderParameters(RHICmdList, TileVertexShader, TileVertexShader.GetVertexShader(), ParametersVS);
//			RHICmdList.SetViewport(0, 0, 0.0f, Resolution.X, Resolution.Y, 1.0f);
			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitiveIndirect(Parameters->TileData.TileIndirectBuffer->GetRHI(), 0);
		}
		else
		{
			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				ScreenVertexShader,
				EDRF_UseTriangleOptimization);
		}
	});
}


static void AddHairVisibilityColorAndDepthPatchPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsTiles& TileData,
	const FRDGTextureRef& CategorisationTexture,
	FRDGTextureRef& OutGBufferBTexture,
	FRDGTextureRef& OutGBufferCTexture,
	FRDGTextureRef& OutColorTexture,
	FRDGTextureRef& OutDepthTexture)
{
	if (!OutGBufferBTexture || !OutGBufferCTexture || !OutColorTexture || !OutDepthTexture)
	{
		return;
	}

	AddHairVisibilityCommonPatchPass(
		GraphBuilder,
		View,
		TileData,
		false,
		CategorisationTexture,
		OutGBufferBTexture,
		OutGBufferCTexture,
		OutColorTexture,
		OutDepthTexture);
}

static void AddHairOnlyDepthPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsTiles& TileData,
	const FRDGTextureRef& CategorisationTexture,
	FRDGTextureRef& OutDepthTexture)
{
	if (!OutDepthTexture)
	{
		return;
	}
	AddHairVisibilityCommonPatchPass(
		GraphBuilder,
		View,
		TileData,
		true,
		CategorisationTexture,
		nullptr,
		nullptr,
		nullptr,
		OutDepthTexture);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairCountToCoverageCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairCountToCoverageCS);
	SHADER_USE_PARAMETER_STRUCT(FHairCountToCoverageCS, FGlobalShader);

	class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FInputType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(float, LUT_HairCount)
		SHADER_PARAMETER(float, LUT_HairRadiusCount)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCoverageLUT)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairCountToCoverageCS, "/Engine/Private/HairStrands/HairStrandsCoverage.usf", "MainCS", SF_Compute);

static FRDGTextureRef AddHairHairCountToTransmittancePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& ViewInfo,
	const FRDGTextureRef HairCountTexture)
{
	const FIntPoint OutputResolution = HairCountTexture->Desc.Extent;

	check(HairCountTexture->Desc.Format == PF_R32_UINT || HairCountTexture->Desc.Format == PF_G32R32F)
	const bool bUseOneChannel = HairCountTexture->Desc.Format == PF_R32_UINT;

	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(OutputResolution, PF_R32_FLOAT, FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f)), TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable);
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("Hair.VisibilityTexture"));
	FRDGTextureRef HairCoverageLUT = GetHairLUT(GraphBuilder, ViewInfo, HairLUTType_Coverage);

	FHairCountToCoverageCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairCountToCoverageCS::FParameters>();
	PassParameters->LUT_HairCount = HairCoverageLUT->Desc.Extent.X;
	PassParameters->LUT_HairRadiusCount = HairCoverageLUT->Desc.Extent.Y;
	PassParameters->OutputResolution = OutputResolution;
	PassParameters->HairCoverageLUT = HairCoverageLUT;
	PassParameters->HairCountTexture = HairCountTexture;
	PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

	FHairCountToCoverageCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairCountToCoverageCS::FInputType>(bUseOneChannel ? 1 : 0);
	TShaderMapRef<FHairCountToCoverageCS> ComputeShader(ViewInfo.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairCountToTransmittancePass"), ComputeShader, PassParameters, FComputeShaderUtils::GetGroupCount(OutputResolution, FIntPoint(8,8)));

	return OutputTexture;
}

// Transit resources used during the MeshDraw passes
static void AddMeshDrawTransitionPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas)
{
	for (const FHairStrandsMacroGroupData& MacroGroup : MacroGroupDatas)
	{
		for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroup.PrimitivesInfos)
		{
			FHairGroupPublicData* HairGroupPublicData = reinterpret_cast<FHairGroupPublicData*>(PrimitiveInfo.Mesh->Elements[0].VertexFactoryUserData);
			check(HairGroupPublicData);

			FRDGResourceAccessFinalizer ResourceAccessFinalizer;

			FHairGroupPublicData::FVertexFactoryInput& VFInput = HairGroupPublicData->VFInput;
			ResourceAccessFinalizer.AddBuffer(VFInput.Strands.PositionBuffer.Buffer,				ERHIAccess::SRVMask);
			ResourceAccessFinalizer.AddBuffer(VFInput.Strands.PrevPositionBuffer.Buffer,			ERHIAccess::SRVMask);
			ResourceAccessFinalizer.AddBuffer(VFInput.Strands.TangentBuffer.Buffer,				ERHIAccess::SRVMask);
			ResourceAccessFinalizer.AddBuffer(VFInput.Strands.AttributeBuffer.Buffer,				ERHIAccess::SRVMask);
			ResourceAccessFinalizer.AddBuffer(VFInput.Strands.MaterialBuffer.Buffer,				ERHIAccess::SRVMask);
			ResourceAccessFinalizer.AddBuffer(VFInput.Strands.PositionOffsetBuffer.Buffer,		ERHIAccess::SRVMask);
			ResourceAccessFinalizer.AddBuffer(VFInput.Strands.PrevPositionOffsetBuffer.Buffer,	ERHIAccess::SRVMask);

			FRDGBufferRef CulledVertexIdBuffer = Register(GraphBuilder, HairGroupPublicData->CulledVertexIdBuffer, ERDGImportedBufferFlags::None).Buffer;
			FRDGBufferRef CulledVertexRadiusScaleBuffer = Register(GraphBuilder, HairGroupPublicData->CulledVertexRadiusScaleBuffer, ERDGImportedBufferFlags::None).Buffer;
			FRDGBufferRef DrawIndirectBuffer = Register(GraphBuilder, HairGroupPublicData->DrawIndirectBuffer, ERDGImportedBufferFlags::None).Buffer;
			ResourceAccessFinalizer.AddBuffer(CulledVertexIdBuffer, ERHIAccess::SRVMask);
			ResourceAccessFinalizer.AddBuffer(CulledVertexRadiusScaleBuffer, ERHIAccess::SRVMask);
			ResourceAccessFinalizer.AddBuffer(DrawIndirectBuffer, ERHIAccess::IndirectArgs);

			ResourceAccessFinalizer.Finalize(GraphBuilder);

			VFInput.Strands.PositionBuffer				= FRDGImportedBuffer();
			VFInput.Strands.PrevPositionBuffer			= FRDGImportedBuffer();
			VFInput.Strands.TangentBuffer				= FRDGImportedBuffer();
			VFInput.Strands.AttributeBuffer				= FRDGImportedBuffer();
			VFInput.Strands.MaterialBuffer				= FRDGImportedBuffer();
			VFInput.Strands.PositionOffsetBuffer		= FRDGImportedBuffer();
			VFInput.Strands.PrevPositionOffsetBuffer	= FRDGImportedBuffer();
		}
	}
}
///////////////////////////////////////////////////////////////////////////////////////////////////

class FVisiblityRasterComputeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisiblityRasterComputeCS);
	SHADER_USE_PARAMETER_STRUCT(FVisiblityRasterComputeCS, FGlobalShader);

	class FRasterAtomic : SHADER_PERMUTATION_INT("PERMUTATION_RASTER_ATOMIC", 4);
	class FSPP : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_SPP", 1, 2, 4); 
	class FCulling : SHADER_PERMUTATION_BOOL("PERMUTATION_CULLING");
	class FStochastic : SHADER_PERMUTATION_BOOL("PERMUTATION_STOCHASTIC");
	using FPermutationDomain = TShaderPermutationDomain<FRasterAtomic, FSPP, FCulling, FStochastic>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(uint32, MaxRasterCount)
		SHADER_PARAMETER(uint32, FrameIdMod8)
		SHADER_PARAMETER(uint32, HairMaterialId)
		SHADER_PARAMETER(uint32, ResolutionMultiplier)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(uint32, HairStrandsVF_bIsCullingEnable)
		SHADER_PARAMETER(float, HairStrandsVF_Density)
		SHADER_PARAMETER(float, HairStrandsVF_Radius)
		SHADER_PARAMETER(float, HairStrandsVF_RootScale)
		SHADER_PARAMETER(float, HairStrandsVF_TipScale)
		SHADER_PARAMETER(float, HairStrandsVF_Length)
		SHADER_PARAMETER(uint32, HairStrandsVF_bUseStableRasterization)
		SHADER_PARAMETER(uint32, HairStrandsVF_VertexCount)
		SHADER_PARAMETER(FMatrix, HairStrandsVF_LocalToWorldPrimitiveTransform)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairStrandsVF_PositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairStrandsVF_PositionOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairStrandsVF_CullingIndirectBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairStrandsVF_CullingIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairStrandsVF_CullingRadiusScaleBuffer)
		RDG_BUFFER_ACCESS(IndirectBufferArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutHairCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutVisibilityTexture0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutVisibilityTexture1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutVisibilityTexture2)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutVisibilityTexture3)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		//if (!FDataDrivenShaderPlatformInfo::GetSupportsUInt64ImageAtomics(Parameters.Platform))
		//{
		//	return false;
		//}
		if (IsVulkanPlatform(Parameters.Platform))
		{
			return false;
		}

		if (!IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform))
		{
			return false;
		}

		if (IsPCPlatform(Parameters.Platform))
		{
			FPermutationDomain PermutationVector(Parameters.PermutationId);
			return PermutationVector.Get<FRasterAtomic>() != 0;
		}
		else
		{
			FPermutationDomain PermutationVector(Parameters.PermutationId);
			return PermutationVector.Get<FRasterAtomic>() == 0;
		}
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_RASTERCOMPUTE"), 1);
		// Need to force optimization for driver injection to work correctly.
		// https://developer.nvidia.com/unlocking-gpu-intrinsics-hlsl
		// https://gpuopen.com/gcn-shader-extensions-for-direct3d-and-vulkan/
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FRasterAtomic>() == 3) // AMD, DX12
		{
			// Force shader model 6.0+
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisiblityRasterComputeCS, "/Engine/Private/HairStrands/HairStrandsVisibilityRasterCompute.usf", "MainCS", SF_Compute);

static FRasterComputeOutput AddVisibilityComputeRasterPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& InResolution,
	const uint32 SamplePerPixelCount,
	const FRDGTextureRef SceneDepthTexture)
{	
	check(DoesSupportRasterCompute());

	FRasterComputeOutput Out;

	Out.ResolutionMultiplier = 1;
	Out.BaseResolution		 = InResolution;
	Out.SuperResolution		 = InResolution * Out.ResolutionMultiplier;
	Out.VisibilityTexture0	 = nullptr;
	Out.VisibilityTexture1	 = nullptr;
	Out.VisibilityTexture2	 = nullptr;
	Out.VisibilityTexture3	 = nullptr;

	FRDGTextureDesc DescCount = FRDGTextureDesc::Create2D(Out.SuperResolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable);
	FRDGTextureDesc DescVis   = FRDGTextureDesc::Create2D(Out.SuperResolution, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable);
	FRDGTextureUAVRef VisibilityTexture0UAV = nullptr;
	FRDGTextureUAVRef VisibilityTexture1UAV = nullptr;
	FRDGTextureUAVRef VisibilityTexture2UAV = nullptr;
	FRDGTextureUAVRef VisibilityTexture3UAV = nullptr;

	uint32 ClearValues[4] = { 0,0,0,0 };
	Out.HairCountTexture = GraphBuilder.CreateTexture(DescCount, TEXT("Hair.ViewTransmittanceTexture"));
	FRDGTextureUAVRef HairCountTextureUAV = GraphBuilder.CreateUAV(Out.HairCountTexture);
	AddClearUAVPass(GraphBuilder, HairCountTextureUAV, ClearValues);

	Out.VisibilityTexture0 = GraphBuilder.CreateTexture(DescVis, TEXT("Hair.VisibilityTexture0"));
	VisibilityTexture0UAV = GraphBuilder.CreateUAV(Out.VisibilityTexture0);
	AddClearUAVPass(GraphBuilder, VisibilityTexture0UAV, ClearValues);
	if (SamplePerPixelCount > 1)
	{
		Out.VisibilityTexture1 = GraphBuilder.CreateTexture(DescVis, TEXT("Hair.VisibilityTexture1"));
		VisibilityTexture1UAV = GraphBuilder.CreateUAV(Out.VisibilityTexture1);
		AddClearUAVPass(GraphBuilder, VisibilityTexture1UAV, ClearValues);
		if (SamplePerPixelCount > 2)
		{
			Out.VisibilityTexture2 = GraphBuilder.CreateTexture(DescVis, TEXT("Hair.VisibilityTexture2"));
			VisibilityTexture2UAV = GraphBuilder.CreateUAV(Out.VisibilityTexture2);
			AddClearUAVPass(GraphBuilder, VisibilityTexture2UAV, ClearValues);
			if (SamplePerPixelCount > 3)
			{
				Out.VisibilityTexture3 = GraphBuilder.CreateTexture(DescVis, TEXT("Hair.VisibilityTexture3"));
				VisibilityTexture3UAV = GraphBuilder.CreateUAV(Out.VisibilityTexture3);
				AddClearUAVPass(GraphBuilder, VisibilityTexture3UAV, ClearValues);
			}
		}
	}

	// Create and set the uniform buffer
	const bool bStochasticRaster = GHairVisibilityComputeRaster_Stochastic > 0;
	const bool bEnableMSAA = false;
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
	SetUpViewHairRenderInfo(ViewInfo, bEnableMSAA, ViewInfo.CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo.CachedViewUniformShaderParameters->HairRenderInfoBits, ViewInfo.CachedViewUniformShaderParameters->HairComponents);
	ViewUniformShaderParameters = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);

	const uint32 FrameIdMode8 = ViewInfo.ViewState ? (ViewInfo.ViewState->GetFrameIndex() % 8) : 0;
	const uint32 GroupSize = 32;
	const uint32 DispatchCountX = 64;

	FVisiblityRasterComputeCS::FPermutationDomain PermutationVector0;
	FVisiblityRasterComputeCS::FPermutationDomain PermutationVector1;
#if PLATFORM_WINDOWS
	if (IsRHIDeviceNVIDIA())
	{
		PermutationVector0.Set<FVisiblityRasterComputeCS::FRasterAtomic>(1);
	}
	else if (IsRHIDeviceAMD())
	{
		static const bool bIsDx12 = FCString::Strcmp(GDynamicRHI->GetName(), TEXT("D3D12")) == 0;
		PermutationVector0.Set<FVisiblityRasterComputeCS::FRasterAtomic>(bIsDx12 ? 2 : 3);
	}
#else
	{
		PermutationVector0.Set<FVisiblityRasterComputeCS::FRasterAtomic>(0);
	}
#endif
	PermutationVector0.Set<FVisiblityRasterComputeCS::FStochastic>(bStochasticRaster);
	PermutationVector0.Set<FVisiblityRasterComputeCS::FSPP>(SamplePerPixelCount);
	PermutationVector1 = PermutationVector0; 

	PermutationVector0.Set<FVisiblityRasterComputeCS::FCulling>(false);
	PermutationVector1.Set<FVisiblityRasterComputeCS::FCulling>(true);
	TShaderMapRef<FVisiblityRasterComputeCS> ComputeShader_CullingOff(ViewInfo.ShaderMap, PermutationVector0);
	TShaderMapRef<FVisiblityRasterComputeCS> ComputeShader_CullingOn (ViewInfo.ShaderMap, PermutationVector1);

	for (const FHairStrandsMacroGroupData& MacroGroup : MacroGroupDatas)
	{
		const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitiveSceneInfos = MacroGroup.PrimitivesInfos;

		for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : PrimitiveSceneInfos)
		{
			FVisiblityRasterComputeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisiblityRasterComputeCS::FParameters>();
			PassParameters->OutputResolution = Out.SuperResolution;
			PassParameters->ResolutionMultiplier = Out.ResolutionMultiplier;
			PassParameters->MacroGroupId = MacroGroup.MacroGroupId;
			PassParameters->DispatchCountX = DispatchCountX;
			PassParameters->MaxRasterCount = FMath::Clamp(GHairVisibilityComputeRaster_MaxPixelCount, 1, 256);			
			PassParameters->FrameIdMod8 = FrameIdMode8;
			PassParameters->HairMaterialId = PrimitiveInfo.MaterialId;
			PassParameters->ViewUniformBuffer = ViewUniformShaderParameters;
			PassParameters->SceneDepthTexture = SceneDepthTexture;
			PassParameters->OutHairCountTexture = HairCountTextureUAV;
			PassParameters->OutVisibilityTexture0 = VisibilityTexture0UAV;
			PassParameters->OutVisibilityTexture1 = VisibilityTexture1UAV;
			PassParameters->OutVisibilityTexture2 = VisibilityTexture2UAV;
			PassParameters->OutVisibilityTexture3 = VisibilityTexture3UAV;
			
			check(PrimitiveInfo.Mesh && PrimitiveInfo.Mesh->Elements.Num() > 0);
			const FHairGroupPublicData* HairGroupPublicData = reinterpret_cast<const FHairGroupPublicData*>(PrimitiveInfo.Mesh->Elements[0].VertexFactoryUserData);
			check(HairGroupPublicData);

			const FHairGroupPublicData::FVertexFactoryInput& VFInput = HairGroupPublicData->VFInput;
			PassParameters->HairStrandsVF_PositionBuffer	= VFInput.Strands.PositionBuffer.SRV;
			PassParameters->HairStrandsVF_PositionOffsetBuffer = VFInput.Strands.PositionOffsetBuffer.SRV;
			PassParameters->HairStrandsVF_VertexCount		= VFInput.Strands.VertexCount;
			PassParameters->HairStrandsVF_Radius			= VFInput.Strands.HairRadius;
			PassParameters->HairStrandsVF_RootScale			= VFInput.Strands.HairRootScale;
			PassParameters->HairStrandsVF_TipScale			= VFInput.Strands.HairTipScale;
			PassParameters->HairStrandsVF_Length			= VFInput.Strands.HairLength;
			PassParameters->HairStrandsVF_bUseStableRasterization = VFInput.Strands.bUseStableRasterization ? 1 : 0;
			PassParameters->HairStrandsVF_Density			= VFInput.Strands.HairDensity;
			PassParameters->HairStrandsVF_LocalToWorldPrimitiveTransform = VFInput.LocalToWorldTransform.ToMatrixWithScale();

			const bool bCullingEnable = HairGroupPublicData->GetCullingResultAvailable();
			if (bCullingEnable)
			{
				FRDGImportedBuffer CullingIndirectBuffer = Register(GraphBuilder, HairGroupPublicData->GetDrawIndirectRasterComputeBuffer(), ERDGImportedBufferFlags::CreateSRV);
				PassParameters->HairStrandsVF_CullingIndirectBuffer		= CullingIndirectBuffer.SRV;
				PassParameters->HairStrandsVF_bIsCullingEnable			= bCullingEnable ? 1 : 0;
				PassParameters->HairStrandsVF_CullingIndexBuffer		= RegisterAsSRV(GraphBuilder, HairGroupPublicData->GetCulledVertexIdBuffer());
				PassParameters->HairStrandsVF_CullingRadiusScaleBuffer	= RegisterAsSRV(GraphBuilder, HairGroupPublicData->GetCulledVertexRadiusScaleBuffer());
				PassParameters->IndirectBufferArgs						= CullingIndirectBuffer.Buffer;

				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsVisibilityComputeRaster(culling=on)"), ComputeShader_CullingOn, PassParameters, CullingIndirectBuffer.Buffer, 0);
			}
			else
			{
				const uint32 DispatchCountY = FMath::CeilToInt(PassParameters->HairStrandsVF_VertexCount / float(GroupSize * DispatchCountX));
				const FIntVector DispatchCount(DispatchCountX, DispatchCountY, 1);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsVisibilityComputeRaster(culling=off)"), ComputeShader_CullingOff, PassParameters, DispatchCount);
			}
		}
	}

	return Out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool GetHairStrandsSkyLightingEnable();

void RenderHairStrandsVisibilityBuffer(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	FViewInfo& View,
	FRDGTextureRef SceneGBufferATexture,
	FRDGTextureRef SceneGBufferBTexture,
	FRDGTextureRef SceneGBufferCTexture,
	FRDGTextureRef SceneGBufferDTexture,
	FRDGTextureRef SceneGBufferETexture,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture,
	FRDGTextureRef SceneVelocityTexture,
	FInstanceCullingManager& InstanceCullingManager)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_CLM_RenderHairStrandsVisibility);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsVisibility");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsVisibility);

	FHairStrandsMacroGroupDatas& MacroGroupDatas = View.HairStrandsViewData.MacroGroupDatas;
	check(View.Family);
	check(MacroGroupDatas.Num() > 0);

	const bool bGenerateTile = GHairStrandsTile > 0;

	const FIntRect HairRect = ComputeVisibleHairStrandsMacroGroupsRect(View.ViewRect, MacroGroupDatas);
	const int32 HairPixelCount = HairRect.Width() * HairRect.Height();
	if (HairPixelCount <= 0)
	{
		View.HairStrandsViewData.VisibilityData = FHairStrandsVisibilityData();
		return;
	}

	{
		
		{
			FHairStrandsVisibilityData& VisibilityData = View.HairStrandsViewData.VisibilityData;
			VisibilityData.NodeGroupSize = GetVendorOptimalGroupSize1D();
			VisibilityData.MaxSampleCount = GetMaxSamplePerPixel();

			// Use the scene color for computing target resolution as the View.ViewRect, 
			// doesn't include the actual resolution padding which make buffer size 
			// mismatch, and create artifact (e.g. velocity computation)
			check(SceneDepthTexture);
			const FIntPoint Resolution = SceneDepthTexture->Desc.Extent;

			const bool bRunColorAndDepthPatching = SceneGBufferBTexture && SceneColorTexture;
			const EHairVisibilityRenderMode RenderMode = GetHairVisibilityRenderMode();
			check(RenderMode == HairVisibilityRenderMode_MSAA_Visibility || RenderMode == HairVisibilityRenderMode_PPLL || RenderMode == HairVisibilityRenderMode_ComputeRaster);

			FRDGTextureRef HairOnlyDepthTexture = GraphBuilder.CreateTexture(SceneDepthTexture->Desc, TEXT("Hair.HairOnlyDepthTexture"));
			FRDGTextureRef CategorizationTexture = nullptr;
			FRDGTextureRef CompactNodeIndex = nullptr;
			FRDGBufferRef  CompactNodeData = nullptr;
			FRDGTextureRef NodeCounter = nullptr;

			if (RenderMode != HairVisibilityRenderMode_ComputeRaster)
			{
				AddMeshDrawTransitionPass(GraphBuilder, View, MacroGroupDatas);
			}

			if (RenderMode == HairVisibilityRenderMode_ComputeRaster)
			{
				FRasterComputeOutput RasterOutput = AddVisibilityComputeRasterPass(
					GraphBuilder,
					View,
					MacroGroupDatas,
					Resolution,
					VisibilityData.MaxSampleCount,
					SceneDepthTexture);

				// Merge this pass within the compaction pass
				FHairPrimaryTransmittance ViewTransmittance;
				{
					ViewTransmittance.TransmittanceTexture = AddHairHairCountToTransmittancePass(
						GraphBuilder,
						View,
						RasterOutput.HairCountTexture);

					ViewTransmittance.HairCountTextureUint = RasterOutput.HairCountTexture;
					VisibilityData.ViewHairCountUintTexture = ViewTransmittance.HairCountTextureUint;
				}

				const bool bUseComplexPath = IsHairStrandsComplexLightingEnabled();
				if (bUseComplexPath)
				{
					{
						FRDGBufferRef CompactNodeCoord;
						FRDGBufferRef IndirectArgsBuffer;
						FRDGTextureRef ResolveMaskTexture = nullptr;
						AddHairVisibilityCompactionComputeRasterPass(
							GraphBuilder,
							View,
							VisibilityData.NodeGroupSize,
							VisibilityData.MaxSampleCount,
							RasterOutput,
							ViewTransmittance.TransmittanceTexture,
							NodeCounter,
							CompactNodeIndex,
							CompactNodeData,
							CompactNodeCoord,
							CategorizationTexture,
							SceneVelocityTexture,
							IndirectArgsBuffer,
							VisibilityData.MaxNodeCount);

						// Generate Tile data
						if (CategorizationTexture && bGenerateTile)
						{
							VisibilityData.TileData = AddHairStrandsGenerateTilesPass(GraphBuilder, View, CategorizationTexture);
						}

						// Evaluate material based on the visiblity pass result
						// Output both complete sample data + per-sample velocity
						FMaterialPassOutput PassOutput = AddHairMaterialPass(
							GraphBuilder,
							Scene,
							&View,
							false,
							MacroGroupDatas,
							InstanceCullingManager,
							VisibilityData.NodeGroupSize,
							CompactNodeIndex,
							CompactNodeData,
							CompactNodeCoord,
							NodeCounter,
							IndirectArgsBuffer);

						// Merge per-sample velocity into the scene velocity buffer
						AddHairVelocityPass(
							GraphBuilder,
							View,
							MacroGroupDatas,
							CategorizationTexture,
							CompactNodeIndex,
							CompactNodeData,
							PassOutput.NodeVelocity,
							SceneVelocityTexture,
							ResolveMaskTexture);

						CompactNodeData = PassOutput.NodeData;

						// Allocate buffer for storing all the light samples
						FRDGTextureRef SampleLightingBuffer = AddClearLightSamplePass(GraphBuilder, &View, VisibilityData.MaxNodeCount, NodeCounter);
						VisibilityData.SampleLightingViewportResolution = SampleLightingBuffer->Desc.Extent;

						VisibilityData.SampleLightingBuffer = SampleLightingBuffer;
						VisibilityData.NodeIndex = CompactNodeIndex;
						VisibilityData.CategorizationTexture = CategorizationTexture;
						VisibilityData.HairOnlyDepthTexture = HairOnlyDepthTexture;
						VisibilityData.NodeData = CompactNodeData;
						VisibilityData.NodeCoord = CompactNodeCoord;
						VisibilityData.NodeIndirectArg = IndirectArgsBuffer;
						VisibilityData.NodeCount = NodeCounter;
						VisibilityData.ResolveMaskTexture = ResolveMaskTexture;	
						VisibilityData.EmissiveTexture = PassOutput.EmissiveTexture;
					}

					// For fully covered pixels, write: 
					// * black color into the scene color
					// * closest depth
					// * unlit shading model ID 
					if (bRunColorAndDepthPatching)
					{
						AddHairVisibilityColorAndDepthPatchPass(
							GraphBuilder,
							View,
							VisibilityData.TileData,
							CategorizationTexture,
							SceneGBufferBTexture,
							SceneGBufferCTexture,
							SceneColorTexture,
							SceneDepthTexture);
					}

					AddHairOnlyDepthPass(
						GraphBuilder,
						View,
						VisibilityData.TileData,
						CategorizationTexture,
						HairOnlyDepthTexture);
				}
				else
				{
					AddHairMaterialGBufferPass(
						GraphBuilder,
						Scene,
						&View,
						InstanceCullingManager,
						MacroGroupDatas,

						ViewTransmittance.TransmittanceTexture,
						RasterOutput.VisibilityTexture0,
						nullptr,
						nullptr,

						SceneGBufferATexture,
						SceneGBufferBTexture,
						SceneGBufferCTexture,
						SceneGBufferDTexture,
						SceneGBufferETexture,

						SceneColorTexture,
						SceneDepthTexture,
						SceneVelocityTexture);

					ViewTransmittance.HairCountTexture = nullptr;
				}
			}
			else if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility)
			{
				// Run the view transmittance pass if needed (not in PPLL mode that is already a high quality render path)
				FHairPrimaryTransmittance ViewTransmittance;
				if (GHairStrandsViewTransmittancePassEnable > 0 && RenderMode != HairVisibilityRenderMode_PPLL)
				{
					// Note: Hair count is required for the sky lighting at the moment as it is used for the TT term
					// TT sampling is disable in hair sky lighting integrator 0. So the GetHairStrandsSkyLightingEnable() check is no longer needed
					const bool bOutputHairCount = GHairStrandsHairCountToTransmittance > 0;
					ViewTransmittance = AddHairViewTransmittancePass(
						GraphBuilder,
						Scene,
						&View,
						MacroGroupDatas,
						Resolution,
						bOutputHairCount,
						SceneDepthTexture,
						InstanceCullingManager);

					const bool bHairCountToTransmittance = GHairStrandsHairCountToTransmittance > 0;
					if (bHairCountToTransmittance)
					{
						ViewTransmittance.TransmittanceTexture = AddHairHairCountToTransmittancePass(
							GraphBuilder,
							View,
							ViewTransmittance.HairCountTexture);
					}

				}

				struct FRDGMsaaVisibilityResources
				{
					FRDGTextureRef DepthTexture;
					FRDGTextureRef IdTexture;
				} MsaaVisibilityResources;

				MsaaVisibilityResources.DepthTexture = AddHairVisibilityFillOpaqueDepth(
					GraphBuilder,
					View,
					Resolution,
					MacroGroupDatas,
					SceneDepthTexture);

				AddHairVisibilityMSAAPass(
					GraphBuilder,
					Scene,
					&View,
					MacroGroupDatas,
					Resolution,
					InstanceCullingManager,
					MsaaVisibilityResources.IdTexture,
					MsaaVisibilityResources.DepthTexture);

				// This is used when compaction is not enabled.
				VisibilityData.MaxSampleCount = MsaaVisibilityResources.IdTexture->Desc.NumSamples;
				VisibilityData.HairOnlyDepthTexture = HairOnlyDepthTexture;
				
				const bool bUseComplexPath = IsHairStrandsComplexLightingEnabled();
				if (bUseComplexPath)
				{
					FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityPrimitiveIdCompactionCS::FParameters>();
					PassParameters->MSAA_DepthTexture = MsaaVisibilityResources.DepthTexture;
					PassParameters->MSAA_IDTexture = MsaaVisibilityResources.IdTexture;
					PassParameters->ViewTransmittanceTexture = ViewTransmittance.TransmittanceTexture;

					FRDGBufferRef CompactNodeCoord;
					FRDGBufferRef IndirectArgsBuffer;
					FRDGTextureRef ResolveMaskTexture = nullptr;
					FRDGTextureRef EmissiveTexture = nullptr;
					AddHairVisibilityPrimitiveIdCompactionPass(
						false, // bUsePPLL
						GraphBuilder,
						View,
						MacroGroupDatas,
						VisibilityData.NodeGroupSize,
						PassParameters,
						NodeCounter,
						CompactNodeIndex,
						CompactNodeData,
						CompactNodeCoord,
						CategorizationTexture,
						SceneVelocityTexture,
						IndirectArgsBuffer,
						VisibilityData.MaxNodeCount);

					// Generate Tile data
					if (CategorizationTexture && bGenerateTile)
					{
						VisibilityData.TileData = AddHairStrandsGenerateTilesPass(GraphBuilder, View, CategorizationTexture);
					}

					{
						const bool bUpdateSampleCoverage = GHairStrandsSortHairSampleByDepth > 0;

						// Evaluate material based on the visiblity pass result
						// Output both complete sample data + per-sample velocity
						FMaterialPassOutput PassOutput = AddHairMaterialPass(
							GraphBuilder,
							Scene,
							&View,
							bUpdateSampleCoverage,
							MacroGroupDatas,
							InstanceCullingManager,
							VisibilityData.NodeGroupSize,
							CompactNodeIndex,
							CompactNodeData,
							CompactNodeCoord,
							NodeCounter,
							IndirectArgsBuffer);

						// Merge per-sample velocity into the scene velocity buffer
						AddHairVelocityPass(
							GraphBuilder,
							View,
							MacroGroupDatas,
							CategorizationTexture,
							CompactNodeIndex,
							CompactNodeData,
							PassOutput.NodeVelocity,
							SceneVelocityTexture,
							ResolveMaskTexture);

						if (bUpdateSampleCoverage)
						{
							PassOutput.NodeData = AddUpdateSampleCoveragePass(
								GraphBuilder,
								&View,
								CompactNodeIndex,
								PassOutput.NodeData);
						}

						CompactNodeData = PassOutput.NodeData;
						EmissiveTexture = PassOutput.EmissiveTexture;
					}

					// Allocate buffer for storing all the light samples
					FRDGTextureRef SampleLightingBuffer = AddClearLightSamplePass(GraphBuilder, &View, VisibilityData.MaxNodeCount, NodeCounter);
					VisibilityData.SampleLightingViewportResolution = SampleLightingBuffer->Desc.Extent;

					 VisibilityData.SampleLightingBuffer	= SampleLightingBuffer;
					 VisibilityData.NodeIndex				= CompactNodeIndex;
					 VisibilityData.CategorizationTexture	= CategorizationTexture;
					 VisibilityData.HairOnlyDepthTexture	= HairOnlyDepthTexture;
					 VisibilityData.NodeData				= CompactNodeData;
					 VisibilityData.NodeCoord				= CompactNodeCoord;
					 VisibilityData.NodeIndirectArg			= IndirectArgsBuffer;
					 VisibilityData.NodeCount				= NodeCounter;
					 VisibilityData.ResolveMaskTexture		= ResolveMaskTexture;
					 VisibilityData.EmissiveTexture			= EmissiveTexture;

					// View transmittance depth test needs to happen before the scene depth is patched with the hair depth (for fully-covered-by-hair pixels)
					if (ViewTransmittance.HairCountTexture)
					{
						AddHairViewTransmittanceDepthPass(
							GraphBuilder,
							View,
							CategorizationTexture,
							SceneDepthTexture,
							ViewTransmittance.HairCountTexture);
						VisibilityData.ViewHairCountTexture = ViewTransmittance.HairCountTexture;
					}

					// For fully covered pixels, write: 
					// * black color into the scene color
					// * closest depth
					// * unlit shading model ID 
					if (bRunColorAndDepthPatching)
					{
						AddHairVisibilityColorAndDepthPatchPass(
							GraphBuilder,
							View,
							VisibilityData.TileData,
							CategorizationTexture,
							SceneGBufferBTexture,
							SceneGBufferCTexture,
							SceneColorTexture,
							SceneDepthTexture);
					}

					AddHairOnlyDepthPass(
						GraphBuilder,
						View,
						VisibilityData.TileData,
						CategorizationTexture,
						HairOnlyDepthTexture);
				}
				else
				{
					AddHairMaterialGBufferPass(
						GraphBuilder,
						Scene,
						&View,
						InstanceCullingManager,
						MacroGroupDatas,

						ViewTransmittance.TransmittanceTexture,
						nullptr,
						MsaaVisibilityResources.IdTexture,
						MsaaVisibilityResources.DepthTexture,

						SceneGBufferATexture,
						SceneGBufferBTexture,
						SceneGBufferCTexture,
						SceneGBufferDTexture,
						SceneGBufferETexture,

						SceneColorTexture,
						SceneDepthTexture,
						SceneVelocityTexture);

						ViewTransmittance.HairCountTexture = nullptr;
				}
			}
			else if (RenderMode == HairVisibilityRenderMode_PPLL)
			{
				// In this pas we reuse the scene depth buffer to cull hair pixels out.
				// Pixel data is accumulated in buffer containing data organized in a linked list with node scattered in memory according to pixel shader execution. 
				// This with up to width * height * GHairVisibilityPPLLGlobalMaxPixelNodeCount node total maximum.
				// After we have that a node sorting pass happening and we finally output all the data once into the common compaction node list.

				FRDGTextureRef PPLLNodeCounterTexture;
				FRDGTextureRef PPLLNodeIndexTexture;
				FRDGBufferRef PPLLNodeDataBuffer;
				FRDGTextureRef ViewZDepthTexture = SceneDepthTexture;

				// Linked list generation pass
				AddHairVisibilityPPLLPass(GraphBuilder, Scene, &View, MacroGroupDatas, Resolution, InstanceCullingManager, ViewZDepthTexture, PPLLNodeCounterTexture, PPLLNodeIndexTexture, PPLLNodeDataBuffer);

				// Linked list sorting pass and compaction into common representation
				{
					FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityPrimitiveIdCompactionCS::FParameters>();
					PassParameters->PPLLCounter  = PPLLNodeCounterTexture;
					PassParameters->PPLLNodeIndex= PPLLNodeIndexTexture;
					PassParameters->PPLLNodeData = GraphBuilder.CreateSRV(PPLLNodeDataBuffer);
					PassParameters->ViewTransmittanceTexture = nullptr;

					FRDGBufferRef CompactNodeCoord;
					FRDGBufferRef IndirectArgsBuffer;
					AddHairVisibilityPrimitiveIdCompactionPass(
						true, // bUsePPLL
						GraphBuilder,
						View,
						MacroGroupDatas,
						VisibilityData.NodeGroupSize,
						PassParameters,
						NodeCounter,
						CompactNodeIndex,
						CompactNodeData,
						CompactNodeCoord,
						CategorizationTexture,
						SceneVelocityTexture,
						IndirectArgsBuffer,
						VisibilityData.MaxNodeCount);

					VisibilityData.MaxSampleCount = GetMaxSamplePerPixel();
					VisibilityData.NodeIndex = CompactNodeIndex;
					VisibilityData.CategorizationTexture = CategorizationTexture;
					VisibilityData.HairOnlyDepthTexture = HairOnlyDepthTexture;
					VisibilityData.NodeData = CompactNodeData;
					VisibilityData.NodeCoord = CompactNodeCoord;
					VisibilityData.NodeIndirectArg = IndirectArgsBuffer;
					VisibilityData.NodeCount = NodeCounter;
				}

				// Generate Tile data
				if (CategorizationTexture && bGenerateTile)
				{
					VisibilityData.TileData = AddHairStrandsGenerateTilesPass(GraphBuilder, View, CategorizationTexture);
				}

				if (bRunColorAndDepthPatching)
				{
					AddHairVisibilityColorAndDepthPatchPass(
						GraphBuilder,
						View,
						VisibilityData.TileData,
						CategorizationTexture,
						SceneGBufferBTexture,
						SceneGBufferCTexture,
						SceneColorTexture,
						SceneDepthTexture);
				}

				AddHairOnlyDepthPass(
					GraphBuilder,
					View,
					VisibilityData.TileData,
					CategorizationTexture,
					HairOnlyDepthTexture);

				// Allocate buffer for storing all the light samples
				FRDGTextureRef SampleLightingBuffer = AddClearLightSamplePass(GraphBuilder, &View, VisibilityData.MaxNodeCount, NodeCounter);
				VisibilityData.SampleLightingViewportResolution = SampleLightingBuffer->Desc.Extent;
				VisibilityData.SampleLightingBuffer = SampleLightingBuffer;

			#if WITH_EDITOR
				// Extract texture for debug visualization
				if (GHairStrandsDebugPPLL > 0)
				{
					View.HairStrandsViewData.DebugData.PPLLNodeCounterTexture = PPLLNodeCounterTexture;
					View.HairStrandsViewData.DebugData.PPLLNodeIndexTexture = PPLLNodeIndexTexture;
					View.HairStrandsViewData.DebugData.PPLLNodeDataBuffer = PPLLNodeDataBuffer;
				}
			#endif
			}

		#if RHI_RAYTRACING
			if (IsRayTracingEnabled() && CategorizationTexture)
			{
				FRDGTextureRef LightingChannelMaskTexture = AddHairLightChannelMaskPass(
					GraphBuilder,
					View,
					Resolution,
					CompactNodeData,
					CompactNodeIndex);
				VisibilityData.LightChannelMaskTexture = LightingChannelMaskTexture;
			}
		#endif
		}
	}
}
