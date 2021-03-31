// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsVisibility.h"
#include "HairStrandsCluster.h"
#include "HairStrandsUtils.h"
#include "HairStrandsInterface.h"

#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"
#include "MeshPassProcessor.inl"

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

static int32 GHairStrandsVisibilityMaterialPass = 1;
static FAutoConsoleVariableRef CVarHairStrandsVisibilityMaterialPass(TEXT("r.HairStrands.Visibility.MaterialPass"), GHairStrandsVisibilityMaterialPass, TEXT("Enable the deferred material pass evaluation after the hair visibility is resolved."));

static float GHairStrandsViewHairCountDepthDistanceThreshold = 30.f;
static FAutoConsoleVariableRef CVarHairStrandsViewHairCountDepthDistanceThreshold(TEXT("r.HairStrands.Visibility.HairCount.DistanceThreshold"), GHairStrandsViewHairCountDepthDistanceThreshold, TEXT("Distance threshold defining if opaque depth get injected into the 'view-hair-count' buffer."));

static int32 GHairVisibilityComputeRaster = 0;
static int32 GHairVisibilityComputeRaster_MaxSamplePerPixel = 1;
static float GHairVisibilityComputeRaster_MeanSamplePerPixel = 1;
static int32 GHairVisibilityComputeRaster_MaxPixelCount = 64;
static FAutoConsoleVariableRef CVarHairStrandsVisibilityComputeRaster(TEXT("r.HairStrands.Visibility.ComputeRaster"), GHairVisibilityComputeRaster, TEXT("Hair Visiblity uses raster compute."), ECVF_Scalability | ECVF_RenderThreadSafe);
static FAutoConsoleVariableRef CVarHairStrandsVisibilityComputeRaster_MaxSamplePerPixel(TEXT("r.HairStrands.Visibility.ComputeRaster.SamplePerPixel"), GHairVisibilityComputeRaster_MaxSamplePerPixel, TEXT("Define the number of sampler per pixel using raster compute."));
static FAutoConsoleVariableRef CVarHairStrandsVisibilityComputeRaster_MaxPixelCount(TEXT("r.HairStrands.Visibility.ComputeRaster.MaxPixelCount"), GHairVisibilityComputeRaster_MaxPixelCount, TEXT("Define the maximal length rasterize in compute."));

static float GHairStrandsFullCoverageThreshold = 0.98f;
static FAutoConsoleVariableRef CVarHairStrandsFullCoverageThreshold(TEXT("r.HairStrands.Visibility.FullCoverageThreshold"), GHairStrandsFullCoverageThreshold, TEXT("Define the coverage threshold at which a pixel is considered fully covered."));

static int32 GHairStrandsSortHairSampleByDepth = 0;
static FAutoConsoleVariableRef CVarHairStrandsSortHairSampleByDepth(TEXT("r.HairStrands.Visibility.SortByDepth"), GHairStrandsSortHairSampleByDepth, TEXT("Sort hair fragment by depth and update their coverage based on ordered transmittance."));

static int32 GHairStrandsHairCountToTransmittance = 0;
static FAutoConsoleVariableRef CVarHairStrandsHairCountToTransmittance(TEXT("r.HairStrands.Visibility.UseCoverageMappping"), GHairStrandsHairCountToTransmittance, TEXT("Use hair count to coverage transfer function."));

static int32 GHairStrandsVisibility_UseFastPath = 0;
static FAutoConsoleVariableRef CVarHairStrandsVisibility_UseFastPath(TEXT("r.HairStrands.Visibility.UseFastPath"), GHairStrandsVisibility_UseFastPath, TEXT("Use fast path writing hair data into Gbuffer."));

static int32 GHairStrandsVisibility_OutputEmissiveData = 0;
static FAutoConsoleVariableRef CVarHairStrandsVisibility_OutputEmissiveData(TEXT("r.HairStrands.Visibility.Emissive"), GHairStrandsVisibility_OutputEmissiveData, TEXT("Enable emissive data during the material pass."));

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
	HairVisibilityRenderMode_MSAA,
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
		return HairVisibilityRenderMode_MSAA;
	}
}

inline bool IsMsaaEnabled()
{
	const EHairVisibilityRenderMode Mode = GetHairVisibilityRenderMode();
	return Mode == HairVisibilityRenderMode_MSAA || Mode == HairVisibilityRenderMode_MSAA_Visibility;
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
		case HairVisibilityRenderMode_MSAA:
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
	case HairVisibilityRenderMode_MSAA:
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
	const uint32 SampleTextureResolution = FMath::CeilToInt(FMath::Sqrt(MaxNodeCount));
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(SampleTextureResolution, SampleTextureResolution), PF_FloatRGBA, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable);
	FRDGTextureRef Output = GraphBuilder.CreateTexture(Desc, TEXT("HairLightSample"));

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
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMaterialGBufferPassParameters, )
	SHADER_PARAMETER(FIntPoint, MaxResolution)
	SHADER_PARAMETER(uint32, InputType)
	SHADER_PARAMETER(float, CoverageThreshold)
	SHADER_PARAMETER_TEXTURE(Texture2D<float>, InTransmittanceTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>,  InRasterOutputVisibilityTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>,  InMSAAIDTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D<float>, InMSAADepthTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMaterialGBufferPassParameters, "MaterialGBufferPassParameters");

class FHairMaterialGBufferVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairMaterialGBufferVS, MeshMaterial);

protected:
	FHairMaterialGBufferVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FMaterialGBufferPassParameters::StaticStructMetadata.GetShaderVariableName());
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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FMaterialGBufferPassParameters::StaticStructMetadata.GetShaderVariableName());
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
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, InRasterOutputVisibilityTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>,  InMSAAIDTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InMSAADepthTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InTransmittanceTexture)
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
	void Process(
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
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
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

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		Process(MeshBatchCopy, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MacroGroupId, HairMaterialId, PrimitiveId, LightChannelMask);
	}
}

void FHairMaterialGBufferProcessor::Process(
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
		FMeshMaterialShader,
		FMeshMaterialShader,
		FHairMaterialGBufferPS> PassShaders;
	{
		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
		PassShaders.VertexShader = MaterialResource.GetShader<FHairMaterialGBufferVS>(VertexFactoryType);
		PassShaders.PixelShader = MaterialResource.GetShader<FHairMaterialGBufferPS>(VertexFactoryType);
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

	// Add resources reference to the pass parameters, in order to get the resource lifetime extended to this pass
	FVisibilityMaterialGBufferPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityMaterialGBufferPassParameters>();
	PassParameters->InTransmittanceTexture = InTransmittanceTexture;
	PassParameters->InRasterOutputVisibilityTexture = InRasterOutputVisibilityTexture;
	PassParameters->InMSAAIDTexture = InMSAAIDTexture;
	PassParameters->InMSAADepthTexture = InMSAADepthTexture;
	const FIntPoint Resolution = OutBufferATexture->Desc.Extent;

	// If there is velocity texture, we recreate a dummy one
	bool bIsVelocityDummy = false;
	if (OutVelocityTexture == nullptr)
	{
		FRDGTextureDesc VelocityDesc = FVelocityRendering::GetRenderTargetDesc(ViewInfo->GetShaderPlatform());
		VelocityDesc.Extent = OutDepthTexture->Desc.Extent;
		OutVelocityTexture = GraphBuilder.CreateTexture(VelocityDesc, TEXT("DummyVelocity"));
		bIsVelocityDummy = true;
	}
	PassParameters->RenderTargets[0] = FRenderTargetBinding(OutBufferATexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(OutBufferBTexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[2] = FRenderTargetBinding(OutBufferCTexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[3] = FRenderTargetBinding(OutBufferDTexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[4] = FRenderTargetBinding(OutBufferETexture, ERenderTargetLoadAction::ELoad, 0);
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
			check(RHICmdList.IsInsideRenderPass());
			check(IsInRenderingThread());

			FMaterialGBufferPassParameters MaterialPassParameters;
			MaterialPassParameters.InputType = PassParameters->InRasterOutputVisibilityTexture != nullptr ? 1 : 0;
			MaterialPassParameters.MaxResolution = Resolution;
			MaterialPassParameters.CoverageThreshold = FMath::Clamp(GHairStrandsFullCoverageThreshold, 0.1f, 1.f);
			MaterialPassParameters.InTransmittanceTexture = PassParameters->InTransmittanceTexture->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;
			
			FTextureRHIRef DefaultTexture = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
			MaterialPassParameters.InMSAAIDTexture = DefaultTexture;
			MaterialPassParameters.InMSAADepthTexture = DefaultTexture;
			MaterialPassParameters.InRasterOutputVisibilityTexture = DefaultTexture;
			if (MaterialPassParameters.InputType == 0)
			{
				check(PassParameters->InMSAAIDTexture);
				check(PassParameters->InMSAADepthTexture);
				MaterialPassParameters.InMSAAIDTexture = PassParameters->InMSAAIDTexture->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;
				MaterialPassParameters.InMSAADepthTexture = PassParameters->InMSAADepthTexture->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;;
			}
			else if (MaterialPassParameters.InputType == 1)
			{
				check(PassParameters->InRasterOutputVisibilityTexture);
				MaterialPassParameters.InRasterOutputVisibilityTexture = PassParameters->InRasterOutputVisibilityTexture->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;
			}

			TUniformBufferRef<FMaterialGBufferPassParameters> MaterialPassParametersBuffer = TUniformBufferRef<FMaterialGBufferPassParameters>::CreateUniformBufferImmediate(MaterialPassParameters, UniformBuffer_SingleFrame);

			FMeshPassProcessorRenderState DrawRenderState(*ViewInfo, MaterialPassParametersBuffer);
			// Note: this reference needs to persistent until SubmitMeshDrawCommands() is called, as DrawRenderState does not ref count 
			// the view uniform buffer (raw pointer). It is only within the MeshProcessor that the uniform buffer get reference
			TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
			{
				const bool bEnableMSAA = false;
				SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfoBits, ViewInfo->CachedViewUniformShaderParameters->HairComponents);
				ViewUniformShaderParameters = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
				DrawRenderState.SetViewUniformBuffer(ViewUniformShaderParameters);
			}

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

				for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
				{
					for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroupData.PrimitivesInfos)
					{
						const FMeshBatch& MeshBatch = *PrimitiveInfo.MeshBatchAndRelevance.Mesh;
						const uint64 BatchElementMask = ~0ull;
						MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.MeshBatchAndRelevance.PrimitiveSceneProxy, -1, MacroGroupData.MacroGroupId, PrimitiveInfo.MaterialId);
					}
				}

				if (VisibleMeshDrawCommands.Num() > 0)
				{
					FRHIVertexBuffer* PrimitiveIdVertexBuffer = nullptr;
					SortAndMergeDynamicPassMeshDrawCommands(ViewInfo->GetFeatureLevel(), VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1);
					SubmitMeshDrawCommands(VisibleMeshDrawCommands, PipelineStateSet, PrimitiveIdVertexBuffer, 0, false, 1, RHICmdList);
				}
			}
		});
}

/////////////////////////////////////////////////////////////////////////////////////////

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMaterialPassParameters, )
	SHADER_PARAMETER(FIntPoint, MaxResolution)
	SHADER_PARAMETER(uint32, MaxSampleCount)
	SHADER_PARAMETER(uint32, NodeGroupSize)
	SHADER_PARAMETER(uint32, bUpdateSampleCoverage)
	SHADER_PARAMETER(uint32, bOutputEmissive)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>, NodeIndex)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>, TotalNodeCounter)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, NodeCoord)
	SHADER_PARAMETER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
	SHADER_PARAMETER_SRV(Buffer<uint>, IndirectArgs)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<FPackedHairSample>, OutNodeData)
	SHADER_PARAMETER_UAV(RWBuffer<float2>, OutNodeVelocity)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMaterialPassParameters, "MaterialPassParameters");

class FHairMaterialVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairMaterialVS, MeshMaterial);

protected:
	FHairMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FMaterialPassParameters::StaticStructMetadata.GetShaderVariableName());
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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FMaterialPassParameters::StaticStructMetadata.GetShaderVariableName());
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
	void Process(
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
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName();
	const bool bShouldRender = (!PrimitiveSceneProxy && MeshBatch.Elements.Num()>0) || (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass());

	if (bIsCompatible
		&& bIsHairStrandsFactory
		&& bShouldRender
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		// For the mesh patch to be rendered a single triangle triangle to spawn the necessary amount of thread
		FMeshBatch MeshBatchCopy = MeshBatch;
		for (uint32 ElementIt = 0, ElementCount=uint32(MeshBatch.Elements.Num()); ElementIt < ElementCount; ++ElementIt)
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

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		Process(MeshBatchCopy, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MacroGroupId, HairMaterialId, PrimitiveId, LightChannelMask);
	}
}

void FHairMaterialProcessor::Process(
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
		FMeshMaterialShader,
		FMeshMaterialShader,
		FHairMaterialPS> PassShaders;
	{
		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
		PassShaders.VertexShader = MaterialResource.GetShader<FHairMaterialVS>(VertexFactoryType);
		PassShaders.PixelShader = MaterialResource.GetShader<FHairMaterialPS>(VertexFactoryType);
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

BEGIN_SHADER_PARAMETER_STRUCT(FVisibilityMaterialPassParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NodeIndex)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, TotalNodeCounter)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, NodeCoord)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, IndirectArgs)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPackedHairSample>, OutNodeData)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float2>, OutNodeVelocity)
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
	FRDGBufferRef OutNodeDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(InNodeDataBuffer->Desc.BytesPerElement, InNodeDataBuffer->Desc.NumElements), TEXT("HairCompactNodeData"));

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
	Output.NodeData		= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(HairStrandsVisibilityInternal::NodeData), MaxNodeCount), TEXT("HairCompactNodeData"));
	Output.NodeVelocity = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, CompactNodeVis->Desc.NumElements), TEXT("HairCompactNodeVelocity"));

	const uint32 ResolutionDim = FMath::CeilToInt(FMath::Sqrt(MaxNodeCount));
	const FIntPoint Resolution(ResolutionDim, ResolutionDim);


	// Add resources reference to the pass parameters, in order to get the resource lifetime extended to this pass
	FVisibilityMaterialPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityMaterialPassParameters>();
	PassParameters->TotalNodeCounter= CompactNodeCounter;
	PassParameters->NodeIndex		= CompactNodeIndex;
	PassParameters->NodeVis			= GraphBuilder.CreateSRV(CompactNodeVis);
	PassParameters->NodeCoord		= GraphBuilder.CreateSRV(CompactNodeCoord);
	PassParameters->IndirectArgs	= GraphBuilder.CreateSRV(IndirectArgBuffer);
	PassParameters->OutNodeData		= GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.NodeData));
	PassParameters->OutNodeVelocity	= GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.NodeVelocity, FMaterialPassOutput::VelocityFormat));

	// For debug purpose only
	const bool bOutputEmissive = IsHairStrandsEmissiveEnable();
	const bool bIsPlatformRequireRenderTarget = IsPlatformRequiringRenderTargetForMaterialPass(Scene->GetShaderPlatform()) || GRHIRequiresRenderTargetForPixelShaderUAVs;
	if (bOutputEmissive)
	{
		FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(Resolution, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_RenderTargetable);
		Output.EmissiveTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("HairMaterialEmissiveOutput"));
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Output.EmissiveTexture, ERenderTargetLoadAction::EClear, 0);
		
	}
	else if (bIsPlatformRequireRenderTarget)
	{
		FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(Resolution, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_RenderTargetable);
		FRDGTextureRef OutDummyTexture0 = GraphBuilder.CreateTexture(OutputDesc, TEXT("HairMaterialDummyOutput"));
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutDummyTexture0, ERenderTargetLoadAction::EClear, 0);
	}
	

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsMaterialPass"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Scene = Scene, ViewInfo, &MacroGroupDatas, MaxNodeCount, Resolution, NodeGroupSize, bUpdateSampleCoverage, bOutputEmissive](FRHICommandListImmediate& RHICmdList)
	{
		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		FMaterialPassParameters MaterialPassParameters;
		MaterialPassParameters.bUpdateSampleCoverage = bUpdateSampleCoverage ? 1 : 0;
		MaterialPassParameters.bOutputEmissive	= bOutputEmissive ? 1 : 0;
		MaterialPassParameters.MaxResolution	= Resolution;
		MaterialPassParameters.NodeGroupSize	= NodeGroupSize;
		MaterialPassParameters.MaxSampleCount	= MaxNodeCount;
		MaterialPassParameters.TotalNodeCounter	= PassParameters->TotalNodeCounter->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;
		MaterialPassParameters.NodeIndex		= PassParameters->NodeIndex->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;
		MaterialPassParameters.NodeCoord		= PassParameters->NodeCoord->GetRHI();
		MaterialPassParameters.NodeVis			= PassParameters->NodeVis->GetRHI();
		MaterialPassParameters.IndirectArgs		= PassParameters->IndirectArgs->GetRHI();
		MaterialPassParameters.OutNodeData 		= PassParameters->OutNodeData->GetRHI();
		MaterialPassParameters.OutNodeVelocity 	= PassParameters->OutNodeVelocity->GetRHI();

		TUniformBufferRef<FMaterialPassParameters> MaterialPassParametersBuffer = TUniformBufferRef<FMaterialPassParameters>::CreateUniformBufferImmediate(MaterialPassParameters, UniformBuffer_SingleFrame);

		FMeshPassProcessorRenderState DrawRenderState(*ViewInfo, MaterialPassParametersBuffer);
		// Note: this reference needs to persistent until SubmitMeshDrawCommands() is called, as DrawRenderState does not ref count 
		// the view uniform buffer (raw pointer). It is only within the MeshProcessor that the uniform buffer get reference
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
		{
			const bool bEnableMSAA = false;
			SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfoBits, ViewInfo->CachedViewUniformShaderParameters->HairComponents);
			ViewUniformShaderParameters = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
			DrawRenderState.SetViewUniformBuffer(ViewUniformShaderParameters);
		}

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

			for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
			{
				for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroupData.PrimitivesInfos)
				{
					const FMeshBatch& MeshBatch = *PrimitiveInfo.MeshBatchAndRelevance.Mesh;
					const uint64 BatchElementMask = ~0ull;
					MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.MeshBatchAndRelevance.PrimitiveSceneProxy, -1, MacroGroupData.MacroGroupId, PrimitiveInfo.MaterialId);
				}
			}

			if (VisibleMeshDrawCommands.Num() > 0)
			{
				FRHIVertexBuffer* PrimitiveIdVertexBuffer = nullptr;
				SortAndMergeDynamicPassMeshDrawCommands(ViewInfo->GetFeatureLevel(), VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1);
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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NodeIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, NodeVelocity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutVelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutResolveMaskTexture)
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
		OutResolveMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("VelocityResolveMaskTexture"));
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
	PassParameters->NodeIndex = NodeIndex;
	PassParameters->NodeVis = GraphBuilder.CreateSRV(NodeVis);
	PassParameters->NodeVelocity = GraphBuilder.CreateSRV(NodeVelocity, FMaterialPassOutput::VelocityFormat);
	PassParameters->OutVelocityTexture = GraphBuilder.CreateUAV(OutVelocityTexture);
	PassParameters->OutResolveMaskTexture = GraphBuilder.CreateUAV(OutResolveMaskTexture);

	FIntRect TotalRect = ComputeVisibleHairStrandsMacroGroupsRect(View.ViewRect, MacroGroupDatas);

	// Snap the rect onto thread group boundary
	const FIntPoint GroupSize = GetVendorOptimalGroupSize2D();
	TotalRect.Min.X = FMath::FloorToInt(float(TotalRect.Min.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Min.Y = FMath::FloorToInt(float(TotalRect.Min.Y) / float(GroupSize.Y)) * GroupSize.Y;
	TotalRect.Max.X = FMath::CeilToInt(float(TotalRect.Max.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Max.Y = FMath::CeilToInt(float(TotalRect.Max.Y) / float(GroupSize.Y)) * GroupSize.Y;

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
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutLightChannelMaskTexture)
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
	FRDGTextureRef OutLightChannelMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairLightChannelMask"));

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
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVisibilityPassGlobalParameters, )
	SHADER_PARAMETER(uint32, MaxPPLLNodeCount)
	SHADER_PARAMETER_UAV(RWTexture2D<uint>, PPLLCounter)
	SHADER_PARAMETER_UAV(RWTexture2D<uint>, PPLLNodeIndex)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<FPPLLNodeData>, PPLLNodeData)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVisibilityPassGlobalParameters, "HairVisibilityPass");

BEGIN_SHADER_PARAMETER_STRUCT(FVisibilityPassParameters, )
	SHADER_PARAMETER(uint32, MaxPPLLNodeCount)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, PPLLCounter)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, PPLLNodeIndex)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPPLLNodeData, PPLLNodeData)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static FVisibilityPassGlobalParameters ConvertToGlobalPassParameter(const FVisibilityPassParameters* In)
{
	FVisibilityPassGlobalParameters Out;
	Out.MaxPPLLNodeCount = In->MaxPPLLNodeCount;
	Out.PPLLCounter = In->PPLLCounter->GetRHI();
	Out.PPLLNodeIndex = In->PPLLNodeIndex->GetRHI();
	Out.PPLLNodeData = In->PPLLNodeData->GetRHI();
	return Out;
}

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

void CreatePassDummyTextures(FRDGBuilder& GraphBuilder, FVisibilityPassParameters* PassParameters)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(1,1), PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
	PassParameters->PPLLCounter		= GraphBuilder.CreateUAV(GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityPPLLNodeIndex")));
	PassParameters->PPLLNodeIndex	= GraphBuilder.CreateUAV(GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityPPLLNodeIndex")));
	PassParameters->PPLLNodeData	= GraphBuilder.CreateUAV(GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(PPLLNodeData), 1), TEXT("DummyPPLLNodeData")));
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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FVisibilityPassGlobalParameters::StaticStructMetadata.GetShaderVariableName());
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
typedef FHairVisibilityVS<HairVisibilityRenderMode_MSAA, true >							THairVisiblityVS_MSAA;
typedef FHairVisibilityVS<HairVisibilityRenderMode_Transmittance, true >				THairVisiblityVS_Transmittance;
typedef FHairVisibilityVS<HairVisibilityRenderMode_TransmittanceAndHairCount, true >	THairVisiblityVS_TransmittanceAndHairCount;
typedef FHairVisibilityVS<HairVisibilityRenderMode_PPLL, true >							THairVisiblityVS_PPLL;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_MSAAVisibility_NoCulling,	TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_MSAAVisibility_Culling,		TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_MSAA,						TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FVisibilityPassGlobalParameters::StaticStructMetadata.GetShaderVariableName());
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
		else if (RenderMode == HairVisibilityRenderMode_MSAA)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32G32_UINT);
			OutEnvironment.SetRenderTargetOutputFormat(1, PF_R32G32_UINT);
			OutEnvironment.SetRenderTargetOutputFormat(2, PF_R32G32_UINT);
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
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_MSAA>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);
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
	template<EHairVisibilityRenderMode RenderMode, bool bCullingEnable=true>
	void Process(
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
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName();
	const bool bShouldRender = (!PrimitiveSceneProxy && MeshBatch.Elements.Num()>0) || (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass());
	const uint32 LightChannelMask = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetLightingChannelMask() : 0;

	if (bIsCompatible 
		&& bIsHairStrandsFactory
		&& bShouldRender
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility && bCullingEnable)
			Process<HairVisibilityRenderMode_MSAA_Visibility, true>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility && !bCullingEnable)
			Process<HairVisibilityRenderMode_MSAA_Visibility, false>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_MSAA)
			Process<HairVisibilityRenderMode_MSAA>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_Transmittance)
			Process<HairVisibilityRenderMode_Transmittance>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
			Process<HairVisibilityRenderMode_TransmittanceAndHairCount>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_PPLL)
			Process<HairVisibilityRenderMode_PPLL>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
	}
}

template<EHairVisibilityRenderMode TRenderMode, bool bCullingEnable>
void FHairVisibilityProcessor::Process(
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
		FMeshMaterialShader,
		FMeshMaterialShader,
		FHairVisibilityPS<TRenderMode>> PassShaders;
	{
		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
		PassShaders.VertexShader = MaterialResource.GetShader<FHairVisibilityVS<TRenderMode,bCullingEnable>>(VertexFactoryType);
		PassShaders.PixelShader  = MaterialResource.GetShader<FHairVisibilityPS<TRenderMode>>(VertexFactoryType);
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
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, View](FRHICommandList& RHICmdList)
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

		VertexShader->SetParameters(RHICmdList, View->ViewUniformBuffer);
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

	FRDGBufferRef OutBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("HairVisibilityIndirectArgBuffer"));

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
	class FVisibility 	: SHADER_PERMUTATION_INT("PERMUTATION_VISIBILITY", 2);
	class FMSAACount 	: SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_MSAACOUNT", 1, 2, 4, 8);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FVelocity, FViewTransmittance, FMaterial, FPPLL, FVisibility, FMSAACount>;

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

		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutCompactNodeCounter)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutCompactNodeIndex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutCategorizationTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutCompactNodeData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutCompactNodeCoord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutVelocityTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	END_SHADER_PARAMETER_STRUCT()

public:

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FPPLL>() > 0)
		{
			PermutationVector.Set<FViewTransmittance>(0);
			PermutationVector.Set<FVisibility>(0);
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
		if (PermutationVector.Get<FPPLL>() > 0 && PermutationVector.Get<FVisibility>() > 0)
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
	const bool bUseVisibility,
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

		if (bUseVisibility)
		{
			check(PassParameters->MSAA_DepthTexture);
			check(PassParameters->MSAA_IDTexture);
			Resolution = PassParameters->MSAA_DepthTexture->Desc.Extent;
		}
		else
		{
			check(PassParameters->MSAA_DepthTexture);
			check(PassParameters->MSAA_IDTexture);
			check(PassParameters->MSAA_MaterialTexture);
			check(PassParameters->MSAA_AttributeTexture);
			Resolution = PassParameters->MSAA_DepthTexture->Desc.Extent;
		}
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCompactCounter = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityCompactCounter"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCompactNodeIndex = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityCompactNodeIndex"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R16G16B16A16_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCategorizationTexture = GraphBuilder.CreateTexture(Desc, TEXT("CategorizationTexture"));
	}

	const uint32 ClearValues[4] = { 0u,0u,0u,0u };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactCounter), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactNodeIndex), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCategorizationTexture), ClearValues);

	// Select render node count according to current mode
	const uint32 MSAASampleCount = GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA ? GetMaxSamplePerPixel() : 1;
	const uint32 PPLLMaxRenderNodePerPixel = GetMaxSamplePerPixel();
	const uint32 MaxRenderNodeCount = GetTotalSampleCountForAllocation(Resolution);
	OutCompactNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(bUseVisibility ? sizeof(HairStrandsVisibilityInternal::NodeVis) : sizeof(HairStrandsVisibilityInternal::NodeData), MaxRenderNodeCount), TEXT("HairVisibilityPrimitiveIdCompactNodeData"));

	{
		// Pixel coord of the node. Stored as 2*uint16, packed into a single uint32
		OutCompactNodeCoord = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxRenderNodeCount), TEXT("HairVisibilityPrimitiveIdCompactNodeCoord"));
	}

	const bool bWriteOutVelocity = OutVelocityTexture != nullptr;
	const uint32 VelocityPermutation = bWriteOutVelocity ? FMath::Clamp(GHairVelocityType + 1, 0, 3) : 0;
	FHairVisibilityPrimitiveIdCompactionCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FGroupSize>(GetVendor() == HairVisibilityVendor_NVIDIA ? 0 : 1);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FVelocity>(VelocityPermutation > 0 ? 1 : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FViewTransmittance>(PassParameters->ViewTransmittanceTexture ? 1 : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FMaterial>(GHairStrandsMaterialCompactionEnable ? 1 : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FPPLL>(bUsePPLL ? PPLLMaxRenderNodePerPixel : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FVisibility>(bUseVisibility ? 1 : 0);	
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
	PassParameters->OutCompactNodeCoord = GraphBuilder.CreateUAV(OutCompactNodeCoord);
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

		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutCompactNodeCounter)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutCompactNodeIndex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutCategorizationTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutCompactNodeData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutCompactNodeCoord)

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
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
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
		OutCompactCounter = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityCompactCounter"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCompactNodeIndex = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityCompactNodeIndex"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R16G16B16A16_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutCategorizationTexture = GraphBuilder.CreateTexture(Desc, TEXT("CategorizationTexture"));
	}

	const uint32 ClearValues[4] = { 0u,0u,0u,0u };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactCounter), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactNodeIndex), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCategorizationTexture), ClearValues);

	// Select render node count according to current mode
	const uint32 MSAASampleCount = GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA ? GetMaxSamplePerPixel() : 1;
	const uint32 PPLLMaxRenderNodePerPixel = GetMaxSamplePerPixel();
	const uint32 MaxRenderNodeCount = GetTotalSampleCountForAllocation(Resolution);
	OutCompactNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(HairStrandsVisibilityInternal::NodeVis), MaxRenderNodeCount), TEXT("HairVisibilityPrimitiveIdCompactNodeData"));
	OutCompactNodeCoord = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxRenderNodeCount), TEXT("HairVisibilityPrimitiveIdCompactNodeCoord"));

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
	PassParameters->OutCompactNodeCoord		= GraphBuilder.CreateUAV(OutCompactNodeCoord);
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
class FHairGenerateTileCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairGenerateTileCS);
	SHADER_USE_PARAMETER_STRUCT(FHairGenerateTileCS, FGlobalShader);

	class FTileSize : SHADER_PERMUTATION_INT("PERMUTATION_TILESIZE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FTileSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER(FIntPoint, TileResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutTileCounter)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutTileIndexTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutTileBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairGenerateTileCS, "/Engine/Private/HairStrands/HairStrandsVisibilityTile.usf", "MainCS", SF_Compute);

static void AddGenerateTilePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const uint32 ThreadGroupSize,
	const uint32 TileSize,
	const FRDGTextureRef& CategorizationTexture,
	FRDGTextureRef& OutTileIndexTexture,
	FRDGBufferRef& OutTileBuffer,
	FRDGBufferRef& OutTileIndirectArgs)
{
	check(TileSize == 8); // only size supported for now
	const FIntPoint Resolution = CategorizationTexture->Desc.Extent;
	const FIntPoint TileResolution = FIntPoint(FMath::CeilToInt(Resolution.X / float(TileSize)), FMath::CeilToInt(Resolution.Y / float(TileSize)));
	const uint32 TileCount = TileResolution.X * TileResolution.Y;

	FRDGTextureRef TileCounter;
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(1,1), PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		TileCounter = GraphBuilder.CreateTexture(Desc, TEXT("HairTileCounter"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(TileResolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutTileIndexTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairTileIndexTexture"));
	}

	OutTileBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileCount), TEXT("HairTileBuffer"));

	uint32 ClearValues[4] = { 0,0,0,0 };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileCounter), ClearValues);

	FHairGenerateTileCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairGenerateTileCS::FTileSize>(0);

	FHairGenerateTileCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairGenerateTileCS::FParameters>();
	PassParameters->Resolution = Resolution;
	PassParameters->TileResolution = TileResolution;
	PassParameters->CategorizationTexture = CategorizationTexture;
	PassParameters->OutTileCounter = GraphBuilder.CreateUAV(TileCounter);
	PassParameters->OutTileIndexTexture = GraphBuilder.CreateUAV(OutTileIndexTexture);
	PassParameters->OutTileBuffer = GraphBuilder.CreateUAV(OutTileBuffer, PF_R16G16_UINT);

	TShaderMapRef<FHairGenerateTileCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairGenerateTile"),
		ComputeShader,
		PassParameters,
		FIntVector(TileResolution.X, TileResolution.Y, 1));

	OutTileIndirectArgs = AddCopyIndirectArgPass(GraphBuilder, &View, ThreadGroupSize, TileSize*TileSize, TileCounter);
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
		check(GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA);

		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, 1, GetMaxSamplePerPixel());
		OutVisibilityDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityDepthTexture"));
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
	const FViewInfo* CapturedView = &View;

	TArray<FIntRect> MacroGroupRects;
	if (IsHairStrandsViewRectOptimEnable())
	{
		for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
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
			[Parameters, VertexShader, PixelShader, MacroGroupRects, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
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

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
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
	for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
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
	FVisibilityPassParameters* PassParameters)
{
	auto GetPassName = [RenderMode]()
	{
		switch (RenderMode)
		{
		case HairVisibilityRenderMode_PPLL:						return RDG_EVENT_NAME("HairStrandsVisibilityPPLLPass");
		case HairVisibilityRenderMode_MSAA:						return RDG_EVENT_NAME("HairStrandsVisibilityMSAAPass");
		case HairVisibilityRenderMode_MSAA_Visibility:			return RDG_EVENT_NAME("HairStrandsVisibilityMSAAVisPass");
		case HairVisibilityRenderMode_Transmittance:			return RDG_EVENT_NAME("HairStrandsTransmittancePass");
		case HairVisibilityRenderMode_TransmittanceAndHairCount:return RDG_EVENT_NAME("HairStrandsTransmittanceAndHairCountPass");
		default:												return RDG_EVENT_NAME("Noname");
		}
	};

	AddHairCulledVertexResourcesTransitionPass(GraphBuilder, MacroGroupDatas);

	GraphBuilder.AddPass(
		GetPassName(),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Scene = Scene, ViewInfo, &MacroGroupDatas, RenderMode](FRHICommandListImmediate& RHICmdList)
	{
		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		FVisibilityPassGlobalParameters GlobalPassParameters = ConvertToGlobalPassParameter(PassParameters);
		TUniformBufferRef<FVisibilityPassGlobalParameters> GlobalPassParametersBuffer = TUniformBufferRef<FVisibilityPassGlobalParameters>::CreateUniformBufferImmediate(GlobalPassParameters, UniformBuffer_SingleFrame);

		FMeshPassProcessorRenderState DrawRenderState(*ViewInfo, GlobalPassParametersBuffer);

		// Note: this reference needs to persistent until SubmitMeshDrawCommands() is called, as DrawRenderState does not ref count 
		// the view uniform buffer (raw pointer). It is only within the MeshProcessor that the uniform buffer get reference
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
		if(RenderMode == HairVisibilityRenderMode_Transmittance || RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount || RenderMode == HairVisibilityRenderMode_PPLL)
		{
			const bool bEnableMSAA = false;
			SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfoBits, ViewInfo->CachedViewUniformShaderParameters->HairComponents);
			// Create and set the uniform buffer
			ViewUniformShaderParameters = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
			DrawRenderState.SetViewUniformBuffer(ViewUniformShaderParameters);
		}

		{
			RHICmdList.SetViewport(0, 0, 0.0f, ViewInfo->ViewRect.Width(), ViewInfo->ViewRect.Height(), 1.0f);
			if (RenderMode == HairVisibilityRenderMode_MSAA)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
			}
			else if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility)
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
			
			for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
			{
				for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroupData.PrimitivesInfos)
				{
					const FMeshBatch& MeshBatch = *PrimitiveInfo.MeshBatchAndRelevance.Mesh;
					const uint64 BatchElementMask = ~0ull;
					MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.MeshBatchAndRelevance.PrimitiveSceneProxy, -1, MacroGroupData.MacroGroupId, PrimitiveInfo.MaterialId, PrimitiveInfo.IsCullingEnable());
				}
			}

			if (VisibleMeshDrawCommands.Num() > 0)
			{
				FRHIVertexBuffer* PrimitiveIdVertexBuffer = nullptr;
				SortAndMergeDynamicPassMeshDrawCommands(ViewInfo->GetFeatureLevel(), VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1);
				SubmitMeshDrawCommands(VisibleMeshDrawCommands, PipelineStateSet, PrimitiveIdVertexBuffer, 0, false, 1, RHICmdList);
			}
		}
	});
}

static void AddHairVisibilityMSAAPass(
	const bool bUseVisibility,
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& Resolution,
	FRDGTextureRef& OutVisibilityIdTexture,
	FRDGTextureRef& OutVisibilityMaterialTexture,
	FRDGTextureRef& OutVisibilityAttributeTexture,
	FRDGTextureRef& OutVisibilityVelocityTexture,
	FRDGTextureRef& OutVisibilityDepthTexture)
{
	const uint32 MSAASampleCount = GetMaxSamplePerPixel();

	if (bUseVisibility)
	{
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding(EClearBinding::ENoneBound), TexCreate_NoFastClear | TexCreate_RenderTargetable | TexCreate_ShaderResource, 1, MSAASampleCount);
			OutVisibilityIdTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityIDTexture"));
		}
		OutVisibilityMaterialTexture = nullptr;
		OutVisibilityAttributeTexture = nullptr;
		OutVisibilityVelocityTexture = nullptr;

		AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAIdTexture"), ViewInfo, 0xFFFFFFFF, OutVisibilityIdTexture);

		FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
		CreatePassDummyTextures(GraphBuilder, PassParameters);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutVisibilityIdTexture, ERenderTargetLoadAction::ELoad, 0);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			OutVisibilityDepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
		AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, HairVisibilityRenderMode_MSAA_Visibility, PassParameters);
	}
	else
	{
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_ShaderResource, 1, MSAASampleCount);
			OutVisibilityIdTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityIDTexture"));
		}

		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R8G8B8A8, FClearValueBinding(FLinearColor(0, 0, 0, 0)), TexCreate_RenderTargetable | TexCreate_ShaderResource, 1, MSAASampleCount);
			OutVisibilityMaterialTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityMaterialTexture"));
		}

		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R8G8B8A8, FClearValueBinding(FLinearColor(0, 0, 0, 0)), TexCreate_RenderTargetable | TexCreate_ShaderResource, 1, MSAASampleCount);
			OutVisibilityAttributeTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityAttributeTexture"));
		}

		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_G16R16, FClearValueBinding(FLinearColor(0, 0, 0, 0)), TexCreate_RenderTargetable | TexCreate_ShaderResource, 1, MSAASampleCount);
			OutVisibilityVelocityTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityVelocityTexture"));
		}
		AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAIdTexture"), ViewInfo, 0xFFFFFFFF, OutVisibilityIdTexture);

		// Manually clear RTs as using the Clear action on the RT, issue a global clean on all targets, while still need a special clear 
		// for the PrimitiveId buffer
		// const ERenderTargetLoadAction LoadAction = GHairClearVisibilityBuffer ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction;
		ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ENoAction;
		if (GHairClearVisibilityBuffer)
		{
			LoadAction = ERenderTargetLoadAction::ELoad;
			AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAMaterial"), ViewInfo, 0, OutVisibilityMaterialTexture);
			AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAAttribute"), ViewInfo, 0, OutVisibilityAttributeTexture);
			AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAVelocity"), ViewInfo, 0, OutVisibilityVelocityTexture);
		}

		FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
		CreatePassDummyTextures(GraphBuilder, PassParameters);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutVisibilityIdTexture, ERenderTargetLoadAction::ELoad, 0);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(OutVisibilityMaterialTexture, LoadAction, 0);
		PassParameters->RenderTargets[2] = FRenderTargetBinding(OutVisibilityAttributeTexture, LoadAction, 0);
		PassParameters->RenderTargets[3] = FRenderTargetBinding(OutVisibilityVelocityTexture, LoadAction, 0);

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			OutVisibilityDepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
		AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, HairVisibilityRenderMode_MSAA, PassParameters);
	}
}

static void AddHairVisibilityPPLLPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& Resolution,
	FRDGTextureRef& InViewZDepthTexture,
	FRDGTextureRef& OutVisibilityPPLLNodeCounter,
	FRDGTextureRef& OutVisibilityPPLLNodeIndex,
	FRDGBufferRef&  OutVisibilityPPLLNodeData)
{
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(1,1), PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutVisibilityPPLLNodeCounter = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityPPLLCounter"));
	}

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource);
		OutVisibilityPPLLNodeIndex = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityPPLLNodeIndex"));
	}

	const uint32 PPLLMaxTotalListElementCount = GetTotalSampleCountForAllocation(Resolution);
	{
		OutVisibilityPPLLNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(PPLLNodeData), PPLLMaxTotalListElementCount), TEXT("HairVisibilityPPLLNodeData"));
	}
	const uint32 ClearValue0[4] = { 0,0,0,0 };
	const uint32 ClearValueInvalid[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutVisibilityPPLLNodeCounter), ClearValue0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutVisibilityPPLLNodeIndex), ClearValueInvalid);

	FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
	PassParameters->PPLLCounter = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutVisibilityPPLLNodeCounter, 0));
	PassParameters->PPLLNodeIndex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutVisibilityPPLLNodeIndex, 0));
	PassParameters->PPLLNodeData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutVisibilityPPLLNodeData));
	PassParameters->MaxPPLLNodeCount = PPLLMaxTotalListElementCount;
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(InViewZDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, HairVisibilityRenderMode_PPLL, PassParameters);
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
	FRDGTextureRef SceneDepthTexture)
{
	check(SceneDepthTexture->Desc.Extent == Resolution);
	const EHairVisibilityRenderMode RenderMode = bOutputHairCount ? HairVisibilityRenderMode_TransmittanceAndHairCount : HairVisibilityRenderMode_Transmittance;

	// Clear to transmittance 1
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Resolution, PF_R32_FLOAT, FClearValueBinding(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)), TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
	CreatePassDummyTextures(GraphBuilder, PassParameters);
	FHairPrimaryTransmittance Out;

	Out.TransmittanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairViewTransmittanceTexture"));
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Out.TransmittanceTexture, ERenderTargetLoadAction::EClear, 0);

	if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
	{
		Desc.Format = PF_G32R32F;
		Desc.ClearValue = FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		Out.HairCountTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairViewHairCountTexture"));
		PassParameters->RenderTargets[1] = FRenderTargetBinding(Out.HairCountTexture, ERenderTargetLoadAction::EClear, 0);
	}

	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		SceneDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ENoAction,
		FExclusiveDepthStencil::DepthRead_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, RenderMode, PassParameters);

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
	const FViewInfo* CapturedView = &View;
	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsViewTransmittanceDepth"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
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

		VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
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
		OutEnvironment.SetRenderTargetOutputFormat(1, PF_FloatRGBA);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityDepthPS, "/Engine/Private/HairStrands/HairStrandsVisibilityDepthPS.usf", "MainPS", SF_Pixel);

static void AddHairVisibilityColorAndDepthPatchPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
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

	FHairVisibilityDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityDepthPS::FParameters>();
	Parameters->CategorisationTexture = CategorisationTexture;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutGBufferBTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets[1] = FRenderTargetBinding(OutGBufferCTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets[2] = FRenderTargetBinding(OutColorTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthWrite_StencilNop);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	FHairVisibilityDepthPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityDepthPS::FOutputType>(0);
	TShaderMapRef<FHairVisibilityDepthPS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutDepthTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVisibilityWriteColorAndDepth"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
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
}

static void AddHairOnlyDepthPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& CategorisationTexture,
	FRDGTextureRef& OutDepthTexture)
{
	if (!OutDepthTexture)
	{
		return;
	}

	FHairVisibilityDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityDepthPS::FParameters>();
	Parameters->CategorisationTexture = CategorisationTexture;
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthWrite_StencilNop);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	FHairVisibilityDepthPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityDepthPS::FOutputType>(1);
	TShaderMapRef<FHairVisibilityDepthPS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutDepthTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;

	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsVisibilityHairOnlyDepth"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
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
	const FHairLUT& HairLUT,
	const FRDGTextureRef HairCountTexture)
{
	const FIntPoint OutputResolution = HairCountTexture->Desc.Extent;

	check(HairCountTexture->Desc.Format == PF_R32_UINT || HairCountTexture->Desc.Format == PF_G32R32F)
	const bool bUseOneChannel = HairCountTexture->Desc.Format == PF_R32_UINT;

	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(OutputResolution, PF_R32_FLOAT, FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f)), TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable);
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityTexture"));
	FRDGTextureRef HairCoverageLUT = HairLUT.Textures[HairLUTType_Coverage];

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

///////////////////////////////////////////////////////////////////////////////////////////////////

class FVisiblityRasterComputeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisiblityRasterComputeCS);
	SHADER_USE_PARAMETER_STRUCT(FVisiblityRasterComputeCS, FGlobalShader);

	class FRasterAtomic : SHADER_PERMUTATION_INT("PERMUTATION_RASTER_ATOMIC", 4);
	class FSPP : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_SPP", 1, 2, 4); 
	class FCulling : SHADER_PERMUTATION_INT("PERMUTATION_CULLING", 2);
	using FPermutationDomain = TShaderPermutationDomain<FRasterAtomic, FSPP, FCulling>;

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
		SHADER_PARAMETER(float, HairStrandsVF_Length)
		SHADER_PARAMETER(uint32, HairStrandsVF_bUseStableRasterization)
		SHADER_PARAMETER(uint32, HairStrandsVF_VertexCount)
		SHADER_PARAMETER(FMatrix, HairStrandsVF_LocalToWorldPrimitiveTransform)
		SHADER_PARAMETER_SRV(Buffer, HairStrandsVF_PositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, HairStrandsVF_PositionOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairStrandsVF_CullingIndirectBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairStrandsVF_CullingIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairStrandsVF_CullingRadiusScaleBuffer)
		SHADER_PARAMETER_RDG_BUFFER(Buffer, IndirectBufferArgs)
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
	Out.HairCountTexture = GraphBuilder.CreateTexture(DescCount, TEXT("HairViewTransmittanceTexture"));
	FRDGTextureUAVRef HairCountTextureUAV = GraphBuilder.CreateUAV(Out.HairCountTexture);
	AddClearUAVPass(GraphBuilder, HairCountTextureUAV, ClearValues);

	Out.VisibilityTexture0 = GraphBuilder.CreateTexture(DescVis, TEXT("HairVisibilityTexture0"));
	VisibilityTexture0UAV = GraphBuilder.CreateUAV(Out.VisibilityTexture0);
	AddClearUAVPass(GraphBuilder, VisibilityTexture0UAV, ClearValues);
	if (SamplePerPixelCount > 1)
	{
		Out.VisibilityTexture1 = GraphBuilder.CreateTexture(DescVis, TEXT("HairVisibilityTexture1"));
		VisibilityTexture1UAV = GraphBuilder.CreateUAV(Out.VisibilityTexture1);
		AddClearUAVPass(GraphBuilder, VisibilityTexture1UAV, ClearValues);
		if (SamplePerPixelCount > 2)
		{
			Out.VisibilityTexture2 = GraphBuilder.CreateTexture(DescVis, TEXT("HairVisibilityTexture2"));
			VisibilityTexture2UAV = GraphBuilder.CreateUAV(Out.VisibilityTexture2);
			AddClearUAVPass(GraphBuilder, VisibilityTexture2UAV, ClearValues);
			if (SamplePerPixelCount > 3)
			{
				Out.VisibilityTexture3 = GraphBuilder.CreateTexture(DescVis, TEXT("HairVisibilityTexture3"));
				VisibilityTexture3UAV = GraphBuilder.CreateUAV(Out.VisibilityTexture3);
				AddClearUAVPass(GraphBuilder, VisibilityTexture3UAV, ClearValues);
			}
		}
	}

	// Create and set the uniform buffer
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
	PermutationVector0.Set<FVisiblityRasterComputeCS::FSPP>(SamplePerPixelCount);
	PermutationVector1 = PermutationVector0; 

	PermutationVector0.Set<FVisiblityRasterComputeCS::FCulling>(0);
	PermutationVector1.Set<FVisiblityRasterComputeCS::FCulling>(1);
	TShaderMapRef<FVisiblityRasterComputeCS> ComputeShader_CullingOff(ViewInfo.ShaderMap, PermutationVector0);
	TShaderMapRef<FVisiblityRasterComputeCS> ComputeShader_CullingOn (ViewInfo.ShaderMap, PermutationVector1);

	for (const FHairStrandsMacroGroupData& MacroGroup : MacroGroupDatas.Datas)
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
			
			check(PrimitiveInfo.MeshBatchAndRelevance.Mesh && PrimitiveInfo.MeshBatchAndRelevance.Mesh->Elements.Num() > 0);
			const FHairGroupPublicData* HairGroupPublicData = reinterpret_cast<const FHairGroupPublicData*>(PrimitiveInfo.MeshBatchAndRelevance.Mesh->Elements[0].VertexFactoryUserData);
			check(HairGroupPublicData);

			const FHairGroupPublicData::FVertexFactoryInput& VFInput = HairGroupPublicData->VFInput;
			PassParameters->HairStrandsVF_PositionBuffer	= VFInput.Strands.PositionBuffer;
			PassParameters->HairStrandsVF_PositionOffsetBuffer = VFInput.Strands.PositionOffsetBuffer;
			PassParameters->HairStrandsVF_VertexCount		= VFInput.Strands.VertexCount;
			PassParameters->HairStrandsVF_Radius			= VFInput.Strands.HairRadius;
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

FHairStrandsVisibilityViews RenderHairStrandsVisibilityBuffer(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const TArray<FViewInfo>& Views,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferATexture,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferBTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferCTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferDTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferETexture,
	TRefCountPtr<IPooledRenderTarget> InSceneColorTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneDepthTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneVelocityTexture,
	const FHairStrandsMacroGroupViews& MacroGroupViews)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_CLM_RenderHairStrandsVisibility);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsVisibility");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsVisibility);

	FRDGTextureRef SceneGBufferATexture = TryRegisterExternalTexture(GraphBuilder, InSceneGBufferATexture);
	FRDGTextureRef SceneGBufferBTexture = TryRegisterExternalTexture(GraphBuilder, InSceneGBufferBTexture);
	FRDGTextureRef SceneGBufferCTexture = TryRegisterExternalTexture(GraphBuilder, InSceneGBufferCTexture);
	FRDGTextureRef SceneGBufferDTexture = TryRegisterExternalTexture(GraphBuilder, InSceneGBufferDTexture);
	FRDGTextureRef SceneGBufferETexture = TryRegisterExternalTexture(GraphBuilder, InSceneGBufferETexture);
	FRDGTextureRef SceneColorTexture = TryRegisterExternalTexture(GraphBuilder, InSceneColorTexture);
	FRDGTextureRef SceneDepthTexture = GraphBuilder.RegisterExternalTexture(InSceneDepthTexture);
	FRDGTextureRef SceneVelocityTexture = TryRegisterExternalTexture(GraphBuilder, InSceneVelocityTexture);

	FHairStrandsVisibilityViews Output;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.Family)
		{
			FHairLUT HairLUT = GetHairLUT(GraphBuilder, View);

			FHairStrandsVisibilityData& VisibilityData = Output.HairDatas.AddDefaulted_GetRef();
			VisibilityData.NodeGroupSize = GetVendorOptimalGroupSize1D();
			VisibilityData.MaxSampleCount = GetMaxSamplePerPixel();
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = MacroGroupViews.Views[ViewIndex];

			if (MacroGroupDatas.Datas.Num() == 0)
				continue;

			// Use the scene color for computing target resolution as the View.ViewRect, 
			// doesn't include the actual resolution padding which make buffer size 
			// mismatch, and create artifact (e.g. velocity computation)
			check(InSceneDepthTexture);
			const FIntPoint Resolution = InSceneDepthTexture->GetDesc().Extent;

			const bool bRunColorAndDepthPatching = SceneGBufferBTexture && SceneColorTexture;
			const EHairVisibilityRenderMode RenderMode = GetHairVisibilityRenderMode();
			check(RenderMode == HairVisibilityRenderMode_MSAA || RenderMode == HairVisibilityRenderMode_PPLL || RenderMode == HairVisibilityRenderMode_ComputeRaster);

			FRDGTextureRef HairOnlyDepthTexture = GraphBuilder.CreateTexture(SceneDepthTexture->Desc, TEXT("HairStrandsHairOnlyDepthTexture"));
			FRDGTextureRef CategorizationTexture = nullptr;
			FRDGTextureRef CompactNodeIndex = nullptr;
			FRDGBufferRef  CompactNodeData = nullptr;
			FRDGTextureRef NodeCounter = nullptr;
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
						HairLUT,
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
							MacroGroupDatas,
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

						// Evaluate material based on the visiblity pass result
						// Output both complete sample data + per-sample velocity
						FMaterialPassOutput PassOutput = AddHairMaterialPass(
							GraphBuilder,
							Scene,
							&View,
							false,
							MacroGroupDatas,
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

					VisibilityData.ViewHairVisibilityTexture0 = RasterOutput.VisibilityTexture0;
					VisibilityData.ViewHairVisibilityTexture1 = RasterOutput.VisibilityTexture1;
					VisibilityData.ViewHairVisibilityTexture2 = RasterOutput.VisibilityTexture2;
					VisibilityData.ViewHairVisibilityTexture3 = RasterOutput.VisibilityTexture3;

					// For fully covered pixels, write: 
					// * black color into the scene color
					// * closest depth
					// * unlit shading model ID 
					if (bRunColorAndDepthPatching)
					{
						AddHairVisibilityColorAndDepthPatchPass(
							GraphBuilder,
							View,
							CategorizationTexture,
							SceneGBufferBTexture,
							SceneGBufferCTexture,
							SceneColorTexture,
							SceneDepthTexture);
					}

					AddHairOnlyDepthPass(
						GraphBuilder,
						View,
						CategorizationTexture,
						HairOnlyDepthTexture);
				}
				else
				{
					AddHairMaterialGBufferPass(
						GraphBuilder,
						Scene,
						&View,
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
			else if (RenderMode == HairVisibilityRenderMode_MSAA)
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
						SceneDepthTexture);

					const bool bHairCountToTransmittance = GHairStrandsHairCountToTransmittance > 0;
					if (bHairCountToTransmittance)
					{
						ViewTransmittance.TransmittanceTexture = AddHairHairCountToTransmittancePass(
							GraphBuilder,
							View,
							HairLUT,
							ViewTransmittance.HairCountTexture);
					}

				}

				const bool bIsVisiblityEnable = GHairStrandsVisibilityMaterialPass > 0;

				struct FRDGMsaaVisibilityResources
				{
					FRDGTextureRef DepthTexture;
					FRDGTextureRef IdTexture;
					FRDGTextureRef MaterialTexture;
					FRDGTextureRef AttributeTexture;
					FRDGTextureRef VelocityTexture;
				} MsaaVisibilityResources;

				MsaaVisibilityResources.DepthTexture = AddHairVisibilityFillOpaqueDepth(
					GraphBuilder,
					View,
					Resolution,
					MacroGroupDatas,
					SceneDepthTexture);

				AddHairVisibilityMSAAPass(
					bIsVisiblityEnable,
					GraphBuilder,
					Scene,
					&View,
					MacroGroupDatas,
					Resolution,
					MsaaVisibilityResources.IdTexture,
					MsaaVisibilityResources.MaterialTexture,
					MsaaVisibilityResources.AttributeTexture,
					MsaaVisibilityResources.VelocityTexture,
					MsaaVisibilityResources.DepthTexture);

				// This is used when compaction is not enabled.
				VisibilityData.MaxSampleCount = MsaaVisibilityResources.IdTexture->Desc.NumSamples;
				VisibilityData.IDTexture = MsaaVisibilityResources.IdTexture;
				VisibilityData.DepthTexture = MsaaVisibilityResources.DepthTexture;
				VisibilityData.HairOnlyDepthTexture = HairOnlyDepthTexture;
				if (!bIsVisiblityEnable)
				{
					VisibilityData.MaterialTexture = MsaaVisibilityResources.MaterialTexture;
					VisibilityData.AttributeTexture = MsaaVisibilityResources.AttributeTexture;
					VisibilityData.VelocityTexture = MsaaVisibilityResources.VelocityTexture;
				}

				const bool bUseComplexPath = IsHairStrandsComplexLightingEnabled();
				if (bUseComplexPath)
				{
					FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityPrimitiveIdCompactionCS::FParameters>();
					PassParameters->MSAA_DepthTexture = MsaaVisibilityResources.DepthTexture;
					PassParameters->MSAA_IDTexture = MsaaVisibilityResources.IdTexture;
					PassParameters->MSAA_MaterialTexture = MsaaVisibilityResources.MaterialTexture;
					PassParameters->MSAA_AttributeTexture = MsaaVisibilityResources.AttributeTexture;
					PassParameters->MSAA_VelocityTexture = MsaaVisibilityResources.VelocityTexture;
					PassParameters->ViewTransmittanceTexture = ViewTransmittance.TransmittanceTexture;

					FRDGBufferRef CompactNodeCoord;
					FRDGBufferRef IndirectArgsBuffer;
					FRDGTextureRef ResolveMaskTexture = nullptr;
					FRDGTextureRef EmissiveTexture = nullptr;
					AddHairVisibilityPrimitiveIdCompactionPass(
						false, // bUsePPLL
						bIsVisiblityEnable,
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

					if (bIsVisiblityEnable)
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
							CategorizationTexture,
							SceneGBufferBTexture,
							SceneGBufferCTexture,
							SceneColorTexture,
							SceneDepthTexture);
					}

					AddHairOnlyDepthPass(
						GraphBuilder,
						View,
						CategorizationTexture,
						HairOnlyDepthTexture);
				}
				else
				{
					AddHairMaterialGBufferPass(
						GraphBuilder,
						Scene,
						&View,
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
				AddHairVisibilityPPLLPass(GraphBuilder, Scene, &View, MacroGroupDatas, Resolution, ViewZDepthTexture, PPLLNodeCounterTexture, PPLLNodeIndexTexture, PPLLNodeDataBuffer);

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
						false,
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

				if (bRunColorAndDepthPatching)
				{
					AddHairVisibilityColorAndDepthPatchPass(
						GraphBuilder,
						View,
						CategorizationTexture,
						SceneGBufferBTexture,
						SceneGBufferCTexture,
						SceneColorTexture,
						SceneDepthTexture);
				}

				AddHairOnlyDepthPass(
					GraphBuilder,
					View,
					CategorizationTexture,
					HairOnlyDepthTexture);

				// Allocate buffer for storing all the light samples
				FRDGTextureRef SampleLightingBuffer = AddClearLightSamplePass(GraphBuilder, &View, VisibilityData.MaxNodeCount, NodeCounter);
				VisibilityData.SampleLightingViewportResolution = SampleLightingBuffer->Desc.Extent;
				VisibilityData.SampleLightingBuffer = SampleLightingBuffer;

			#if WITH_EDITOR
				// Extract texture for debug visualization
				VisibilityData.PPLLNodeCounterTexture = PPLLNodeCounterTexture;
				VisibilityData.PPLLNodeIndexTexture = PPLLNodeIndexTexture;
				VisibilityData.PPLLNodeDataBuffer = PPLLNodeDataBuffer;
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

			// Generate Tile data
			if (CategorizationTexture)
			{
				FRDGTextureRef TileIndexTexture = nullptr;
				FRDGBufferRef TileBuffer = nullptr;
				FRDGBufferRef TileIndirectArgs = nullptr;
				AddGenerateTilePass(GraphBuilder, View, VisibilityData.TileThreadGroupSize, VisibilityData.TileSize, CategorizationTexture, TileIndexTexture, TileBuffer, TileIndirectArgs);

				VisibilityData.TileIndexTexture = TileIndexTexture;
				VisibilityData.TileBuffer = TileBuffer;
				VisibilityData.TileIndirectArgs = TileIndirectArgs;
			}
		}
	}

	return Output;
}
