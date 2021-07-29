// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessFFTBloom.h"
#include "GPUFastFourierTransform.h"
#include "RendererModule.h"
#include "Rendering/Texture2DResource.h"

namespace
{
TAutoConsoleVariable<int32> CVarHalfResFFTBloom(
	TEXT("r.Bloom.HalfResolutionFFT"),
	0,
	TEXT("Experimental half-resolution FFT Bloom convolution. \n")
	TEXT(" 0: Standard full resolution convolution bloom.")
	TEXT(" 1: Half-resolution convolution that excludes the center of the kernel.\n")
	TEXT(" 2: Quarter-resolution convolution that excludes the center of the kernel.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static bool DoesPlatformSupportFFTBloom(EShaderPlatform Platform)
{
	return FDataDrivenShaderPlatformInfo::GetSupportsFFTBloom(Platform);
}

class FFFTBloomShader : public FGlobalShader
{
public:
	// Determine the number of threads used per scanline when writing the physical space kernel
	static const uint32 ThreadsPerGroup = 32;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportFFTBloom(Parameters.Platform);
	}

	FFFTBloomShader() = default;
	FFFTBloomShader(const CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FResizeAndCenterTextureCS : public FFFTBloomShader
{
public:
	DECLARE_GLOBAL_SHADER(FResizeAndCenterTextureCS);
	SHADER_USE_PARAMETER_STRUCT(FResizeAndCenterTextureCS, FFFTBloomShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, DstExtent)
		SHADER_PARAMETER(FIntPoint, ImageExtent)
		SHADER_PARAMETER(FVector4, KernelCenterAndScale)
		SHADER_PARAMETER(FIntPoint, DstBufferExtent)

		SHADER_PARAMETER_TEXTURE(Texture2D, SrcTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SrcSampler)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DstTexture)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FFFTBloomShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INCLUDE_RESIZE_AND_CENTER"), 1);
		OutEnvironment.SetDefine(TEXT("THREADS_PER_GROUP"), ThreadsPerGroup);
	}
};

class FCaptureKernelWeightsCS : public FFFTBloomShader
{
public:
	DECLARE_GLOBAL_SHADER(FCaptureKernelWeightsCS);
	SHADER_USE_PARAMETER_STRUCT(FCaptureKernelWeightsCS, FFFTBloomShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, HalfResSumLocation)
		SHADER_PARAMETER(FVector2D, UVCenter)

		SHADER_PARAMETER_SAMPLER(SamplerState, PhysicalSrcSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HalfResSrcTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, PhysicalSrcTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DstTexture)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FFFTBloomShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INCLUDE_CAPTURE_KERNEL_WEIGHTS"), 1);
	}
};

class FBlendLowResCS : public FFFTBloomShader
{
public:
	DECLARE_GLOBAL_SHADER(FBlendLowResCS);
	SHADER_USE_PARAMETER_STRUCT(FBlendLowResCS, FFFTBloomShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntRect, DstRect)
		SHADER_PARAMETER(FIntRect, HalfRect)
		SHADER_PARAMETER(FIntPoint, HalfBufferSize)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SrcTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HalfResSrcTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HalfResSrcSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CenterWeightTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DstTexture)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FFFTBloomShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INCLUDE_BLEND_LOW_RES"), 1);
		OutEnvironment.SetDefine(TEXT("THREADS_PER_GROUP"), ThreadsPerGroup);
	}
};

IMPLEMENT_GLOBAL_SHADER(FResizeAndCenterTextureCS, "/Engine/Private/PostProcessFFTBloom.usf", "ResizeAndCenterTextureCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FCaptureKernelWeightsCS,   "/Engine/Private/PostProcessFFTBloom.usf", "CaptureKernelWeightsCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FBlendLowResCS,            "/Engine/Private/PostProcessFFTBloom.usf", "BlendLowResCS", SF_Compute);

} //! namespace

bool IsFFTBloomHalfResolutionEnabled()
{
	return CVarHalfResFFTBloom.GetValueOnRenderThread() != 0;
}

bool IsFFTBloomQuarterResolutionEnabled()
{
	return CVarHalfResFFTBloom.GetValueOnRenderThread() == 2;
}

bool IsFFTBloomEnabled(const FViewInfo& View)
{
	const bool bOldMetalNoFFT = IsMetalPlatform(View.GetShaderPlatform()) && (RHIGetShaderLanguageVersion(View.GetShaderPlatform()) < 4) && IsPCPlatform(View.GetShaderPlatform());
	const bool bUseFFTBloom = View.FinalPostProcessSettings.BloomMethod == EBloomMethod::BM_FFT && View.ViewState != nullptr && DoesPlatformSupportFFTBloom(View.GetShaderPlatform());

	static bool bWarnAboutOldMetalFFTOnce = false;

	if (bOldMetalNoFFT && bUseFFTBloom && !bWarnAboutOldMetalFFTOnce)
	{
		UE_LOG(LogRenderer, Error, TEXT("FFT Bloom is only supported on Metal 2.1 and later."));
		bWarnAboutOldMetalFFTOnce = true;
	}

	if (bUseFFTBloom && !bOldMetalNoFFT)
	{
		return View.FFTBloomKernelTexture != nullptr;
	}
	else
	{
		return false;
	}
}

/**
* Used to resample the physical space kernel into the correct sized buffer with the 
* correct periodicity and center.
*
* Resizes the image, moves the center to to 0,0 and applies periodicity 
* across the full TargetSize (periods TargetSize.x & TargetSize.y)
*
* @param SrcTexture    - SRV for the physical space kernel supplied by user
* @param SrcImageSize  - The extent of the src image
* @param SrcImageCenterUV - The location of the center in src image (e.g. where the kernel center really is).
* @param ResizeScale    - Affective size of the physical space kernel in units of the ImageExtent.x 
* @param TargetSize    - Size of the image produced. 
* @param DstUAV        - Holds the result
* @param DstBufferSize - Size of DstBuffer
* @param bForceCenterZero -  is true only for the experimental 1/2 res version, part of conserving energy
*/
void ResizeAndCenterTexture(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FTextureRHIRef& SrcTexture,
	const FIntPoint& SrcImageSize,
	const FVector2D& SrcImageCenterUV,
	const float ResizeScale,
	const FIntPoint& TargetSize,
	FRDGTextureRef DstTexture,
	const FIntPoint& DstBufferSize,
	const bool bForceCenterZero)
{
	// Clamp the image center
	FVector2D ClampedImageCenterUV = SrcImageCenterUV;
	ClampedImageCenterUV.X = FMath::Clamp(SrcImageCenterUV.X, 0.f, 1.f);
	ClampedImageCenterUV.Y = FMath::Clamp(SrcImageCenterUV.Y, 0.f, 1.f);

	check(DstTexture);

	float CenterScale = bForceCenterZero ? 0.f : 1.f;
	const FLinearColor KernelCenterAndScale(ClampedImageCenterUV.X, ClampedImageCenterUV.Y, ResizeScale, CenterScale);

	FResizeAndCenterTextureCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FResizeAndCenterTextureCS::FParameters>();
	PassParameters->DstExtent = TargetSize;
	PassParameters->ImageExtent = SrcImageSize;
	PassParameters->KernelCenterAndScale = KernelCenterAndScale;
	PassParameters->DstBufferExtent = DstBufferSize;

	PassParameters->SrcTexture = SrcTexture;
	PassParameters->SrcSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	PassParameters->DstTexture = GraphBuilder.CreateUAV(DstTexture);

	// Use multiple threads per scan line to insure memory coalescing during the write
	const int32 ThreadsPerGroup = FResizeAndCenterTextureCS::ThreadsPerGroup;
	const int32 ThreadsGroupsPerScanLine = (DstBufferSize.X % ThreadsPerGroup == 0) ? DstBufferSize.X / ThreadsPerGroup : DstBufferSize.X / ThreadsPerGroup + 1;
	
	TShaderMapRef<FResizeAndCenterTextureCS> ComputeShader(View.ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("FFTBloom PreProcessKernel %dx%d", TargetSize.X, TargetSize.Y),
		ComputeShader,
		PassParameters, FIntVector(ThreadsGroupsPerScanLine, DstBufferSize.Y, 1));
}

FRDGTextureRef CaptureKernelWeight(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef HalfResKernel,
	const FIntPoint& HalfResSumLocation,
	const FTextureRHIRef& PhysicalKernel,
	const FVector2D& CenterUV)
{
	FRDGTextureDesc CenterWeightDesc = FRDGTextureDesc::Create2D(
		FIntPoint(2, 1),
		GPUFFT::PixelFormat(),
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);

	// Resize the buffer to hold the transformed kernel
	FRDGTextureRef CenterWeightTexture = GraphBuilder.CreateTexture(CenterWeightDesc, TEXT("Bloom.FFT.KernelCenterWeight"));

	FCaptureKernelWeightsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCaptureKernelWeightsCS::FParameters>();
	PassParameters->HalfResSumLocation = HalfResSumLocation;
	PassParameters->UVCenter = CenterUV;
	PassParameters->PhysicalSrcSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	PassParameters->HalfResSrcTexture = HalfResKernel;
	PassParameters->PhysicalSrcTexture = PhysicalKernel;
	PassParameters->DstTexture = GraphBuilder.CreateUAV(CenterWeightTexture);

	TShaderMapRef<FCaptureKernelWeightsCS> ComputeShader(View.ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("FFTBloom CaptureKernelWeights"),
		ComputeShader,
		PassParameters, 
		FIntVector(1, 1, 1));

	return CenterWeightTexture;
}

/**
* Used by energy conserving 1/2 resolution version of the bloom.
* This blends the results of the low resolution bloom with the full resolution image 
* in an energy conserving manner.  Assumes the 1/2-res bloom is done with a kernel that
* is missing the center pixel (i.e. the self-gather contribution), and this missing contribution
* is supplied by the full-res image.
*
* @param Context  - container for RHI and ShaderMap
* @param FullResImage - Unbloomed full-resolution source image
* @param FullResImageRect      - Region in FullResImage and DstUAV where the image lives
* @param HaflResConvolvedImage - A 1/2 res image that has been convolved with the bloom kernel minus center.
* @param HalfResRect           - Location of image in the HalfResConvolvedImage buffer
* @param HalfBufferSize        - Full size of the 1/2 Res buffer.
* @param CenterWeightTexture   - Texture that holds the weight between the kernel center and sum of 1/2res kernel weights.
*                                 needed to correctly composite the 1/2 res bloomed result with the full-res image.
* @param DstUAV                - Destination buffer that will hold the result.
*/
void BlendLowRes(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef FullResImage,
	const FIntRect& FullResImageRect,
	FRDGTextureRef HalfResConvolvedImage,
	const FIntRect& HalfResRect,
	const FIntPoint& HalfBufferSize,
	FRDGTextureRef CenterWeightTexutre,
	FRDGTextureRef DstTexture)
{
	FBlendLowResCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBlendLowResCS::FParameters>();
	PassParameters->DstRect = FullResImageRect;
	PassParameters->HalfRect = HalfResRect;
	PassParameters->HalfBufferSize = HalfBufferSize;

	PassParameters->SrcTexture = FullResImage;
	PassParameters->HalfResSrcTexture = HalfResConvolvedImage;
	PassParameters->HalfResSrcSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	PassParameters->CenterWeightTexture = CenterWeightTexutre;

	PassParameters->DstTexture = GraphBuilder.CreateUAV(DstTexture);

	TShaderMapRef<FBlendLowResCS> ComputeShader(View.ShaderMap);

	const FIntPoint TargetSize = FullResImageRect.Size();

	// Use multiple threads per scan line to insure memory coalescing during the write
	const int32 ThreadsPerGroup = ComputeShader->ThreadsPerGroup;
	const int32 ThreadsGroupsPerScanLine = (TargetSize.X % ThreadsPerGroup == 0) ? TargetSize.X / ThreadsPerGroup : TargetSize.X / ThreadsPerGroup + 1;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("FFTBloom Upscale&Blend %dx%d -> %dx%d",
			HalfResRect.Width(),
			HalfResRect.Height(),
			FullResImageRect.Width(),
			FullResImageRect.Height()),
		ComputeShader,
		PassParameters,
		FIntVector(ThreadsGroupsPerScanLine, TargetSize.Y, 1));
}

FRDGTextureRef TransformKernelFFT(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef ResizedKernel,
	bool bDoHorizontalFirst,
	FIntPoint FrequencySize)
{
	// Our frequency storage layout adds two elements to the first transform direction. 
	const FIntPoint FrequencyPadding = (bDoHorizontalFirst) ? FIntPoint(2, 0) : FIntPoint(0, 2);
	const FIntPoint PaddedFrequencySize = FrequencySize + FrequencyPadding;

	// Should read / write to PF_G16R16F or PF_G32R32F (float2 formats)
	// Need to set the render target description before we "request surface"
	FRDGTextureRef SpectralKernel;
	{

		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			PaddedFrequencySize,
			GPUFFT::PixelFormat(),
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		SpectralKernel = GraphBuilder.CreateTexture(Desc, TEXT("Bloom.FFT.SpectralKernel"));
	}

	FIntRect SrcRect(FIntPoint(0, 0), FrequencySize);
	GPUFFT::FFTImage2D(
		GraphBuilder,
		View.ShaderMap,
		FrequencySize, bDoHorizontalFirst,
		ResizedKernel, SrcRect,
		SpectralKernel);

	return SpectralKernel;
}

struct FFFTBloomIntermediates
{
	FRDGTextureRef InputTexture;

	// The size of the input buffer.
	FIntPoint InputBufferSize;

	// The sub-domain of the Input/Output buffers 
	// where the image lives, i.e. the region of interest
	FIntRect  ImageRect;

	// Image space, padded by black for kernel and  rounded up to powers of two
	// this defines the size of the FFT in each direction.
	FIntPoint FrequencySize;

	FVector PreFilter;

	float KernelSupportScale = 0.0f;
	float KernelSupportScaleClamp = 0.0f;

	// The order of the two-dimensional transform.  This implicitly defines
	// the data layout in transform space for both the kernel and image transform
	bool bDoHorizontalFirst = false;
};

FFFTBloomIntermediates GetFFTBloomIntermediates(
	const FViewInfo& View,
	const FFFTBloomInputs& Inputs)
{
	FSceneViewState* ViewState = (FSceneViewState*)View.State;
	check(ViewState);

	const auto& PPSettings = View.FinalPostProcessSettings;

	// The kernel parameters on the FinalPostProcess.

	const float BloomConvolutionSize = PPSettings.BloomConvolutionSize;
	const float KernelSupportScaleClamp = FMath::Clamp(PPSettings.BloomConvolutionBufferScale, 0.f, 1.f);

	// Clip the Kernel support (i.e. bloom size) to 100% the screen width 
	const float MaxBloomSize = 1.f;
	const float KernelSupportScale = FMath::Clamp(BloomConvolutionSize, 0.f, MaxBloomSize);

	// We padd by 1/2 the number of pixels the kernel needs in the x-direction
	// so if the kernel is being applied on the edge of the image it will see padding and not periodicity
	// NB:  If the kernel padding would force a transform buffer that is too big for group shared memory (> 4096)
	//      we clamp it.  This could result in a wrap-around in the bloom (from one side of the screen to the other),
	//      but since the amplitude of the bloom kernel tails is usually very small, this shouldn't be too bad.
	auto KernelRadiusSupportFunctor = [KernelSupportScale, KernelSupportScaleClamp](const FIntPoint& Size) ->int32
	{
		float ClampedKernelSupportScale = (KernelSupportScaleClamp > 0) ? FMath::Min(KernelSupportScale, KernelSupportScaleClamp) : KernelSupportScale;
		int32 FilterRadius = FMath::CeilToInt(0.5 * ClampedKernelSupportScale * Size.X);
		const int32 MaxFFTSize = GPUFFT::MaxScanLineLength();
		int32 MaxDim = FMath::Max(Size.X, Size.Y);
		if (MaxDim + FilterRadius > MaxFFTSize && MaxDim < MaxFFTSize) FilterRadius = MaxFFTSize - MaxDim;

		return FilterRadius;
	};

	const bool bHalfResolutionFFT = IsFFTBloomHalfResolutionEnabled();

	FFFTBloomIntermediates Intermediates;
	if (bHalfResolutionFFT)
	{
		Intermediates.InputTexture = Inputs.HalfResolutionTexture;
		Intermediates.InputBufferSize = Inputs.HalfResolutionViewRect.Size();
		Intermediates.ImageRect = Inputs.HalfResolutionViewRect;
	}
	else
	{
		Intermediates.InputTexture = Inputs.FullResolutionTexture;
		Intermediates.InputBufferSize = Inputs.FullResolutionViewRect.Size();
		Intermediates.ImageRect = Inputs.FullResolutionViewRect;
	}

	Intermediates.KernelSupportScale = KernelSupportScale;
	Intermediates.KernelSupportScaleClamp = KernelSupportScaleClamp;

	// The pre-filter boost parameters for bright pixels. Because the Convolution PP work in pre-exposure space, the min and max needs adjustment.
	Intermediates.PreFilter = FVector(PPSettings.BloomConvolutionPreFilterMin, PPSettings.BloomConvolutionPreFilterMax, PPSettings.BloomConvolutionPreFilterMult);

	// Capture the region of interest
	const FIntPoint ImageSize = Intermediates.ImageRect.Size();

	// The length of the a side of the square kernel image in pixels

	const int32 KernelSize = FMath::CeilToInt(KernelSupportScale * FMath::Max(ImageSize.X, ImageSize.Y));

	const int32 SpectralPadding = KernelRadiusSupportFunctor(ImageSize);

	// The following are mathematically equivalent
	// 1) Horizontal FFT / Vertical FFT / Filter / Vertical InvFFT / Horizontal InvFFT
	// 2) Vertical FFT / Horizontal FFT / Filter / Horizontal InvFFT / Vertical InvFFT
	// but we choose the one with the smallest intermediate buffer size

	// The size of the input image plus padding that accounts for
	// the width of the kernel.  The ImageRect is virtually padded
	// with black to account for the gather action of the convolution.
	FIntPoint PaddedImageSize = ImageSize + FIntPoint(SpectralPadding, SpectralPadding);
	PaddedImageSize.X = FMath::Max(PaddedImageSize.X, KernelSize);
	PaddedImageSize.Y = FMath::Max(PaddedImageSize.Y, KernelSize);

	Intermediates.FrequencySize = FIntPoint(FMath::RoundUpToPowerOfTwo(PaddedImageSize.X), FMath::RoundUpToPowerOfTwo(PaddedImageSize.Y));

	// Choose to do to transform in the direction that results in writing the least amount of data to main memory.

	Intermediates.bDoHorizontalFirst = ((Intermediates.FrequencySize.Y * PaddedImageSize.X) > (Intermediates.FrequencySize.X * PaddedImageSize.Y));

	return Intermediates;
}

void InitDomainAndGetKernel(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FFFTBloomIntermediates& Intermediates,
	bool bForceCenterZero,
	FRDGTextureRef* OutCachedSpectralKernel,
	FRDGTextureRef* OutCenterWeightTexture)
{
	FSceneViewState* ViewState = View.ViewState;

	const auto& PPSettings = View.FinalPostProcessSettings;

	UTexture2D* BloomConvolutionTexture = PPSettings.BloomConvolutionTexture;
	const FTexture2DResource* BloomConvolutionTextureResource = View.FFTBloomKernelTexture;

	// Fall back to the default bloom texture if provided.
	if (BloomConvolutionTexture == nullptr)
	{
		BloomConvolutionTexture = GEngine->DefaultBloomKernelTexture;
	}

	// This should exist if we called IsFFTBloomPhysicalKernelReady.
	check(BloomConvolutionTexture && BloomConvolutionTextureResource && BloomConvolutionTextureResource->TextureRHI);

	const float BloomConvolutionSize = PPSettings.BloomConvolutionSize;
	const FVector2D CenterUV = PPSettings.BloomConvolutionCenterUV;

	// Our frequency storage layout adds two elements to the first transform direction. 
	const FIntPoint FrequencyPadding = (Intermediates.bDoHorizontalFirst) ? FIntPoint(2, 0) : FIntPoint(0, 2);
	const FIntPoint PaddedFrequencySize = Intermediates.FrequencySize + FrequencyPadding;

	// Should read / write to PF_G16R16F or PF_G32R32F (float2 formats)
	// Need to set the render target description before we "request surface"
	const FRDGTextureDesc TransformDesc = FRDGTextureDesc::Create2D(
		PaddedFrequencySize,
		GPUFFT::PixelFormat(),
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef CachedSpectralKernel = nullptr;
	FRDGTextureRef CenterWeightTexture = nullptr;
	if (ViewState && ViewState->BloomFFTKernel.Spectral)
	{
		auto& FFTKernel = ViewState->BloomFFTKernel;

		FRDGTextureRef PrevCachedSpectralKernel = GraphBuilder.RegisterExternalTexture(FFTKernel.Spectral);

		const bool bSameTexture = (FFTKernel.Physical == static_cast<const UTexture2D*>(BloomConvolutionTexture));
		const bool bSameSpectralBuffer = TransformDesc.ClearValue == PrevCachedSpectralKernel->Desc.ClearValue
			&& TransformDesc.Flags == PrevCachedSpectralKernel->Desc.Flags
			&& TransformDesc.Format == PrevCachedSpectralKernel->Desc.Format
			&& TransformDesc.Extent == PrevCachedSpectralKernel->Desc.Extent;

		const bool bSameKernelSize = FMath::IsNearlyEqual(FFTKernel.Scale, BloomConvolutionSize, float(1.e-6) /*tol*/);
		const bool bSameImageSize = (Intermediates.ImageRect.Size() == FFTKernel.ImageSize);
		const bool bSameKernelCenterUV = FFTKernel.CenterUV.Equals(CenterUV, float(1.e-6) /*tol*/);
		const bool bSameMipLevel = bSameTexture && FFTKernel.PhysicalMipLevel == BloomConvolutionTextureResource->GetCurrentMipCount();

		if (bSameTexture && bSameSpectralBuffer && bSameKernelSize && bSameImageSize && bSameKernelCenterUV && bSameMipLevel)
		{
			CachedSpectralKernel = PrevCachedSpectralKernel;

			if (bForceCenterZero)
			{
				CenterWeightTexture = GraphBuilder.RegisterExternalTexture(FFTKernel.CenterWeight);
			}
		}
	}

	// Re-transform the kernel if needed.
	if (!CachedSpectralKernel)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "InitDomainAndGetKernel");

		// Sample the physical space kernel into the resized buffer
		const FTextureRHIRef& PhysicalSpaceKernelTextureRef = BloomConvolutionTextureResource->TextureRHI;

		// Rescale the physical space kernel ( and omit the center if this is a 1/2 resolution fft, it will be added later)
		FRDGTextureRef ResizedKernel = GraphBuilder.CreateTexture(TransformDesc, TEXT("Bloom.FFT.ResizedKernel"));
		ResizeAndCenterTexture(
			GraphBuilder,
			View, PhysicalSpaceKernelTextureRef, Intermediates.ImageRect.Size(), CenterUV, Intermediates.KernelSupportScale,
			Intermediates.FrequencySize, ResizedKernel, PaddedFrequencySize, bForceCenterZero);

		// Two Dimensional FFT of the physical space kernel.  
		CachedSpectralKernel = TransformKernelFFT(
			GraphBuilder, View,
			ResizedKernel,
			Intermediates.bDoHorizontalFirst,
			Intermediates.FrequencySize);

		if (bForceCenterZero)
		{
			// Capture the missing center weight from the kernel and the sum of the existing weights.
			CenterWeightTexture = CaptureKernelWeight(
				GraphBuilder,
				View,
				CachedSpectralKernel,
				PaddedFrequencySize,
				PhysicalSpaceKernelTextureRef,
				CenterUV);
		}

		// Update the data on the ViewState
		if (ViewState)
		{
			ViewState->BloomFFTKernel.Scale = BloomConvolutionSize;
			ViewState->BloomFFTKernel.ImageSize = Intermediates.ImageRect.Size();
			ViewState->BloomFFTKernel.Physical = BloomConvolutionTexture;
			ViewState->BloomFFTKernel.CenterUV = CenterUV;
			ViewState->BloomFFTKernel.PhysicalMipLevel = BloomConvolutionTextureResource->GetCurrentMipCount();
			{
				ViewState->BloomFFTKernel.Spectral.SafeRelease();
				GraphBuilder.QueueTextureExtraction(CachedSpectralKernel, &ViewState->BloomFFTKernel.Spectral);
			}
			{
				ViewState->BloomFFTKernel.CenterWeight.SafeRelease();
				if (CenterWeightTexture)
					GraphBuilder.QueueTextureExtraction(CenterWeightTexture, &ViewState->BloomFFTKernel.CenterWeight);
			}
		}
	}

	*OutCachedSpectralKernel = CachedSpectralKernel;
	*OutCenterWeightTexture = CenterWeightTexture;
}

FRDGTextureRef AddFFTBloomPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FFFTBloomInputs& Inputs)
{
	check(Inputs.FullResolutionTexture);
	check(!Inputs.FullResolutionViewRect.IsEmpty());
	check(Inputs.HalfResolutionTexture);
	check(!Inputs.HalfResolutionViewRect.IsEmpty());

	const bool bHalfResolutionFFT = IsFFTBloomHalfResolutionEnabled();

	FFFTBloomIntermediates Intermediates = GetFFTBloomIntermediates(View, Inputs);
	
	// Init the domain data update the cached kernel if needed.
	FRDGTextureRef SpectralKernelTexture;
	FRDGTextureRef CenterWeightTexture;
	InitDomainAndGetKernel(
		GraphBuilder, View, Intermediates,
		/* bForceCenterZero = */ bHalfResolutionFFT,
		/* out */ &SpectralKernelTexture,
		/* out */ &CenterWeightTexture);

	const FLinearColor Tint(1, 1, 1, 1);

	RDG_EVENT_SCOPE(GraphBuilder, "FFTBloom %dx%d", Intermediates.ImageRect.Width(), Intermediates.ImageRect.Height());

	if (bHalfResolutionFFT)
	{
		// Get a half-resolution destination buffer.
		TRefCountPtr<IPooledRenderTarget> HalfResConvolutionResult;

		FRDGTextureRef FFTConvolutionTexture;
		{
			const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				Intermediates.InputBufferSize,
				GPUFFT::PixelFormat(),
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			FFTConvolutionTexture = GraphBuilder.CreateTexture(Desc, TEXT("Bloom.FFT.SceneColor"));
		}

		GPUFFT::ConvolutionWithTextureImage2D(
			GraphBuilder,
			View.ShaderMap,
			Intermediates.FrequencySize,
			Intermediates.bDoHorizontalFirst,
			SpectralKernelTexture,
			Intermediates.InputTexture, Intermediates.ImageRect,
			FFTConvolutionTexture, Intermediates.ImageRect,
			Intermediates.PreFilter);

		// Blend with  alpha * SrcBuffer + betta * BloomedBuffer  where alpha = Weights[0], beta = Weights[1]
		const FIntPoint HalfResBufferSize = Intermediates.InputBufferSize;

		FRDGTextureRef OutputSceneColorTexture;
		{
			const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				Inputs.FullResolutionTexture->Desc.Extent,
				Inputs.FullResolutionTexture->Desc.Format,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			OutputSceneColorTexture = GraphBuilder.CreateTexture(Desc, TEXT("Bloom.FFT.SceneColor"));
		}

		BlendLowRes(
			GraphBuilder,
			View,
			Inputs.FullResolutionTexture, Inputs.FullResolutionViewRect,
			FFTConvolutionTexture, Intermediates.ImageRect,
			Intermediates.InputBufferSize,
			CenterWeightTexture,
			OutputSceneColorTexture);

		return OutputSceneColorTexture;
	}
	else
	{
		FRDGTextureDesc OutputSceneColorDesc = FRDGTextureDesc::Create2D(
			Inputs.FullResolutionTexture->Desc.Extent,
			Inputs.FullResolutionTexture->Desc.Format,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		
		FRDGTextureRef OutputSceneColorTexture = GraphBuilder.CreateTexture(OutputSceneColorDesc, TEXT("Bloom.FFT.SceneColor"));

		GPUFFT::ConvolutionWithTextureImage2D(
			GraphBuilder,
			View.ShaderMap,
			Intermediates.FrequencySize,
			Intermediates.bDoHorizontalFirst,
			SpectralKernelTexture,
			Intermediates.InputTexture, Intermediates.ImageRect,
			OutputSceneColorTexture, Intermediates.ImageRect,
			Intermediates.PreFilter);

		return OutputSceneColorTexture;
	}

	return Inputs.FullResolutionTexture;
}
