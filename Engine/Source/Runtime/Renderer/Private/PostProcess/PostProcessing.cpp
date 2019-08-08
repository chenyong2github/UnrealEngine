// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessing.cpp: The center for all post processing activities.
=============================================================================*/

#include "PostProcess/PostProcessing.h"
#include "EngineGlobals.h"
#include "ScenePrivate.h"
#include "RendererModule.h"
#include "PostProcess/PostProcessInput.h"
#include "PostProcess/PostProcessAA.h"
#if WITH_EDITOR
	#include "PostProcess/PostProcessBufferInspector.h"
#endif
#include "PostProcess/DiaphragmDOF.h"
#include "PostProcess/PostProcessMaterial.h"
#include "PostProcess/PostProcessWeightedSampleSum.h"
#include "PostProcess/PostProcessBloomSetup.h"
#include "PostProcess/PostProcessMobile.h"
#include "PostProcess/PostProcessDownsample.h"
#include "PostProcess/PostProcessHistogram.h"
#include "PostProcess/PostProcessHistogramReduce.h"
#include "PostProcess/PostProcessVisualizeHDR.h"
#include "PostProcess/VisualizeShadingModels.h"
#include "PostProcess/PostProcessSelectionOutline.h"
#include "PostProcess/PostProcessGBufferHints.h"
#include "PostProcess/PostProcessVisualizeBuffer.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "PostProcess/PostProcessTonemap.h"
#include "PostProcess/PostProcessLensFlares.h"
#include "PostProcess/PostProcessLensBlur.h"
#include "PostProcess/PostProcessBokehDOF.h"
#include "PostProcess/PostProcessCombineLUTs.h"
#include "PostProcess/PostProcessTemporalAA.h"
#include "PostProcess/PostProcessMotionBlur.h"
#include "PostProcess/PostProcessDOF.h"
#include "PostProcess/PostProcessUpscale.h"
#include "PostProcess/PostProcessHMD.h"
#include "PostProcess/PostProcessMitchellNetravali.h"
#include "PostProcess/PostProcessVisualizeComplexity.h"
#include "PostProcess/PostProcessCompositeEditorPrimitives.h"
#include "PostProcess/PostProcessShaderPrint.h"
#include "PostProcess/PostProcessTestImage.h"
#include "PostProcess/PostProcessFFTBloom.h"
#include "PostProcess/PostProcessStreamingAccuracyLegend.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/PostProcessMorpheus.h"
#include "CompositionLighting/PostProcessPassThrough.h"
#include "CompositionLighting/PostProcessLpvIndirect.h"
#include "HighResScreenshot.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "BufferVisualizationData.h"
#include "DeferredShadingRenderer.h"
#include "MobileSeparateTranslucencyPass.h"
#include "MobileDistortionPass.h"
#include "SceneTextureParameters.h"
#include "PixelShaderUtils.h"

/** The global center for all post processing activities. */
FPostProcessing GPostProcessing;

static TAutoConsoleVariable<float> CVarDepthOfFieldNearBlurSizeThreshold(
	TEXT("r.DepthOfField.NearBlurSizeThreshold"),
	0.01f,
	TEXT("Sets the minimum near blur size before the effect is forcably disabled. Currently only affects Gaussian DOF.\n")
	TEXT(" (default: 0.01)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDepthOfFieldMaxSize(
	TEXT("r.DepthOfField.MaxSize"),
	100.0f,
	TEXT("Allows to clamp the gaussian depth of field radius (for better performance), default: 100"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRenderTargetSwitchWorkaround(
	TEXT("r.RenderTargetSwitchWorkaround"),
	0,
	TEXT("Workaround needed on some mobile platforms to avoid a performance drop related to switching render targets.\n")
	TEXT("Only enabled on some hardware. This affects the bloom quality a bit. It runs slower than the normal code path but\n")
	TEXT("still faster as it avoids the many render target switches. (Default: 0)\n")
	TEXT("We want this enabled (1) on all 32 bit iOS devices (implemented through DeviceProfiles)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarUpscaleQuality(
	TEXT("r.Upscale.Quality"),
	3,
	TEXT("Defines the quality in which ScreenPercentage and WindowedFullscreen scales the 3d rendering.\n")
	TEXT(" 0: Nearest filtering\n")
	TEXT(" 1: Simple Bilinear\n")
	TEXT(" 2: Directional blur with unsharp mask upsample.\n")
	TEXT(" 3: 5-tap Catmull-Rom bicubic, approximating Lanczos 2. (default)\n")
	TEXT(" 4: 13-tap Lanczos 3.\n")
	TEXT(" 5: 36-tap Gaussian-filtered unsharp mask (very expensive, but good for extreme upsampling).\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarBloomCross(
	TEXT("r.Bloom.Cross"),
	0.0f,
	TEXT("Experimental feature to give bloom kernel a more bright center sample (values between 1 and 3 work without causing aliasing)\n")
	TEXT("Existing bloom get lowered to match the same brightness\n")
	TEXT("<0 for a anisomorphic lens flare look (X only)\n")
	TEXT(" 0 off (default)\n")
	TEXT(">0 for a cross look (X and Y)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTonemapperMergeMode(
	TEXT("r.Tonemapper.MergeWithUpscale.Mode"),
	0,
	TEXT("ScreenPercentage upscale integrated into tonemapper pass (if certain conditions apply, e.g., no FXAA)\n")
	TEXT(" if enabled both features are done in one pass (faster, affects post process passes after the tonemapper including material post process e.g. sharpen)\n")
	TEXT("  0: off, the features run in separate passes (default)\n")
	TEXT("  1: always enabled, try to merge the passes unless something makes it impossible\n")
	TEXT("  2: merge when the ratio of areas is above the r.Tonemapper.MergeWithUpscale.Threshold and it is otherwise possible"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarTonemapperMergeThreshold(
	TEXT("r.Tonemapper.MergeWithUpscale.Threshold"),
	0.49f,
	TEXT("If r.Tonemapper.MergeWithUpscale.Mode is 2, the ratio of the area before upscale/downscale to the area afterwards\n")
	TEXT("is compared to this threshold when deciding whether or not to merge the passes.  The reasoning is that if the ratio\n")
	TEXT("is too low, running the tonemapper on the higher number of pixels is more expensive than doing two passes\n")
	TEXT("\n")
	TEXT("Defauls to 0.49 (e.g., if r.ScreenPercentage is 70 or higher, try to merge)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAlphaChannel(
	TEXT("r.PostProcessing.PropagateAlpha"),
	0,
	TEXT("0 to disable scene alpha channel support in the post processing.\n")
	TEXT(" 0: disabled (default);\n")
	TEXT(" 1: enabled in linear color space;\n")
	TEXT(" 2: same as 1, but also enable it through the tonemapper. Compositing after the tonemapper is incorrect, as their is no meaning to tonemap the alpha channel. This is only meant to be use exclusively for broadcasting hardware that does not support linear color space compositing and tonemapping."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarPostProcessingPreferCompute(
	TEXT("r.PostProcessing.PreferCompute"),
	0,
	TEXT("Will use compute shaders for post processing where implementations available."),
	ECVF_RenderThreadSafe);

#if !(UE_BUILD_SHIPPING)
static TAutoConsoleVariable<int32> CVarPostProcessingForceAsyncDispatch(
	TEXT("r.PostProcessing.ForceAsyncDispatch"),
	0,
	TEXT("Will force asynchronous dispatch for post processing compute shaders where implementations available.\n")
	TEXT("Only available for testing in non-shipping builds."),
	ECVF_RenderThreadSafe);
#endif

TAutoConsoleVariable<int32> CVarHalfResFFTBloom(
	TEXT("r.Bloom.HalfResoluionFFT"),
	0,
	TEXT("Experimental half-resolution FFT Bloom convolution. \n")
	TEXT(" 0: Standard full resolution convolution bloom.")
	TEXT(" 1: Half-resolution convoltuion that excludes the center of the kernel.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTemporalAAAllowDownsampling(
	TEXT("r.TemporalAA.AllowDownsampling"),
	1,
	TEXT("Allows half-resolution color buffer to be produced during TAA. Only possible when motion blur is off and when using compute shaders for post processing."),
	ECVF_RenderThreadSafe);

// -------------------------------------------------------

bool ShouldDoComputePostProcessing(const FViewInfo& View)
{
	return CVarPostProcessingPreferCompute.GetValueOnRenderThread() && View.FeatureLevel >= ERHIFeatureLevel::SM5;
}

bool IsTemporalAASceneDownsampleAllowed(const FViewInfo& View)
{
	return (CVarTemporalAAAllowDownsampling.GetValueOnRenderThread() != 0)
		&& !IsMotionBlurEnabled(View)
		&& !IsVisualizeMotionBlurEnabled(View)
		&& GetDownsampleQuality() == EDownsampleQuality::Low;
}

FPostprocessContext::FPostprocessContext(FRHICommandListImmediate& InRHICmdList, FRenderingCompositionGraph& InGraph, const FViewInfo& InView)
: RHICmdList(InRHICmdList)
	, Graph(InGraph)
	, View(InView)
	, SceneColor(0)
	, SceneDepth(0)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(InRHICmdList);
	if(SceneContext.IsSceneColorAllocated())
	{
		SceneColor = Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(SceneContext.GetSceneColor()));
	}

	SceneDepth = Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(SceneContext.SceneDepthZ));

	FinalOutput = FRenderingCompositeOutputRef(SceneColor);
}

// Array of downsampled color with optional log2 luminance stored in alpha
template <int32 DownSampleStages>
class TBloomDownSampleArray
{
public:
	// Convenience typedefs
	typedef FRenderingCompositeOutputRef         FRenderingRefArray[DownSampleStages];
	typedef TSharedPtr<TBloomDownSampleArray>    Ptr;

	// Constructor: Generates and registers the downsamples with the Context Graph.
	TBloomDownSampleArray(FPostprocessContext& InContext, FRenderingCompositeOutputRef SourceDownsample, bool bGenerateLog2Alpha) :
		bHasLog2Alpha(bGenerateLog2Alpha), Context(InContext)
	{

		static const TCHAR* PassLabels[] =
		{ NULL, TEXT("BloomDownsample1"), TEXT("BloomDownsample2"), TEXT("BloomDownsample3"), TEXT("BloomDownsample4"), TEXT("BloomDownsample5") };
		static_assert(ARRAY_COUNT(PassLabels) == DownSampleStages, "PassLabel count must be equal to DownSampleStages.");

		// The first down sample is the input
		PostProcessDownsamples[0] = SourceDownsample;

		const EDownsampleQuality DownsampleQuality = GetDownsampleQuality();

		// Queue the down samples. 
		for (int i = 1; i < DownSampleStages; i++)
		{
			PostProcessDownsamples[i] = AddDownsamplePass(Context.Graph, PassLabels[i], PostProcessDownsamples[i - 1], DownsampleQuality);

			// Add log2 data to the alpha channel after doing the 1st (i==1) down sample pass
			if (bHasLog2Alpha && i == 1 ) {
				FRenderingCompositePass* BasicEyeSetupPass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBasicEyeAdaptationSetUp());
				BasicEyeSetupPass->SetInput(ePId_Input0, PostProcessDownsamples[i]);
				PostProcessDownsamples[i] = FRenderingCompositeOutputRef(BasicEyeSetupPass);
			}
		}

		// Calculate the final viewrect size (matching AddDownsamplePass behavior)
		FinalViewRectSize.X = FMath::Max(1, FMath::DivideAndRoundUp(InContext.View.ViewRect.Width(), 1 << DownSampleStages));
		FinalViewRectSize.Y = FMath::Max(1, FMath::DivideAndRoundUp(InContext.View.ViewRect.Height(), 1 << DownSampleStages));
	}

	// The number of elements in the array.
	inline static int32 Num() { return DownSampleStages; }

	FIntPoint GetFinalViewRectSize() const
	{
		return FinalViewRectSize;
	}

	// Member data kept public for simplicity
	bool bHasLog2Alpha;
	FPostprocessContext& Context;
	FRenderingRefArray PostProcessDownsamples;

private:
	// no default constructor.
	TBloomDownSampleArray() {};

	FIntPoint	FinalViewRectSize;
};

// Standard DownsampleArray shared by Bloom, Tint, and Eye-Adaptation. 
typedef TBloomDownSampleArray<6/*DownSampleStages*/>   FBloomDownSampleArray;  

FBloomDownSampleArray::Ptr CreateDownSampleArray(FPostprocessContext& Context, FRenderingCompositeOutputRef SourceToDownSample, bool bAddLog2)
{
	return FBloomDownSampleArray::Ptr(new FBloomDownSampleArray(Context, SourceToDownSample, bAddLog2));
}


static FRenderingCompositeOutputRef RenderHalfResBloomThreshold(FPostprocessContext& Context, FRenderingCompositeOutputRef SceneColorHalfRes, FRenderingCompositeOutputRef EyeAdaptation)
{
	// with multiple view ports the Setup pass also isolates the view from the others which allows for simpler simpler/faster blur passes.
	if(Context.View.FinalPostProcessSettings.BloomThreshold <= -1 && Context.View.Family->Views.Num() == 1)
	{
		// no need for threshold, we don't need this pass
		return SceneColorHalfRes;
	}
	else
	{
		// todo: optimize later, the missing node causes some wrong behavior
		//	if(Context.View.FinalPostProcessSettings.BloomIntensity <= 0.0f)
		//	{
		//		// this pass is not required
		//		return FRenderingCompositeOutputRef();
		//	}
		// bloom threshold
		const bool bIsComputePass = ShouldDoComputePostProcessing(Context.View);
		FRenderingCompositePass* PostProcessBloomSetup = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomSetup(bIsComputePass));
		PostProcessBloomSetup->SetInput(ePId_Input0, SceneColorHalfRes);
		PostProcessBloomSetup->SetInput(ePId_Input1, EyeAdaptation);

		return FRenderingCompositeOutputRef(PostProcessBloomSetup);
	}
}


// 2 pass Gaussian blur using uni-linear filtering
// @param CrossCenterWeight see r.Bloom.Cross (positive for X and Y, otherwise for X only)
static FRenderingCompositeOutputRef RenderGaussianBlur(
	FPostprocessContext& Context,
	const TCHAR* DebugNameX,
	const TCHAR* DebugNameY,
	const FRenderingCompositeOutputRef& Input,
	float SizeScale,
	FLinearColor Tint = FLinearColor::White,
	const FRenderingCompositeOutputRef Additive = FRenderingCompositeOutputRef(),
	float CrossCenterWeight = 0.0f)
{
	const bool bIsComputePass = ShouldDoComputePostProcessing(Context.View);

	// Gaussian blur in x
	FRCPassPostProcessWeightedSampleSum* PostProcessBlurX = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessWeightedSampleSum(EFS_Horiz, EFCM_Weighted, SizeScale, bIsComputePass, DebugNameX));
	PostProcessBlurX->SetInput(ePId_Input0, Input);
	if(CrossCenterWeight > 0)
	{
		PostProcessBlurX->SetCrossCenterWeight(CrossCenterWeight);
	}

	// Gaussian blur in y
	FRCPassPostProcessWeightedSampleSum* PostProcessBlurY = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessWeightedSampleSum(EFS_Vert, EFCM_Weighted, SizeScale, bIsComputePass, DebugNameY, Tint));
	PostProcessBlurY->SetInput(ePId_Input0, FRenderingCompositeOutputRef(PostProcessBlurX));
	PostProcessBlurY->SetInput(ePId_Input1, Additive);
	PostProcessBlurY->SetCrossCenterWeight(FMath::Abs(CrossCenterWeight));

	return FRenderingCompositeOutputRef(PostProcessBlurY);
}

// render one bloom pass and add another optional texture to it
static FRenderingCompositeOutputRef RenderBloom(
	FPostprocessContext& Context,
	const FRenderingCompositeOutputRef& PreviousBloom,
	float Size,
	FLinearColor Tint = FLinearColor::White,
	const FRenderingCompositeOutputRef Additive = FRenderingCompositeOutputRef())
{
	const float CrossBloom = CVarBloomCross.GetValueOnRenderThread();

	return RenderGaussianBlur(Context, TEXT("BloomBlurX"), TEXT("BloomBlurY"), PreviousBloom, Size, Tint, Additive,CrossBloom);
}

static FRCPassPostProcessTonemap* AddTonemapper(
	FPostprocessContext& Context,
	const FRenderingCompositeOutputRef& BloomOutputCombined,
	const FRenderingCompositeOutputRef& EyeAdaptation,
	const EAutoExposureMethod& EyeAdapationMethodId,
	const bool bDoGammaOnly,
	const bool bHDRTonemapperOutput)
{
	const FViewInfo& View = Context.View;
	const EStereoscopicPass StereoPass = View.StereoPass;

	const FEngineShowFlags& EngineShowFlags = View.Family->EngineShowFlags;
	const bool bIsComputePass = ShouldDoComputePostProcessing(View);

	FRenderingCompositeOutputRef TonemapperCombinedLUTOutputRef;
	if (IStereoRendering::IsAPrimaryView(StereoPass, GEngine->StereoRenderingDevice))
	{
		bool bNeedFloatOutput = View.Family->SceneCaptureSource == SCS_FinalColorHDR;
		bool bAllocateOutput = View.State == nullptr;

		FRenderingCompositePass* CombinedLUT = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessCombineLUTs(View.GetShaderPlatform(), bAllocateOutput, bIsComputePass, bNeedFloatOutput));
		TonemapperCombinedLUTOutputRef =  FRenderingCompositeOutputRef(CombinedLUT);
	}

	const bool bDoEyeAdaptation = IsAutoExposureMethodSupported(View.GetFeatureLevel(), EyeAdapationMethodId);
	FRCPassPostProcessTonemap* PostProcessTonemap =	Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessTonemap(View, bDoGammaOnly, bDoEyeAdaptation, bHDRTonemapperOutput, bIsComputePass));

	PostProcessTonemap->SetInput(ePId_Input0, Context.FinalOutput);
	PostProcessTonemap->SetInput(ePId_Input1, BloomOutputCombined);
	PostProcessTonemap->SetInput(ePId_Input2, EyeAdaptation);
	PostProcessTonemap->SetInput(ePId_Input3, TonemapperCombinedLUTOutputRef);

	Context.FinalOutput = FRenderingCompositeOutputRef(PostProcessTonemap);

	return PostProcessTonemap;
}

#if WITH_EDITOR
void FPostProcessing::AddSelectionOutline(FPostprocessContext& Context)
{
	FRenderingCompositePass* SelectionColorPass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSelectionOutlineColor());
	SelectionColorPass->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));

	FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSelectionOutline());
	Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
	Node->SetInput(ePId_Input1, FRenderingCompositeOutputRef(FRenderingCompositeOutputRef(SelectionColorPass)));

	Context.FinalOutput = FRenderingCompositeOutputRef(Node);
}
#endif

void FPostProcessing::AddGammaOnlyTonemapper(FPostprocessContext& Context)
{
	const bool bIsComputePass = ShouldDoComputePostProcessing(Context.View);
	FRenderingCompositePass* PostProcessTonemap = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessTonemap(Context.View, true, false/*eye*/, false, bIsComputePass));

	PostProcessTonemap->SetInput(ePId_Input0, Context.FinalOutput);

	Context.FinalOutput = FRenderingCompositeOutputRef(PostProcessTonemap);
}

static void AddPostProcessAA(FPostprocessContext& Context)
{
	// console variable override
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.PostProcessAAQuality")); 

	uint32 Quality = FMath::Clamp(CVar->GetValueOnRenderThread(), 1, 6);

	FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAA(Quality));

	Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));

	Context.FinalOutput = FRenderingCompositeOutputRef(Node);
}


static FRenderingCompositeOutputRef AddPostProcessBasicEyeAdaptation(const FViewInfo& View, FBloomDownSampleArray& BloomAndEyeDownSamples)
{
	// Extract the context
	FPostprocessContext& Context = BloomAndEyeDownSamples.Context;

	// Extract the last (i.e. smallest) down sample
	static const int32 FinalDSIdx = FBloomDownSampleArray::Num() - 1;
	FRenderingCompositeOutputRef PostProcessPriorReduction = BloomAndEyeDownSamples.PostProcessDownsamples[FinalDSIdx];

	const FIntPoint DownsampledViewRectSize = BloomAndEyeDownSamples.GetFinalViewRectSize();

	// Compute the eye adaptation value based on average luminance from log2 luminance buffer, history, and specific shader parameters.
	FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBasicEyeAdaptation(DownsampledViewRectSize));
	Node->SetInput(ePId_Input0, PostProcessPriorReduction);
	return FRenderingCompositeOutputRef(Node);
}

static FRenderingCompositeOutputRef AddPostProcessHistogramEyeAdaptation(FPostprocessContext& Context, FRenderingCompositeOutputRef& Histogram)
{
	const bool bIsComputePass = ShouldDoComputePostProcessing(Context.View);
	FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessEyeAdaptation(bIsComputePass));

	Node->SetInput(ePId_Input0, Histogram);
	return FRenderingCompositeOutputRef(Node);
}

static void AddVisualizeBloomOverlay(FPostprocessContext& Context, FRenderingCompositeOutputRef& HDRColor, FRenderingCompositeOutputRef& BloomOutputCombined)
{
	auto Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeBloomOverlay());

	Node->SetInput(ePId_Input0, Context.FinalOutput);
	Node->SetInput(ePId_Input1, HDRColor);
	Node->SetInput(ePId_Input2, BloomOutputCombined);

	Context.FinalOutput = FRenderingCompositeOutputRef(Node);
}

static bool AddPostProcessDepthOfFieldGaussian(FPostprocessContext& Context, FDepthOfFieldStats& Out, FRenderingCompositeOutputRef& VelocityInput, FRenderingCompositeOutputRef& SeparateTranslucencyRef)
{
	// GaussianDOFPass performs Gaussian setup, blur and recombine.
	auto GaussianDOFPass = [&Context, &Out, &VelocityInput](FRenderingCompositeOutputRef& SeparateTranslucency, float FarSize, float NearSize)
	{
		// GenerateGaussianDOFBlur produces a blurred image from setup or potentially from taa result.
		auto GenerateGaussianDOFBlur = [&Context, &VelocityInput](FRenderingCompositeOutputRef& DOFSetup, bool bFarPass, float BlurSize)
		{
			FSceneViewState* ViewState = (FSceneViewState*)Context.View.State;

			const TCHAR* BlurDebugX = bFarPass ? TEXT("FarDOFBlurX") : TEXT("NearDOFBlurX");
			const TCHAR* BlurDebugY = bFarPass ? TEXT("FarDOFBlurY") : TEXT("NearDOFBlurY");

			return RenderGaussianBlur(Context, BlurDebugX, BlurDebugY, DOFSetup, BlurSize);
		};

		const bool bFar = FarSize > 0.0f;
		const bool bNear = NearSize > 0.0f;
		const bool bCombinedNearFarPass = bFar && bNear;
		const bool bMobileQuality = Context.View.FeatureLevel < ERHIFeatureLevel::SM4;

		FRenderingCompositeOutputRef SetupInput(Context.FinalOutput);
		if (bMobileQuality)
		{
			SetupInput = AddDownsamplePass(Context.Graph, TEXT("GaussianSetupHalfRes"), SetupInput, EDownsampleQuality::High, EDownsampleFlags::ForceRaster, PF_FloatRGBA);
		}

		FRenderingCompositePass* DOFSetupPass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDOFSetup(bFar, bNear));
		DOFSetupPass->SetInput(ePId_Input0, FRenderingCompositeOutputRef(SetupInput));
		DOFSetupPass->SetInput(ePId_Input1, FRenderingCompositeOutputRef(Context.SceneDepth));
		FRenderingCompositeOutputRef DOFSetupFar(DOFSetupPass);
		FRenderingCompositeOutputRef DOFSetupNear(DOFSetupPass, bCombinedNearFarPass ? ePId_Output1 : ePId_Output0);

		FRenderingCompositeOutputRef DOFFarBlur, DOFNearBlur;
		if (bFar)
		{
			DOFFarBlur = GenerateGaussianDOFBlur(DOFSetupFar, true, FarSize);
		}

		if (bNear)
		{
			DOFNearBlur = GenerateGaussianDOFBlur(DOFSetupNear, false, NearSize);
		}

		FRenderingCompositePass* GaussianDOFRecombined = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDOFRecombine());
		GaussianDOFRecombined->SetInput(ePId_Input0, Context.FinalOutput);
		GaussianDOFRecombined->SetInput(ePId_Input1, DOFFarBlur);
		GaussianDOFRecombined->SetInput(ePId_Input2, DOFNearBlur);
		GaussianDOFRecombined->SetInput(ePId_Input3, SeparateTranslucency);

		Context.FinalOutput = FRenderingCompositeOutputRef(GaussianDOFRecombined);
	};

	float FarSize = Context.View.FinalPostProcessSettings.DepthOfFieldFarBlurSize;
	float NearSize = Context.View.FinalPostProcessSettings.DepthOfFieldNearBlurSize;
	const float MaxSize = CVarDepthOfFieldMaxSize.GetValueOnRenderThread();
	FarSize = FMath::Min(FarSize, MaxSize);
	NearSize = FMath::Min(NearSize, MaxSize);
	Out.bFar = FarSize >= 0.01f;

	{
		const float CVarThreshold = CVarDepthOfFieldNearBlurSizeThreshold.GetValueOnRenderThread();
		Out.bNear = (NearSize >= CVarThreshold);
	}

	if (Context.View.Family->EngineShowFlags.VisualizeDOF)
	{
		// no need for this pass
		Out.bFar = false;
		Out.bNear = false;
	}

	if (Out.bFar || Out.bNear)
	{
		GaussianDOFPass(SeparateTranslucencyRef, Out.bFar ? FarSize : 0, Out.bNear ? NearSize : 0);

		const bool bMobileQuality = Context.View.FeatureLevel < ERHIFeatureLevel::SM4;
		return SeparateTranslucencyRef.IsValid() && !bMobileQuality;
	}
	else
	{
		return false;
	}
}

static FRenderingCompositeOutputRef AddBloom(FBloomDownSampleArray& BloomDownSampleArray, bool bVisualizeBloom)
{
	
	// Quality level to bloom stages table. Note: 0 is omitted, ensure element count tallys with the range documented with 'r.BloomQuality' definition.
	const static uint32 BloomQualityStages[] =
	{
		3,// Q1
		3,// Q2
		4,// Q3
		5,// Q4
		6,// Q5
	};

	int32 BloomQuality;
	{
		// console variable override
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.BloomQuality"));
		BloomQuality = FMath::Clamp(CVar->GetValueOnRenderThread(), 0, (int32)ARRAY_COUNT(BloomQualityStages));
	}

	// Extract the Context
	FPostprocessContext& Context = BloomDownSampleArray.Context;

	const bool bOldMetalNoFFT = IsMetalPlatform(Context.View.GetShaderPlatform()) && (RHIGetShaderLanguageVersion(Context.View.GetShaderPlatform()) < 4);
	const bool bUseFFTBloom = (Context.View.FinalPostProcessSettings.BloomMethod == EBloomMethod::BM_FFT
		&& Context.View.FeatureLevel >= ERHIFeatureLevel::SM5);
		
	static bool bWarnAboutOldMetalFFTOnce = false;
	if (bOldMetalNoFFT && bUseFFTBloom && !bWarnAboutOldMetalFFTOnce)
	{
		UE_LOG(LogRenderer, Error, TEXT("FFT Bloom is only supported on Metal 2.1 and later."));
		bWarnAboutOldMetalFFTOnce = true;
	}

	// Extract the downsample array.
	FBloomDownSampleArray::FRenderingRefArray& PostProcessDownsamples = BloomDownSampleArray.PostProcessDownsamples;

	FRenderingCompositeOutputRef BloomOutput;  
	if (BloomQuality == 0)
	{
		// No bloom, provide substitute source for lens flare.
		BloomOutput = PostProcessDownsamples[0];
	}
	else if (bUseFFTBloom && !bOldMetalNoFFT)
	{
		
		// verify the physical kernel is valid, or fail gracefully by skipping bloom 
		if (FRCPassFFTBloom::HasValidPhysicalKernel(Context))
		{

			// Use the first down sample as the source:
			const uint32 DownSampleIndex = 0;
			FRenderingCompositeOutputRef HalfResolutionRef = PostProcessDownsamples[DownSampleIndex];
			FRenderingCompositeOutputRef FullResolutionRef = Context.FinalOutput;

			FRenderingCompositePass* FFTPass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassFFTBloom());
			const bool bDoFullResBloom = (CVarHalfResFFTBloom.GetValueOnRenderThread() != 1);
			if (bDoFullResBloom)
			{
				FFTPass->SetInput(ePId_Input0, FRenderingCompositeOutputRef(FullResolutionRef));
			}
			else
			{
				FFTPass->SetInput(ePId_Input0, FRenderingCompositeOutputRef(HalfResolutionRef));
				FFTPass->SetInput(ePId_Input1, FRenderingCompositeOutputRef(FullResolutionRef));
			}

			Context.FinalOutput = FRenderingCompositeOutputRef(FFTPass);
		}
	}
	else
	{
		// Perform bloom blur + accumulate.
		struct FBloomStage
		{
			float BloomSize;
			const FLinearColor* Tint;
		};
		const FFinalPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;

		FBloomStage BloomStages[] =
		{
			{ Settings.Bloom6Size, &Settings.Bloom6Tint },
			{ Settings.Bloom5Size, &Settings.Bloom5Tint },
			{ Settings.Bloom4Size, &Settings.Bloom4Tint },
			{ Settings.Bloom3Size, &Settings.Bloom3Tint },
			{ Settings.Bloom2Size, &Settings.Bloom2Tint },
			{ Settings.Bloom1Size, &Settings.Bloom1Tint },
		};
		static const uint32 NumBloomStages = ARRAY_COUNT(BloomStages);

		const uint32 BloomStageCount = BloomQualityStages[BloomQuality - 1];
		check(BloomStageCount <= NumBloomStages);
		float TintScale = 1.0f / NumBloomStages;
		for (uint32 i = 0, SourceIndex = NumBloomStages - 1; i < BloomStageCount; i++, SourceIndex--)
		{
			FBloomStage& Op = BloomStages[i];

			FLinearColor Tint = (*Op.Tint) * TintScale;

			// Visualize bloom show effect of this modified bloom kernel on a single ray of green at the center of the screen
			// Note: This bloom visualization is pretty bogus for two reasons.  1) The bloom kernel is really 3 kernels (one for each r,g,b),
			// and replacing it by a single kernel for visualization isn't very sound.  2) The actual visualizer compares the response to
			// an arbitrary function..
			if (bVisualizeBloom)
			{
				float LumScale = Tint.ComputeLuminance();

				// R is used to pass down the reference, G is the emulated bloom
				Tint.R = 0;
				Tint.G = LumScale;
				Tint.B = 0;
			}
			// Only bloom this down-sampled input if the bloom size is non-zero
			if (Op.BloomSize > SMALL_NUMBER)
			{

				BloomOutput = RenderBloom(Context, PostProcessDownsamples[SourceIndex], Op.BloomSize * Settings.BloomSizeScale, Tint, BloomOutput);
			}
		}

		if (!BloomOutput.IsValid())
		{
			// Bloom was disabled by setting bloom size to zero in the post process.
			// No bloom, provide substitute source for lens flare.
			BloomOutput = PostProcessDownsamples[0];
		}
	}

	//do not default bloomoutput to PostProcessDownsamples[0] or you will get crazy overbloom with some FFT settings
	//however flares require an input.
	FRenderingCompositeOutputRef BloomFlareInput;
	if (BloomOutput.IsValid())
	{
		BloomFlareInput = BloomOutput;
	}
	else
	{
		BloomFlareInput = PostProcessDownsamples[0];
	}

	// Lens Flares
	FLinearColor LensFlareHDRColor = Context.View.FinalPostProcessSettings.LensFlareTint * Context.View.FinalPostProcessSettings.LensFlareIntensity;
	static const int32 MaxLensFlareQuality = 3;
	int32 LensFlareQuality;
	{
		// console variable override
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.LensFlareQuality"));
		LensFlareQuality = FMath::Clamp(CVar->GetValueOnRenderThread(), 0, MaxLensFlareQuality);
	}

	if (!LensFlareHDRColor.IsAlmostBlack() && LensFlareQuality > 0 && !bVisualizeBloom)
	{
		float PercentKernelSize = Context.View.FinalPostProcessSettings.LensFlareBokehSize;

		bool bLensBlur = PercentKernelSize > 0.3f;

		FRenderingCompositePass* PostProcessFlares = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessLensFlares(bLensBlur ? 2.0f : 1.0f, !bUseFFTBloom));

		PostProcessFlares->SetInput(ePId_Input0, BloomFlareInput);

		FRenderingCompositeOutputRef LensFlareInput = PostProcessDownsamples[MaxLensFlareQuality - LensFlareQuality];

		if (bLensBlur)
		{
			float Threshold = Context.View.FinalPostProcessSettings.LensFlareThreshold;

			FRenderingCompositePass* PostProcessLensBlur = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessLensBlur(PercentKernelSize, Threshold));
			PostProcessLensBlur->SetInput(ePId_Input0, LensFlareInput);
			PostProcessFlares->SetInput(ePId_Input1, FRenderingCompositeOutputRef(PostProcessLensBlur));
		}
		else
		{
			// fast: no blurring or blurring shared from bloom
			PostProcessFlares->SetInput(ePId_Input1, LensFlareInput);
		}

		BloomOutput = FRenderingCompositeOutputRef(PostProcessFlares);
	}

	return BloomOutput;
}

static void AddGBufferVisualizationOverview(
	FPostprocessContext& Context,
	FRenderingCompositeOutputRef SeparateTranslucencyInput,
	FRenderingCompositeOutputRef PreTonemapHDRColorInput,
	FRenderingCompositeOutputRef PostTonemapHDRColorInput,
	FRenderingCompositeOutputRef PreFlattenVelocity)
{
	static const auto CVarDumpFrames = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.BufferVisualizationDumpFrames"));
	static const auto CVarDumpFramesAsHDR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.BufferVisualizationDumpFramesAsHDR"));

	bool bVisualizationEnabled = Context.View.Family->EngineShowFlags.VisualizeBuffer;
	bool bOverviewModeEnabled = bVisualizationEnabled && (Context.View.CurrentBufferVisualizationMode == NAME_None);
	bool bHighResBufferVisualizationDumpRequried = GIsHighResScreenshot && GetHighResScreenshotConfig().bDumpBufferVisualizationTargets;
	bool bDumpFrames = Context.View.FinalPostProcessSettings.bBufferVisualizationDumpRequired && (CVarDumpFrames->GetValueOnRenderThread() || bHighResBufferVisualizationDumpRequried);
	bool bCaptureAsHDR = CVarDumpFramesAsHDR->GetValueOnRenderThread() || GetHighResScreenshotConfig().bCaptureHDR;
	FString BaseFilename;

	if (!bDumpFrames)
	{
		// We always do this work if there are any buffer visualization pipes
		bDumpFrames = Context.View.FinalPostProcessSettings.BufferVisualizationPipes.Num() > 0;
	}

	if (bDumpFrames)
	{
		BaseFilename = FString(Context.View.FinalPostProcessSettings.BufferVisualizationDumpBaseFilename);
	}
	
	if (bDumpFrames || bVisualizationEnabled)
	{
		FRenderingCompositeOutputRef IncomingStage = Context.FinalOutput;

		if (bDumpFrames || bOverviewModeEnabled)
		{
			FRenderingCompositePass* CompositePass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeBuffer());
			CompositePass->SetInput(ePId_Input0, FRenderingCompositeOutputRef(IncomingStage));
			Context.FinalOutput = FRenderingCompositeOutputRef(CompositePass);
			EPixelFormat OutputFormat = bCaptureAsHDR ? PF_FloatRGBA : PF_Unknown;

			// Loop over materials, creating stages for generation and downsampling of the tiles.			
			for (TArray<UMaterialInterface*>::TConstIterator It = Context.View.FinalPostProcessSettings.BufferVisualizationOverviewMaterials.CreateConstIterator(); It; ++It)
			{
				const UMaterialInterface* MaterialInterface = *It;
				if (MaterialInterface)
				{
					// Apply requested material
					FRenderingCompositePass* MaterialPass = AddPostProcessMaterialPass(Context, MaterialInterface, OutputFormat);
					MaterialPass->SetInput(EPassInputId(EPostProcessMaterialInput::SceneColor), IncomingStage);
					MaterialPass->SetInput(EPassInputId(EPostProcessMaterialInput::SeparateTranslucency), SeparateTranslucencyInput);
					MaterialPass->SetInput(EPassInputId(EPostProcessMaterialInput::PreTonemapHDRColor), PreTonemapHDRColorInput);
					MaterialPass->SetInput(EPassInputId(EPostProcessMaterialInput::PostTonemapHDRColor), PostTonemapHDRColorInput);
					MaterialPass->SetInput(EPassInputId(EPostProcessMaterialInput::Velocity), PreFlattenVelocity);

					FString VisualizationName = MaterialInterface->GetName();

					const TSharedPtr<FImagePixelPipe, ESPMode::ThreadSafe>* OutputPipe = Context.View.FinalPostProcessSettings.BufferVisualizationPipes.Find(MaterialInterface->GetFName());
					if (OutputPipe && OutputPipe->IsValid())
					{
						MaterialPass->SetOutputDumpPipe(ePId_Output0, *OutputPipe);
					}

					if (BaseFilename.Len())
					{
						// First off, allow the user to specify the pass as a format arg (using {material})
						TMap<FString, FStringFormatArg> FormatMappings;
						FormatMappings.Add(TEXT("material"), VisualizationName);

						FString MaterialFilename = FString::Format(*BaseFilename, FormatMappings);

						// If the format made no change to the string, we add the name of the material to ensure uniqueness
						if (MaterialFilename == BaseFilename)
						{
							MaterialFilename = BaseFilename + TEXT("_") + VisualizationName;
						}

						MaterialFilename.Append(TEXT(".png"));
						MaterialPass->SetOutputDumpFilename(ePId_Output0, *MaterialFilename);
					}

					// If the overview mode is activated, downsample the material pass to quarter size
					if (bOverviewModeEnabled)
					{
						// Down-sample to 1/2 size
						FRenderingCompositeOutputRef HalfSize = AddDownsamplePass(Context.Graph, TEXT("MaterialHalfSize"), MaterialPass, EDownsampleQuality::Low, EDownsampleFlags::ForceRaster);

						// Down-sample to 1/4 size
						FRenderingCompositeOutputRef QuarterSize = AddDownsamplePass(Context.Graph, TEXT("MaterialQuarterSize"), HalfSize, EDownsampleQuality::Low, EDownsampleFlags::ForceRaster);

						// Set whether current buffer is selected
						bool bIsSelected = false;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
						bIsSelected = (Context.View.FinalPostProcessSettings.bBufferVisualizationOverviewTargetIsSelected &&
							VisualizationName == Context.View.FinalPostProcessSettings.BufferVisualizationOverviewSelectedTargetMaterialName);
#endif

						// Mark the quarter size target as the dependency for the composite pass
						((FRCPassPostProcessVisualizeBuffer*)CompositePass)->AddVisualizationBuffer(FRenderingCompositeOutputRef(QuarterSize), VisualizationName, bIsSelected);
					}
					else
					{
						// We are just dumping the frames, so the material pass is the dependency of the composite
						CompositePass->AddDependency(MaterialPass);
					}
				}
				else
				{
					if (bOverviewModeEnabled)
					{
						((FRCPassPostProcessVisualizeBuffer*)CompositePass)->AddVisualizationBuffer(FRenderingCompositeOutputRef(), FString());
					}
				}
			}
		}
	}
}

// could be moved into the graph
// allows for Framebuffer blending optimization with the composition graph
void FPostProcessing::OverrideRenderTarget(FRenderingCompositeOutputRef It, TRefCountPtr<IPooledRenderTarget>& RT, FPooledRenderTargetDesc& Desc)
{
	for(;;)
	{
		It.GetOutput()->PooledRenderTarget = RT;
		It.GetOutput()->RenderTargetDesc = Desc;

		if(!It.GetPass()->FrameBufferBlendingWithInput0())
		{
			break;
		}

		It = *It.GetPass()->GetInput(ePId_Input0);
	}
}

bool FPostProcessing::AllowFullPostProcessing(const FViewInfo& View, ERHIFeatureLevel::Type FeatureLevel)
{
	if(FeatureLevel >= ERHIFeatureLevel::SM4)
	{
		return View.Family->EngineShowFlags.PostProcessing
			&& !View.Family->EngineShowFlags.VisualizeDistanceFieldAO
			&& !View.Family->EngineShowFlags.VisualizeDistanceFieldGI
			&& !View.Family->EngineShowFlags.VisualizeShadingModels
			&& !View.Family->EngineShowFlags.VisualizeMeshDistanceFields
			&& !View.Family->EngineShowFlags.VisualizeGlobalDistanceField
			&& !View.Family->EngineShowFlags.ShaderComplexity;
	}
	else
	{
		// Mobile post processing
		return View.Family->EngineShowFlags.PostProcessing
			&& !View.Family->EngineShowFlags.ShaderComplexity;
	}
}

void FPostProcessing::RegisterHMDPostprocessPass(FPostprocessContext& Context, const FEngineShowFlags& EngineShowFlags) const
{
	if (EngineShowFlags.StereoRendering && EngineShowFlags.HMDDistortion)
	{
		check(GEngine && GEngine->XRSystem.IsValid());
		FRenderingCompositePass* Node = nullptr;

		const IHeadMountedDisplay* HMD = GEngine->XRSystem->GetHMDDevice();
		checkf(HMD, TEXT("EngineShowFlags.HMDDistortion can not be true when IXRTrackingSystem::GetHMDDevice returns null"));

		static const FName MorpheusName(TEXT("PSVR"));
#if defined(MORPHEUS_ENGINE_DISTORTION) && MORPHEUS_ENGINE_DISTORTION
		if (GEngine->XRSystem->GetSystemName() == MorpheusName)
		{

			FRCPassPostProcessMorpheus* MorpheusPass = new FRCPassPostProcessMorpheus();
			MorpheusPass->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Node = Context.Graph.RegisterPass(MorpheusPass);
		}
		else
#endif
		{
			Node = Context.Graph.RegisterPass(new FRCPassPostProcessHMD());
		}

		if (Node)
		{
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}
	}
}


namespace
{

class FComposeSeparateTranslucencyPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComposeSeparateTranslucencyPS);
	SHADER_USE_PARAMETER_STRUCT(FComposeSeparateTranslucencyPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeparateTranslucency)
		SHADER_PARAMETER_SAMPLER(SamplerState, SeparateTranslucencySampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}
};

IMPLEMENT_GLOBAL_SHADER(FComposeSeparateTranslucencyPS, "/Engine/Private/ComposeSeparateTranslucency.usf", "MainPS", SF_Pixel);

FRDGTextureRef AddSeparateTranslucencyCompositionPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SeparateTranslucency)
{
	FRDGTextureDesc SceneColorDesc = SceneColor->Desc;
	SceneColorDesc.TargetableFlags &= ~TexCreate_UAV;
	SceneColorDesc.TargetableFlags |= TexCreate_RenderTargetable;

	FRDGTextureRef NewSceneColor = GraphBuilder.CreateTexture(SceneColorDesc, TEXT("SceneColor"));

	FComposeSeparateTranslucencyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeSeparateTranslucencyPS::FParameters>();
	PassParameters->SceneColor = SceneColor;
	PassParameters->SceneColorSampler = TStaticSamplerState<SF_Point>::GetRHI();
	PassParameters->SeparateTranslucency = SeparateTranslucency;
	PassParameters->SeparateTranslucencySampler = TStaticSamplerState<SF_Point>::GetRHI();
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(NewSceneColor, ERenderTargetLoadAction::ENoAction);

	TShaderMapRef<FComposeSeparateTranslucencyPS> PixelShader(View.ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		View.ShaderMap,
		RDG_EVENT_NAME("ComposeSeparateTranslucency %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
		*PixelShader,
		PassParameters,
		View.ViewRect);

	return NewSceneColor;
}

bool IsDepthOfFieldEnabledForView(const FViewInfo& View)
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DepthOfFieldQuality"));
	check(CVar);
	return
		View.Family->EngineShowFlags.DepthOfField &&
		CVar->GetValueOnRenderThread() > 0 &&
		View.FinalPostProcessSettings.DepthOfFieldFstop > 0 &&
		View.FinalPostProcessSettings.DepthOfFieldFocalDistance > 0;
}

EPixelFormat GetSceneColorHalfResFormat()
{
	return PF_FloatRGB;
}

void AddTemporalAAMainPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FScreenPassViewInfo& ScreenPassView,
	FRDGTextureRef InSceneColorTexture,
	FRDGTextureRef InCustomDepthTexture,
	FRDGTextureRef* OutSceneColorTexture,
	FRDGTextureRef* OutSceneColorHalfResTexture,
	FIntRect* OutSecondaryViewRect)
{
	const FViewInfo& View = ScreenPassView.View;

	// Skip if TAA is disabled or if we don't have any persistent view state (required to track history).
	if (View.AntiAliasingMethod != AAM_TemporalAA || !View.ViewState)
	{
		*OutSceneColorTexture = InSceneColorTexture;
		*OutSceneColorHalfResTexture = nullptr;
		*OutSecondaryViewRect = View.ViewRect;
		return;
	}

	FPreviousViewInfo& PrevViewInfo = View.ViewState->PrevFrameViewInfo;

	// Temporal AA passes.
	FTAAPassParameters TAAParameters(View);

	TAAParameters.Pass = View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale
		? ETAAPassConfig::MainUpsampling
		: ETAAPassConfig::Main;

	TAAParameters.SetupViewRect(View);

	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.PostProcessAAQuality"));
		uint32 Quality = FMath::Clamp(CVar->GetValueOnRenderThread(), 1, 6);
		TAAParameters.bUseFast = Quality == 3;
	}

	const FIntRect SecondaryViewRect = TAAParameters.OutputViewRect;

	const float HistoryUpscaleFactor = GetTemporalAAHistoryUpscaleFactor(View);

	// Configures TAA to upscale the history buffer; this is in addition to the secondary screen percentage upscale.
	// We end up with a scene color that is larger than the secondary screen percentage. We immediately downscale
	// afterwards using a Mitchel-Netravali filter.
	if (HistoryUpscaleFactor > 1.0f)
	{
		const FIntPoint HistoryViewSize(
			TAAParameters.OutputViewRect.Width() * HistoryUpscaleFactor,
			TAAParameters.OutputViewRect.Height() * HistoryUpscaleFactor);

		FIntPoint QuantizedMinHistorySize;
		QuantizeSceneBufferSize(HistoryViewSize, QuantizedMinHistorySize);

		TAAParameters.Pass = ETAAPassConfig::MainSuperSampling;
		TAAParameters.bUseFast = false;

		TAAParameters.OutputViewRect.Min.X = 0;
		TAAParameters.OutputViewRect.Min.Y = 0;
		TAAParameters.OutputViewRect.Max = HistoryViewSize;
	}

	TAAParameters.DownsampleOverrideFormat = GetSceneColorHalfResFormat();

	// Downsample pass may be merged with with TemporalAA when there is no motion blur and compute shader is used.
	// This is currently only possible for r.Downsample.Quality = 0 (box filter).
	TAAParameters.bDownsample = TAAParameters.bUseFast && IsTemporalAASceneDownsampleAllowed(View);

	TAAParameters.SceneColorInput = InSceneColorTexture;

	const FTAAOutputs TAAOutputs = AddTemporalAAPass(
		GraphBuilder,
		SceneTextures,
		View,
		TAAParameters,
		View.PrevViewInfo.TemporalAAHistory,
		&PrevViewInfo.TemporalAAHistory);

	*OutSceneColorTexture = TAAOutputs.SceneColor;
	*OutSceneColorHalfResTexture = TAAOutputs.DownsampledSceneColor;

	// If we upscaled the history buffer, downsize back to the secondary screen percentage size.
	if (HistoryUpscaleFactor > 1.0f)
	{
		const FIntRect InputViewport = TAAParameters.OutputViewRect;

		FIntPoint QuantizedOutputSize;
		QuantizeSceneBufferSize(SecondaryViewRect.Size(), QuantizedOutputSize);

		FScreenPassTextureViewport OutputViewport;
		OutputViewport.Rect = SecondaryViewRect;
		OutputViewport.Extent.X = FMath::Max(InSceneColorTexture->Desc.Extent.X, QuantizedOutputSize.X);
		OutputViewport.Extent.Y = FMath::Max(InSceneColorTexture->Desc.Extent.Y, QuantizedOutputSize.Y);

		*OutSceneColorTexture = ComputeMitchellNetravaliDownsample(GraphBuilder, ScreenPassView, TAAOutputs.SceneColor, InputViewport, OutputViewport);
	}

	// Post Process Material Chain - SSR Input
	if (!View.bViewStateIsReadOnly)
	{
		FPostProcessMaterialInputs Inputs;
		Inputs.SetInput(EPostProcessMaterialInput::SceneColor, InSceneColorTexture, SecondaryViewRect);
		Inputs.CustomDepthTexture = InCustomDepthTexture;

		FRDGTextureRef SSRInputTexture = AddPostProcessMaterialChain(GraphBuilder, ScreenPassView, Inputs, BL_SSRInput);

		if (SSRInputTexture != InSceneColorTexture)
		{
			GraphBuilder.QueueTextureExtraction(SSRInputTexture, &PrevViewInfo.CustomSSRInput);
		}
	}

	*OutSecondaryViewRect = SecondaryViewRect;
}

} // namespace


void FPostProcessing::Process(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& VelocityRT)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderPostProcessing);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PostProcessing_Process);

	check(IsInRenderingThread());
	check(View.VerifyMembersChecks());

	const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();

	GRenderTargetPool.AddPhaseEvent(TEXT("PostProcessing"));

	// All post processing is happening on the render thread side. All passes can access FinalPostProcessSettings and all
	// view settings. Those are copies for the RT then never get access by the main thread again.
	// Pointers to other structures might be unsafe to touch.

	// so that the passes can register themselves to the graph
	{
		FMemMark Mark(FMemStack::Get());
		FRenderingCompositePassContext CompositeContext(RHICmdList, View);

		FPostprocessContext Context(RHICmdList, CompositeContext.Graph, View);

		// not always valid
		FRenderingCompositeOutputRef HistogramOverScreen;
		FRenderingCompositeOutputRef Histogram;
		FRenderingCompositeOutputRef PreTonemapHDRColor;
		FRenderingCompositeOutputRef PostTonemapHDRColor;
		FRenderingCompositeOutputRef PreFlattenVelocity;
		FRenderingCompositeOutputRef SeparateTranslucency;
		FRenderingCompositeOutputRef BloomOutputCombined;
		FRenderingCompositeOutputRef CustomDepth;
		FRenderingCompositeOutputRef SceneColorHalfRes;

		class FAutoExposure
		{
		public:
			FAutoExposure(const FViewInfo& InView) :
				MethodId(GetAutoExposureMethod(InView))
			{}
			// distinguish between Basic and Histogram-based
			EAutoExposureMethod          MethodId;
			// not always valid
			FRenderingCompositeOutputRef EyeAdaptation;
		} AutoExposure(View);

		FRCPassPostProcessUpscale::PaniniParams PaniniConfig(View);

		const bool bDepthOfFieldEnabled = IsDepthOfFieldEnabledForView(View);

		const bool bVisualizeHDR = View.Family->EngineShowFlags.VisualizeHDR && FeatureLevel >= ERHIFeatureLevel::SM5;
		const bool bVisualizeBloom = View.Family->EngineShowFlags.VisualizeBloom && FeatureLevel >= ERHIFeatureLevel::SM4;
		const bool bVisualizeMotionBlur = IsVisualizeMotionBlurEnabled(View);
		const bool bVisualizeDepthOfField = bDepthOfFieldEnabled && Context.View.Family->EngineShowFlags.VisualizeDOF;

		const bool bTonemapEnabled = FeatureLevel >= ERHIFeatureLevel::SM4 && !bVisualizeBloom && !bVisualizeMotionBlur;

		const bool bHDROutputEnabled = GRHISupportsHDROutput && IsHDREnabled();

		static const auto CVarDumpFramesAsHDR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.BufferVisualizationDumpFramesAsHDR"));
		const bool bHDRTonemapperOutput = bTonemapEnabled && (View.Family->SceneCaptureSource == SCS_FinalColorHDR || GetHighResScreenshotConfig().bCaptureHDR || CVarDumpFramesAsHDR->GetValueOnRenderThread() || bHDROutputEnabled);

		const EAntiAliasingMethod AntiAliasingMethod = View.AntiAliasingMethod;

		FRCPassPostProcessTonemap* Tonemapper = nullptr;

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		// Register textures as inputs into the composition graph.
		if (SceneContext.SeparateTranslucencyRT)
		{
			SeparateTranslucency = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(SceneContext.SeparateTranslucencyRT));

			// make sure we only release if this is the last view we're rendering
			if (View.IsLastInFamily())
			{
				// the node keeps another reference so the RT will not be release too early
				SceneContext.FreeSeparateTranslucency();
			}
		}

		if (VelocityRT)
		{
			PreFlattenVelocity = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(VelocityRT));
		}

		// add the passes we want to add to the graph (commenting a line means the pass is not inserted into the graph) ---------

		if (AllowFullPostProcessing(View, FeatureLevel))
		{
			// GBuffers are released prior to executing the composition graph. We take a reference here
			// and then release the reference inside of RDGPass. This allows RDGPass to control lifetime
			// of the GBuffers internally.
			SceneContext.AdjustGBufferRefCount(RHICmdList, 1);

			FRenderingCompositePass* RDGPass = Context.Graph.RegisterPass(
				new(FMemStack::Get()) TRCPassForRDG<5, 2>(
					[&View, &SceneContext, bVisualizeBloom, bDepthOfFieldEnabled]
			(FRenderingCompositePass* Pass, FRenderingCompositePassContext& InContext)
			{
				FRDGBuilder GraphBuilder(InContext.RHICmdList);

				FRDGTextureRef SceneColorTexture = Pass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));
				FRDGTextureRef SceneDepthTexture = Pass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input1, TEXT("SceneDepth"));
				FRDGTextureRef SeparateTranslucencyTexture = Pass->CreateRDGTextureForOptionalInput(GraphBuilder, ePId_Input2, TEXT("SeparateTranslucency"));

				FRDGTextureRef CustomDepthTexture = GraphBuilder.TryRegisterExternalTexture(SceneContext.CustomDepth, TEXT("CustomDepth"));

				FSceneTextureParameters SceneTextures;
				SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

				FRDGTextureRef VelocityTexture = SceneTextures.SceneVelocityBuffer;

				const FScreenPassViewInfo& ScreenPassView(View);

				const FIntRect PrimaryViewRect = View.ViewRect;

				// Post Process Material Chain - Before Translucency
				{
					FPostProcessMaterialInputs Inputs;
					Inputs.SetInput(EPostProcessMaterialInput::SceneColor, SceneColorTexture, PrimaryViewRect);
					Inputs.SetInput(EPostProcessMaterialInput::SeparateTranslucency, SeparateTranslucencyTexture, PrimaryViewRect);
					Inputs.SetInput(EPostProcessMaterialInput::Velocity, VelocityTexture, PrimaryViewRect);
					Inputs.CustomDepthTexture = CustomDepthTexture;

					SceneColorTexture = AddPostProcessMaterialChain(GraphBuilder, ScreenPassView, Inputs, BL_BeforeTranslucency);
				}

				// Diaphragm Depth of Field
				{
					FRDGTextureRef LocalSceneColorTexture = SceneColorTexture;

					if (bDepthOfFieldEnabled && DiaphragmDOF::IsSupported(View.GetShaderPlatform()))
					{
						LocalSceneColorTexture = DiaphragmDOF::AddPasses(
							GraphBuilder,
							SceneTextures,
							View,
							SceneColorTexture,
							SeparateTranslucencyTexture);
					}

					// DOF passes were not added, therefore need to compose Separate translucency manually.
					if (LocalSceneColorTexture == SceneColorTexture && SeparateTranslucencyTexture)
					{
						LocalSceneColorTexture = AddSeparateTranslucencyCompositionPass(GraphBuilder, View, SceneColorTexture, SeparateTranslucencyTexture);
					}

					SceneColorTexture = LocalSceneColorTexture;
				}

				// Post Process Material Chain - Before Tonemapping
				{
					FPostProcessMaterialInputs Inputs;
					Inputs.SetInput(EPostProcessMaterialInput::SceneColor, SceneColorTexture, PrimaryViewRect);
					Inputs.SetInput(EPostProcessMaterialInput::SeparateTranslucency, SeparateTranslucencyTexture, PrimaryViewRect);
					Inputs.SetInput(EPostProcessMaterialInput::Velocity, VelocityTexture, PrimaryViewRect);
					Inputs.CustomDepthTexture = CustomDepthTexture;

					SceneColorTexture = AddPostProcessMaterialChain(GraphBuilder, ScreenPassView, Inputs, BL_BeforeTonemapping);
				}

				// Half-resolution scene color texture.
				FRDGTextureRef SceneColorHalfResTexture = nullptr;

				// Scene color view rectangle after temporal AA upscale to secondary screen percentage.
				FIntRect SecondaryViewRect;

				AddTemporalAAMainPass(
					GraphBuilder,
					SceneTextures,
					ScreenPassView,
					SceneColorTexture,
					CustomDepthTexture,
					&SceneColorTexture,
					&SceneColorHalfResTexture,
					&SecondaryViewRect);

				// SceneColorTexture is now upscaled to the SecondaryViewRect due to TAAU.

				if (VelocityTexture)
				{
					SceneColorTexture = AddMotionBlurPass(
						GraphBuilder,
						ScreenPassView,
						SecondaryViewRect,
						PrimaryViewRect,
						SceneColorTexture,
						SceneDepthTexture,
						VelocityTexture);
				}

				if (bVisualizeBloom)
				{
					SceneColorTexture = AddVisualizeBloomSetupPass(GraphBuilder, ScreenPassView, SceneColorTexture, SecondaryViewRect);
				}

				// If TAA didn't do it, downsample the scene color texture by half.
				if (!SceneColorHalfResTexture)
				{
					FDownsamplePassInputs Inputs;
					Inputs.Name = TEXT("SceneColorHalfRes");
					Inputs.Texture = SceneColorTexture;
					Inputs.Viewport = SecondaryViewRect;
					Inputs.Quality = GetDownsampleQuality();
					Inputs.FormatOverride = GetSceneColorHalfResFormat();

					FDownsamplePassOutputs Outputs = AddDownsamplePass(GraphBuilder, ScreenPassView, Inputs);

					SceneColorHalfResTexture = Outputs.Texture;
				}

				// Release held GBuffer reference taken during composition graph setup. Passes will take their own references during RDG setup.
				SceneContext.AdjustGBufferRefCount(InContext.RHICmdList, -1);

				Pass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, SceneColorTexture);
				Pass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output1, SceneColorHalfResTexture);

				GraphBuilder.Execute();

				InContext.SceneColorViewRect = SecondaryViewRect;
				InContext.ReferenceBufferSize = SceneColorTexture->Desc.Extent;
			}));

			RDGPass->SetInput(ePId_Input0, Context.FinalOutput);
			RDGPass->SetInput(ePId_Input1, Context.SceneDepth);
			RDGPass->SetInput(ePId_Input2, SeparateTranslucency);

			Context.FinalOutput = FRenderingCompositeOutputRef(RDGPass, ePId_Output0);

			SceneColorHalfRes = FRenderingCompositeOutputRef(RDGPass, ePId_Output1);

			{
				bool bHistogramNeeded = false;

				if (View.Family->EngineShowFlags.EyeAdaptation && (AutoExposure.MethodId == EAutoExposureMethod::AEM_Histogram)
					&& View.FinalPostProcessSettings.AutoExposureMinBrightness < View.FinalPostProcessSettings.AutoExposureMaxBrightness
					&& !View.bIsSceneCapture // Eye adaption is not available for scene captures.
					&& !bVisualizeBloom)
				{
					bHistogramNeeded = true;
				}

				if(!bTonemapEnabled)
				{
					bHistogramNeeded = false;
				}

				if(View.Family->EngineShowFlags.VisualizeHDR)
				{
					bHistogramNeeded = true;
				}

				if (!GIsHighResScreenshot && bHistogramNeeded && FeatureLevel >= ERHIFeatureLevel::SM5 && IStereoRendering::IsAPrimaryView(View.StereoPass, GEngine->StereoRenderingDevice))
				{
					FRenderingCompositePass* NodeHistogram = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessHistogram());

					NodeHistogram->SetInput(ePId_Input0, SceneColorHalfRes);

					HistogramOverScreen = FRenderingCompositeOutputRef(NodeHistogram);

					FRenderingCompositePass* NodeHistogramReduce = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessHistogramReduce());

					NodeHistogramReduce->SetInput(ePId_Input0, NodeHistogram);

					Histogram = FRenderingCompositeOutputRef(NodeHistogramReduce);
				}
			}

			// Compute DownSamples passes used by bloom, tint and eye-adaptation if possible.
			FBloomDownSampleArray::Ptr BloomAndEyeDownSamplesPtr;
			if (View.FinalPostProcessSettings.BloomIntensity > 0.f) // do bloom
			{
				// No Threshold:  We can share with Eye-Adaptation.
				if (Context.View.FinalPostProcessSettings.BloomThreshold <= -1 && Context.View.Family->Views.Num() == 1)
				{
					if (!GIsHighResScreenshot && View.State &&
						IStereoRendering::IsAPrimaryView(View.StereoPass, GEngine->StereoRenderingDevice) &&
						AutoExposure.MethodId == EAutoExposureMethod::AEM_Basic)
					{
						BloomAndEyeDownSamplesPtr = CreateDownSampleArray(Context, SceneColorHalfRes, true /*bGenerateLog2Alpha*/);
					}
				}
			}

			// some views don't have a state (thumbnail rendering)
			if(!GIsHighResScreenshot && View.State && IStereoRendering::IsAPrimaryView(View.StereoPass, GEngine->StereoRenderingDevice))
			{
				const bool bUseBasicEyeAdaptation = (AutoExposure.MethodId == EAutoExposureMethod::AEM_Basic);

				if (bUseBasicEyeAdaptation) // log average ps reduction ( non histogram ) 
				{
					
					if (!BloomAndEyeDownSamplesPtr.IsValid()) 
					{
						// need downsamples for eye-adaptation.
						FBloomDownSampleArray::Ptr EyeDownSamplesPtr = CreateDownSampleArray(Context, SceneColorHalfRes, true /*bGenerateLog2Alpha*/);
						AutoExposure.EyeAdaptation = AddPostProcessBasicEyeAdaptation(View, *EyeDownSamplesPtr);
					}
					else
					{
						// Use the alpha channel in the last downsample (smallest) to compute eye adaptations values.			
						AutoExposure.EyeAdaptation = AddPostProcessBasicEyeAdaptation(View, *BloomAndEyeDownSamplesPtr);
					}
				}
				else  // Use histogram version version
				{
					// We always add eye adaptation, if the engine show flag is disabled we set the ExposureScale in the texture to a fixed value
					AutoExposure.EyeAdaptation = AddPostProcessHistogramEyeAdaptation(Context, Histogram);
				}
			}

			if(View.FinalPostProcessSettings.BloomIntensity > 0.0f)
			{
				if (!BloomAndEyeDownSamplesPtr.IsValid())
				{
					FRenderingCompositeOutputRef HalfResBloomThreshold = RenderHalfResBloomThreshold(Context, SceneColorHalfRes, AutoExposure.EyeAdaptation);
					BloomAndEyeDownSamplesPtr = CreateDownSampleArray(Context, HalfResBloomThreshold, false /*bGenerateLog2Alpha*/);
				}
				BloomOutputCombined = AddBloom(*BloomAndEyeDownSamplesPtr, bVisualizeBloom);
			}

			PreTonemapHDRColor = Context.FinalOutput;

			if(bTonemapEnabled)
			{
				{
					FRenderingCompositeOutputRef FinalOutputPrev = Context.FinalOutput;

					Context.FinalOutput = AddPostProcessMaterialReplaceTonemapPass(Context, SeparateTranslucency, BloomOutputCombined);

					// No-op from post process material pass; run built-in tonemapper instead.
					if (Context.FinalOutput == FinalOutputPrev)
					{
						Tonemapper = AddTonemapper(Context, BloomOutputCombined, AutoExposure.EyeAdaptation, AutoExposure.MethodId, false, bHDRTonemapperOutput);
					}
				}

				PostTonemapHDRColor = Context.FinalOutput;

				// Add a pass-through as tonemapper will be forced LDR if final pass in chain 
				if (bHDRTonemapperOutput && !bHDROutputEnabled)
				{
					FRenderingCompositePass* PassthroughNode = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessPassThrough(nullptr));
					PassthroughNode->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
					Context.FinalOutput = FRenderingCompositeOutputRef(PassthroughNode);
				}
			}

			if(AntiAliasingMethod == AAM_FXAA)
			{
				AddPostProcessAA(Context);
			}

			if(bVisualizeDepthOfField)
			{
				FDepthOfFieldStats DepthOfFieldStat;

				FRenderingCompositePass* VisualizeNode = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeDOF(DepthOfFieldStat));
				VisualizeNode->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));

				Context.FinalOutput = FRenderingCompositeOutputRef(VisualizeNode);
			}
		}
		else
		{
			// Composes separate translucency,
			{
				FRenderingCompositePass* ComposeSeparateTranslucencyPass = Context.Graph.RegisterPass(
					new(FMemStack::Get()) TRCPassForRDG<2, 1>([](FRenderingCompositePass* Pass, FRenderingCompositePassContext& InContext)
				{
					FRDGBuilder GraphBuilder(InContext.RHICmdList);

					FSceneTextureParameters SceneTextures;
					SetupSceneTextureParameters(GraphBuilder, &SceneTextures);
	
					FRDGTextureRef SceneColor = Pass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));
					FRDGTextureRef LocalSeparateTranslucency = Pass->CreateRDGTextureForOptionalInput(GraphBuilder, ePId_Input1, TEXT("SeparateTranslucency"));

					FRDGTextureRef NewSceneColor = SceneColor;
					if (LocalSeparateTranslucency)
					{
						NewSceneColor = AddSeparateTranslucencyCompositionPass(GraphBuilder, InContext.View, SceneColor, LocalSeparateTranslucency);
					}

					Pass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, NewSceneColor);

					GraphBuilder.Execute();
				}));
				ComposeSeparateTranslucencyPass->SetInput(ePId_Input0, Context.FinalOutput);
				ComposeSeparateTranslucencyPass->SetInput(ePId_Input1, SeparateTranslucency);
				Context.FinalOutput = FRenderingCompositeOutputRef(ComposeSeparateTranslucencyPass, ePId_Output0);
			}

			// Shader complexity does not actually output a color
			if (!View.Family->EngineShowFlags.ShaderComplexity)
			{
				AddGammaOnlyTonemapper(Context);
			}
		}
		
		// Whether Context.FinalOutput is already unscaled.
		// If doing temporal upsampling, the final output is already unscaled in TAA pass.
		bool bUnscaledFinalOutput = Context.View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale;

		if(View.Family->EngineShowFlags.StationaryLightOverlap)
		{
			ensureMsgf(!bUnscaledFinalOutput, TEXT("Should not unscale final output multiple times."));

			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeComplexity(GEngine->StationaryLightOverlapColors, FVisualizeComplexityApplyPS::CS_RAMP, 1.f, false));
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.SceneColor));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

		if(View.Family->EngineShowFlags.VisualizeLightCulling) 
		{
			ensureMsgf(!bUnscaledFinalOutput, TEXT("Should not unscale final output multiple times."));

			float ComplexityScale = 1.f / (float)(GEngine->LightComplexityColors.Num() - 1) / .1f; // .1f comes from the values used in LightAccumulator_GetResult
			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeComplexity(GEngine->LightComplexityColors, FVisualizeComplexityApplyPS::CS_LINEAR,  ComplexityScale, false));
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.SceneColor));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

		if(View.Family->EngineShowFlags.VisualizeLPV)
		{
			ensureMsgf(!bUnscaledFinalOutput, TEXT("Should not unscale final output multiple times."));
			bUnscaledFinalOutput = true;

			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeLPV());
			Node->SetInput(ePId_Input0, Context.FinalOutput);
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

#if WITH_EDITOR
		// Show the selection outline if it is in the editor and we aren't in wireframe 
		// If the engine is in demo mode and game view is on we also do not show the selection outline
		if ( GIsEditor
			&& View.Family->EngineShowFlags.SelectionOutline
			&& !(View.Family->EngineShowFlags.Wireframe)
			&& !bVisualizeBloom
			&& !View.Family->EngineShowFlags.VisualizeHDR)
		{
			// Selection outline is after bloom, but before AA
			AddSelectionOutline(Context);
		}

		// Composite editor primitives if we had any to draw and compositing is enabled
		if (FSceneRenderer::ShouldCompositeEditorPrimitives(View) && !bVisualizeBloom)
		{
			//ensureMsgf(!bUnscaledFinalOutput, TEXT("Editor primitives should not be composited with already unscaled output."));

			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessCompositeEditorPrimitives(true));
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}
#endif
		if(View.Family->EngineShowFlags.VisualizeShadingModels && FeatureLevel >= ERHIFeatureLevel::SM4)
		{
			ensureMsgf(!bUnscaledFinalOutput, TEXT("VisualizeShadingModels is incompatible with unscaled output."));

			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeShadingModels(RHICmdList));
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

		if (View.Family->EngineShowFlags.GBufferHints && FeatureLevel >= ERHIFeatureLevel::SM4)
		{
			ensureMsgf(!bUnscaledFinalOutput, TEXT("GBufferHints is incompatible with unscaled output."));

			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessGBufferHints(RHICmdList));
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			// Ideally without lighting as we want the emissive, we should do that later.
			Node->SetInput(ePId_Input1, FRenderingCompositeOutputRef(Context.SceneColor));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}
		
		Context.FinalOutput = AddPostProcessMaterialChain(Context, BL_AfterTonemapping, SeparateTranslucency, PreTonemapHDRColor, PostTonemapHDRColor, PreFlattenVelocity);

#if WITH_EDITOR
		//Inspect the Final color, GBuffer and HDR
		//No more postprocess Final color should be the real one
		//The HDR was save before the tonemapping
		//GBuffer should not be change during post process 
		if (View.bUsePixelInspector && FeatureLevel >= ERHIFeatureLevel::SM4)
		{
			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBufferInspector(RHICmdList));
			Node->SetInput(ePId_Input0, Context.FinalOutput);
			Node->SetInput(ePId_Input1, PreTonemapHDRColor);
			Node->SetInput(ePId_Input2, Context.SceneColor);
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}
#endif //WITH_EDITOR

		if(bVisualizeBloom)
		{
			ensureMsgf(!bUnscaledFinalOutput, TEXT("VisualizeBloom is incompatible with unscaled output."));

			AddVisualizeBloomOverlay(Context, PreTonemapHDRColor, BloomOutputCombined);
		}

		if (View.Family->EngineShowFlags.VisualizeSSS)
		{
			ensureMsgf(!bUnscaledFinalOutput, TEXT("VisualizeSSS is incompatible with unscaled output."));
			Context.FinalOutput = VisualizeSubsurfaceShim(RHICmdList, Context.Graph, Context.FinalOutput);
		}

		AddGBufferVisualizationOverview(Context, SeparateTranslucency, PreTonemapHDRColor, PostTonemapHDRColor, PreFlattenVelocity);

		RegisterHMDPostprocessPass(Context, View.Family->EngineShowFlags);

		if(bVisualizeHDR)
		{
			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeHDR());
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Node->SetInput(ePId_Input1, Histogram);
			Node->SetInput(ePId_Input2, PreTonemapHDRColor);
			Node->SetInput(ePId_Input3, HistogramOverScreen);
			Node->AddDependency(AutoExposure.EyeAdaptation);

			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

		if(View.Family->EngineShowFlags.TestImage && FeatureLevel >= ERHIFeatureLevel::SM4)
		{
			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessTestImage());
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

		if (FRCPassPostProcessShaderPrint::IsEnabled(View))
		{
			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessShaderPrint());
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

		AddHighResScreenshotMask(Context);

		FIntPoint PrimaryUpscaleViewSize = Context.View.GetSecondaryViewRectSize();

		// If the final output is still not unscaled, therefore add Upscale pass.
		if ((!bUnscaledFinalOutput && View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::SpatialUpscale && View.ViewRect.Size() != PrimaryUpscaleViewSize) || PaniniConfig.IsEnabled())
		{
			bool bRequireUpscalePass = true;

			// Check if we can save the Upscale pass and do it in the Tonemapper to save performance
			if(Tonemapper && !PaniniConfig.IsEnabled() && !Tonemapper->bDoGammaOnly)
			{
				if (Context.FinalOutput.GetPass() == Tonemapper)
				{
					const int32 TonemapperMergeMode = CVarTonemapperMergeMode.GetValueOnRenderThread();
					bool bCombineTonemapperAndUpsample = false;

					if (TonemapperMergeMode == 1)
					{
						bCombineTonemapperAndUpsample = true;
					}
					else if (TonemapperMergeMode == 2)
					{
						const float TonemapperMergeThreshold = CVarTonemapperMergeThreshold.GetValueOnRenderThread();
						const float AreaRatio = View.ViewRect.Area() / (float)View.UnscaledViewRect.Area();
						bCombineTonemapperAndUpsample = AreaRatio > TonemapperMergeThreshold;
					}

					if (bCombineTonemapperAndUpsample)
					{
						Tonemapper->bDoScreenPercentageInTonemapper = true;
						// the upscale pass is no longer needed.
						bRequireUpscalePass = false;
					}
				}
			}

			if (PaniniConfig.IsEnabled() || bRequireUpscalePass)
			{
				int32 UpscaleQuality = CVarUpscaleQuality.GetValueOnRenderThread();
				UpscaleQuality = FMath::Clamp(UpscaleQuality, 0, 5);
				FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessUpscale(View, UpscaleQuality, PaniniConfig));
				Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput)); // Bilinear sampling.
				Node->SetInput(ePId_Input1, FRenderingCompositeOutputRef(Context.FinalOutput)); // Point sampling.
				Context.FinalOutput = FRenderingCompositeOutputRef(Node);
			}
		}

		// Adds secondary upscale.
		if (Context.View.RequiresSecondaryUpscale())
		{
			int32 UpscaleQuality = View.Family->SecondaryScreenPercentageMethod == ESecondaryScreenPercentageMethod::LowerPixelDensitySimulation ? 6 : 0;

			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessUpscale(
				View, UpscaleQuality, FRCPassPostProcessUpscale::PaniniParams::Default, /* bIsSecondaryUpscale = */ true));
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Node->SetInput(ePId_Input1, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

		// After the graph is built but before the graph is processed.
		// If a postprocess material is using a GBuffer it adds the refcount int FRCPassPostProcessMaterial::Process()
		// and when it gets processed it removes the refcount
		// We only release the GBuffers after the last view was processed (SplitScreen)
		if(View.IsLastInFamily())
		{
			// Generally we no longer need the GBuffers, anyone that wants to keep the GBuffers for longer should have called AdjustGBufferRefCount(1) to keep it for longer
			// and call AdjustGBufferRefCount(-1) once it's consumed. This needs to happen each frame. PostProcessMaterial do that automatically
			FSceneRenderTargets::Get(RHICmdList).AdjustGBufferRefCount(RHICmdList, -1);
		}

		// Add a pass-through for the final step if a backbuffer UAV is required but unsupported by this RHI
		if (Context.FinalOutput.IsComputePass() && !View.Family->RenderTarget->GetRenderTargetUAV().IsValid())
		{
			FRenderingCompositePass* PassthroughNode = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessPassThrough(nullptr));
			PassthroughNode->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(PassthroughNode);
		}

		// The graph setup should be finished before this line ----------------------------------------
		{
			// currently created on the heap each frame but View.Family->RenderTarget could keep this object and all would be cleaner
			TRefCountPtr<IPooledRenderTarget> Temp;
			FSceneRenderTargetItem Item;
			Item.TargetableTexture = (FTextureRHIRef&)View.Family->RenderTarget->GetRenderTargetTexture();
			Item.ShaderResourceTexture = (FTextureRHIRef&)View.Family->RenderTarget->GetRenderTargetTexture();
			Item.UAV = View.Family->RenderTarget->GetRenderTargetUAV();

			FPooledRenderTargetDesc Desc;

			// Texture could be bigger than viewport
			if (View.Family->RenderTarget->GetRenderTargetTexture())
			{
				Desc.Extent.X = View.Family->RenderTarget->GetRenderTargetTexture()->GetSizeX();
				Desc.Extent.Y = View.Family->RenderTarget->GetRenderTargetTexture()->GetSizeY();
			}
			else
			{
				Desc.Extent = View.Family->RenderTarget->GetSizeXY();
			}

			const bool bIsFinalOutputComputePass = Context.FinalOutput.IsComputePass();
			Desc.TargetableFlags |= bIsFinalOutputComputePass ? TexCreate_UAV : TexCreate_RenderTargetable;
			Desc.Format = bIsFinalOutputComputePass ? PF_R8G8B8A8 : PF_B8G8R8A8;

			// todo: this should come from View.Family->RenderTarget
			Desc.Format = bHDROutputEnabled ? GRHIHDRDisplayOutputFormat : Desc.Format;
			if (View.Family->SceneCaptureSource == SCS_FinalColorHDR) Desc.Format = PF_FloatRGBA;
			Desc.NumMips = 1;
			Desc.DebugName = TEXT("FinalPostProcessColor");

			GRenderTargetPool.CreateUntrackedElement(Desc, Temp, Item);

			OverrideRenderTarget(Context.FinalOutput, Temp, Desc);

			TArray<FRenderingCompositePass*> TargetedRoots;
			TargetedRoots.Add(Context.FinalOutput.GetPass());

			// execute the graph/DAG
			CompositeContext.Process(TargetedRoots, TEXT("PostProcessing"));

			// May need to wait on the final pass to complete
			if (Context.FinalOutput.IsAsyncComputePass())
			{
				FRHIComputeFence* ComputeFinalizeFence = Context.FinalOutput.GetComputePassEndFence();
				if (ComputeFinalizeFence)
				{
					Context.RHICmdList.WaitComputeFence(ComputeFinalizeFence);
				}
			}
		}
	}

	GRenderTargetPool.AddPhaseEvent(TEXT("AfterPostprocessing"));
}

static bool IsGaussianActive(FPostprocessContext& Context)
{

	float FarSize = Context.View.FinalPostProcessSettings.DepthOfFieldFarBlurSize;
	float NearSize = Context.View.FinalPostProcessSettings.DepthOfFieldNearBlurSize;

	float MaxSize = CVarDepthOfFieldMaxSize.GetValueOnRenderThread();

	FarSize = FMath::Min(FarSize, MaxSize);
	NearSize = FMath::Min(NearSize, MaxSize);
	const float CVarThreshold = CVarDepthOfFieldNearBlurSizeThreshold.GetValueOnRenderThread();

	if ((FarSize < 0.01f) && (NearSize < CVarThreshold))
	{
		return false;
	}
	return true;
}

void FPostProcessing::ProcessES2(FRHICommandListImmediate& RHICmdList, FScene* Scene, const FViewInfo& View)
{
	check(IsInRenderingThread());

	// This page: https://udn.epicgames.com/Three/RenderingOverview#Rendering%20state%20defaults 
	// describes what state a pass can expect and to what state it need to be set back.

	// All post processing is happening on the render thread side. All passes can access FinalPostProcessSettings and all
	// view settings. Those are copies for the RT then never get access by the main thread again.
	// Pointers to other structures might be unsafe to touch.

	const EDebugViewShaderMode DebugViewShaderMode = View.Family->GetDebugViewShaderMode();
	bool bAllowFullPostProcess =
		!(
			DebugViewShaderMode == DVSM_ShaderComplexity ||
			DebugViewShaderMode == DVSM_ShaderComplexityContainedQuadOverhead ||
			DebugViewShaderMode == DVSM_ShaderComplexityBleedingQuadOverhead
		);

	// so that the passes can register themselves to the graph
	{
		FMemMark Mark(FMemStack::Get());
		FRenderingCompositePassContext CompositeContext(RHICmdList, View);

		FPostprocessContext Context(RHICmdList, CompositeContext.Graph, View);
		FRenderingCompositeOutputRef BloomOutput;
		FRenderingCompositeOutputRef DofOutput;

		bool bUseAa = View.AntiAliasingMethod == AAM_TemporalAA;

		// AA with Mobile32bpp mode requires this outside of bUsePost.
		if(bUseAa)
		{
			// Handle pointer swap for double buffering.
			FSceneViewState* ViewState = (FSceneViewState*)View.State;
			if(ViewState)
			{
				// Note that this drops references to the render targets from two frames ago. This
				// causes them to be added back to the pool where we can grab them again.
				ViewState->MobileAaBloomSunVignette1 = ViewState->MobileAaBloomSunVignette0;
				ViewState->MobileAaColor1 = ViewState->MobileAaColor0;
			}
		}

		const FIntPoint FinalTargetSize = View.Family->RenderTarget->GetSizeXY();
		FIntRect FinalOutputViewRect = View.ViewRect;
		FIntPoint PrePostSourceViewportSize = View.ViewRect.Size();
		// ES2 preview uses a subsection of the scene RT
		FIntPoint SceneColorSize = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();
		bool bViewRectSource = SceneColorSize != PrePostSourceViewportSize;
		bool bMobileHDR32bpp = IsMobileHDR32bpp();

		// temporary solution for SP_METAL using HW sRGB flag during read vs all other mob platforms using
		// incorrect UTexture::SRGB state. (UTexture::SRGB != HW texture state)
		bool bSRGBAwareTarget = View.Family->RenderTarget->GetDisplayGamma() == 1.0f
			&& View.bIsSceneCapture
			&& IsMetalMobilePlatform(View.GetShaderPlatform());

		// add the passes we want to add to the graph (commenting a line means the pass is not inserted into the graph) ---------
		if( View.Family->EngineShowFlags.PostProcessing && bAllowFullPostProcess)
		{
			const EMobileHDRMode HDRMode = GetMobileHDRMode();
			bool bUseEncodedHDR = HDRMode == EMobileHDRMode::EnabledRGBE;
			bool bHDRModeAllowsPost = bUseEncodedHDR || HDRMode == EMobileHDRMode::EnabledFloat16;

			bool bUseSun = !bUseEncodedHDR && View.bLightShaftUse;
			bool bUseDof = !bUseEncodedHDR && GetMobileDepthOfFieldScale(View) > 0.0f && !Context.View.Family->EngineShowFlags.VisualizeDOF;
			bool bUseBloom = View.FinalPostProcessSettings.BloomIntensity > 0.0f;
			bool bUseVignette = View.FinalPostProcessSettings.VignetteIntensity > 0.0f;

			bool bWorkaround = CVarRenderTargetSwitchWorkaround.GetValueOnRenderThread() != 0;

			// Use original mobile Dof on ES2 devices regardless of bMobileHQGaussian.
			// HQ gaussian 
#if PLATFORM_HTML5 // EMSCRITPEN_TOOLCHAIN_UPGRADE_CHECK -- i.e. remove this when LLVM no longer errors -- appologies for the mess
			// UE-61742 : the following will coerce i160 bit (bMobileHQGaussian) to an i8 LLVM variable
			bool bUseMobileDof = bUseDof && ((1 - View.FinalPostProcessSettings.bMobileHQGaussian) + (Context.View.GetFeatureLevel() < ERHIFeatureLevel::ES3_1));
#else
			bool bUseMobileDof = bUseDof && (!View.FinalPostProcessSettings.bMobileHQGaussian || (Context.View.GetFeatureLevel() < ERHIFeatureLevel::ES3_1));
#endif

			// This is a workaround to avoid a performance cliff when using many render targets. 
			bool bUseBloomSmall = bUseBloom && !bUseSun && !bUseDof && bWorkaround;

			// Post is not supported on ES2 devices using mosaic.
			bool bUsePost = bHDRModeAllowsPost && IsMobileHDR();
			
			if (bUsePost && IsMobileDistortionActive(View))
			{
				FRenderingCompositePass* AccumulatedDistortion = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCDistortionAccumulatePassES2(SceneColorSize, Scene));
				AccumulatedDistortion->SetInput(ePId_Input0, Context.FinalOutput); // unused atm
				FRenderingCompositeOutputRef AccumulatedDistortionRef(AccumulatedDistortion);
				
				FRenderingCompositePass* PostProcessDistorsion = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCDistortionMergePassES2(SceneColorSize));
				PostProcessDistorsion->SetInput(ePId_Input0, Context.FinalOutput);
				PostProcessDistorsion->SetInput(ePId_Input1, AccumulatedDistortionRef);
				Context.FinalOutput = FRenderingCompositeOutputRef(PostProcessDistorsion);
			}

			// Always evaluate custom post processes
			if (bUsePost)
			{
				Context.FinalOutput = AddPostProcessMaterialChain(Context, BL_BeforeTranslucency, nullptr);
				Context.FinalOutput = AddPostProcessMaterialChain(Context, BL_BeforeTonemapping, nullptr);
			}

			// Optional fixed pass processes
			if (bUsePost && (bUseSun | bUseDof | bUseBloom | bUseVignette))
			{
				if (bUseSun || bUseDof)
				{
					// Convert depth to {circle of confusion, sun shaft intensity}
				//	FRenderingCompositePass* PostProcessSunMask = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSunMaskES2(PrePostSourceViewportSize, false));
					FRenderingCompositePass* PostProcessSunMask = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSunMaskES2(SceneColorSize));
					PostProcessSunMask->SetInput(ePId_Input0, Context.FinalOutput);
					Context.FinalOutput = FRenderingCompositeOutputRef(PostProcessSunMask);
					//@todo Ronin sunmask pass isnt clipping to image only.
				}

				FRenderingCompositeOutputRef PostProcessBloomSetup;
				if (bUseSun || bUseMobileDof || bUseBloom)
				{
					if(bUseBloomSmall)
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomSetupSmallES2(PrePostSourceViewportSize, bViewRectSource));
						Pass->SetInput(ePId_Input0, Context.FinalOutput);
						PostProcessBloomSetup = FRenderingCompositeOutputRef(Pass);
					}
					else
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomSetupES2(FinalOutputViewRect, bViewRectSource));
						Pass->SetInput(ePId_Input0, Context.FinalOutput);
						PostProcessBloomSetup = FRenderingCompositeOutputRef(Pass);
					}
				}

				if (bUseDof)
				{
					if (bUseMobileDof)
					{
						// Near dilation circle of confusion size.
						// Samples at 1/16 area, writes to 1/16 area.
						FRenderingCompositeOutputRef PostProcessNear;
						{
							FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDofNearES2(FinalOutputViewRect.Size()));
							Pass->SetInput(ePId_Input0, PostProcessBloomSetup);
							PostProcessNear = FRenderingCompositeOutputRef(Pass);
						}

						// DOF downsample pass.
						// Samples at full resolution, writes to 1/4 area.
						FRenderingCompositeOutputRef PostProcessDofDown;
						{
							FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDofDownES2(FinalOutputViewRect, bViewRectSource));
							Pass->SetInput(ePId_Input0, Context.FinalOutput);
							Pass->SetInput(ePId_Input1, PostProcessNear);
							PostProcessDofDown = FRenderingCompositeOutputRef(Pass);
						}

						// DOF blur pass.
						// Samples at 1/4 area, writes to 1/4 area.
						FRenderingCompositeOutputRef PostProcessDofBlur;
						{
							FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessDofBlurES2(FinalOutputViewRect.Size()));
							Pass->SetInput(ePId_Input0, PostProcessDofDown);
							Pass->SetInput(ePId_Input1, PostProcessNear);
							PostProcessDofBlur = FRenderingCompositeOutputRef(Pass);
							DofOutput = PostProcessDofBlur;
						}
					}
					else
					{
						// black is how we clear the velocity buffer so this means no velocity
						FRenderingCompositePass* NoVelocity = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(GSystemTextures.BlackDummy));
						FRenderingCompositeOutputRef NoVelocityRef(NoVelocity);
						
						bool bDepthOfField = 
							View.Family->EngineShowFlags.DepthOfField &&
							IsGaussianActive(Context);

						if(bDepthOfField)
						{
							FDepthOfFieldStats DepthOfFieldStat;
							FRenderingCompositeOutputRef DummySeparateTranslucency;
							AddPostProcessDepthOfFieldGaussian(Context, DepthOfFieldStat, NoVelocityRef, DummySeparateTranslucency);
						}
					}
				}

				// Bloom.
				FRenderingCompositeOutputRef PostProcessDownsample2;
				FRenderingCompositeOutputRef PostProcessDownsample3;
				FRenderingCompositeOutputRef PostProcessDownsample4;
				FRenderingCompositeOutputRef PostProcessDownsample5;
				FRenderingCompositeOutputRef PostProcessUpsample4;
				FRenderingCompositeOutputRef PostProcessUpsample3;
				FRenderingCompositeOutputRef PostProcessUpsample2;

				if(bUseBloomSmall)
				{
					float DownScale = 0.66f * 4.0f;
					// Downsample by 2
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomDownES2(PrePostSourceViewportSize/4, DownScale * 2.0f));
						Pass->SetInput(ePId_Input0, PostProcessBloomSetup);
						PostProcessDownsample2 = FRenderingCompositeOutputRef(Pass);
					}
				}

				if(bUseBloom && (!bUseBloomSmall))
				{
					float DownScale = 0.66f * 4.0f;
					// Downsample by 2
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomDownES2(PrePostSourceViewportSize/4, DownScale));
						Pass->SetInput(ePId_Input0, PostProcessBloomSetup);
						PostProcessDownsample2 = FRenderingCompositeOutputRef(Pass);
					}

					// Downsample by 2
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomDownES2(PrePostSourceViewportSize/8, DownScale));
						Pass->SetInput(ePId_Input0, PostProcessDownsample2);
						PostProcessDownsample3 = FRenderingCompositeOutputRef(Pass);
					}

					// Downsample by 2
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomDownES2(PrePostSourceViewportSize/16, DownScale));
						Pass->SetInput(ePId_Input0, PostProcessDownsample3);
						PostProcessDownsample4 = FRenderingCompositeOutputRef(Pass);
					}

					// Downsample by 2
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomDownES2(PrePostSourceViewportSize/32, DownScale));
						Pass->SetInput(ePId_Input0, PostProcessDownsample4);
						PostProcessDownsample5 = FRenderingCompositeOutputRef(Pass);
					}

					const FFinalPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;

					float UpScale = 0.66f * 2.0f;
					// Upsample by 2
					{
						FVector4 TintA = FVector4(Settings.Bloom4Tint.R, Settings.Bloom4Tint.G, Settings.Bloom4Tint.B, 0.0f);
						FVector4 TintB = FVector4(Settings.Bloom5Tint.R, Settings.Bloom5Tint.G, Settings.Bloom5Tint.B, 0.0f);
						TintA *= View.FinalPostProcessSettings.BloomIntensity;
						TintB *= View.FinalPostProcessSettings.BloomIntensity;
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomUpES2(PrePostSourceViewportSize/32, FVector2D(UpScale, UpScale), TintA, TintB));
						Pass->SetInput(ePId_Input0, PostProcessDownsample4);
						Pass->SetInput(ePId_Input1, PostProcessDownsample5);
						PostProcessUpsample4 = FRenderingCompositeOutputRef(Pass);
					}

					// Upsample by 2
					{
						FVector4 TintA = FVector4(Settings.Bloom3Tint.R, Settings.Bloom3Tint.G, Settings.Bloom3Tint.B, 0.0f);
						TintA *= View.FinalPostProcessSettings.BloomIntensity;
						FVector4 TintB = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomUpES2(PrePostSourceViewportSize/16, FVector2D(UpScale, UpScale), TintA, TintB));
						Pass->SetInput(ePId_Input0, PostProcessDownsample3);
						Pass->SetInput(ePId_Input1, PostProcessUpsample4);
						PostProcessUpsample3 = FRenderingCompositeOutputRef(Pass);
					}

					// Upsample by 2
					{
						FVector4 TintA = FVector4(Settings.Bloom2Tint.R, Settings.Bloom2Tint.G, Settings.Bloom2Tint.B, 0.0f);
						TintA *= View.FinalPostProcessSettings.BloomIntensity;
						// Scaling Bloom2 by extra factor to match filter area difference between PC default and mobile.
						TintA *= 0.5;
						FVector4 TintB = FVector4(1.0f, 1.0f, 1.0f, 0.0f);
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBloomUpES2(PrePostSourceViewportSize/8, FVector2D(UpScale, UpScale), TintA, TintB));
						Pass->SetInput(ePId_Input0, PostProcessDownsample2);
						Pass->SetInput(ePId_Input1, PostProcessUpsample3);
						PostProcessUpsample2 = FRenderingCompositeOutputRef(Pass);
					}
				}

				FRenderingCompositeOutputRef PostProcessSunBlur;
				if(bUseSun)
				{
					// Sunshaft depth blur using downsampled alpha.
					FRenderingCompositeOutputRef PostProcessSunAlpha;
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSunAlphaES2(PrePostSourceViewportSize));
						Pass->SetInput(ePId_Input0, PostProcessBloomSetup);
						PostProcessSunAlpha = FRenderingCompositeOutputRef(Pass);
					}

					// Sunshaft blur number two.
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSunBlurES2(PrePostSourceViewportSize));
						Pass->SetInput(ePId_Input0, PostProcessSunAlpha);
						PostProcessSunBlur = FRenderingCompositeOutputRef(Pass);
					}
				}

				if(bUseSun | bUseVignette | bUseBloom)
				{
					FRenderingCompositeOutputRef PostProcessSunMerge;
					if(bUseBloomSmall) 
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSunMergeSmallES2(PrePostSourceViewportSize));
						Pass->SetInput(ePId_Input0, PostProcessBloomSetup);
						Pass->SetInput(ePId_Input1, PostProcessDownsample2);
						PostProcessSunMerge = FRenderingCompositeOutputRef(Pass);
						BloomOutput = PostProcessSunMerge;
					}
					else
					{
						FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSunMergeES2(PrePostSourceViewportSize));
						if(bUseSun)
						{
							Pass->SetInput(ePId_Input0, PostProcessSunBlur);
						}
						if(bUseBloom)
						{
							Pass->SetInput(ePId_Input1, PostProcessBloomSetup);
							Pass->SetInput(ePId_Input2, PostProcessUpsample2);
						}
						PostProcessSunMerge = FRenderingCompositeOutputRef(Pass);
						BloomOutput = PostProcessSunMerge;
					}

					// Mobile temporal AA requires a composite of two of these frames.
					if(bUseAa && (bUseBloom || bUseSun))
					{
						FSceneViewState* ViewState = (FSceneViewState*)View.State;
						FRenderingCompositeOutputRef PostProcessSunMerge2;
						if(ViewState && ViewState->MobileAaBloomSunVignette1)
						{
							FRenderingCompositePass* History;
							History = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(ViewState->MobileAaBloomSunVignette1));
							PostProcessSunMerge2 = FRenderingCompositeOutputRef(History);
						}
						else
						{
							PostProcessSunMerge2 = PostProcessSunMerge;
						}

						FRenderingCompositeOutputRef PostProcessSunAvg;
						{
							FRenderingCompositePass* Pass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSunAvgES2(PrePostSourceViewportSize));
							Pass->SetInput(ePId_Input0, PostProcessSunMerge);
							Pass->SetInput(ePId_Input1, PostProcessSunMerge2);
							PostProcessSunAvg = FRenderingCompositeOutputRef(Pass);
						}
						BloomOutput = PostProcessSunAvg;
					}
				}
			} // bUsePost

			// mobile separate translucency 
			if (IsMobileSeparateTranslucencyActive(Context.View))
			{
				FRCSeparateTranslucensyPassES2* Pass = (FRCSeparateTranslucensyPassES2*)Context.Graph.RegisterPass(new(FMemStack::Get()) FRCSeparateTranslucensyPassES2());
				Pass->SetInput(ePId_Input0, Context.FinalOutput);
				Context.FinalOutput = FRenderingCompositeOutputRef(Pass);
			}
		}
		
		static const auto VarTonemapperFilm = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.TonemapperFilm"));
		const bool bUseTonemapperFilm = Context.View.GetFeatureLevel() == ERHIFeatureLevel::ES3_1 && IsMobileHDR() && !bMobileHDR32bpp && GSupportsRenderTargetFormat_PF_FloatRGBA && (VarTonemapperFilm && VarTonemapperFilm->GetValueOnRenderThread());


		static const auto VarTonemapperUpscale = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileTonemapperUpscale"));
		bool bDisableUpscaleInTonemapper = IsMobileHDRMosaic() || !VarTonemapperUpscale || VarTonemapperUpscale->GetValueOnRenderThread() == 0;

		bool* DoScreenPercentageInTonemapperPtr = nullptr;
		FRenderingCompositePass* TonemapperPass = nullptr;
		if (bAllowFullPostProcess)
		{
			if (bUseTonemapperFilm)
			{
				//@todo Ronin Set to EAutoExposureMethod::AEM_Basic for PC vk crash.
				FRCPassPostProcessTonemap* PostProcessTonemap = AddTonemapper(Context, BloomOutput, nullptr, EAutoExposureMethod::AEM_Histogram, false, false);
				// remember the tonemapper pass so we can check if it's last
				TonemapperPass = PostProcessTonemap;

				PostProcessTonemap->bDoScreenPercentageInTonemapper = false;
				DoScreenPercentageInTonemapperPtr = &PostProcessTonemap->bDoScreenPercentageInTonemapper;			
			}
			else
			{
				// Must run to blit to back buffer even if post processing is off.
				FRCPassPostProcessTonemapES2* PostProcessTonemap = (FRCPassPostProcessTonemapES2*)Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessTonemapES2(Context.View, bViewRectSource, bSRGBAwareTarget));
				// remember the tonemapper pass so we can check if it's last
				TonemapperPass = PostProcessTonemap;

				PostProcessTonemap->SetInput(ePId_Input0, Context.FinalOutput);
				if (!BloomOutput.IsValid())
				{
					FRenderingCompositePass* NoBloom = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(GSystemTextures.BlackAlphaOneDummy));
					FRenderingCompositeOutputRef NoBloomRef(NoBloom);
					PostProcessTonemap->SetInput(ePId_Input1, NoBloomRef);
				}
				else
				{
					PostProcessTonemap->SetInput(ePId_Input1, BloomOutput);
				}
				PostProcessTonemap->SetInput(ePId_Input2, DofOutput);

				Context.FinalOutput = FRenderingCompositeOutputRef(PostProcessTonemap);

				PostProcessTonemap->bDoScreenPercentageInTonemapper = false;
				DoScreenPercentageInTonemapperPtr = &PostProcessTonemap->bDoScreenPercentageInTonemapper;
			}
			SetMobilePassFlipVerticalAxis(TonemapperPass);
		}

		// if Context.FinalOutput was the clipped result of sunmask stage then this stage also restores Context.FinalOutput back original target size.
		FinalOutputViewRect = View.UnscaledViewRect;

		if (View.Family->EngineShowFlags.PostProcessing && bAllowFullPostProcess)
		{
			if (IsMobileHDR() && !IsMobileHDRMosaic())
			{
				Context.FinalOutput = AddPostProcessMaterialChain(Context, BL_AfterTonemapping, nullptr);
			}
			SetMobilePassFlipVerticalAxis(Context.FinalOutput.GetPass());

			if (bUseAa)
			{
				// Double buffer post output.
				FSceneViewState* ViewState = (FSceneViewState*)View.State;

				FRenderingCompositeOutputRef PostProcessPrior = Context.FinalOutput;
				if(ViewState && ViewState->MobileAaColor1)
				{
					FRenderingCompositePass* History;
					History = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessInput(ViewState->MobileAaColor1));
					PostProcessPrior = FRenderingCompositeOutputRef(History);
				}

				// Mobile temporal AA is done after tonemapping.
				FRenderingCompositePass* PostProcessAa = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessAaES2());
				PostProcessAa->SetInput(ePId_Input0, Context.FinalOutput);
				PostProcessAa->SetInput(ePId_Input1, PostProcessPrior);
				Context.FinalOutput = FRenderingCompositeOutputRef(PostProcessAa);
			}
		}

		AddHighResScreenshotMask(Context);

#if WITH_EDITOR
		// Show the selection outline if it is in the editor and we aren't in wireframe 
		// If the engine is in demo mode and game view is on we also do not show the selection outline
		if ( GIsEditor
			&& View.Family->EngineShowFlags.SelectionOutline
			&& !(View.Family->EngineShowFlags.Wireframe)
			)
		{
			// Editor selection outline
			AddSelectionOutline(Context);
		}

		if (FSceneRenderer::ShouldCompositeEditorPrimitives(View))
		{
			FRenderingCompositePass* EditorCompNode = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessCompositeEditorPrimitives(false));
			EditorCompNode->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(EditorCompNode);
		}
#endif

		// Apply ScreenPercentage
		if (View.UnscaledViewRect != View.ViewRect)
		{
			if (bDisableUpscaleInTonemapper || Context.FinalOutput.GetPass() != TonemapperPass)
			{
				FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessUpscaleES2(View));
				Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput)); // Bilinear sampling.
				Node->SetInput(ePId_Input1, FRenderingCompositeOutputRef(Context.FinalOutput)); // Point sampling.
				Context.FinalOutput = FRenderingCompositeOutputRef(Node);
			}
			else if (DoScreenPercentageInTonemapperPtr)
			{
				*DoScreenPercentageInTonemapperPtr = true;
			}
		}

#ifdef WITH_EDITOR
		bool bES2Legend = true;
#else
		// Legend is costly so we don't do it for ES2, ideally we make a shader permutation
		bool bES2Legend = false;
#endif

		if(DebugViewShaderMode == DVSM_QuadComplexity)
		{
			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeComplexity(GEngine->QuadComplexityColors, FVisualizeComplexityApplyPS::CS_STAIR,  1.f, bES2Legend));
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

		if(DebugViewShaderMode == DVSM_ShaderComplexity || DebugViewShaderMode == DVSM_ShaderComplexityContainedQuadOverhead || DebugViewShaderMode == DVSM_ShaderComplexityBleedingQuadOverhead)
		{
			FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessVisualizeComplexity(GEngine->ShaderComplexityColors, FVisualizeComplexityApplyPS::CS_RAMP,  1.f, bES2Legend));
			Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
			Context.FinalOutput = FRenderingCompositeOutputRef(Node);
		}

		RegisterHMDPostprocessPass(Context, View.Family->EngineShowFlags);

		// The graph setup should be finished before this line ----------------------------------------

		{
			// currently created on the heap each frame but View.Family->RenderTarget could keep this object and all would be cleaner
			TRefCountPtr<IPooledRenderTarget> Temp;
			FSceneRenderTargetItem Item;
			Item.TargetableTexture = (FTextureRHIRef&)View.Family->RenderTarget->GetRenderTargetTexture();
			Item.ShaderResourceTexture = (FTextureRHIRef&)View.Family->RenderTarget->GetRenderTargetTexture();

			FPooledRenderTargetDesc Desc;

			if (View.Family->RenderTarget->GetRenderTargetTexture())
			{
				Desc.Extent.X = View.Family->RenderTarget->GetRenderTargetTexture()->GetSizeX();
				Desc.Extent.Y = View.Family->RenderTarget->GetRenderTargetTexture()->GetSizeY();
			}
			else
			{
				Desc.Extent = View.Family->RenderTarget->GetSizeXY();
			}

			// todo: this should come from View.Family->RenderTarget
			Desc.Format = PF_B8G8R8A8;
			Desc.NumMips = 1;
			Desc.DebugName = TEXT("OverriddenRenderTarget");

			GRenderTargetPool.CreateUntrackedElement(Desc, Temp, Item);

			OverrideRenderTarget(Context.FinalOutput, Temp, Desc);

			CompositeContext.Process(Context.FinalOutput.GetPass(), TEXT("PostProcessingES2"));
		}
	}
	SetMobilePassFlipVerticalAxis(nullptr);
}

void FPostProcessing::ProcessPlanarReflection(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& OutFilteredSceneColor)
{
	FSceneViewState* ViewState = View.ViewState;
	const EAntiAliasingMethod AntiAliasingMethod = View.AntiAliasingMethod;

	const FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	if (AntiAliasingMethod == AAM_TemporalAA && ViewState)
	{
		FRDGBuilder GraphBuilder(RHICmdList);

		FSceneTextureParameters SceneTextures;
		SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

		// Planar reflections don't support velocity.
		SceneTextures.SceneVelocityBuffer = nullptr;

		const FTemporalAAHistory& InputHistory = View.PrevViewInfo.TemporalAAHistory;
		FTemporalAAHistory* OutputHistory = &ViewState->PrevFrameViewInfo.TemporalAAHistory;

		FTAAPassParameters Parameters(View);
		Parameters.SceneColorInput = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));

		FTAAOutputs Outputs = AddTemporalAAPass(
			GraphBuilder,
			SceneTextures,
			View,
			Parameters,
			InputHistory,
			OutputHistory);

		GraphBuilder.QueueTextureExtraction(Outputs.SceneColor, &OutFilteredSceneColor);

		GraphBuilder.Execute();
	}
	else
	{
		OutFilteredSceneColor = SceneContext.GetSceneColor();
	}
}

bool FPostProcessing::HasAlphaChannelSupport()
{
	return CVarAlphaChannel.GetValueOnAnyThread() != 0;
}
