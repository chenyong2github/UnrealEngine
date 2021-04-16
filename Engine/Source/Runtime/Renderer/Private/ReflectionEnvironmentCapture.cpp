// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Functionality for capturing the scene into reflection capture cubemaps, and prefiltering
=============================================================================*/

#include "ReflectionEnvironmentCapture.h"
#include "Misc/FeedbackContext.h"
#include "RenderingThread.h"
#include "RenderResource.h"
#include "ShowFlags.h"
#include "UnrealClient.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "LegacyScreenPercentageDriver.h"
#include "Shader.h"
#include "TextureResource.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "SceneManagement.h"
#include "Components/SkyLightComponent.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Engine/TextureCube.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "ScreenRendering.h"
#include "ReflectionEnvironment.h"
#include "OneColorShader.h"
#include "PipelineStateCache.h"
#include "MobileReflectionEnvironmentCapture.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "EngineModule.h"
#include "ClearQuad.h"
#include "VolumetricCloudRendering.h"
#include "VolumetricCloudProxy.h"
#include "RenderGraphUtils.h"

/** Near plane to use when capturing the scene. */
float GReflectionCaptureNearPlane = 5;

constexpr int32 MinSupersampleCaptureFactor = 1;
constexpr int32 MaxSupersampleCaptureFactor = 8;

int32 GSupersampleCaptureFactor = 1;
static FAutoConsoleVariableRef CVarGSupersampleCaptureFactor(
	TEXT("r.ReflectionCaptureSupersampleFactor"),
	GSupersampleCaptureFactor,
	TEXT("Super sample factor when rendering reflection captures.\n")
	TEXT("Default = 1, no super sampling\n")
	TEXT("Maximum clamped to 8."),
	ECVF_RenderThreadSafe
	);

/** 
 * Mip map used by a Roughness of 0, counting down from the lowest resolution mip (MipCount - 1).  
 * This has been tweaked along with ReflectionCaptureRoughnessMipScale to make good use of the resolution in each mip, especially the highest resolution mips.
 * This value is duplicated in ReflectionEnvironmentShared.usf!
 */
float ReflectionCaptureRoughestMip = 1;

/** 
 * Scales the log2 of Roughness when computing which mip to use for a given roughness.
 * Larger values make the higher resolution mips sharper.
 * This has been tweaked along with ReflectionCaptureRoughnessMipScale to make good use of the resolution in each mip, especially the highest resolution mips.
 * This value is duplicated in ReflectionEnvironmentShared.usf!
 */
float ReflectionCaptureRoughnessMipScale = 1.2f;

int32 GDiffuseIrradianceCubemapSize = 32;

static TAutoConsoleVariable<int32> CVarReflectionCaptureGPUArrayCopy(
	TEXT("r.ReflectionCaptureGPUArrayCopy"),
	1,
	TEXT("Do a fast copy of the reflection capture array when resizing if possible. This avoids hitches on the rendering thread when the cubemap array needs to grow.\n")
	TEXT(" 0 is off, 1 is on (default)"),
	ECVF_ReadOnly);

// Chaos addition
static TAutoConsoleVariable<int32> CVarReflectionCaptureStaticSceneOnly(
	TEXT("r.chaos.ReflectionCaptureStaticSceneOnly"),
	1,
	TEXT("")
	TEXT(" 0 is off, 1 is on (default)"),
	ECVF_ReadOnly);

static int32 GFreeReflectionScratchAfterUse = 0;
static FAutoConsoleVariableRef CVarFreeReflectionScratchAfterUse(
	TEXT("r.FreeReflectionScratchAfterUse"),
	GFreeReflectionScratchAfterUse,
	TEXT("Free reflection scratch render targets after use."));

bool DoGPUArrayCopy()
{
	return GRHISupportsResolveCubemapFaces && CVarReflectionCaptureGPUArrayCopy.GetValueOnAnyThread();
}

void FullyResolveReflectionScratchCubes(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList, FullyResolveReflectionScratchCubes);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FTextureRHIRef& Scratch0 = SceneContext.ReflectionColorScratchCubemap[0]->GetRenderTargetItem().TargetableTexture;
	FTextureRHIRef& Scratch1 = SceneContext.ReflectionColorScratchCubemap[1]->GetRenderTargetItem().TargetableTexture;
	FResolveParams ResolveParams(FResolveRect(), CubeFace_PosX, -1, -1, -1);
	RHICmdList.CopyToResolveTarget(Scratch0, Scratch0, ResolveParams);
	RHICmdList.CopyToResolveTarget(Scratch1, Scratch1, ResolveParams);  
}

IMPLEMENT_SHADER_TYPE(,FCubeFilterPS,TEXT("/Engine/Private/ReflectionEnvironmentShaders.usf"),TEXT("DownsamplePS"),SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>,TCubeFilterPS<0>,TEXT("/Engine/Private/ReflectionEnvironmentShaders.usf"),TEXT("FilterPS"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TCubeFilterPS<1>,TEXT("/Engine/Private/ReflectionEnvironmentShaders.usf"),TEXT("FilterPS"),SF_Pixel);

/** Computes the average brightness of a 1x1 mip of a cubemap. */
class FComputeBrightnessPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputeBrightnessPS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPUTEBRIGHTNESS_PIXELSHADER"), 1);
	}

	FComputeBrightnessPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReflectionEnvironmentColorTexture.Bind(Initializer.ParameterMap,TEXT("ReflectionEnvironmentColorTexture"));
		ReflectionEnvironmentColorSampler.Bind(Initializer.ParameterMap,TEXT("ReflectionEnvironmentColorSampler"));
		NumCaptureArrayMips.Bind(Initializer.ParameterMap, TEXT("NumCaptureArrayMips"));
	}

	FComputeBrightnessPS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, int32 TargetSize, FSceneRenderTargetItem& Cubemap)
	{
		const int32 EffectiveTopMipSize = TargetSize;
		const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;
		// Read from the smallest mip that was downsampled to

		if (Cubemap.IsValid())
		{
			SetTextureParameter(
				RHICmdList,
				RHICmdList.GetBoundPixelShader(),
				ReflectionEnvironmentColorTexture, 
				ReflectionEnvironmentColorSampler, 
				TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), 
				Cubemap.ShaderResourceTexture);
		}

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), NumCaptureArrayMips, FMath::CeilLogTwo(TargetSize) + 1);
	}

private:

	LAYOUT_FIELD(FShaderResourceParameter, ReflectionEnvironmentColorTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ReflectionEnvironmentColorSampler);
	LAYOUT_FIELD(FShaderParameter, NumCaptureArrayMips);
};

IMPLEMENT_SHADER_TYPE(,FComputeBrightnessPS,TEXT("/Engine/Private/ReflectionEnvironmentShaders.usf"),TEXT("ComputeBrightnessMain"),SF_Pixel);

void CreateCubeMips( FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 NumMips, FSceneRenderTargetItem& Cubemap )
{	
	SCOPED_DRAW_EVENT(RHICmdList, CreateCubeMips);

	FRHITexture* CubeRef = Cubemap.TargetableTexture.GetReference();

	auto* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	TArray<TPair<FRHITextureSRVCreateInfo, TRefCountPtr<FRHIShaderResourceView>>> SRVs;
	SRVs.Empty(NumMips);

	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		FRHITextureSRVCreateInfo SRVDesc;
		SRVDesc.MipLevel = MipIndex;
		SRVs.Emplace(SRVDesc, RHICreateShaderResourceView(Cubemap.ShaderResourceTexture, SRVDesc));
	}

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	// Downsample all the mips, each one reads from the mip above it
	for (int32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
	{
		// For the first iteration, we don't know what the previous state
		// of the source mip was, but we *do* for all the other iterations...
		ERHIAccess Previous = MipIndex == 1
			? ERHIAccess::Unknown
			: ERHIAccess::RTV;

		FRHITransitionInfo Transitions[] =
		{
			// Make the source mip readable (SRVGraphics)
			FRHITransitionInfo(CubeRef, Previous, ERHIAccess::SRVGraphics, EResourceTransitionFlags::None, uint32(MipIndex - 1)),

			// Make the destination mip writable (RTV)
			FRHITransitionInfo(CubeRef, ERHIAccess::Unknown, ERHIAccess::RTV, EResourceTransitionFlags::None, uint32(MipIndex))
		};
		RHICmdList.Transition(Transitions);

		const int32 MipSize = 1 << (NumMips - MipIndex - 1);
		SCOPED_DRAW_EVENT(RHICmdList, CreateCubeMipsPerFace);
		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			FRHIRenderPassInfo RPInfo(Cubemap.TargetableTexture, ERenderTargetActions::DontLoad_Store, nullptr, MipIndex, CubeFace);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("CreateCubeMips"));
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			const FIntRect ViewRect(0, 0, MipSize, MipSize);
			RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, (float)MipSize, (float)MipSize, 1.0f);


			TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
			TShaderMapRef<FCubeFilterPS> PixelShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			{
				FRHIPixelShader* ShaderRHI = PixelShader.GetPixelShader();

				SetShaderValue(RHICmdList, ShaderRHI, PixelShader->CubeFace, CubeFace);
				SetShaderValue(RHICmdList, ShaderRHI, PixelShader->MipIndex, MipIndex);

				SetShaderValue(RHICmdList, ShaderRHI, PixelShader->NumMips, NumMips);

				check(SRVs.IsValidIndex(MipIndex - 1) && SRVs[MipIndex - 1].Key.MipLevel == MipIndex - 1);
				SetSRVParameter(RHICmdList, ShaderRHI, PixelShader->SourceCubemapTexture, SRVs[MipIndex - 1].Value);
				SetSamplerParameter(RHICmdList, ShaderRHI, PixelShader->SourceCubemapSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
			}

			DrawRectangle(
				RHICmdList,
				ViewRect.Min.X, ViewRect.Min.Y,
				ViewRect.Width(), ViewRect.Height(),
				ViewRect.Min.X, ViewRect.Min.Y,
				ViewRect.Width(), ViewRect.Height(),
				FIntPoint(ViewRect.Width(), ViewRect.Height()),
				FIntPoint(MipSize, MipSize),
				VertexShader);

			RHICmdList.EndRenderPass();
		}
	}

	RHICmdList.Transition(FRHITransitionInfo(CubeRef, ERHIAccess::Unknown, ERHIAccess::SRVMask));

	SRVs.Empty();
}

/** Computes the average brightness of the given reflection capture and stores it in the scene. */
float ComputeSingleAverageBrightnessFromCubemap(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 TargetSize, FSceneRenderTargetItem& Cubemap)
{
	SCOPED_DRAW_EVENT(RHICmdList, ComputeSingleAverageBrightnessFromCubemap);

	TRefCountPtr<IPooledRenderTarget> ReflectionBrightnessTarget;
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable, false));
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ReflectionBrightnessTarget, TEXT("ReflectionBrightness"));

	FTextureRHIRef& BrightnessTarget = ReflectionBrightnessTarget->GetRenderTargetItem().TargetableTexture;

	FRHIRenderPassInfo RPInfo(BrightnessTarget, ERenderTargetActions::Load_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ReflectionBrightness"));
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FPostProcessVS> VertexShader(ShaderMap);
		TShaderMapRef<FComputeBrightnessPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, TargetSize, Cubemap);

		DrawRectangle(
			RHICmdList,
			0, 0,
			1, 1,
			0, 0,
			1, 1,
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			VertexShader);
	}
	RHICmdList.EndRenderPass();
	RHICmdList.CopyToResolveTarget(BrightnessTarget, BrightnessTarget, FResolveParams());

	FSceneRenderTargetItem& EffectiveRT = ReflectionBrightnessTarget->GetRenderTargetItem();
	check(EffectiveRT.ShaderResourceTexture->GetFormat() == PF_FloatRGBA);

	TArray<FFloat16Color> SurfaceData;
	RHICmdList.ReadSurfaceFloatData(EffectiveRT.ShaderResourceTexture, FIntRect(0, 0, 1, 1), SurfaceData, CubeFace_PosX, 0, 0);

	// Shader outputs luminance to R
	float AverageBrightness = SurfaceData[0].R.GetFloat();
	return AverageBrightness;
}

void ComputeAverageBrightness(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 CubmapSize, float& OutAverageBrightness)
{
	SCOPED_DRAW_EVENT(RHICmdList, ComputeAverageBrightness);

	const int32 EffectiveTopMipSize = CubmapSize;
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	// necessary to resolve the clears which touched all the mips.  scene rendering only resolves mip 0.
	FullyResolveReflectionScratchCubes(RHICmdList);	

	FSceneRenderTargetItem& DownSampledCube = FSceneRenderTargets::Get(RHICmdList).ReflectionColorScratchCubemap[0]->GetRenderTargetItem();
	CreateCubeMips( RHICmdList, FeatureLevel, NumMips, DownSampledCube );

	OutAverageBrightness = ComputeSingleAverageBrightnessFromCubemap(RHICmdList, FeatureLevel, CubmapSize, DownSampledCube);
}


void FilterCubeMap(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 NumMips, 
	FSceneRenderTargetItem& DownSampledCube, FSceneRenderTargetItem& FilteredCube)
{
	SCOPED_DRAW_EVENT(RHICmdList, FilterCubeMap);
	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	RHICmdList.Transition(FRHITransitionInfo(FilteredCube.TargetableTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

	// Filter all the mips
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		const int32 MipSize = 1 << (NumMips - MipIndex - 1);

		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			FRHIRenderPassInfo RPInfo(FilteredCube.TargetableTexture, ERenderTargetActions::DontLoad_Store, nullptr, MipIndex, CubeFace);
			RHICmdList.Transition(FRHITransitionInfo(FilteredCube.TargetableTexture, ERHIAccess::Unknown, ERHIAccess::RTV));
			RHICmdList.BeginRenderPass(RPInfo, TEXT("FilterMips"));

			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			const FIntRect ViewRect(0, 0, MipSize, MipSize);
			RHICmdList.SetViewport(0, 0, 0.0f, MipSize, MipSize, 1.0f);

			//TShaderMapRef<TCubeFilterPS<1>> CaptureCubemapArrayPixelShader(GetGlobalShaderMap(FeatureLevel));

			TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
			TShaderMapRef<TCubeFilterPS<0>> PixelShader(GetGlobalShaderMap(FeatureLevel));
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			{
				FRHIPixelShader* ShaderRHI = PixelShader.GetPixelShader();

				SetShaderValue(RHICmdList, ShaderRHI, PixelShader->CubeFace, CubeFace);
				SetShaderValue(RHICmdList, ShaderRHI, PixelShader->MipIndex, MipIndex);

				SetShaderValue(RHICmdList, ShaderRHI, PixelShader->NumMips, NumMips);

				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					PixelShader->SourceCubemapTexture,
					PixelShader->SourceCubemapSampler,
					TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					DownSampledCube.ShaderResourceTexture);
			}

			DrawRectangle(
				RHICmdList,
				ViewRect.Min.X, ViewRect.Min.Y,
				ViewRect.Width(), ViewRect.Height(),
				ViewRect.Min.X, ViewRect.Min.Y,
				ViewRect.Width(), ViewRect.Height(),
				FIntPoint(ViewRect.Width(), ViewRect.Height()),
				FIntPoint(MipSize, MipSize),
				VertexShader);

			RHICmdList.EndRenderPass();
		}
	}
}

/** Generates mips for glossiness and filters the cubemap for a given reflection. */
void FilterReflectionEnvironment(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 CubmapSize, FSHVectorRGB3* OutIrradianceEnvironmentMap)
{
	SCOPED_DRAW_EVENT(RHICmdList, FilterReflectionEnvironment);

	const int32 EffectiveTopMipSize = CubmapSize;
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	FSceneRenderTargetItem& EffectiveColorRT = FSceneRenderTargets::Get(RHICmdList).ReflectionColorScratchCubemap[0]->GetRenderTargetItem();

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_DestAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
		
	RHICmdList.Transition(FRHITransitionInfo(EffectiveColorRT.TargetableTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

	// Premultiply alpha in-place using alpha blending
	for (uint32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
	{
		FRHIRenderPassInfo RPInfo(EffectiveColorRT.TargetableTexture, ERenderTargetActions::Load_Store, nullptr, 0, CubeFace);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("FilterReflectionEnvironmentRP"));
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		const FIntPoint SourceDimensions(CubmapSize, CubmapSize);
		const FIntRect ViewRect(0, 0, EffectiveTopMipSize, EffectiveTopMipSize);
		RHICmdList.SetViewport(0, 0, 0.0f, EffectiveTopMipSize, EffectiveTopMipSize, 1.0f);

		TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
		TShaderMapRef<FOneColorPS> PixelShader(GetGlobalShaderMap(FeatureLevel));

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		FLinearColor UnusedColors[1] = { FLinearColor::Black };
		PixelShader->SetColors(RHICmdList, UnusedColors, UE_ARRAY_COUNT(UnusedColors));

		DrawRectangle(
			RHICmdList,
			ViewRect.Min.X, ViewRect.Min.Y, 
			ViewRect.Width(), ViewRect.Height(),
			0, 0, 
			SourceDimensions.X, SourceDimensions.Y,
			FIntPoint(ViewRect.Width(), ViewRect.Height()),
			SourceDimensions,
			VertexShader);

		RHICmdList.EndRenderPass();
	}

	RHICmdList.Transition(FRHITransitionInfo(EffectiveColorRT.TargetableTexture, ERHIAccess::Unknown, ERHIAccess::SRVMask));

	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	FSceneRenderTargetItem& DownSampledCube = FSceneRenderTargets::Get(RHICmdList).ReflectionColorScratchCubemap[0]->GetRenderTargetItem();
	FSceneRenderTargetItem& FilteredCube = FSceneRenderTargets::Get(RHICmdList).ReflectionColorScratchCubemap[1]->GetRenderTargetItem();

	CreateCubeMips( RHICmdList, FeatureLevel, NumMips, DownSampledCube );

	if (OutIrradianceEnvironmentMap)
	{
		SCOPED_DRAW_EVENT(RHICmdList, ComputeDiffuseIrradiance);
		
		const int32 NumDiffuseMips = FMath::CeilLogTwo( GDiffuseIrradianceCubemapSize ) + 1;
		const int32 DiffuseConvolutionSourceMip = FMath::Max(0, NumMips - NumDiffuseMips);

		ComputeDiffuseIrradiance(RHICmdList, FeatureLevel, DownSampledCube.ShaderResourceTexture, DiffuseConvolutionSourceMip, OutIrradianceEnvironmentMap);
	}

	FilterCubeMap(RHICmdList, FeatureLevel, NumMips, DownSampledCube, FilteredCube);
	RHICmdList.CopyToResolveTarget(FilteredCube.TargetableTexture, FilteredCube.ShaderResourceTexture, FResolveParams());
}

class FCopyToCubeFaceShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FCopyToCubeFaceShader() = default;
	FCopyToCubeFaceShader(const CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

/** Vertex shader used when writing to a cubemap. */
class FCopyToCubeFaceVS : public FCopyToCubeFaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FCopyToCubeFaceVS);
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FCopyToCubeFaceVS, FCopyToCubeFaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FCopyToCubeFaceVS, "/Engine/Private/ReflectionEnvironmentShaders.usf", "CopyToCubeFaceVS", SF_Vertex);

/** Pixel shader used when copying scene color from a scene render into a face of a reflection capture cubemap. */
class FCopySceneColorToCubeFacePS : public FCopyToCubeFaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FCopySceneColorToCubeFacePS);
	SHADER_USE_PARAMETER_STRUCT(FCopySceneColorToCubeFacePS, FCopyToCubeFaceShader);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FCopyToCubeFaceShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		if (IsMobilePlatform(Parameters.Platform))
		{
			// SceneDepth is memoryless on mobile
			OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), 1u);
		}
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthSampler)
		SHADER_PARAMETER(FVector, SkyLightCaptureParameters)
		SHADER_PARAMETER(FVector4, LowerHemisphereColor)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FCopySceneColorToCubeFacePS, "/Engine/Private/ReflectionEnvironmentShaders.usf", "CopySceneColorToCubeFaceColorPS", SF_Pixel);

/** Pixel shader used when copying a cubemap into a face of a reflection capture cubemap. */
class FCopyCubemapToCubeFacePS : public FCopyToCubeFaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FCopyCubemapToCubeFacePS);
	SHADER_USE_PARAMETER_STRUCT(FCopyCubemapToCubeFacePS, FCopyToCubeFaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(TextureCube, SourceCubemapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceCubemapSampler)
		SHADER_PARAMETER(FVector, SkyLightCaptureParameters)
		SHADER_PARAMETER(int32, CubeFace)
		SHADER_PARAMETER(FVector4, LowerHemisphereColor)
		SHADER_PARAMETER(FVector2D, SinCosSourceCubemapRotation)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FCopyCubemapToCubeFacePS, "/Engine/Private/ReflectionEnvironmentShaders.usf", "CopyCubemapToCubeFaceColorPS", SF_Pixel);

int32 FindOrAllocateCubemapIndex(FScene* Scene, const UReflectionCaptureComponent* Component)
{
	int32 CubemapIndex = -1;

	// Try to find an existing capture index for this component
	const FCaptureComponentSceneState* CaptureSceneStatePtr = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Find(Component);

	if (CaptureSceneStatePtr)
	{
		CubemapIndex = CaptureSceneStatePtr->CubemapIndex;
	}
	else
	{
		// Reuse a freed index if possible
		CubemapIndex = Scene->ReflectionSceneData.CubemapArraySlotsUsed.FindAndSetFirstZeroBit();
		if (CubemapIndex == INDEX_NONE)
		{
			// If we didn't find a free index, allocate a new one from the CubemapArraySlotsUsed bitfield
			CubemapIndex = Scene->ReflectionSceneData.CubemapArraySlotsUsed.Num();
			Scene->ReflectionSceneData.CubemapArraySlotsUsed.Add(true);
		}

		Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Add(Component, FCaptureComponentSceneState(CubemapIndex));
		Scene->ReflectionSceneData.AllocatedReflectionCaptureStateHasChanged = true;

		check(CubemapIndex < GMaxNumReflectionCaptures);
	}

	check(CubemapIndex >= 0);
	return CubemapIndex;
}

void ClearScratchCubemaps(FRHICommandListImmediate& RHICmdList, int32 TargetSize)
{
	SCOPED_DRAW_EVENT(RHICmdList, ClearScratchCubemaps);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.AllocateReflectionTargets(RHICmdList, TargetSize);

	FMemMark Mark(FMemStack::Get());
	FRDGBuilder GraphBuilder(RHICmdList);

	for (int32 RenderTargetIndex = 0; RenderTargetIndex < 2; ++RenderTargetIndex)
	{
		FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(SceneContext.ReflectionColorScratchCubemap[RenderTargetIndex], TEXT("OutputCubemap"));

		RDG_EVENT_SCOPE(GraphBuilder, "ClearScratchCubemapsRT%d", RenderTargetIndex);

		const int32 NumMips = OutputTexture->Desc.NumMips;

		for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				FRenderTargetParameters* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
				PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::EClear, MipIndex, CubeFace);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("ClearCubeFace(Mip: %d, Face: %d)", MipIndex, CubeFace),
					PassParameters,
					ERDGPassFlags::Raster,
					[](FRHICommandList&) {});
			}
		}
	}

	GraphBuilder.Execute();
}

/** Captures the scene for a reflection capture by rendering the scene multiple times and copying into a cubemap texture. */
void CaptureSceneToScratchCubemap(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer, ECubeFace CubeFace, int32 CubemapSize, bool bCapturingForSkyLight, bool bLowerHemisphereIsBlack, const FLinearColor& LowerHemisphereColor, bool bCapturingForMobile)
{
	FMemMark MemStackMark(FMemStack::Get());

	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);
	FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

	const auto FeatureLevel = SceneRenderer->FeatureLevel;
	
	{
		SCOPED_DRAW_EVENT(RHICmdList, CubeMapCapture);

		// Render the scene normally for one face of the cubemap
		SceneRenderer->Render(RHICmdList);
		check(&RHICmdList == &FRHICommandListExecutor::GetImmediateCommandList());
		check(IsInRenderingThread());
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_CaptureSceneToScratchCubemap_Flush);
			FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		}

		// some platforms may not be able to keep enqueueing commands like crazy, this will
		// allow them to restart their command buffers
		RHICmdList.SubmitCommandsAndFlushGPU();

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		SceneContext.AllocateReflectionTargets(RHICmdList, CubemapSize);

		const FViewInfo& View = SceneRenderer->Views[0];

		FRDGBuilder GraphBuilder(RHICmdList);

		FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(SceneContext.ReflectionColorScratchCubemap[0], TEXT("ReflectionColorScratchCubemap"));

		auto* PassParameters = GraphBuilder.AllocParameters<FCopySceneColorToCubeFacePS::FParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction, 0, CubeFace);
		PassParameters->LowerHemisphereColor = LowerHemisphereColor;

		{
			FVector SkyLightParametersValue = FVector::ZeroVector;
			FScene* Scene = SceneRenderer->Scene;

			if (bCapturingForSkyLight)
			{
				// When capturing reflection captures, support forcing all low hemisphere lighting to be black
				SkyLightParametersValue = FVector(0, 0, bLowerHemisphereIsBlack ? 1.0f : 0.0f);
			}
			else if (!bCapturingForMobile && Scene->SkyLight && !Scene->SkyLight->bHasStaticLighting)	
			{
				// Mobile renderer can't blend reflections with a sky at runtime, so we dont use this path when capturing for a mobile renderer
				
				// When capturing reflection captures and there's a stationary sky light, mask out any pixels whose depth classify it as part of the sky
				// This will allow changing the stationary sky light at runtime
				SkyLightParametersValue = FVector(1, Scene->SkyLight->SkyDistanceThreshold, 0);
			}
			else
			{
				// When capturing reflection captures and there's no sky light, or only a static sky light, capture all depth ranges
				SkyLightParametersValue = FVector(2, 0, 0);
			}

			PassParameters->SkyLightCaptureParameters = SkyLightParametersValue;
		}

		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("ColorTexture"));
		PassParameters->SceneDepthSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SceneDepthTexture = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ, TEXT("DepthTexture"));

		const int32 EffectiveSize = CubemapSize;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CopySceneToCubeFace"),
			PassParameters,
			ERDGPassFlags::Raster,
			[EffectiveSize, &SceneContext, FeatureLevel, PassParameters](FRHICommandList& InRHICmdList)
		{
			const FIntRect ViewRect(0, 0, EffectiveSize, EffectiveSize);
			InRHICmdList.SetViewport(0.0f, 0.0f, 0.0f, (float)EffectiveSize, (float)EffectiveSize, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

			TShaderMapRef<FCopyToCubeFaceVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
			TShaderMapRef<FCopySceneColorToCubeFacePS> PixelShader(GetGlobalShaderMap(FeatureLevel));

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);

			FCopyToCubeFaceVS::FParameters VertexParameters;
			VertexParameters.View = PassParameters->View;
			SetShaderParameters(InRHICmdList, VertexShader, VertexShader.GetVertexShader(), VertexParameters);
			SetShaderParameters(InRHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

			const int32 SupersampleCaptureFactor = FMath::Clamp(GSupersampleCaptureFactor, MinSupersampleCaptureFactor, MaxSupersampleCaptureFactor);

			DrawRectangle( 
				InRHICmdList,
				ViewRect.Min.X, ViewRect.Min.Y, 
				ViewRect.Width(), ViewRect.Height(),
				ViewRect.Min.X, ViewRect.Min.Y, 
				ViewRect.Width() * SupersampleCaptureFactor, ViewRect.Height() * SupersampleCaptureFactor,
				FIntPoint(ViewRect.Width(), ViewRect.Height()),
				SceneContext.GetBufferSizeXY(),
				VertexShader);
		});

		GraphBuilder.Execute();
	}

	FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
}

void CopyCubemapToScratchCubemap(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, UTextureCube* SourceCubemap, int32 CubemapSize, bool bIsSkyLight, bool bLowerHemisphereIsBlack, float SourceCubemapRotation, const FLinearColor& LowerHemisphereColorValue)
{
	SCOPED_DRAW_EVENT(RHICmdList, CopyCubemapToScratchCubemap);
	check(SourceCubemap);

	const FTexture* SourceCubemapResource = SourceCubemap->Resource;
	if (SourceCubemapResource == nullptr)
	{
		UE_LOG(LogEngine, Warning, TEXT("Unable to copy from cubemap %s, it's RHI resource is null"), *SourceCubemap->GetPathName());
		return;
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	FMemMark Mark(FMemStack::Get());
	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(SceneContext.ReflectionColorScratchCubemap[0], TEXT("ReflectionColorScratchCubemap"));

	for (uint32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FCopyCubemapToCubeFacePS::FParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction, 0, CubeFace);
		PassParameters->LowerHemisphereColor = LowerHemisphereColorValue;
		PassParameters->SkyLightCaptureParameters = FVector(bIsSkyLight ? 1.0f : 0.0f, 0.0f, bLowerHemisphereIsBlack ? 1.0f : 0.0f);
		PassParameters->SourceCubemapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SourceCubemapTexture = SourceCubemapResource->TextureRHI;
		PassParameters->SinCosSourceCubemapRotation = FVector2D(FMath::Sin(SourceCubemapRotation), FMath::Cos(SourceCubemapRotation));
		PassParameters->CubeFace = CubeFace;

		const int32 EffectiveSize = CubemapSize;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CopyCubemapToCubeFace"),
			PassParameters,
			ERDGPassFlags::Raster,
			[EffectiveSize, &SceneContext, SourceCubemapResource, PassParameters, FeatureLevel](FRHICommandList& InRHICmdList)
		{
			const FIntPoint SourceDimensions(SourceCubemapResource->GetSizeX(), SourceCubemapResource->GetSizeY());
			const FIntRect ViewRect(0, 0, EffectiveSize, EffectiveSize);
			InRHICmdList.SetViewport(0.0f, 0.0f, 0.0f, (float)EffectiveSize, (float)EffectiveSize, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

			TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
			TShaderMapRef<FCopyCubemapToCubeFacePS> PixelShader(GetGlobalShaderMap(FeatureLevel));

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
			SetShaderParameters(InRHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

			DrawRectangle(
				InRHICmdList,
				ViewRect.Min.X, ViewRect.Min.Y,
				ViewRect.Width(), ViewRect.Height(),
				0, 0,
				SourceDimensions.X, SourceDimensions.Y,
				FIntPoint(ViewRect.Width(), ViewRect.Height()),
				SourceDimensions,
				VertexShader);
		});
	}

	GraphBuilder.Execute();
}

const int32 MinCapturesForSlowTask = 20;

void BeginReflectionCaptureSlowTask(int32 NumCaptures, const TCHAR* CaptureReason)
{
	if (NumCaptures > MinCapturesForSlowTask)
	{
		FText Status;
		
		if (CaptureReason)
		{
			Status = FText::Format(NSLOCTEXT("Engine", "UpdateReflectionCapturesForX", "Building reflection captures for {0}"), FText::FromString(FString(CaptureReason)));
		}
		else
		{
			Status = FText(NSLOCTEXT("Engine", "UpdateReflectionCaptures", "Building reflection captures..."));
		}

		GWarn->BeginSlowTask(Status, true);
		GWarn->StatusUpdate(0, NumCaptures, Status);
	}
}

void UpdateReflectionCaptureSlowTask(int32 CaptureIndex, int32 NumCaptures)
{
	const int32 UpdateDivisor = FMath::Max(NumCaptures / 5, 1);

	if (NumCaptures > MinCapturesForSlowTask && (CaptureIndex % UpdateDivisor) == 0)
	{
		GWarn->UpdateProgress(CaptureIndex, NumCaptures);
	}
}

void EndReflectionCaptureSlowTask(int32 NumCaptures)
{
	if (NumCaptures > MinCapturesForSlowTask)
	{
		GWarn->EndSlowTask();
	}
}

int32 GetReflectionCaptureSizeForArrayCount(int32 InRequestedCaptureSize, int32 InRequestedMaxCubeMaps)
{
	int32 OutCaptureSize = InRequestedCaptureSize;
#if WITH_EDITOR
	if(GIsEditor)
	{
		FTextureMemoryStats TextureMemStats;
		RHIGetTextureMemoryStats(TextureMemStats);
		
		SIZE_T TexMemRequired = CalcTextureSize(OutCaptureSize, OutCaptureSize, PF_FloatRGBA, FMath::CeilLogTwo(OutCaptureSize) + 1) * CubeFace_MAX * InRequestedMaxCubeMaps;
		// Assumption: Texture arrays prefer to be contiguous in memory but not always
		// Single large cube array allocations can fail on low end systems even if we try to fit it in total avail video and/or avail system memory
		
		// Attempt to limit the resource size to within percentage (3/4) of total video memory to give consistant stable results
		const SIZE_T MaxResourceVideoMemoryFootprint = ((SIZE_T)TextureMemStats.DedicatedVideoMemory * (SIZE_T)3) / (SIZE_T)4;
		
		// Bottom out at 128 as that is the default for CVarReflectionCaptureSize
        while(TexMemRequired > MaxResourceVideoMemoryFootprint && OutCaptureSize > 128)
        {
			OutCaptureSize = FMath::RoundUpToPowerOfTwo(OutCaptureSize) >> 1;
			TexMemRequired = CalcTextureSize(OutCaptureSize, OutCaptureSize, PF_FloatRGBA, FMath::CeilLogTwo(OutCaptureSize) + 1) * CubeFace_MAX * InRequestedMaxCubeMaps;
        }
        
        if(OutCaptureSize != InRequestedCaptureSize)
        {
			UE_LOG(LogEngine, Error, TEXT("Requested reflection capture cube size of %d with %d elements results in too large a resource for host machine, limiting reflection capture cube size to %d"), InRequestedCaptureSize, InRequestedMaxCubeMaps, OutCaptureSize);
        }
	}
#endif // WITH_EDITOR
	return OutCaptureSize;
}

/** 
 * Allocates reflection captures in the scene's reflection cubemap array and updates them by recapturing the scene.
 * Existing captures will only be uploaded.  Must be called from the game thread.
 */
void FScene::AllocateReflectionCaptures(const TArray<UReflectionCaptureComponent*>& NewCaptures, const TCHAR* CaptureReason, bool bVerifyOnlyCapturing, bool bCapturingForMobile)
{
	if (NewCaptures.Num() > 0)
	{
		if (SupportsTextureCubeArray(GetFeatureLevel()))
		{
			int32_t PlatformMaxNumReflectionCaptures = FMath::Min(FMath::FloorToInt(GMaxTextureArrayLayers / 6.0f), GMaxNumReflectionCaptures);

			for (int32 CaptureIndex = 0; CaptureIndex < NewCaptures.Num(); CaptureIndex++)
			{
				bool bAlreadyExists = false;

				// Try to find an existing allocation
				for (TSparseArray<UReflectionCaptureComponent*>::TIterator It(ReflectionSceneData.AllocatedReflectionCapturesGameThread); It; ++It)
				{
					UReflectionCaptureComponent* OtherComponent = *It;

					if (OtherComponent == NewCaptures[CaptureIndex])
					{
						bAlreadyExists = true;
					}
				}
				
				// Add the capture to the allocated list
				if (!bAlreadyExists && ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num() < PlatformMaxNumReflectionCaptures)
				{
					ReflectionSceneData.AllocatedReflectionCapturesGameThread.Add(NewCaptures[CaptureIndex]);
				}
			}

			// Request the exact amount needed by default
			int32 DesiredMaxCubemaps = ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num();
			const float MaxCubemapsRoundUpBase = 1.5f;

			// If this is not the first time the scene has allocated the cubemap array, include slack to reduce reallocations
			if (ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread > 0)
			{
				float Exponent = FMath::LogX(MaxCubemapsRoundUpBase, ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num());

				// Round up to the next integer exponent to provide stability and reduce reallocations
				DesiredMaxCubemaps = FMath::Pow(MaxCubemapsRoundUpBase, FMath::TruncToInt(Exponent) + 1);
			}

			DesiredMaxCubemaps = FMath::Min(DesiredMaxCubemaps, PlatformMaxNumReflectionCaptures);

			const int32 ReflectionCaptureSize = GetReflectionCaptureSizeForArrayCount(UReflectionCaptureComponent::GetReflectionCaptureSize(), DesiredMaxCubemaps);
			bool bNeedsUpdateAllCaptures = DesiredMaxCubemaps != ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread || ReflectionCaptureSize != ReflectionSceneData.CubemapArray.GetCubemapSize();

			if (DoGPUArrayCopy() && bNeedsUpdateAllCaptures)
			{
				// If we're not in the editor, we discard the CPU-side reflection capture data after loading to save memory, so we can't resize if the resolution changes. If this happens, we assert
				check(GIsEditor || ReflectionCaptureSize == ReflectionSceneData.CubemapArray.GetCubemapSize() || ReflectionSceneData.CubemapArray.GetCubemapSize() == 0);

				if (ReflectionCaptureSize == ReflectionSceneData.CubemapArray.GetCubemapSize())
				{
					// We can do a fast GPU copy to realloc the array, so we don't need to update all captures
					ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread = DesiredMaxCubemaps;
					FScene* Scene = this;
					uint32 MaxSize = ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread;
					ENQUEUE_RENDER_COMMAND(GPUResizeArrayCommand)(
						[Scene, MaxSize, ReflectionCaptureSize](FRHICommandListImmediate& RHICmdList)
						{
							// Update the scene's cubemap array, preserving the original contents with a GPU-GPU copy
							Scene->ReflectionSceneData.ResizeCubemapArrayGPU(MaxSize, ReflectionCaptureSize);
						});

					bNeedsUpdateAllCaptures = false;
				}
			}

			if (bNeedsUpdateAllCaptures)
			{
				ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread = DesiredMaxCubemaps;

				FScene* Scene = this;
				uint32 MaxSize = ReflectionSceneData.MaxAllocatedReflectionCubemapsGameThread;
				ENQUEUE_RENDER_COMMAND(ResizeArrayCommand)(
					[Scene, MaxSize, ReflectionCaptureSize](FRHICommandListImmediate& RHICmdList)
					{
						// Update the scene's cubemap array, which will reallocate it, so we no longer have the contents of existing entries
						Scene->ReflectionSceneData.CubemapArray.UpdateMaxCubemaps(MaxSize, ReflectionCaptureSize);
					});

				// Recapture all reflection captures now that we have reallocated the cubemap array
				UpdateAllReflectionCaptures(CaptureReason, ReflectionCaptureSize, bVerifyOnlyCapturing, bCapturingForMobile);
			}
			else
			{
				const int32 NumCapturesForStatus = bVerifyOnlyCapturing ? NewCaptures.Num() : 0;
				BeginReflectionCaptureSlowTask(NumCapturesForStatus, CaptureReason);

				// No teardown of the cubemap array was needed, just update the captures that were requested
				for (int32 CaptureIndex = 0; CaptureIndex < NewCaptures.Num(); CaptureIndex++)
				{
					UReflectionCaptureComponent* CurrentComponent = NewCaptures[CaptureIndex];
					UpdateReflectionCaptureSlowTask(CaptureIndex, NumCapturesForStatus);

					bool bAllocated = false;

					for (TSparseArray<UReflectionCaptureComponent*>::TIterator It(ReflectionSceneData.AllocatedReflectionCapturesGameThread); It; ++It)
					{
						if (*It == CurrentComponent)
						{
							bAllocated = true;
						}
					}

					if (bAllocated)
					{
						CaptureOrUploadReflectionCapture(CurrentComponent, ReflectionCaptureSize, bVerifyOnlyCapturing, bCapturingForMobile);
					}
				}

				EndReflectionCaptureSlowTask(NumCapturesForStatus);
			}
		}

		for (int32 CaptureIndex = 0; CaptureIndex < NewCaptures.Num(); CaptureIndex++)
		{
			UReflectionCaptureComponent* Component = NewCaptures[CaptureIndex];

			Component->SetCaptureCompleted();

			if (Component->SceneProxy)
			{
				// Update the transform of the reflection capture
				// This is not done earlier by the reflection capture when it detects that it is dirty,
				// To ensure that the RT sees both the new transform and the new contents on the same frame.
				Component->SendRenderTransform_Concurrent();
			}
		}
	}
}

/** Updates the contents of all reflection captures in the scene.  Must be called from the game thread. */
void FScene::UpdateAllReflectionCaptures(const TCHAR* CaptureReason, int32 ReflectionCaptureSize, bool bVerifyOnlyCapturing, bool bCapturingForMobile)
{
	if (IsReflectionEnvironmentAvailable(GetFeatureLevel()))
	{
		FScene* Scene = this;
		ENQUEUE_RENDER_COMMAND(CaptureCommand)(
			[Scene](FRHICommandListImmediate& RHICmdList)
			{
				Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Empty();
				Scene->ReflectionSceneData.CubemapArraySlotsUsed.Reset();
			});

		// Only display status during building reflection captures, otherwise we may interrupt a editor widget manipulation of many captures
		const int32 NumCapturesForStatus = bVerifyOnlyCapturing ? ReflectionSceneData.AllocatedReflectionCapturesGameThread.Num() : 0;
		BeginReflectionCaptureSlowTask(NumCapturesForStatus, CaptureReason);

		int32 CaptureIndex = 0;

		for (TSparseArray<UReflectionCaptureComponent*>::TIterator It(ReflectionSceneData.AllocatedReflectionCapturesGameThread); It; ++It)
		{
			UpdateReflectionCaptureSlowTask(CaptureIndex, NumCapturesForStatus);

			CaptureIndex++;
			UReflectionCaptureComponent* CurrentComponent = *It;
			CaptureOrUploadReflectionCapture(CurrentComponent, ReflectionCaptureSize, bVerifyOnlyCapturing, bCapturingForMobile);
		}

		EndReflectionCaptureSlowTask(NumCapturesForStatus);
	}
}

void GetReflectionCaptureData_RenderingThread(FRHICommandListImmediate& RHICmdList, FScene* Scene, const UReflectionCaptureComponent* Component, FReflectionCaptureData* OutCaptureData)
{
	const FCaptureComponentSceneState* ComponentStatePtr = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Find(Component);

	if (ComponentStatePtr)
	{
		FSceneRenderTargetItem& EffectiveDest = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();

		const int32 CubemapIndex = ComponentStatePtr->CubemapIndex;
		const int32 NumMips = EffectiveDest.ShaderResourceTexture->GetNumMips();
		const int32 EffectiveTopMipSize = FMath::Pow(2, NumMips - 1);

		int32 CaptureDataSize = 0;

		for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			const int32 MipSize = 1 << (NumMips - MipIndex - 1);

			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				CaptureDataSize += MipSize * MipSize * sizeof(FFloat16Color);
			}
		}

		OutCaptureData->FullHDRCapturedData.Empty(CaptureDataSize);
		OutCaptureData->FullHDRCapturedData.AddZeroed(CaptureDataSize);
		int32 MipBaseIndex = 0;

		for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			check(EffectiveDest.ShaderResourceTexture->GetFormat() == PF_FloatRGBA);
			const int32 MipSize = 1 << (NumMips - MipIndex - 1);
			const int32 CubeFaceBytes = MipSize * MipSize * sizeof(FFloat16Color);

			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				TArray<FFloat16Color> SurfaceData;
				// Read each mip face
				//@todo - do this without blocking the GPU so many times
				//@todo - pool the temporary textures in RHIReadSurfaceFloatData instead of always creating new ones
				RHICmdList.ReadSurfaceFloatData(EffectiveDest.ShaderResourceTexture, FIntRect(0, 0, MipSize, MipSize), SurfaceData, (ECubeFace)CubeFace, CubemapIndex, MipIndex);
				const int32 DestIndex = MipBaseIndex + CubeFace * CubeFaceBytes;
				uint8* FaceData = &OutCaptureData->FullHDRCapturedData[DestIndex];
				check(SurfaceData.Num() * SurfaceData.GetTypeSize() == CubeFaceBytes);
				FMemory::Memcpy(FaceData, SurfaceData.GetData(), CubeFaceBytes);
			}

			MipBaseIndex += CubeFaceBytes * CubeFace_MAX;
		}

		OutCaptureData->CubemapSize = EffectiveTopMipSize;

		OutCaptureData->AverageBrightness = ComponentStatePtr->AverageBrightness;
	}
}

void FScene::GetReflectionCaptureData(UReflectionCaptureComponent* Component, FReflectionCaptureData& OutCaptureData) 
{
	check(GetFeatureLevel() >= ERHIFeatureLevel::SM5);

	FScene* Scene = this;
	FReflectionCaptureData* OutCaptureDataPtr = &OutCaptureData;
	ENQUEUE_RENDER_COMMAND(GetReflectionDataCommand)(
		[Scene, Component, OutCaptureDataPtr](FRHICommandListImmediate& RHICmdList)
		{
			GetReflectionCaptureData_RenderingThread(RHICmdList, Scene, Component, OutCaptureDataPtr);
		});

	// Necessary since the RT is writing to OutDerivedData directly
	FlushRenderingCommands();

	// Required for cooking of Encoded HDR data
	OutCaptureData.Brightness = Component->Brightness;
}

void UploadReflectionCapture_RenderingThread(FScene* Scene, const FReflectionCaptureData* CaptureData, const UReflectionCaptureComponent* CaptureComponent)
{
	const int32 EffectiveTopMipSize = CaptureData->CubemapSize;
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	const int32 CaptureIndex = FindOrAllocateCubemapIndex(Scene, CaptureComponent);
	check(CaptureData->CubemapSize == Scene->ReflectionSceneData.CubemapArray.GetCubemapSize());
	check(CaptureIndex < Scene->ReflectionSceneData.CubemapArray.GetMaxCubemaps());
	FTextureCubeRHIRef& CubeMapArray = (FTextureCubeRHIRef&)Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().ShaderResourceTexture;
	check(CubeMapArray->GetFormat() == PF_FloatRGBA);

	int32 MipBaseIndex = 0;

	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		const int32 MipSize = 1 << (NumMips - MipIndex - 1);
		const int32 CubeFaceBytes = MipSize * MipSize * sizeof(FFloat16Color);

		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			uint32 DestStride = 0;
			uint8* DestBuffer = (uint8*)RHILockTextureCubeFace(CubeMapArray, CubeFace, CaptureIndex, MipIndex, RLM_WriteOnly, DestStride, false);

			// Handle DestStride by copying each row
			for (int32 Y = 0; Y < MipSize; Y++)
			{
				FFloat16Color* DestPtr = (FFloat16Color*)((uint8*)DestBuffer + Y * DestStride);
				const int32 SourceIndex = MipBaseIndex + CubeFace * CubeFaceBytes + Y * MipSize * sizeof(FFloat16Color);
				const uint8* SourcePtr = &CaptureData->FullHDRCapturedData[SourceIndex];
				FMemory::Memcpy(DestPtr, SourcePtr, MipSize * sizeof(FFloat16Color));
			}

			RHIUnlockTextureCubeFace(CubeMapArray, CubeFace, CaptureIndex, MipIndex, false);
		}

		MipBaseIndex += CubeFaceBytes * CubeFace_MAX;
	}

	FCaptureComponentSceneState& FoundState = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.FindChecked(CaptureComponent);
	FoundState.AverageBrightness = CaptureData->AverageBrightness;
}

/** Creates a transformation for a cubemap face, following the D3D cubemap layout. */
FMatrix CalcCubeFaceViewRotationMatrix(ECubeFace Face)
{
	FMatrix Result(FMatrix::Identity);

	static const FVector XAxis(1.f,0.f,0.f);
	static const FVector YAxis(0.f,1.f,0.f);
	static const FVector ZAxis(0.f,0.f,1.f);

	// vectors we will need for our basis
	FVector vUp(YAxis);
	FVector vDir;

	switch( Face )
	{
	case CubeFace_PosX:
		vDir = XAxis;
		break;
	case CubeFace_NegX:
		vDir = -XAxis;
		break;
	case CubeFace_PosY:
		vUp = -ZAxis;
		vDir = YAxis;
		break;
	case CubeFace_NegY:
		vUp = ZAxis;
		vDir = -YAxis;
		break;
	case CubeFace_PosZ:
		vDir = ZAxis;
		break;
	case CubeFace_NegZ:
		vDir = -ZAxis;
		break;
	}

	// derive right vector
	FVector vRight( vUp ^ vDir );
	// create matrix from the 3 axes
	Result = FBasisVectorMatrix( vRight, vUp, vDir, FVector::ZeroVector );	

	return Result;
}

FMatrix GetCubeProjectionMatrix(float HalfFovDeg, float CubeMapSize, float NearPlane)
{
	if ((bool)ERHIZBuffer::IsInverted)
	{
		return FReversedZPerspectiveMatrix(HalfFovDeg * float(PI) / 180.0f, CubeMapSize, CubeMapSize, NearPlane);
	}
	return FPerspectiveMatrix(HalfFovDeg, CubeMapSize, CubeMapSize, NearPlane);
}

/** 
 * Render target class required for rendering the scene.
 * This doesn't actually allocate a render target as we read from scene color to get HDR results directly.
 */
class FCaptureRenderTarget : public FRenderResource, public FRenderTarget
{
public:

	FCaptureRenderTarget() :
		Size(0)
	{}

	virtual const FTexture2DRHIRef& GetRenderTargetTexture() const 
	{
		static FTexture2DRHIRef DummyTexture;
		return DummyTexture;
	}

	void SetSize(int32 TargetSize) { Size = TargetSize; }
	virtual FIntPoint GetSizeXY() const { return FIntPoint(Size, Size); }
	virtual float GetDisplayGamma() const { return 1.0f; }

private:

	int32 Size;
};

TGlobalResource<FCaptureRenderTarget> GReflectionCaptureRenderTarget;

void CaptureSceneIntoScratchCubemap(
	FScene* Scene, 
	FVector CapturePosition, 
	int32 CubemapSize,
	bool bCapturingForSkyLight,
	bool bStaticSceneOnly, 
	float SkyLightNearPlane,
	bool bLowerHemisphereIsBlack, 
	bool bCaptureEmissiveOnly,
	const FLinearColor& LowerHemisphereColor,
	bool bCapturingForMobile
	)
{
	int32 SupersampleCaptureFactor = FMath::Clamp(GSupersampleCaptureFactor, MinSupersampleCaptureFactor, MaxSupersampleCaptureFactor);

	for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
	{
		if( !bCapturingForSkyLight )
		{
			// Alert the RHI that we're rendering a new frame
			// Not really a new frame, but it will allow pooling mechanisms to update, like the uniform buffer pool
			ENQUEUE_RENDER_COMMAND(BeginFrame)(
				[](FRHICommandList& RHICmdList)
				{
					GFrameNumberRenderThread++;
					RHICmdList.BeginFrame();
				});
		}

		GReflectionCaptureRenderTarget.SetSize(CubemapSize);

		auto ViewFamilyInit = FSceneViewFamily::ConstructionValues(
			&GReflectionCaptureRenderTarget,
			Scene,
			FEngineShowFlags(ESFIM_Game)
			)
			.SetResolveScene(false);

		if( bStaticSceneOnly )
		{
			ViewFamilyInit.SetWorldTimes( 0.0f, 0.0f, 0.0f );
		}

		FSceneViewFamilyContext ViewFamily( ViewFamilyInit );

		// Disable features that are not desired when capturing the scene
		ViewFamily.EngineShowFlags.PostProcessing = 0;
		ViewFamily.EngineShowFlags.MotionBlur = 0;
		ViewFamily.EngineShowFlags.SetOnScreenDebug(false);
		ViewFamily.EngineShowFlags.HMDDistortion = 0;
		// Exclude particles and light functions as they are usually dynamic, and can't be captured well
		ViewFamily.EngineShowFlags.Particles = 0;
		ViewFamily.EngineShowFlags.LightFunctions = 0;
		ViewFamily.EngineShowFlags.SetCompositeEditorPrimitives(false);
		// These are highly dynamic and can't be captured effectively
		ViewFamily.EngineShowFlags.LightShafts = 0;
		// Don't apply sky lighting diffuse when capturing the sky light source, or we would have feedback
		ViewFamily.EngineShowFlags.SkyLighting = !bCapturingForSkyLight;
		// Skip lighting for emissive only
		ViewFamily.EngineShowFlags.Lighting = !bCaptureEmissiveOnly;
		// Never do screen percentage in reflection environment capture.
		ViewFamily.EngineShowFlags.ScreenPercentage = false;

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverlayColor = FLinearColor::Black;
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, CubemapSize * SupersampleCaptureFactor, CubemapSize * SupersampleCaptureFactor));

		const float NearPlane = bCapturingForSkyLight ? SkyLightNearPlane : GReflectionCaptureNearPlane;

		// Projection matrix based on the fov, near / far clip settings
		// Each face always uses a 90 degree field of view
		ViewInitOptions.ProjectionMatrix = GetCubeProjectionMatrix(45.0f, (float)CubemapSize * SupersampleCaptureFactor, NearPlane);

		ViewInitOptions.ViewOrigin = CapturePosition;
		ViewInitOptions.ViewRotationMatrix = CalcCubeFaceViewRotationMatrix((ECubeFace)CubeFace);

		FSceneView* View = new FSceneView(ViewInitOptions);

		// Force all surfaces diffuse
		View->RoughnessOverrideParameter = FVector2D( 1.0f, 0.0f );

		if (bCaptureEmissiveOnly)
		{
			View->DiffuseOverrideParameter = FVector4(0, 0, 0, 0);
			View->SpecularOverrideParameter = FVector4(0, 0, 0, 0);
		}

		View->bIsReflectionCapture = true;
		View->bStaticSceneOnly = bStaticSceneOnly;
		View->StartFinalPostprocessSettings(CapturePosition);
		View->EndFinalPostprocessSettings(ViewInitOptions);

		ViewFamily.Views.Add(View);

		ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
			ViewFamily, /* GlobalResolutionFraction = */ 1.0f, /* AllowPostProcessSettingsScreenPercentage = */ false));

		FSceneRenderer* SceneRenderer = FSceneRenderer::CreateSceneRenderer(&ViewFamily, NULL);

		ENQUEUE_RENDER_COMMAND(CaptureCommand)(
			[SceneRenderer, CubeFace, CubemapSize, bCapturingForSkyLight, bLowerHemisphereIsBlack, LowerHemisphereColor, bCapturingForMobile](FRHICommandListImmediate& RHICmdList)
			{
				CaptureSceneToScratchCubemap(RHICmdList, SceneRenderer, (ECubeFace)CubeFace, CubemapSize, bCapturingForSkyLight, bLowerHemisphereIsBlack, LowerHemisphereColor, bCapturingForMobile);

				if (!bCapturingForSkyLight)
				{
					RHICmdList.EndFrame();
				}
			});
	}
}

void CopyToSceneArray(FRHICommandListImmediate& RHICmdList, FScene* Scene, FReflectionCaptureProxy* ReflectionProxy)
{
	SCOPED_DRAW_EVENT(RHICmdList, CopyToSceneArray);
	const int32 EffectiveTopMipSize = Scene->ReflectionSceneData.CubemapArray.GetCubemapSize();
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	const int32 CaptureIndex = FindOrAllocateCubemapIndex(Scene, ReflectionProxy->Component);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	
	FSceneRenderTargetItem& FilteredCube = SceneContext.ReflectionColorScratchCubemap[1]->GetRenderTargetItem();
	FSceneRenderTargetItem& DestCube = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();

	// GPU copy back to the scene's texture array, which is not a render target
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			RHICmdList.CopyToResolveTarget(FilteredCube.ShaderResourceTexture, DestCube.ShaderResourceTexture, FResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex, 0, CaptureIndex));
		}
	}
}



/** 
 * Updates the contents of the given reflection capture by rendering the scene. 
 * This must be called on the game thread.
 */
void FScene::CaptureOrUploadReflectionCapture(UReflectionCaptureComponent* CaptureComponent, int32 ReflectionCaptureSize, bool bVerifyOnlyCapturing, bool bCapturingForMobile)
{
	if (IsReflectionEnvironmentAvailable(GetFeatureLevel()))
	{
		FReflectionCaptureData* CaptureData = CaptureComponent->GetMapBuildData();

		// Upload existing derived data if it exists, instead of capturing
		if (CaptureData)
		{
			// Safety check during the reflection capture build, there should not be any map build data
			ensure(!bVerifyOnlyCapturing);

			check(SupportsTextureCubeArray(GetFeatureLevel()));

			FScene* Scene = this;

			ENQUEUE_RENDER_COMMAND(UploadCaptureCommand)
				([Scene, CaptureData, CaptureComponent](FRHICommandListImmediate& RHICmdList)
			{
				// After the final upload we cannot upload again because we tossed the source MapBuildData,
				// After uploading it into the scene's texture array, to guaratee there's only one copy in memory.
				// This means switching between LightingScenarios only works if the scenario level is reloaded (not simply made hidden / visible again)
				if (!CaptureData->HasBeenUploadedFinal())
				{
					UploadReflectionCapture_RenderingThread(Scene, CaptureData, CaptureComponent);

					if (DoGPUArrayCopy())
					{
						CaptureData->OnDataUploadedToGPUFinal();
					}
				}
				else
				{
					const FCaptureComponentSceneState* CaptureSceneStatePtr = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Find(CaptureComponent);
					
					if (!CaptureSceneStatePtr)
					{
						ensureMsgf(CaptureSceneStatePtr, TEXT("Reflection capture %s uploaded twice without reloading its lighting scenario level.  The Lighting scenario level must be loaded once for each time the reflection capture is uploaded."), *CaptureComponent->GetPathName());
					}
				}
			});
		}
		// Capturing only supported in the editor.  Game can only use built reflection captures.
		else if (bIsEditorScene)
		{
			if (CaptureComponent->ReflectionSourceType == EReflectionSourceType::SpecifiedCubemap && !CaptureComponent->Cubemap)
			{
				return;
			}

			if (FPlatformProperties::RequiresCookedData())
			{
				UE_LOG(LogEngine, Warning, TEXT("No built data for %s, skipping generation in cooked build."), *CaptureComponent->GetPathName());
				return;
			}

			// Prefetch all virtual textures so that we have content available
			if (UseVirtualTexturing(GetFeatureLevel()))
			{
				const ERHIFeatureLevel::Type InFeatureLevel = FeatureLevel;
				const FVector2D ScreenSpaceSize(ReflectionCaptureSize, ReflectionCaptureSize);

				ENQUEUE_RENDER_COMMAND(LoadTiles)(
					[InFeatureLevel, ScreenSpaceSize](FRHICommandListImmediate& RHICmdList)
				{
					GetRendererModule().RequestVirtualTextureTiles(ScreenSpaceSize, -1);
					GetRendererModule().LoadPendingVirtualTextureTiles(RHICmdList, InFeatureLevel);
				});

				FlushRenderingCommands();
			}

			ENQUEUE_RENDER_COMMAND(ClearCommand)(
				[ReflectionCaptureSize](FRHICommandListImmediate& RHICmdList)
				{
					ClearScratchCubemaps(RHICmdList, ReflectionCaptureSize);
				});
			
			if (CaptureComponent->ReflectionSourceType == EReflectionSourceType::CapturedScene)
			{
				bool const bCaptureStaticSceneOnly = CVarReflectionCaptureStaticSceneOnly.GetValueOnGameThread() != 0;
				CaptureSceneIntoScratchCubemap(this, CaptureComponent->GetComponentLocation() + CaptureComponent->CaptureOffset, ReflectionCaptureSize, false, bCaptureStaticSceneOnly, 0, false, false, FLinearColor(), bCapturingForMobile);
			}
			else if (CaptureComponent->ReflectionSourceType == EReflectionSourceType::SpecifiedCubemap)
			{
				UTextureCube* SourceCubemap = CaptureComponent->Cubemap;
				float SourceCubemapRotation = CaptureComponent->SourceCubemapAngle * (PI / 180.f);
				ERHIFeatureLevel::Type InFeatureLevel = FeatureLevel;
				ENQUEUE_RENDER_COMMAND(CopyCubemapCommand)(
					[SourceCubemap, ReflectionCaptureSize, SourceCubemapRotation, InFeatureLevel](FRHICommandListImmediate& RHICmdList)
					{
						CopyCubemapToScratchCubemap(RHICmdList, InFeatureLevel, SourceCubemap, ReflectionCaptureSize, false, false, SourceCubemapRotation, FLinearColor());
					});
			}
			else
			{
				check(!TEXT("Unknown reflection source type"));
			}

			{
				ERHIFeatureLevel::Type InFeatureLevel = GetFeatureLevel();
				int32 InReflectionCaptureSize = ReflectionCaptureSize;
				FScene* Scene = this;
				const UReflectionCaptureComponent* InCaptureComponent = CaptureComponent;
				ENQUEUE_RENDER_COMMAND(FilterCommand)(
					[InFeatureLevel, InReflectionCaptureSize, Scene, InCaptureComponent](FRHICommandListImmediate& RHICmdList)
					{
						FindOrAllocateCubemapIndex(Scene, InCaptureComponent);
						FCaptureComponentSceneState& FoundState = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.FindChecked(InCaptureComponent);

						ComputeAverageBrightness(RHICmdList, InFeatureLevel, InReflectionCaptureSize, FoundState.AverageBrightness);
						FilterReflectionEnvironment(RHICmdList, InFeatureLevel, InReflectionCaptureSize, nullptr);
					});
			}

			// Create a proxy to represent the reflection capture to the rendering thread
			// The rendering thread will be responsible for deleting this when done with the filtering operation
			// We can't use the component's SceneProxy here because the component may not be registered with the scene
			FReflectionCaptureProxy* ReflectionProxy = new FReflectionCaptureProxy(CaptureComponent);

			FScene* Scene = this;
			ERHIFeatureLevel::Type InFeatureLevel = GetFeatureLevel();
			ENQUEUE_RENDER_COMMAND(CopyCommand)(
				[Scene, ReflectionProxy, InFeatureLevel](FRHICommandListImmediate& RHICmdList)
			{
				if (InFeatureLevel == ERHIFeatureLevel::SM5)
				{
					CopyToSceneArray(RHICmdList, Scene, ReflectionProxy);
				}

				// Clean up the proxy now that the rendering thread is done with it
				delete ReflectionProxy;
			});
		} //-V773
	}
}

void ReadbackRadianceMap(FRHICommandListImmediate& RHICmdList, int32 CubmapSize, TArray<FFloat16Color>& OutRadianceMap)
{
	OutRadianceMap.Empty(CubmapSize * CubmapSize * 6);
	OutRadianceMap.AddZeroed(CubmapSize * CubmapSize * 6);

	const int32 MipIndex = 0;

	FSceneRenderTargetItem& SourceCube = FSceneRenderTargets::Get(RHICmdList).ReflectionColorScratchCubemap[0]->GetRenderTargetItem();
	check(SourceCube.ShaderResourceTexture->GetFormat() == PF_FloatRGBA);
	const int32 CubeFaceBytes = CubmapSize * CubmapSize * OutRadianceMap.GetTypeSize();

	for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
	{
		TArray<FFloat16Color> SurfaceData;

		// Read each mip face
		RHICmdList.ReadSurfaceFloatData(SourceCube.ShaderResourceTexture, FIntRect(0, 0, CubmapSize, CubmapSize), SurfaceData, (ECubeFace)CubeFace, 0, MipIndex);
		const int32 DestIndex = CubeFace * CubmapSize * CubmapSize;
		FFloat16Color* FaceData = &OutRadianceMap[DestIndex];
		check(SurfaceData.Num() * SurfaceData.GetTypeSize() == CubeFaceBytes);
		FMemory::Memcpy(FaceData, SurfaceData.GetData(), CubeFaceBytes);
	}
}

void CopyToSkyTexture(FRHICommandList& RHICmdList, FScene* Scene, FTexture* ProcessedTexture)
{
	SCOPED_DRAW_EVENT(RHICmdList, CopyToSkyTexture);
	if (ProcessedTexture->TextureRHI)
	{
		const int32 EffectiveTopMipSize = ProcessedTexture->GetSizeX();
		const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		FSceneRenderTargetItem& FilteredCube = FSceneRenderTargets::Get(RHICmdList).ReflectionColorScratchCubemap[1]->GetRenderTargetItem();

		// GPU copy back to the skylight's texture, which is not a render target
		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size = FilteredCube.ShaderResourceTexture->GetSizeXYZ();
		CopyInfo.NumSlices = 6;
		CopyInfo.NumMips = NumMips;

		FRHITransitionInfo TransitionsBefore[] =
		{
			FRHITransitionInfo(FilteredCube.ShaderResourceTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc),
			FRHITransitionInfo(ProcessedTexture->TextureRHI, ERHIAccess::Unknown, ERHIAccess::CopyDest)
		};
		RHICmdList.Transition(MakeArrayView(TransitionsBefore, UE_ARRAY_COUNT(TransitionsBefore)));

		RHICmdList.CopyTexture(FilteredCube.ShaderResourceTexture, ProcessedTexture->TextureRHI, CopyInfo);

		FRHITransitionInfo TransitionsAfter[] =
		{
			FRHITransitionInfo(FilteredCube.ShaderResourceTexture, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
			FRHITransitionInfo(ProcessedTexture->TextureRHI, ERHIAccess::CopyDest, ERHIAccess::SRVMask)
		};
		RHICmdList.Transition(MakeArrayView(TransitionsAfter, UE_ARRAY_COUNT(TransitionsAfter)));
	}
}

// Warning: returns before writes to OutIrradianceEnvironmentMap have completed, as they are queued on the rendering thread
void FScene::UpdateSkyCaptureContents(
	const USkyLightComponent* CaptureComponent, 
	bool bCaptureEmissiveOnly, 
	UTextureCube* SourceCubemap, 
	FTexture* OutProcessedTexture, 
	float& OutAverageBrightness, 
	FSHVectorRGB3& OutIrradianceEnvironmentMap,
	TArray<FFloat16Color>* OutRadianceMap)
{	
	if (GSupportsRenderTargetFormat_PF_FloatRGBA || GetFeatureLevel() >= ERHIFeatureLevel::SM5)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateSkyCaptureContents);
		{
			World = GetWorld();
			if (World)
			{
				//guarantee that all render proxies are up to date before kicking off this render
				World->SendAllEndOfFrameUpdates();
			}
		}
		{
			int32 CubemapSize = CaptureComponent->CubemapResolution;
			ENQUEUE_RENDER_COMMAND(ClearCommand)(
				[CubemapSize](FRHICommandListImmediate& RHICmdList)
				{
					ClearScratchCubemaps(RHICmdList, CubemapSize);
				});
		}

		if (CaptureComponent->SourceType == SLS_CapturedScene)
		{
			bool bStaticSceneOnly = CaptureComponent->Mobility == EComponentMobility::Static;
			bool bCapturingForMobile = false;
			CaptureSceneIntoScratchCubemap(this, CaptureComponent->GetComponentLocation(), CaptureComponent->CubemapResolution, true, bStaticSceneOnly, CaptureComponent->SkyDistanceThreshold, CaptureComponent->bLowerHemisphereIsBlack, bCaptureEmissiveOnly, CaptureComponent->LowerHemisphereColor, bCapturingForMobile);
		}
		else if (CaptureComponent->SourceType == SLS_SpecifiedCubemap)
		{
			int32 CubemapSize = CaptureComponent->CubemapResolution;
			bool bLowerHemisphereIsBlack = CaptureComponent->bLowerHemisphereIsBlack;
			float SourceCubemapRotation = CaptureComponent->SourceCubemapAngle * (PI / 180.f);
			ERHIFeatureLevel::Type InnerFeatureLevel = FeatureLevel;
			FLinearColor LowerHemisphereColor = CaptureComponent->LowerHemisphereColor;
			ENQUEUE_RENDER_COMMAND(CopyCubemapCommand)(
				[SourceCubemap, CubemapSize, bLowerHemisphereIsBlack, SourceCubemapRotation, InnerFeatureLevel, LowerHemisphereColor](FRHICommandListImmediate& RHICmdList)
				{
					CopyCubemapToScratchCubemap(RHICmdList, InnerFeatureLevel, SourceCubemap, CubemapSize, true, bLowerHemisphereIsBlack, SourceCubemapRotation, LowerHemisphereColor);
				});
		}
		else if (CaptureComponent->IsRealTimeCaptureEnabled())
		{
			ensureMsgf(false, TEXT("A sky light with RealTimeCapture enabled cannot be scheduled for a cubemap update. This will be done dynamically each frame by the renderer."));
			return;
		}
		else
		{
			check(0);
		}

		if (OutRadianceMap)
		{
			int32 CubemapSize = CaptureComponent->CubemapResolution;
			ENQUEUE_RENDER_COMMAND(ReadbackCommand)(
				[CubemapSize, OutRadianceMap](FRHICommandListImmediate& RHICmdList)
				{
					ReadbackRadianceMap(RHICmdList, CubemapSize, *OutRadianceMap);
				});
		}

		{
			int32 CubemapSize = CaptureComponent->CubemapResolution;
			float* AverageBrightness = &OutAverageBrightness;
			FSHVectorRGB3* IrradianceEnvironmentMap = &OutIrradianceEnvironmentMap;
			ERHIFeatureLevel::Type InFeatureLevel = GetFeatureLevel();
			ENQUEUE_RENDER_COMMAND(FilterCommand)(
				[CubemapSize, AverageBrightness, IrradianceEnvironmentMap, InFeatureLevel](FRHICommandListImmediate& RHICmdList)
				{
					if (InFeatureLevel <= ERHIFeatureLevel::ES3_1)
					{
						MobileReflectionEnvironmentCapture::ComputeAverageBrightness(RHICmdList, InFeatureLevel, CubemapSize, *AverageBrightness);
						MobileReflectionEnvironmentCapture::FilterReflectionEnvironment(RHICmdList, InFeatureLevel, CubemapSize, IrradianceEnvironmentMap);
					}
					else
					{
						ComputeAverageBrightness(RHICmdList, InFeatureLevel, CubemapSize, *AverageBrightness);
						FilterReflectionEnvironment(RHICmdList, InFeatureLevel, CubemapSize, IrradianceEnvironmentMap);
					}
				});
		}

		// Optionally copy the filtered mip chain to the output texture
		if (OutProcessedTexture)
		{
			FScene* Scene = this;
			ERHIFeatureLevel::Type InFeatureLevel = GetFeatureLevel();
			ENQUEUE_RENDER_COMMAND(CopyCommand)(
				[Scene, OutProcessedTexture, InFeatureLevel](FRHICommandListImmediate& RHICmdList)
				{
					if (InFeatureLevel <= ERHIFeatureLevel::ES3_1)
					{
						MobileReflectionEnvironmentCapture::CopyToSkyTexture(RHICmdList, Scene, OutProcessedTexture);
					}
					else
					{
						CopyToSkyTexture(RHICmdList, Scene, OutProcessedTexture);
					}
				});
		}

		if (!!GFreeReflectionScratchAfterUse)
		{
			ENQUEUE_RENDER_COMMAND(FreeReflectionScratch)(
				[](FRHICommandListImmediate& RHICmdList)
			{
				FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
				SceneContext.FreeReflectionScratchRenderTargets();
				GRenderTargetPool.FreeUnusedResources();
			});
		}

		// These textures should only be manipulated by the render thread,
		// so enqueue a render command for them to be processed there
		ENQUEUE_RENDER_COMMAND(ReleasePathTracerSkylightData)(
			[this](FRHICommandListImmediate& RHICmdList)
		{
			PathTracingSkylightTexture.SafeRelease();
			PathTracingSkylightPdf.SafeRelease();
		});
	}
}
