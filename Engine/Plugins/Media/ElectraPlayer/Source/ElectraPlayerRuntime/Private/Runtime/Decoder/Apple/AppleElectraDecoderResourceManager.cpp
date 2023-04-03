// Copyright Epic Games, Inc. All Rights Reserved.

#include "AppleElectraDecoderResourceManager.h"

#if PLATFORM_MAC || PLATFORM_IOS || PLATFORM_TVOS

#include "IElectraDecoderOutputVideo.h"
#include "ElectraDecodersUtils.h"
#include "Renderer/RendererBase.h"
#include "ElectraVideoDecoder_Apple.h"

#include "ElectraPlayerMisc.h"

#include <VideoToolbox/VideoToolbox.h>


namespace Electra
{

namespace AppleDecoderResources
{
	static constexpr uint32 Make4CC(const uint8 A, const uint8 B, const uint8 C, const uint8 D)
	{
		return (static_cast<uint32>(A) << 24) | (static_cast<uint32>(B) << 16) | (static_cast<uint32>(C) << 8) | static_cast<uint32>(D);
	}

	static bool IsHAP(uint32 Codec4CC)
	{
		return Codec4CC == AppleDecoderResources::Make4CC('H','a','p','1') ||
			   Codec4CC == AppleDecoderResources::Make4CC('H','a','p','5') ||
			   Codec4CC == AppleDecoderResources::Make4CC('H','a','p','Y') ||
			   Codec4CC == AppleDecoderResources::Make4CC('H','a','p','M') ||
			   Codec4CC == AppleDecoderResources::Make4CC('H','a','p','A') ||
			   Codec4CC == AppleDecoderResources::Make4CC('H','a','p','7') ||
			   Codec4CC == AppleDecoderResources::Make4CC('H','a','p','H');
	}

	static bool IsProRes(uint32 Codec4CC)
	{
		return Codec4CC == AppleDecoderResources::Make4CC('a','p','c','h') ||
			   Codec4CC == AppleDecoderResources::Make4CC('a','p','c','n') ||
			   Codec4CC == AppleDecoderResources::Make4CC('a','p','c','s') ||
			   Codec4CC == AppleDecoderResources::Make4CC('a','p','c','o') ||
			   Codec4CC == AppleDecoderResources::Make4CC('a','p','4','h') ||
			   Codec4CC == AppleDecoderResources::Make4CC('a','p','4','x');
	}

	static FElectraDecoderResourceManagerApple::FCallbacks Callbacks;
}


bool FElectraDecoderResourceManagerApple::Startup(const FCallbacks& InCallbacks)
{
	AppleDecoderResources::Callbacks = InCallbacks;
	return AppleDecoderResources::Callbacks.GetMetalDevice ? true : false;
}

void FElectraDecoderResourceManagerApple::Shutdown()
{
}

TSharedPtr<IElectraDecoderResourceDelegate, ESPMode::ThreadSafe> FElectraDecoderResourceManagerApple::GetDelegate()
{
	static TSharedPtr<IElectraDecoderResourceDelegate, ESPMode::ThreadSafe> Self = MakeShared<FElectraDecoderResourceManagerApple, ESPMode::ThreadSafe>();
	return Self;
}


FElectraDecoderResourceManagerApple::~FElectraDecoderResourceManagerApple()
{
}

class FElectraDecoderResourceManagerApple::FInstanceVars : public FElectraDecoderResourceManagerApple::IDecoderPlatformResource
{
public:
	bool (*SetupRenderBufferFromDecoderOutput)(IMediaRenderer::IBuffer* /*InOutBufferToSetup*/, TSharedPtr<FParamDict, ESPMode::ThreadSafe> /*InOutBufferPropertes*/, TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> /*InDecoderOutput*/, IDecoderPlatformResource* /*InPlatformSpecificResource*/) = nullptr;
	uint32 Codec4CC = 0;
};



IElectraDecoderResourceDelegateApple::IDecoderPlatformResource* FElectraDecoderResourceManagerApple::CreatePlatformResource(void* InOwnerHandle, EDecoderPlatformResourceType InDecoderResourceType, const TMap<FString, FVariant> InOptions)
{
	FInstanceVars* Vars = new FInstanceVars;

	/*
		We can't be as codec agnostic here as we would like to be.
		Image decoders return specific texture formats in a CPU buffer that one must be aware of.
	*/
	uint32 Codec4CC = (uint32)ElectraDecodersUtil::GetVariantValueSafeU64(InOptions, TEXT("codec_4cc"), 0);
	if (AppleDecoderResources::IsHAP(Codec4CC))
	{
		Vars->Codec4CC = Codec4CC;
		Vars->SetupRenderBufferFromDecoderOutput = FElectraDecoderResourceManagerApple::SetupRenderBufferFromDecoderOutput_Images;
	}
	else
	{
		Vars->SetupRenderBufferFromDecoderOutput = FElectraDecoderResourceManagerApple::SetupRenderBufferFromDecoderOutput_Default;
	}
	return Vars;
}

void FElectraDecoderResourceManagerApple::ReleasePlatformResource(void* InOwnerHandle, IDecoderPlatformResource* InHandleToDestroy)
{
	FInstanceVars* Vars = static_cast<FInstanceVars*>(InHandleToDestroy);
	if (Vars)
	{
		delete Vars;
	}
}


bool FElectraDecoderResourceManagerApple::GetMetalDevice(void **OutMetalDevice)
{
	check(OutMetalDevice);
	if (AppleDecoderResources::Callbacks.GetMetalDevice)
	{
		return AppleDecoderResources::Callbacks.GetMetalDevice(OutMetalDevice, AppleDecoderResources::Callbacks.UserValue);
	}
	return false;
}


bool FElectraDecoderResourceManagerApple::SetupRenderBufferFromDecoderOutput(IMediaRenderer::IBuffer* InOutBufferToSetup, TSharedPtr<FParamDict, ESPMode::ThreadSafe> InOutBufferPropertes, TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> InDecoderOutput, IDecoderPlatformResource* InPlatformSpecificResource)
{
	check(InOutBufferToSetup);
	check(InOutBufferPropertes.IsValid());
	check(InDecoderOutput.IsValid());

	if (!InPlatformSpecificResource)
	{
		return SetupRenderBufferFromDecoderOutput_Default(InOutBufferToSetup, InOutBufferPropertes, InDecoderOutput, InPlatformSpecificResource);
	}
	else
	{
		FInstanceVars* Vars = static_cast<FInstanceVars*>(InPlatformSpecificResource);
		return Vars->SetupRenderBufferFromDecoderOutput(InOutBufferToSetup, InOutBufferPropertes, InDecoderOutput, InPlatformSpecificResource);
	}
}

bool FElectraDecoderResourceManagerApple::SetupRenderBufferFromDecoderOutput_Default(IMediaRenderer::IBuffer* InOutBufferToSetup, TSharedPtr<FParamDict, ESPMode::ThreadSafe> InOutBufferPropertes, TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> InDecoderOutput, IDecoderPlatformResource* InPlatformSpecificResource)
{
	TSharedPtr<FElectraPlayerVideoDecoderOutputApple, ESPMode::ThreadSafe> DecoderOutput = InOutBufferToSetup->GetBufferProperties().GetValue("texture").GetSharedPointer<FElectraPlayerVideoDecoderOutputApple>();
	if (DecoderOutput.IsValid())
	{
		CVImageBufferRef ImageBuffer = reinterpret_cast<CVImageBufferRef>(InDecoderOutput->GetPlatformOutputHandle(EElectraDecoderPlatformOutputHandleType::ImageBufferRef));
		if (ImageBuffer)
		{
			FElectraVideoDecoderOutputCropValues Crop = InDecoderOutput->GetCropValues();
			InOutBufferPropertes->Set(TEXT("width"), FVariantValue((int64) InDecoderOutput->GetWidth()));
			InOutBufferPropertes->Set(TEXT("height"), FVariantValue((int64) InDecoderOutput->GetHeight()));
			InOutBufferPropertes->Set(TEXT("pitch"), FVariantValue((int64) InDecoderOutput->GetFrameWidth()));
			InOutBufferPropertes->Set(TEXT("crop_left"), FVariantValue((int64) Crop.Left));
			InOutBufferPropertes->Set(TEXT("crop_right"), FVariantValue((int64) Crop.Right));
			InOutBufferPropertes->Set(TEXT("crop_top"), FVariantValue((int64) Crop.Top));
			InOutBufferPropertes->Set(TEXT("crop_bottom"), FVariantValue((int64) Crop.Bottom));
			InOutBufferPropertes->Set(TEXT("aspect_ratio"), FVariantValue((double) InDecoderOutput->GetAspectRatioW() / (double) InDecoderOutput->GetAspectRatioH()));
			InOutBufferPropertes->Set(TEXT("aspect_w"), FVariantValue((int64) InDecoderOutput->GetAspectRatioW()));
			InOutBufferPropertes->Set(TEXT("aspect_h"), FVariantValue((int64) InDecoderOutput->GetAspectRatioH()));
			InOutBufferPropertes->Set(TEXT("fps_num"), FVariantValue((int64) InDecoderOutput->GetFrameRateNumerator()));
			InOutBufferPropertes->Set(TEXT("fps_denom"), FVariantValue((int64) InDecoderOutput->GetFrameRateDenominator()));
			int32 PixelFormat =	InDecoderOutput->GetPixelFormat();
			if (PixelFormat == kCVPixelFormatType_4444AYpCbCr16)
			{
				InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)PF_R16G16B16A16_UNORM));
			}
			else if ((PixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) || (PixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange))
			{
				InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)PF_NV12));
			}
			else
			{
				InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)PF_P010));
			}
			InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64) InDecoderOutput->GetNumberOfBits()));
#if 0
			// What type of decoder output do we have here?
			TMap<FString, FVariant> ExtraValues;
			InDecoderOutput->GetExtraValues(ExtraValues);
			if (ElectraDecodersUtil::GetVariantValueFString(ExtraValues, TEXT("codec")).Equals(TEXT("prores")))
			{
				uint32 Codec4CC = (uint32)ElectraDecodersUtil::GetVariantValueSafeI64(ExtraValues, TEXT("codec_4cc"), 0);
				// ...
			}
#endif
			DecoderOutput->Initialize(ImageBuffer, InOutBufferPropertes);
			return true;
		}
	}
	return false;
}


bool FElectraDecoderResourceManagerApple::SetupRenderBufferFromDecoderOutput_Images(IMediaRenderer::IBuffer* InOutBufferToSetup, TSharedPtr<FParamDict, ESPMode::ThreadSafe> InOutBufferPropertes, TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> InDecoderOutput, IDecoderPlatformResource* InPlatformSpecificResource)
{
	TSharedPtr<FElectraPlayerVideoDecoderOutputApple, ESPMode::ThreadSafe> DecoderOutput = InOutBufferToSetup->GetBufferProperties().GetValue("texture").GetSharedPointer<FElectraPlayerVideoDecoderOutputApple>();
	IElectraDecoderVideoOutputImageBuffers* ImageBuffers = reinterpret_cast<IElectraDecoderVideoOutputImageBuffers*>(InDecoderOutput->GetPlatformOutputHandle(EElectraDecoderPlatformOutputHandleType::ImageBuffers));
	FInstanceVars* Vars = static_cast<FInstanceVars*>(InPlatformSpecificResource);
	if (DecoderOutput.IsValid() && Vars)
	{
		FElectraVideoDecoderOutputCropValues Crop = InDecoderOutput->GetCropValues();
		InOutBufferPropertes->Set(TEXT("width"), FVariantValue((int64) InDecoderOutput->GetWidth()));
		InOutBufferPropertes->Set(TEXT("height"), FVariantValue((int64) InDecoderOutput->GetHeight()));
		InOutBufferPropertes->Set(TEXT("pitch"), FVariantValue((int64) InDecoderOutput->GetFrameWidth()));
		InOutBufferPropertes->Set(TEXT("crop_left"), FVariantValue((int64) Crop.Left));
		InOutBufferPropertes->Set(TEXT("crop_right"), FVariantValue((int64) Crop.Right));
		InOutBufferPropertes->Set(TEXT("crop_top"), FVariantValue((int64) Crop.Top));
		InOutBufferPropertes->Set(TEXT("crop_bottom"), FVariantValue((int64) Crop.Bottom));
		InOutBufferPropertes->Set(TEXT("aspect_ratio"), FVariantValue((double) InDecoderOutput->GetAspectRatioW() / (double) InDecoderOutput->GetAspectRatioH()));
		InOutBufferPropertes->Set(TEXT("aspect_w"), FVariantValue((int64) InDecoderOutput->GetAspectRatioW()));
		InOutBufferPropertes->Set(TEXT("aspect_h"), FVariantValue((int64) InDecoderOutput->GetAspectRatioH()));
		InOutBufferPropertes->Set(TEXT("fps_num"), FVariantValue((int64) InDecoderOutput->GetFrameRateNumerator()));
		InOutBufferPropertes->Set(TEXT("fps_denom"), FVariantValue((int64) InDecoderOutput->GetFrameRateDenominator()));

		int32 Width = InDecoderOutput->GetDecodedWidth();
		int32 Height = InDecoderOutput->GetDecodedHeight();
		int32 Pitch = InDecoderOutput->GetFrameWidth();

		if (AppleDecoderResources::IsHAP(Vars->Codec4CC))
		{
			if (!ImageBuffers)
			{
				UE_LOG(LogElectraPlayer, Log, TEXT("Did not receive image buffers for HAP output"));
				return false;
			}

			// One or two buffers (color or color+alpha)
			int32 NumImageBuffers = ImageBuffers->GetNumberOfBuffers();
			check(NumImageBuffers == 1 || NumImageBuffers == 2);

			uint32 ColorBufferFormat = (uint32) ImageBuffers->GetBufferFormatByIndex(0);
			switch(ColorBufferFormat)
			{
				case 0x01:		// HapTextureFormat_YCoCg_DXT5
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_DXT5));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64) 8));
					InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::YCoCg));
					Pitch = ((Pitch + 3) / 4) * 16;		// 4 pixel wide blocks with 16 bytes
					break;
				}
				case 0x83F0:	// HapTextureFormat_RGB_DXT1
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_DXT1));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64) 8));
					Pitch = ((Pitch + 3) / 4) * 8;		// 4 pixel wide blocks with 8 bytes
					break;
				}
				case 0x83F3:	// HapTextureFormat_RGBA_DXT5
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_DXT5));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64) 8));
					Pitch = ((Pitch + 3) / 4) * 16;		// 4 pixel wide blocks with 16 bytes
					break;
				}
				case 0x8DBB:	// HapTextureFormat_A_RGTC1
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_BC4));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64) 8));
					Pitch = ((Pitch + 3) / 4) * 8;		// 4 pixel wide blocks with 8 bytes
					break;
				}
				case 0x8E8C:	// HapTextureFormat_RGBA_BPTC_UNORM
				case 0x8E8F:	// HapTextureFormat_RGB_BPTC_UNSIGNED_FLOAT
				case 0x8E8E:	// HapTextureFormat_RGB_BPTC_SIGNED_FLOAT
				default:
				{
					UE_LOG(LogElectraPlayer, Log, TEXT("Unsupported HAP texture format 0x%04x"), ColorBufferFormat);
					return false;
				}
			}

			TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> ColorBuffer = ImageBuffers->GetBufferByIndex(0);
			if (ColorBuffer.IsValid() && ColorBuffer->Num())
			{
				DecoderOutput->InitializeWithBuffer(ColorBuffer,
					Pitch,							// Buffer stride
					FIntPoint(Width, Height),		// Buffer dimensions
					InOutBufferPropertes);
				return true;
			}
		}
		else
		{
			UE_LOG(LogElectraPlayer, Log, TEXT("Unsupported decoded 4CC 0x%08x"), Vars->Codec4CC);
			return false;
		}
	}
	return false;
}

} // namespace Electra

#endif
