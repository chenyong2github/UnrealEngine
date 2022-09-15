// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS && !UE_SERVER

#include "ElectraTextureSample.h"

#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderUtils.h"

#ifdef ELECTRA_HAVE_DX11
#include "Windows/AllowWindowsPlatformTypes.h"
#include "D3D11State.h"
#include "D3D11Resources.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

/*
	Short summary of how we get data:

	- Win10+ (HW decode is used at all times)
	-- DX11:   we receive data in GPU space as NV12/P010 texture
	-- DX12:   we receive data in CPU(yes) space as NV12/P010 texture
	-- Vulkan: we receive data in CPU(yes) space as NV12/P010 texture

	- Win8:
	-- SW-decode fallback: we receive data in a shared DX11 texture (despite it being SW decode) in NV12 format

	- Win7:
	-- we receive data in a CPU space buffer in NV12 format (no P010 support)
*/

// -------------------------------------------------------------------------------------------------------------------------

DECLARE_GPU_STAT_NAMED(MediaWinDecoder_Convert, TEXT("MediaWinDecoder_Convert"));

// --------------------------------------------------------------------------------------------------------------------------

void FElectraTextureSample::Initialize(FVideoDecoderOutput *InVideoDecoderOutput)
{
	VideoDecoderOutput = StaticCastSharedPtr<FVideoDecoderOutputPC, IDecoderOutputPoolable, ESPMode::ThreadSafe>(InVideoDecoderOutput->AsShared());
	SampleFormat = (VideoDecoderOutput->GetFormat() == PF_NV12) ? EMediaTextureSampleFormat::CharNV12 : EMediaTextureSampleFormat::P010;
	HDRInfo = VideoDecoderOutput->GetHDRInformation();
	Colorimetry = VideoDecoderOutput->GetColorimetry();

	bool bFullRange = false; 
	if (auto PinnedColorimetry = Colorimetry.Pin())
	{
		bFullRange = (PinnedColorimetry->GetMPEGDefinition()->VideoFullRangeFlag != 0);
	}

	// Prepare YUV -> RGB matrix containing all necessary offsets and scales to produce RGB straight from sample data
	const FMatrix* Mtx = bFullRange ? &MediaShaders::YuvToRgbRec709Unscaled : &MediaShaders::YuvToRgbRec709Scaled;
	FVector Off = bFullRange ? MediaShaders::YUVOffsetNoScale8bits : MediaShaders::YUVOffset8bits;
	float Scale = 1.0f;

	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		switch (PinnedHDRInfo->GetHDRType())
		{
		case IVideoDecoderHDRInformation::EType::PQ10:
		case IVideoDecoderHDRInformation::EType::HLG10:
		case IVideoDecoderHDRInformation::EType::HDR10:
			Mtx = &MediaShaders::YuvToRgbRec2020Unscaled;
			Off = MediaShaders::YUVOffsetNoScale16bits;
			break;
		default:
			break;
		}
	}

	// Matrix to transform sample data to standard YUV values
	FMatrix PreMtx = FMatrix::Identity;
	PreMtx.M[0][0] = Scale;
	PreMtx.M[1][1] = Scale;
	PreMtx.M[2][2] = Scale;
	PreMtx.M[0][3] = -Off.X;
	PreMtx.M[1][3] = -Off.Y;
	PreMtx.M[2][3] = -Off.Z;

	// Combine this with the actual YUV-RGB conversion
	YuvToRgbMtx = FMatrix44f(*Mtx * PreMtx);
}


#if !UE_SERVER
void FElectraTextureSample::InitializePoolable()
{
}

void FElectraTextureSample::ShutdownPoolable()
{
	VideoDecoderOutput.Reset();
}
#endif


IMediaTextureSampleConverter* FElectraTextureSample::GetMediaTextureSampleConverter()
{
#ifdef ELECTRA_HAVE_DX11
	// All versions might need SW fallback - check if we have a real texture as source -> converter needed
	return (VideoDecoderOutput && VideoDecoderOutput->GetTexture()) ? this : nullptr;
#else
	return nullptr;
#endif
}

#ifdef ELECTRA_HAVE_DX11
struct FRHICommandCopyResource final : public FRHICommand<FRHICommandCopyResource>
{
	TRefCountPtr<ID3D11Texture2D> SampleTexture;
	FTexture2DRHIRef SampleDestinationTexture;
	bool bCrossDevice;

	FRHICommandCopyResource(ID3D11Texture2D* InSampleTexture, FRHITexture2D* InSampleDestinationTexture, bool bInCrossDevice)
		: SampleTexture(InSampleTexture)
		, SampleDestinationTexture(InSampleDestinationTexture)
		, bCrossDevice(bInCrossDevice)
	{
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		LLM_SCOPE(ELLMTag::MediaStreaming);
		ID3D11Device* D3D11Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
		ID3D11DeviceContext* D3D11DeviceContext = nullptr;

		D3D11Device->GetImmediateContext(&D3D11DeviceContext);
		if (D3D11DeviceContext)
		{
			ID3D11Resource* DestinationTexture = reinterpret_cast<ID3D11Resource*>(SampleDestinationTexture->GetNativeResource());
			if (DestinationTexture)
			{
				if (bCrossDevice)
				{
					TRefCountPtr<IDXGIResource> OtherResource(nullptr);
					SampleTexture->QueryInterface(__uuidof(IDXGIResource), (void**)OtherResource.GetInitReference());

					if (OtherResource)
					{
						//
						// Copy shared texture from decoder device to render device
						//
						HANDLE SharedHandle = nullptr;
						if (OtherResource->GetSharedHandle(&SharedHandle) == S_OK)
						{
							if (SharedHandle != 0)
							{
								TRefCountPtr<ID3D11Resource> SharedResource;
								D3D11Device->OpenSharedResource(SharedHandle, __uuidof(ID3D11Texture2D), (void**)SharedResource.GetInitReference());

								if (SharedResource)
								{
									TRefCountPtr<IDXGIKeyedMutex> KeyedMutex;
									SharedResource->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**)KeyedMutex.GetInitReference());

									if (KeyedMutex)
									{
										// Key is 1 : Texture as just been updated
										// Key is 2 : Texture as already been updated.
										// Do not wait to acquire key 1 since there is race no condition between writer and reader.
										if (KeyedMutex->AcquireSync(1, 0) == S_OK)
										{
											// Copy from shared texture of FSink device to Rendering device
											D3D11DeviceContext->CopyResource(DestinationTexture, SharedResource);
											KeyedMutex->ReleaseSync(2);
										}
										else
										{
											// If key 1 cannot be acquired, another reader is already copying the resource
											// and will release key with 2. 
											// Wait to acquire key 2.
											if (KeyedMutex->AcquireSync(2, INFINITE) == S_OK)
											{
												KeyedMutex->ReleaseSync(2);
											}
										}
									}
								}
							}
						}
					}
				}
				else
				{
					//
					// Simple copy on render device
					//
					D3D11DeviceContext->CopyResource(DestinationTexture, SampleTexture);
				}
			}
			D3D11DeviceContext->Release();
		}
	}
};
#endif

/**
 * "Converter" for textures - here: a copy from the decoder owned texture (possibly in another device) into a RHI one (as prep for the real conversion to RGB etc.)
 */
bool FElectraTextureSample::Convert(FTexture2DRHIRef & InDstTexture, const FConversionHints & Hints)
{
#ifdef ELECTRA_HAVE_DX11

	LLM_SCOPE(ELLMTag::MediaStreaming);

	check(IsInRenderingThread());

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	SCOPED_DRAW_EVENT(RHICmdList, WinMediaOutputConvertTexture);
	SCOPED_GPU_STAT(RHICmdList, MediaWinDecoder_Convert);

	// Get actual sample dimensions
	FIntPoint Dim = VideoDecoderOutput->GetDim();

	bool bCrossDeviceCopy;
	EPixelFormat Format;
	if (VideoDecoderOutput->GetOutputType() != FVideoDecoderOutputPC::EOutputType::HardwareWin8Plus)
	{
		//
		// SW decoder has decoded into a HW texture (not known to RHI) -> copy it into an RHI one
		//
		check(VideoDecoderOutput->GetOutputType() == FVideoDecoderOutputPC::EOutputType::SoftwareWin8Plus);
		check(VideoDecoderOutput->GetFormat() == EPixelFormat::PF_NV12);

		Format = EPixelFormat::PF_G8; // use fixed format: we flag this as NV12, too - as it is - but DX11 will only support a "higher than normal G8 texture" (with specialized access in shader)
		bCrossDeviceCopy = false;
	}
	else
	{
		//
		// HW decoder has delivered a texture (this is already a copy) which is on its own device. Copy into one created by RHI (and hence on our rendering device)
		//

		// note: on DX platforms we won't get any SRV generated for NV12 -> so any user needs to do that manually! (as they please: R8, R8G8...)
		Format = VideoDecoderOutput->GetFormat();
		bCrossDeviceCopy = true;
	}

	// Do we need a new RHI texture?
	if (!Texture.IsValid() || Texture->GetSizeX() != Dim.X || Texture->GetSizeY() != Dim.Y)
	{
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create2D(TEXT("FElectraTextureSample"), Dim, Format)
			.SetFlags(ETextureCreateFlags::Dynamic);

		Texture = RHICreateTexture(Desc);
	}
	
	// Copy data into RHI texture
	if (RHICmdList.Bypass())
	{
		FRHICommandCopyResource Cmd(VideoDecoderOutput->GetTexture(), Texture, bCrossDeviceCopy);
		Cmd.Execute(RHICmdList);
	}
	else
	{
		new (RHICmdList.AllocCommand<FRHICommandCopyResource>()) FRHICommandCopyResource(VideoDecoderOutput->GetTexture(), Texture, bCrossDeviceCopy);
	}
	
	return true;
#else
	return false;
#endif
}


const void* FElectraTextureSample::GetBuffer()
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetBuffer().GetData();
	}
	return nullptr;
}


uint32 FElectraTextureSample::GetStride() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetStride();
	}
	return 0;
}


FIntPoint FElectraTextureSample::GetDim() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetDim();
	}
	return FIntPoint::ZeroValue;
}


FIntPoint FElectraTextureSample::GetOutputDim() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetOutputDim();
	}
	return FIntPoint::ZeroValue;
}


FMediaTimeStamp FElectraTextureSample::GetTime() const
{
	if (VideoDecoderOutput)
	{
		const FDecoderTimeStamp TimeStamp = VideoDecoderOutput->GetTime();
		return FMediaTimeStamp(TimeStamp.Time, TimeStamp.SequenceIndex);
	}
	return FMediaTimeStamp();
}


FTimespan FElectraTextureSample::GetDuration() const
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetDuration();
	}
	return FTimespan::Zero();
}

IMFSample* FElectraTextureSample::GetMFSample()
{
	if (VideoDecoderOutput)
	{
		return VideoDecoderOutput->GetMFSample().GetReference();
	}
	return nullptr;
}


bool FElectraTextureSample::IsOutputSrgb() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		// If the HDR type is unknown we also assume sRGB
		return (PinnedHDRInfo->GetHDRType() == IVideoDecoderHDRInformation::EType::Unknown);
	}
	// If no HDR info is present, we assume sRGB
	return true;
}

const FMatrix& FElectraTextureSample::GetYUVToRGBMatrix() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		switch (PinnedHDRInfo->GetHDRType())
		{
		case IVideoDecoderHDRInformation::EType::PQ10:
		case IVideoDecoderHDRInformation::EType::HLG10:
		case IVideoDecoderHDRInformation::EType::HDR10:		return MediaShaders::YuvToRgbRec2020Unscaled;
		case IVideoDecoderHDRInformation::EType::Unknown:	break;
		}
	}

	// If no HDR info is present, we assume sRGB
	return GetFullRange() ? MediaShaders::YuvToRgbRec709Unscaled : MediaShaders::YuvToRgbRec709Scaled;
}

bool FElectraTextureSample::GetFullRange() const
{
	bool bFullRange = false;
	if (auto PinnedColorimetry = Colorimetry.Pin())
	{
		bFullRange = (PinnedColorimetry->GetMPEGDefinition()->VideoFullRangeFlag != 0);
	}
	return bFullRange;
}

FMatrix44f FElectraTextureSample::GetSampleToRGBMatrix() const
{
	return YuvToRgbMtx;
}

FMatrix44f FElectraTextureSample::GetGamutToXYZMatrix() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		switch (PinnedHDRInfo->GetHDRType())
		{
		case IVideoDecoderHDRInformation::EType::PQ10:
		case IVideoDecoderHDRInformation::EType::HLG10:
		case IVideoDecoderHDRInformation::EType::HDR10:		return GamutToXYZMatrix(EDisplayColorGamut::Rec2020_D65);
		case IVideoDecoderHDRInformation::EType::Unknown:	return GamutToXYZMatrix(EDisplayColorGamut::sRGB_D65);
		}
	}
	// If no HDR info is present, we assume sRGB
	return GamutToXYZMatrix(EDisplayColorGamut::sRGB_D65);
}

FVector2f FElectraTextureSample::GetWhitePoint() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		if (auto ColorVolume = PinnedHDRInfo->GetMasteringDisplayColourVolume())
		{
			return FVector2f(ColorVolume->white_point_x, ColorVolume->white_point_y);
		}
	}
	// If no HDR info is present, we assume sRGB
	return FVector2f(UE::Color::GetWhitePoint(UE::Color::EWhitePoint::CIE1931_D65));
}

UE::Color::EEncoding FElectraTextureSample::GetEncodingType() const
{
	if (auto PinnedHDRInfo = HDRInfo.Pin())
	{
		switch (PinnedHDRInfo->GetHDRType())
		{
		case IVideoDecoderHDRInformation::EType::PQ10:
		case IVideoDecoderHDRInformation::EType::HDR10:		return UE::Color::EEncoding::ST2084;
		case IVideoDecoderHDRInformation::EType::Unknown:	return UE::Color::EEncoding::sRGB;
		case IVideoDecoderHDRInformation::EType::HLG10:
			{
			check(!"*** Implement support for HLG10 in UE::Color!");
			return UE::Color::EEncoding::sRGB;
			}
		}
	}
	// If no HDR info is present, we assume sRGB
	return UE::Color::EEncoding::sRGB;
}

#endif
