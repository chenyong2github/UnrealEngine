// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsElectraDecoderResourceManager.h"
#include "Templates/AlignmentTemplates.h"
#include "Misc/ScopeLock.h"

#include "IElectraDecoderOutputVideo.h"
#include "ElectraDecodersUtils.h"

#include "Renderer/RendererBase.h"
#include "ElectraVideoDecoder_PC.h"

#include "ElectraPlayerMisc.h"

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
namespace Electra
{

namespace WindowsDecoderResources
{
	static constexpr uint32 Make4CC(const uint8 A, const uint8 B, const uint8 C, const uint8 D)
	{
		return (static_cast<uint32>(A) << 24) | (static_cast<uint32>(B) << 16) | (static_cast<uint32>(C) << 8) | static_cast<uint32>(D);
	}

	static bool IsHAP(uint32 Codec4CC)
	{
		return Codec4CC == WindowsDecoderResources::Make4CC('H','a','p','1') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('H','a','p','5') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('H','a','p','Y') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('H','a','p','M') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('H','a','p','A') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('H','a','p','7') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('H','a','p','H');
	}

	static bool IsProRes(uint32 Codec4CC)
	{
		return Codec4CC == WindowsDecoderResources::Make4CC('a','p','c','h') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('a','p','c','n') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('a','p','c','s') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('a','p','c','o') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('a','p','4','h') ||
			   Codec4CC == WindowsDecoderResources::Make4CC('a','p','4','x');
	}

	static bool IsAvidDNxHD(uint32 Codec4CC)
	{
		return Codec4CC == WindowsDecoderResources::Make4CC('A', 'V', 'd', 'h');
	}

	static FElectraDecoderResourceManagerWindows::FCallbacks Callbacks;
	static bool bDidInitializeMF = false;
}

bool FElectraDecoderResourceManagerWindows::Startup(const FCallbacks& InCallbacks)
{
	// Windows 8 or better?
	if (!FPlatformMisc::VerifyWindowsVersion(6, 2))
	{
		UE_LOG(LogElectraPlayer, Log, TEXT("Electra is incompatible with Windows prior to 8.0 version: %s"), *FPlatformMisc::GetOSVersion());
		return false;
	}
	if (!(FPlatformProcess::GetDllHandle(TEXT("mf.dll"))
		&& FPlatformProcess::GetDllHandle(TEXT("mfplat.dll"))
		&& FPlatformProcess::GetDllHandle(TEXT("msmpeg2vdec.dll"))
		&& FPlatformProcess::GetDllHandle(TEXT("MSAudDecMFT.dll"))))
	{
		UE_LOG(LogElectraPlayer, Error, TEXT("Electra can't load Media Foundation, %s"), *FPlatformMisc::GetOSVersion());
		return false;
	}
	HRESULT Result = MFStartup(MF_VERSION);
	if (FAILED(Result))
	{
		UE_LOG(LogElectraPlayer, Error, TEXT("MFStartup failed with 0x%08x"), Result);
		return false;
	}
	WindowsDecoderResources::bDidInitializeMF = true;
	WindowsDecoderResources::Callbacks = InCallbacks;
	return WindowsDecoderResources::Callbacks.GetD3DDevice ? true : false;
}

void FElectraDecoderResourceManagerWindows::Shutdown()
{
	if (WindowsDecoderResources::bDidInitializeMF)
	{
		MFShutdown();
		WindowsDecoderResources::bDidInitializeMF = false;
	}
}

TSharedPtr<IElectraDecoderResourceDelegate, ESPMode::ThreadSafe> FElectraDecoderResourceManagerWindows::GetDelegate()
{
	static TSharedPtr<IElectraDecoderResourceDelegate, ESPMode::ThreadSafe> Self = MakeShared<FElectraDecoderResourceManagerWindows, ESPMode::ThreadSafe>();
	return Self;
}


FElectraDecoderResourceManagerWindows::~FElectraDecoderResourceManagerWindows()
{
}


class FElectraDecoderResourceManagerWindows::FInstanceVars : public IElectraDecoderResourceDelegateWindows::IDecoderPlatformResource
{
public:
	bool (*SetupRenderBufferFromDecoderOutput)(IMediaRenderer::IBuffer* /*InOutBufferToSetup*/, TSharedPtr<FParamDict, ESPMode::ThreadSafe> /*InOutBufferPropertes*/, TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> /*InDecoderOutput*/, IDecoderPlatformResource* /*InPlatformSpecificResource*/) = nullptr;
	uint32 Codec4CC = 0;
};



IElectraDecoderResourceDelegateWindows::IDecoderPlatformResource* FElectraDecoderResourceManagerWindows::CreatePlatformResource(void* InOwnerHandle, EDecoderPlatformResourceType InDecoderResourceType, const TMap<FString, FVariant> InOptions)
{
	FInstanceVars* Vars = new FInstanceVars;

	/*
		We can't be as codec agnostic here as we would like to be.
		Image decoders return specific texture formats in a CPU buffer that one must be aware of.
	*/
	uint32 Codec4CC = (uint32)ElectraDecodersUtil::GetVariantValueSafeU64(InOptions, TEXT("codec_4cc"), 0);
	if (WindowsDecoderResources::IsHAP(Codec4CC) || WindowsDecoderResources::IsProRes(Codec4CC) || WindowsDecoderResources::IsAvidDNxHD(Codec4CC))
	{
		Vars->Codec4CC = Codec4CC;
		Vars->SetupRenderBufferFromDecoderOutput = FElectraDecoderResourceManagerWindows::SetupRenderBufferFromDecoderOutput_Images;
	}
	else
	{
		Vars->SetupRenderBufferFromDecoderOutput = FElectraDecoderResourceManagerWindows::SetupRenderBufferFromDecoderOutput_Default;
	}
	return Vars;
}

void FElectraDecoderResourceManagerWindows::ReleasePlatformResource(void* InOwnerHandle, IDecoderPlatformResource* InHandleToDestroy)
{
	FInstanceVars* Vars = static_cast<FInstanceVars*>(InHandleToDestroy);
	if (Vars)
	{
		delete Vars;
	}
}


bool FElectraDecoderResourceManagerWindows::SetupRenderBufferFromDecoderOutput(IMediaRenderer::IBuffer* InOutBufferToSetup, TSharedPtr<FParamDict, ESPMode::ThreadSafe> InOutBufferPropertes, TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> InDecoderOutput, IDecoderPlatformResource* InPlatformSpecificResource)
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

bool FElectraDecoderResourceManagerWindows::GetD3DDevice(void **OutD3DDevice, int32* OutD3DVersionTimes1000)
{
	check(OutD3DDevice);
	check(OutD3DVersionTimes1000);
	if (WindowsDecoderResources::Callbacks.GetD3DDevice)
	{
		return WindowsDecoderResources::Callbacks.GetD3DDevice(OutD3DDevice, OutD3DVersionTimes1000, WindowsDecoderResources::Callbacks.UserValue);
	}
	return false;
}





bool FElectraDecoderResourceManagerWindows::SetupRenderBufferFromDecoderOutput_Default(IMediaRenderer::IBuffer* InOutBufferToSetup, TSharedPtr<FParamDict, ESPMode::ThreadSafe> InOutBufferPropertes, TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> InDecoderOutput, FElectraDecoderResourceManagerWindows::IDecoderPlatformResource* InPlatformSpecificResource)
{
	TSharedPtr<FElectraPlayerVideoDecoderOutputPC, ESPMode::ThreadSafe> DecoderOutput = InOutBufferToSetup->GetBufferProperties().GetValue("texture").GetSharedPointer<FElectraPlayerVideoDecoderOutputPC>();
	if (DecoderOutput.IsValid())
	{
		FElectraVideoDecoderOutputCropValues Crop = InDecoderOutput->GetCropValues();
		InOutBufferPropertes->Set(TEXT("width"), FVariantValue((int64)InDecoderOutput->GetWidth()));
		InOutBufferPropertes->Set(TEXT("height"), FVariantValue((int64)InDecoderOutput->GetHeight()));
		InOutBufferPropertes->Set(TEXT("pitch"), FVariantValue((int64)InDecoderOutput->GetFrameWidth()));
		InOutBufferPropertes->Set(TEXT("crop_left"), FVariantValue((int64)Crop.Left));
		InOutBufferPropertes->Set(TEXT("crop_right"), FVariantValue((int64)Crop.Right));
		InOutBufferPropertes->Set(TEXT("crop_top"), FVariantValue((int64)Crop.Top));
		InOutBufferPropertes->Set(TEXT("crop_bottom"), FVariantValue((int64)Crop.Bottom));
		InOutBufferPropertes->Set(TEXT("aspect_ratio"), FVariantValue((double)InDecoderOutput->GetAspectRatioW() / (double)InDecoderOutput->GetAspectRatioH()));
		InOutBufferPropertes->Set(TEXT("aspect_w"), FVariantValue((int64)InDecoderOutput->GetAspectRatioW()));
		InOutBufferPropertes->Set(TEXT("aspect_h"), FVariantValue((int64)InDecoderOutput->GetAspectRatioH()));
		InOutBufferPropertes->Set(TEXT("fps_num"), FVariantValue((int64)InDecoderOutput->GetFrameRateNumerator()));
		InOutBufferPropertes->Set(TEXT("fps_denom"), FVariantValue((int64)InDecoderOutput->GetFrameRateDenominator()));
		InOutBufferPropertes->Set(TEXT("pixelfmt"), InDecoderOutput->GetPixelFormat() == 0 ? FVariantValue((int64)EPixelFormat::PF_NV12) : FVariantValue((int64)EPixelFormat::PF_P010));
		InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64)InDecoderOutput->GetNumberOfBits()));

		// What type of decoder output do we have here?
		TMap<FString, FVariant> ExtraValues;
		InDecoderOutput->GetExtraValues(ExtraValues);

		// Presently we handle only decoder output from a DirectX decoder transform.
		check(ElectraDecodersUtil::GetVariantValueFString(ExtraValues, TEXT("platform")).Equals(TEXT("dx")));

		int32 DXVersion = (int32)ElectraDecodersUtil::GetVariantValueSafeI64(ExtraValues, TEXT("dxversion"), 0);
		bool bIsSW = !!ElectraDecodersUtil::GetVariantValueSafeI64(ExtraValues, TEXT("sw"), 0);

		HRESULT Result;
		// DX12 or non-DX, but HW accelerated?
		if ((DXVersion == 0 || DXVersion >= 12000) && !bIsSW)
		{
			TRefCountPtr<IMFSample> DecodedOutputSample = reinterpret_cast<IMFSample*>(InDecoderOutput->GetPlatformOutputHandle(EElectraDecoderPlatformOutputHandleType::MFSample));

			DWORD BuffersNum = 0;
			if ((Result = DecodedOutputSample->GetBufferCount(&BuffersNum)) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFSample::GetBufferCount() failed with %08x"), Result);
				return false;
			}
			if (BuffersNum != 1)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFSample::GetBufferCount() returned %u buffers instead of 1"), Result);
				return false;
			}

			TRefCountPtr<IMFMediaBuffer> Buffer;
			if ((Result = DecodedOutputSample->GetBufferByIndex(0, Buffer.GetInitReference())) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFSample::GetBufferByIndex() failed with %08x"), Result);
				return false;
			}

			TRefCountPtr<IMFDXGIBuffer> DXGIBuffer;
			if ((Result = Buffer->QueryInterface(__uuidof(IMFDXGIBuffer), (void**)DXGIBuffer.GetInitReference())) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFMediaBuffer::QueryInterface(IMFDXGIBuffer) failed with %08x"), Result);
				return false;
			}

			TRefCountPtr<ID3D11Texture2D> Texture2D;
			if ((Result = DXGIBuffer->GetResource(IID_PPV_ARGS(Texture2D.GetInitReference()))) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFDXGIBuffer::GetResource(ID3D11Texture2D) failed with %08x"), Result);
				return false;
			}
			D3D11_TEXTURE2D_DESC TextureDesc;
			Texture2D->GetDesc(&TextureDesc);
			if (TextureDesc.Format != DXGI_FORMAT_NV12 && TextureDesc.Format != DXGI_FORMAT_P010)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("ID3D11Texture2D::GetDesc() did not return NV12 or P010 format as expected"));
				return false;
			}

			TRefCountPtr<IMF2DBuffer> Buffer2D;
			if ((Result = Buffer->QueryInterface(__uuidof(IMF2DBuffer), (void**)Buffer2D.GetInitReference())) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFDXGIBuffer::QueryInterface(IMF2DBuffer) failed with %08x"), Result);
				return false;
			}

			uint8* Data = nullptr;
			LONG Pitch = 0;
			if ((Result = Buffer2D->Lock2D(&Data, &Pitch)) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMF2DBuffer::Lock2D() failed with %08x"), Result);
				return false;
			}

			DecoderOutput->InitializeWithBuffer(Data, Pitch * (TextureDesc.Height * 3 / 2),
				Pitch,															// Buffer stride
				FIntPoint(TextureDesc.Width, TextureDesc.Height * 3 / 2),		// Buffer dimensions
				InOutBufferPropertes);

			if ((Result = Buffer2D->Unlock2D()) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMF2DBuffer::Unlock2D() failed with %08x"), Result);
				return false;
			}
			return true;
		}
		if (DXVersion >= 11000 && !bIsSW)
		{
			TRefCountPtr<IMFSample> DecodedOutputSample = reinterpret_cast<IMFSample*>(InDecoderOutput->GetPlatformOutputHandle(EElectraDecoderPlatformOutputHandleType::MFSample));

			TRefCountPtr<ID3D11Device> DxDevice = reinterpret_cast<ID3D11Device*>(InDecoderOutput->GetPlatformOutputHandle(EElectraDecoderPlatformOutputHandleType::DXDevice));
			TRefCountPtr<ID3D11DeviceContext> DxDeviceContext = reinterpret_cast<ID3D11DeviceContext*>(InDecoderOutput->GetPlatformOutputHandle(EElectraDecoderPlatformOutputHandleType::DXDeviceContext));

			DWORD BuffersNum = 0;
			if ((Result = DecodedOutputSample->GetBufferCount(&BuffersNum)) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFSample::GetBufferCount() failed with %08x"), Result);
				return false;
			}
			if (BuffersNum != 1)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFSample::GetBufferCount() returned %u buffers instead of 1"), Result);
				return false;
			}

			int32 Width = InDecoderOutput->GetWidth();
			int32 Height = InDecoderOutput->GetHeight();
			DecoderOutput->InitializeWithSharedTexture(DxDevice, DxDeviceContext, DecodedOutputSample, FIntPoint(Width, Height), InOutBufferPropertes);
			if (!DecoderOutput->GetTexture())
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("ID3D11Device::CreateTexture2D() failed!"));
				return false;
			}
			return true;
		}
		else if (bIsSW)
		{
			TRefCountPtr<IMFSample> DecodedOutputSample = reinterpret_cast<IMFSample*>(InDecoderOutput->GetPlatformOutputHandle(EElectraDecoderPlatformOutputHandleType::MFSample));

			DWORD BuffersNum = 0;
			if ((Result = DecodedOutputSample->GetBufferCount(&BuffersNum)) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFSample::GetBufferCount() failed with %08x"), Result);
				return false;
			}
			if (BuffersNum != 1)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFSample::GetBufferCount() returned %u buffers instead of 1"), Result);
				return false;
			}

			TRefCountPtr<IMFMediaBuffer> Buffer;
			if ((Result = DecodedOutputSample->GetBufferByIndex(0, Buffer.GetInitReference())) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFSample::GetBufferByIndex() failed with %08x"), Result);
				return false;
			}

			DWORD BufferSize = 0;
			uint8* Data = nullptr;
			if ((Result = Buffer->GetCurrentLength(&BufferSize)) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFMediaBuffer::GetCurrentLength() failed with %08x"), Result);
				return false;
			}
			if ((Result = Buffer->Lock(&Data, NULL, NULL)) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFMediaBuffer::Lock() failed with %08x"), Result);
				return false;
			}

			int32 Width = InDecoderOutput->GetDecodedWidth();
			int32 Height = InDecoderOutput->GetDecodedHeight();
			int32 Pitch = InDecoderOutput->GetFrameWidth();
			DecoderOutput->InitializeWithBuffer(Data, BufferSize,
				Pitch,									// Buffer stride
				FIntPoint(Width, Height * 3 / 2),		// Buffer dimensions
				InOutBufferPropertes);

			if ((Result = Buffer->Unlock()) != S_OK)
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFMediaBuffer::Unlock() failed with %08x"), Result);
				return false;
			}
			return true;
		}
	}
	return false;
}

bool FElectraDecoderResourceManagerWindows::SetupRenderBufferFromDecoderOutput_Images(IMediaRenderer::IBuffer* InOutBufferToSetup, TSharedPtr<FParamDict, ESPMode::ThreadSafe> InOutBufferPropertes, TSharedPtr<IElectraDecoderVideoOutput, ESPMode::ThreadSafe> InDecoderOutput, FElectraDecoderResourceManagerWindows::IDecoderPlatformResource* InPlatformSpecificResource)
{
	TSharedPtr<FElectraPlayerVideoDecoderOutputPC, ESPMode::ThreadSafe> DecoderOutput = InOutBufferToSetup->GetBufferProperties().GetValue("texture").GetSharedPointer<FElectraPlayerVideoDecoderOutputPC>();
	IElectraDecoderVideoOutputImageBuffers* ImageBuffers = reinterpret_cast<IElectraDecoderVideoOutputImageBuffers*>(InDecoderOutput->GetPlatformOutputHandle(EElectraDecoderPlatformOutputHandleType::ImageBuffers));
	FInstanceVars* Vars = static_cast<FInstanceVars*>(InPlatformSpecificResource);
	if (DecoderOutput.IsValid() && Vars)
	{
		FElectraVideoDecoderOutputCropValues Crop = InDecoderOutput->GetCropValues();
		InOutBufferPropertes->Set(TEXT("width"), FVariantValue((int64)InDecoderOutput->GetWidth()));
		InOutBufferPropertes->Set(TEXT("height"), FVariantValue((int64)InDecoderOutput->GetHeight()));
		InOutBufferPropertes->Set(TEXT("pitch"), FVariantValue((int64)InDecoderOutput->GetFrameWidth()));
		InOutBufferPropertes->Set(TEXT("crop_left"), FVariantValue((int64)Crop.Left));
		InOutBufferPropertes->Set(TEXT("crop_right"), FVariantValue((int64)Crop.Right));
		InOutBufferPropertes->Set(TEXT("crop_top"), FVariantValue((int64)Crop.Top));
		InOutBufferPropertes->Set(TEXT("crop_bottom"), FVariantValue((int64)Crop.Bottom));
		InOutBufferPropertes->Set(TEXT("aspect_ratio"), FVariantValue((double)InDecoderOutput->GetAspectRatioW() / (double)InDecoderOutput->GetAspectRatioH()));
		InOutBufferPropertes->Set(TEXT("aspect_w"), FVariantValue((int64)InDecoderOutput->GetAspectRatioW()));
		InOutBufferPropertes->Set(TEXT("aspect_h"), FVariantValue((int64)InDecoderOutput->GetAspectRatioH()));
		InOutBufferPropertes->Set(TEXT("fps_num"), FVariantValue((int64)InDecoderOutput->GetFrameRateNumerator()));
		InOutBufferPropertes->Set(TEXT("fps_denom"), FVariantValue((int64)InDecoderOutput->GetFrameRateDenominator()));

		int32 Width = InDecoderOutput->GetDecodedWidth();
		int32 Height = InDecoderOutput->GetDecodedHeight();
		int32 Pitch = InDecoderOutput->GetFrameWidth();

		TMap<FString, FVariant> ExtraValues;
		InDecoderOutput->GetExtraValues(ExtraValues);

		if (WindowsDecoderResources::IsHAP(Vars->Codec4CC))
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
		else if (WindowsDecoderResources::IsProRes(Vars->Codec4CC))
		{
			if (!ImageBuffers)
			{
				UE_LOG(LogElectraPlayer, Log, TEXT("Did not receive image buffers for ProRes output"));
				return false;
			}
			check(ImageBuffers->GetNumberOfBuffers() == 1);
			uint32 ColorBufferFormat = (uint32) ImageBuffers->GetBufferFormatByIndex(0);
			switch(ColorBufferFormat)
			{
				case 0x32767579:	// '2vuy'; 4:2:2   Y'CbCr  8-bit video range
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_B8G8R8A8));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64)8));
					InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::CbY0CrY1));
					Width = (Width + 1) / 2; // each pixel contains TWO horizontally adjacent output pixels)
					Pitch *= 2;
					break;
				}
				case 0x79343136:	// 'y416'; 4:4:4:4 AY'CbCr 16-bit little endian full range alpha, video range Y'CbCr
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A16B16G16R16));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64) 16));
					InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::YCbCr_Alpha));
					Pitch *= 8;
					break;
				}
				case 0x7234666c:	// 'r4fl'; 4:4:4:4 AY'CbCr 32-bit float
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A32B32G32R32F));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64)32));
					InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::YCbCr_Alpha));
					Pitch *= 16;
					break;
				}
				case 0x76323130:	// 'v210'; 4:2:2   Y'CbCr 10-bit video range
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A2B10G10R10));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64)10));
					InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::CbY0CrY1));
					Width = 4 * ((Width + 5) / 6); // each 4 pixel contain 6 horizontally adjacent YCbCr pixels (incl. 4x 2-bit padding)
					Pitch = Width * 4;
					break;
				}
				case 0x76323136:	// 'v216'; 4:2:2   Y'CbCr 16-bit little endian video range (Cb:Y0:Cr:Y1)
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A16B16G16R16));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64)16));
					InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::CbY0CrY1));
					Width = (Width + 1) / 2; // each pixel contains TWO horizontally adjacent output pixels)
					Pitch *= 4;
					break;
				}
				case 0x62363461:	// 'b64a'; 4:4:4:4 Full-range (0-65535) ARGB  16-bit big endian per component
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A16B16G16R16));
					InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64)16));
					InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::ARGB_BigEndian));
					Pitch *= 8;
					break;
				}
				default:
				{
					UE_LOG(LogElectraPlayer, Log, TEXT("Unsupported ProRes texture format 0x%08x"), ColorBufferFormat);
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
		else if (WindowsDecoderResources::IsAvidDNxHD(Vars->Codec4CC))
		{
			if (!ImageBuffers)
			{
				UE_LOG(LogElectraPlayer, Log, TEXT("Did not receive image buffers for Avid output"));
				return false;
			}

			int32 AvidComponentType = InDecoderOutput->GetPixelFormat();
				/*
					,DNX_CT_UCHAR        = 0x001   ///< 8 bit
					,DNX_CT_USHORT_10_6  = 0x004   ///< 10 bit
					,DNX_CT_SHORT_2_14   = 0x008   ///< Fixed point
					,DNX_CT_SHORT        = 0x010   ///< 16 bit. Premultiplied by 257. Byte ordering is machine dependent.
					,DNX_CT_10Bit_2_8    = 0x040   ///< 10 bit in 2_8 format. Byte ordering is fixed. This is to be used with 10-bit 4:2:2 YCbCr components.
					,DNX_CT_V210         = 0x400   ///< Apple's V210
					,DNX_CT_USHORT_12_4  = 0x20000 ///< 12 bit
				*/
			uint32 AvidComponentOrder = (uint32)ImageBuffers->GetBufferFormatByIndex(0);
				/*
					,DNX_CCO_YCbYCr_NoA          = 0x00000001    ///< Y0CbY1Cr
					,DNX_CCO_CbYCrY_NoA          = 0x00000002    ///< CbY0CrY1
					,DNX_CCO_ARGB_Interleaved    = 0x00000004    ///< ARGB
					,DNX_CCO_BGRA_Interleaved    = 0x00000008    ///< BGRA
					,DNX_CCO_RGB_NoA             = 0x00000040    ///< RGB
					,DNX_CCO_BGR_NoA             = 0x00000080    ///< BGR
					,DNX_CCO_RGBA_Interleaved    = 0x00000800    ///< RGBA
					,DNX_CCO_ABGR_Interleaved    = 0x00001000    ///< ABGR
					,DNX_CCO_YCbCr_Interleaved   = 0x00002000    ///< YCbCr 444
					,DNX_CCO_Ch1Ch2Ch3           = 0x00004000    ///< Arbitrary 444 subsampled color components for any colorspace other than RGB and YCbCr
					,DNX_CCO_Ch1Ch2Ch1Ch3        = 0x00008000    ///< Arbitrary 422 subsampled color components for any colorspace other than RGB and YCbCr with not subsampled channel first
					,DNX_CCO_Ch2Ch1Ch3Ch1        = 0x00010000    ///< Arbitrary 422 subsampled color components for any colorspace other than RGB and YCbCr with subsampled channel first
					,DNX_CCO_YCbCr_Planar        = 0x00020000    ///< YCbCr 420 stored in planar form. Y followed by Cb followed by Cr
					,DNX_CCO_CbYACrYA_Interleaved= 0x00040000    ///< YCbCrA 4224
					,DNX_CCO_CbYCrA_Interleaved  = 0x00080000    ///< YCbCrA 4444
					,DNX_CCO_YCbCrA_Planar       = 0x00100000    ///< YCbCrA 4204 stored in planar form. Y followed by Cb followed by Cr followed by A
					,DNX_CCO_Ch1Ch2Ch3A          = 0x00200000    ///< Arbitrary 4444 subsampled color components for any colorspace other than RGB and YCbCr with Alpha (Alpha channel last)
					,DNX_CCO_Ch3Ch2Ch1A          = 0x00400000    ///< Arbitrary 4444 subsampled color components for any colorspace other than RGB and YCbCr with Alpha with reversed channel order (Alpha channel last)
					,DNX_CCO_ACh1Ch2Ch3          = 0x00800000    ///< Arbitrary 4444 subsampled color components for any colorspace other than RGB and YCbCr with Alpha (Alpha channel first)
					,DNX_CCO_Ch2Ch1ACh3Ch1A      = 0x01000000    ///< Arbitrary 4224 subsampled color components for any colorspace other than RGB and YCbCr with not subsampled channel first with Alpha
					,DNX_CCO_CbYCrY_A            = 0x02000000    ///< YCbCrA 4224 mixed-planar (separate Alpha plane)
				*/

			int32 AvidColorVolume = (int32)ElectraDecodersUtil::GetVariantValueSafeI64(ExtraValues, TEXT("avid_color_volume"), 0);
				/*
					,DNX_CV_709             = 0x01     ///< Rec. 709
					,DNX_CV_2020            = 0x02     ///< Non-constant luminance Rec. 2020
					,DNX_CV_2020c           = 0x04     ///< Constant luminance Rec. 2020
					,DNX_CV_OutOfBand       = 0x08     ///< Any other
				*/

			switch (AvidComponentType)
			{
				case	0x001:	// DNX_CT_UCHAR
				{
					switch (AvidComponentOrder)
					{
						case	0x00000001:	// DNX_CCO_YCbYCr_NoA
						case	0x00000002:	// DNX_CCO_CbYCrY_NoA
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_B8G8R8A8));
							InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)((AvidComponentOrder == 0x00000002) ? EVideoDecoderPixelEncoding::CbY0CrY1 : EVideoDecoderPixelEncoding::Y0CbY1Cr)));
							Width = (Width + 1) / 2; // each pixel contains TWO horizontally adjacent output pixels)
							Pitch *= 2;
							break;
						}
						case	0x00000004:	// DNX_CCO_ARGB_Interleaved
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A8R8G8B8));
							Pitch *= 4;
							break;
						}
						case	0x00000800:	// DNX_CCO_RGBA_Interleaved
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_R8G8B8A8));
							Pitch *= 4;
							break;
						}
						case	0x00080000:	// DNX_CCO_CbYCrA_Interleaved
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_B8G8R8A8));
							InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::YCbCr_Alpha));
							Pitch *= 4;
							break;
						}
						case	0x00020000:	// DNX_CCO_YCbCr_Planar
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_NV12));
							Height = (Height * 3) / 2;
							break;
						}
						default:
						{
							return false;
						}
					}
					break;
				}
				case	0x010:	// DNX_CT_USHORT
				case	0x004:	// DNX_CT_USHORT_10_6
				case	0x20000: // DNX_CT_USHORT_12_4
				{
					switch (AvidComponentOrder)
					{
						case	0x00000001:	// DNX_CCO_YCbYCr_NoA
						case	0x00000002:	// DNX_CCO_CbYCrY_NoA
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A16B16G16R16));
							InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)((AvidComponentOrder == 0x00000002) ? EVideoDecoderPixelEncoding::CbY0CrY1 : EVideoDecoderPixelEncoding::Y0CbY1Cr)));
							Width = (Width + 1) / 2; // each pixel contains TWO horizontally adjacent output pixels)
							Pitch *= 2;
							break;
						}
						case	0x00000800:	// DNX_CCO_RGBA_Interleaved
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_R16G16B16A16_UNORM));
							Pitch *= 8;
							break;
						}
						case	0x00001000:	// DNX_CCO_ABGR_Interleaved
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A16B16G16R16));
							Pitch *= 8;
							break;
						}
						case	0x00080000:	// DNX_CCO_CbYCrA_Interleaved
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A16B16G16R16));
							InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::YCbCr_Alpha));
							Pitch *= 8;
							break;
						}
						case	0x00020000:	// DNX_CCO_YCbCr_Planar
						{
							InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_P010));
							Height = (Height * 3) / 2;
							break;
						}
						default:
						{
							return false;
						}
					}
					break;
				}
				case	0x400:	// DNX_CT_USHORT_V210
				{
					InOutBufferPropertes->Set(TEXT("pixelfmt"), FVariantValue((int64)EPixelFormat::PF_A2B10G10R10));
					InOutBufferPropertes->Set(TEXT("pixelenc"), FVariantValue((int64)EVideoDecoderPixelEncoding::CbY0CrY1));
					Width = 4 * ((Width + 5) / 6); // each 4 pixel contain 6 horizontally adjacent YCbCr pixels (incl. 4x 2-bit padding)
					Pitch = Width * 4;
					break;
				}
				default:
				{
				}
			}

//TODO >>> AvidColorVolume: right now we only deliver MP4-based HDR information

			InOutBufferPropertes->Set(TEXT("bits_per"), FVariantValue((int64)InDecoderOutput->GetNumberOfBits()));

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

