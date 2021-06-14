// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "ElectraPlayerPrivate.h"
#include "ElectraPlayerPrivate_Platform.h"
#include "StreamAccessUnitBuffer.h"
#include "Decoder/VideoDecoderH265.h"
#include "Renderer/RendererBase.h"
#include "Renderer/RendererVideo.h"
#include "Player/PlayerSessionServices.h"
#include "Utilities/StringHelpers.h"

#if ELECTRA_PLATFORM_HAS_H265_DECODER

#include "Decoder/DX/DecoderErrors_DX.h"
#include "MediaVideoDecoderOutputPC.h"

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "HAL/LowLevelMemTracker.h"

THIRD_PARTY_INCLUDES_START
#include "mftransform.h"
#include "mfapi.h"
#include "mferror.h"
#include "mfidl.h"
THIRD_PARTY_INCLUDES_END

#include "Decoder/DX/MediaFoundationGUIDs.h"

#include "Windows/HideWindowsPlatformTypes.h"

#include "Decoder/DX/VideoDecoderH265_DX.h"
#include "Decoder/Windows/PlatformVideoDecoderH265.h"


#define ELECTRA_USE_IMF2DBUFFER	1		// set to 1 to use IMF2DBuffer interface rather then plain media buffer when retrieving HW decoded data for CPU use

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

class FElectraPlayerVideoDecoderOutputH265PC : public FVideoDecoderOutputPC
{
public:
	FElectraPlayerVideoDecoderOutputH265PC()
		: OutputType(EOutputType::Unknown)
		, Stride(0)
		, SampleDim(0,0)
	{
	}

	// Hardware decode to buffer (DX12)
	void InitializeWithBuffer(const void* InBuffer, uint32 InSize, uint32 InStride, FIntPoint Dim, Electra::FParamDict* InParamDict);

	// Hardware decode to shared DX11 texture (Win8+)
	void InitializeWithSharedTexture(const TRefCountPtr<ID3D11Device>& InD3D11Device, const TRefCountPtr<ID3D11DeviceContext> InDeviceContext, const TRefCountPtr<IMFSample>& MFSample, const FIntPoint& OutputDim, Electra::FParamDict* InParamDict);

	// Software decode (into texture if DX11 device specified - available only Win8+)
	void PreInitForDecode(FIntPoint OutputDim, const TFunction<void(int32 /*ApiReturnValue*/, const FString& /*Message*/, uint16 /*Code*/, UEMediaError /*Error*/)>& PostError);
	void ProcessDecodeOutput(FIntPoint OutputDim, Electra::FParamDict* InParamDict);

	void SetOwner(const TSharedPtr<IDecoderOutputOwner, ESPMode::ThreadSafe>& InOwningRenderer) override
	{
		OwningRenderer = InOwningRenderer;
	}

	void ShutdownPoolable() override;

	virtual EOutputType GetOutputType() const override
	{
		return OutputType;
	}

	virtual TRefCountPtr<IMFSample> GetMFSample() const override
	{
		check(OutputType == EOutputType::SoftwareWin8Plus || OutputType == EOutputType::SoftwareWin7);
		return MFSample;
	}

	virtual const TArray<uint8>& GetBuffer() const override
	{
		check(OutputType == EOutputType::SoftwareWin8Plus || OutputType == EOutputType::SoftwareWin7 || OutputType == EOutputType::HardwareDX9_DX12);
		return Buffer;
	}

	virtual uint32 GetStride() const override
	{
		return Stride;
	}

	virtual TRefCountPtr<ID3D11Texture2D> GetTexture() const override
	{
		return Texture;
	}

	virtual TRefCountPtr<ID3D11Device> GetDevice() const override
	{
		check(OutputType == EOutputType::SoftwareWin8Plus || OutputType == EOutputType::HardwareWin8Plus);
		return D3D11Device;
	}

	virtual FIntPoint GetDim() const override
	{
		return SampleDim;
	}

private:
	// Decoder output type
	EOutputType OutputType;

	// Output texture (with device that created it) for SW decoder output Win8+
	TRefCountPtr<ID3D11Texture2D> Texture;
	TRefCountPtr<ID3D11Texture2D> SharedTexture;
	TRefCountPtr<ID3D11Device> D3D11Device;

	// CPU-side buffer to 
	TArray<uint8> Buffer;
	uint32 Stride;

	// WMF sample (owned by this class if SW decoder is used)
	TRefCountPtr<IMFSample> MFSample;

	// Dimension of any internally allocated buffer - stored explicitly to cover various special cases for DX
	FIntPoint SampleDim;

	// We hold a weak reference to the video renderer. During destruction the video renderer could be destroyed while samples are still out there..
	TWeakPtr<IDecoderOutputOwner, ESPMode::ThreadSafe> OwningRenderer;
};


void FElectraPlayerVideoDecoderOutputH265PC::PreInitForDecode(FIntPoint OutputDim, const TFunction<void(int32 /*ApiReturnValue*/, const FString& /*Message*/, uint16 /*Code*/, UEMediaError /*Error*/ )>& PostError)
{
	FIntPoint Dim;
	Dim.X = OutputDim.X;
	Dim.Y = OutputDim.Y * 3 / 2;	// adjust height to encompass Y and UV planes

	bool bNeedNew = !MFSample.IsValid() || SampleDim != Dim;

	if (bNeedNew)
	{
		HRESULT Result;
		MFSample = nullptr;
		Texture = nullptr;
		TRefCountPtr<IMFMediaBuffer> MediaBuffer;

		SampleDim = Dim;

		// Windows 8+ and a valid DX11 rendering device?
		// (Rendering device will be null if the rendering device is not DX11 (DX12, Vulkan...))
		if (Electra::IsWindows8Plus() && Electra::FDXDeviceInfo::s_DXDeviceInfo->RenderingDx11Device)
		{
			// Software decode into DX11 texture

			// We SW decode right into a DX11 texture (via the media buffer created from it below)
			/*
				As we cannot use RHI at this point, we cannot create a texture RHI can readily use,
				but can just generate a plain D3D one. We cannot easily use a delegate to ask UE to do
				it for use as we want to keep the "how" of the decoder hidden behind the abstract interfaces
				so we can also encapsulate non-UE-aware systems easily.

				So: we will later need to copy this once. (TODO: see if the render team can provide a way RHI could use
				a native texture like this directly)
			*/
			D3D11_TEXTURE2D_DESC TextureDesc;
			TextureDesc.Width = Dim.X;
			TextureDesc.Height = Dim.Y;
			TextureDesc.MipLevels = 1;
			TextureDesc.ArraySize = 1;
			TextureDesc.Format = DXGI_FORMAT_R8_UINT;
			TextureDesc.SampleDesc.Count = 1;
			TextureDesc.SampleDesc.Quality = 0;
			TextureDesc.Usage = D3D11_USAGE_DEFAULT;
			TextureDesc.BindFlags = 0;
			TextureDesc.CPUAccessFlags = 0;
			TextureDesc.MiscFlags = 0;

			if (FAILED(Result = Electra::FDXDeviceInfo::s_DXDeviceInfo->RenderingDx11Device->CreateTexture2D(&TextureDesc, nullptr, Texture.GetInitReference())))
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("CreateTexture2D() failed with 0x%X %s"), Result, *GetComErrorDescription(Result));
				PostError(Result, "Failed to create software decode output texture", ERRCODE_INTERNAL_COULD_NOT_CREATE_OUTPUTBUFFER, UEMEDIA_ERROR_OK);
				return;
			}
			if (FAILED(Result = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), Texture, 0, false, MediaBuffer.GetInitReference())))
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("MFCreateDXGISurfaceBuffer() failed with 0x%X %s"), Result, *GetComErrorDescription(Result));
				PostError(Result, "Failed to create software decode output texture", ERRCODE_INTERNAL_COULD_NOT_CREATE_OUTPUTBUFFER, UEMEDIA_ERROR_OK);
				return;
			}

			OutputType = EOutputType::SoftwareWin8Plus;
		}
		else // Software decode into CPU buffer
		{
			// SW decode results are just delivered in a simple CPU-side buffer. Create the decoder side version of this...
			if (FAILED(Result = MFCreateMemoryBuffer(Dim.X * Dim.Y * sizeof(uint8), MediaBuffer.GetInitReference())))
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("MFCreateMemoryBuffer() failed with 0x%X %s"), Result, *GetComErrorDescription(Result));
				PostError(Result, "Failed to create software decode output buffer", ERRCODE_INTERNAL_COULD_NOT_CREATE_OUTPUTBUFFER, UEMEDIA_ERROR_OK);
				return;
			}
			OutputType = EOutputType::SoftwareWin7;
		}

		if (SUCCEEDED(Result = MFCreateSample(MFSample.GetInitReference())))
		{
			if (FAILED(Result = MFSample->AddBuffer(MediaBuffer)))
			{
				UE_LOG(LogElectraPlayer, Error, TEXT("IMFSample::AddBuffer() failed with 0x%X %s"), Result, *GetComErrorDescription(Result));
				PostError(Result, "Failed to add output buffer to software decoder sample", ERRCODE_INTERNAL_COULD_NOT_ADD_OUTPUT_BUFFER_TO_SAMPLE, UEMEDIA_ERROR_OK);
				return;
			}
		}
		else
		{
			UE_LOG(LogElectraPlayer, Error, TEXT("MFCreateSample() failed with 0x%X %s"), Result, *GetComErrorDescription(Result));
			PostError(Result, "Failed to create software decode output sample", ERRCODE_INTERNAL_COULD_NOT_CREATE_OUTPUT_SAMPLE, UEMEDIA_ERROR_OK);
			return;
		}
	}
}


void FElectraPlayerVideoDecoderOutputH265PC::ProcessDecodeOutput(FIntPoint OutputDim, Electra::FParamDict* InParamDict)
{
	check(OutputType == EOutputType::SoftwareWin7 || OutputType == EOutputType::SoftwareWin8Plus);

	FVideoDecoderOutput::Initialize(InParamDict);

	SampleDim.X = OutputDim.X;
	SampleDim.Y = OutputDim.Y * 3 / 2;

	Stride = SampleDim.X * sizeof(uint8);

	if (OutputType == EOutputType::SoftwareWin7) // Win7 & DX12 (SW)
	{
		// Retrieve frame data and store it in Buffer for rendering later
		TRefCountPtr<IMFMediaBuffer> MediaBuffer;
		if (MFSample->GetBufferByIndex(0, MediaBuffer.GetInitReference()) != S_OK)
		{
			return;
		}
		DWORD BufferSize = 0;
		if (MediaBuffer->GetCurrentLength(&BufferSize) != S_OK)
		{
			return;
		}
		check(SampleDim.X * SampleDim.Y <= (int32)BufferSize);

		// MediaBuffer reports incorrect buffer sizes (too large) for some resolutions: we use our computed values!
		BufferSize = SampleDim.X * SampleDim.Y;

		uint8* Data = nullptr;
		Buffer.Reset(BufferSize);
		if (MediaBuffer->Lock(&Data, NULL, NULL) == S_OK)
		{
			Buffer.Append(Data, BufferSize);
			MediaBuffer->Unlock();
		}

		// The output IMFSample needs to be released (and recreated for the next frame) for unknown reason.
		// If not destroyed decoder throws an unknown error later on.
		MFSample = nullptr;
	}
	else
	{
		// SW decode into texture need Win8+ (but no additional processing)
		check(Electra::IsWindows8Plus());
	}
}


void FElectraPlayerVideoDecoderOutputH265PC::InitializeWithBuffer(const void* InBuffer, uint32 InSize, uint32 InStride, FIntPoint Dim, Electra::FParamDict* InParamDict)
{
	FVideoDecoderOutput::Initialize(InParamDict);

	OutputType = EOutputType::HardwareDX9_DX12;

	Buffer.Reset(InSize);
	Buffer.Append((uint8*)InBuffer, InSize);

	SampleDim = Dim;
	Stride = InStride;
}


void FElectraPlayerVideoDecoderOutputH265PC::InitializeWithSharedTexture(const TRefCountPtr<ID3D11Device>& InD3D11Device, const TRefCountPtr<ID3D11DeviceContext> InDeviceContext, const TRefCountPtr<IMFSample>& InMFSample, const FIntPoint& OutputDim, Electra::FParamDict* InParamDict)
{
	FVideoDecoderOutput::Initialize(InParamDict);

	OutputType = EOutputType::HardwareWin8Plus;

	bool bNeedsNew = !Texture.IsValid() || (SampleDim.X != OutputDim.X || SampleDim.Y != OutputDim.Y);

	if (bNeedsNew)
	{
		SampleDim = OutputDim;

		// Make a texture we pass on as output
		D3D11_TEXTURE2D_DESC TextureDesc;
		TextureDesc.Width = SampleDim.X;
		TextureDesc.Height = SampleDim.Y;
		TextureDesc.MipLevels = 1;
		TextureDesc.ArraySize = 1;
		TextureDesc.Format = DXGI_FORMAT_NV12;
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.SampleDesc.Quality = 0;
		TextureDesc.Usage = D3D11_USAGE_DEFAULT;
		TextureDesc.BindFlags = 0;
		TextureDesc.CPUAccessFlags = 0;
		TextureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

		if (InD3D11Device->CreateTexture2D(&TextureDesc, nullptr, Texture.GetInitReference()) != S_OK)
		{
			return;
		}

		D3D11Device = InD3D11Device;
	}

	// If we got a texture, copy the data from the decoder into it...
	if (Texture)
	{
		// Get output texture from decoder...
		TRefCountPtr<IMFMediaBuffer> MediaBuffer;
		if (InMFSample->GetBufferByIndex(0, MediaBuffer.GetInitReference()) != S_OK)
		{
			return;
		}
		TRefCountPtr<IMFDXGIBuffer> DXGIBuffer;
		if (MediaBuffer->QueryInterface(__uuidof(IMFDXGIBuffer), (void**)DXGIBuffer.GetInitReference()) != S_OK)
		{
			return;
		}
		TRefCountPtr<ID3D11Texture2D> DecoderTexture;
		if (DXGIBuffer->GetResource(IID_PPV_ARGS(DecoderTexture.GetInitReference())) != S_OK)
		{
			return;
		}
		uint32 ViewIndex = 0;
		if (DXGIBuffer->GetSubresourceIndex(&ViewIndex) != S_OK)
		{
			return;
		}

		// Source data may be larger than desired output, but crop area will be aligned such that we can always copy from 0,0
		D3D11_BOX SrcBox;
		SrcBox.left = 0;
		SrcBox.top = 0;
		SrcBox.front = 0;
		SrcBox.right = OutputDim.X;
		SrcBox.bottom = OutputDim.Y;
		SrcBox.back = 1;

		TRefCountPtr<IDXGIKeyedMutex> KeyedMutex;
		HRESULT res = Texture->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**)&KeyedMutex);

		if (KeyedMutex)
		{
			// No wait on acquire since sample is new and key is 0.
			res = KeyedMutex->AcquireSync(0, 0);
			if (res == S_OK)
			{
				// Copy texture using the decoder DX11 device... (and apply any cropping - see above note)
				InDeviceContext->CopySubresourceRegion(Texture, 0, 0, 0, 0, DecoderTexture, ViewIndex, &SrcBox);

				// Mark texture as updated with key of 1
				// Sample will be read in Convert() method of texture sample
				KeyedMutex->ReleaseSync(1);
			}
		}

		// Make sure texture is updated before giving access to the sample in the rendering thread.
		InDeviceContext->Flush();
	}
}

void FElectraPlayerVideoDecoderOutputH265PC::ShutdownPoolable()
{
	TSharedPtr<IDecoderOutputOwner, ESPMode::ThreadSafe> lockedVideoRenderer = OwningRenderer.Pin();
	if (lockedVideoRenderer.IsValid())
	{
		lockedVideoRenderer->SampleReleasedToPool(GetDuration());
	}

	if (OutputType != EOutputType::HardwareWin8Plus)
	{
		return;
	}

	// Correctly release the keyed mutex when the sample is returned to the pool
	TRefCountPtr<IDXGIResource> OtherResource(nullptr);
	if (Texture)
	{
		Texture->QueryInterface(__uuidof(IDXGIResource), (void**)&OtherResource);
	}

	if (OtherResource)
	{
		HANDLE SharedHandle = nullptr;
		if (OtherResource->GetSharedHandle(&SharedHandle) == S_OK)
		{
			TRefCountPtr<ID3D11Resource> SharedResource;
			D3D11Device->OpenSharedResource(SharedHandle, __uuidof(ID3D11Texture2D), (void**)&SharedResource);
			if (SharedResource)
			{
				TRefCountPtr<IDXGIKeyedMutex> KeyedMutex;
				OtherResource->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**)&KeyedMutex);

				if (KeyedMutex)
				{
					// Reset keyed mutex
					if (KeyedMutex->AcquireSync(1, 0) == S_OK)
					{
						// Texture was never read
						KeyedMutex->ReleaseSync(0);
					}
					else if (KeyedMutex->AcquireSync(2, 0) == S_OK)
					{
						// Texture was read at least once
						KeyedMutex->ReleaseSync(0);
					}
				}
			}
		}
	}

}

// -----------------------------------------------------------------------------------------------------------------------------


namespace Electra
{

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

class FVideoDecoderH265_PC : public FVideoDecoderH265
{
public:
private:
	virtual bool InternalDecoderCreate() override;
	virtual bool CreateDecoderOutputBuffer() override;
	virtual void PreInitDecodeOutputForSW(const FIntPoint& Dim);
	virtual bool SetupDecodeOutputData(const FIntPoint& ImageDim, const TRefCountPtr<IMFSample>& DecodedOutputSample, FParamDict* OutputBufferSampleProperties) override;

	bool CopyTexture(const TRefCountPtr<IMFSample>& DecodedSample, Electra::FParamDict* ParamDict, FIntPoint OutputDim);
};

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

IVideoDecoderH265* IVideoDecoderH265::Create()
{
	return new FVideoDecoderH265_PC();
}

//-----------------------------------------------------------------------------
/**
 *	Queries decoder support/capability for a stream with given properties.
 *	Can be called after Startup() but should be called only shortly before playback to ensure all relevant
 *	libraries are initialized.
 */
bool FVideoDecoderH265::GetStreamDecodeCapability(FStreamDecodeCapability& OutResult, const FStreamDecodeCapability& InStreamParameter)
{
	return FPlatformVideoDecoderH265::GetPlatformStreamDecodeCapability(OutResult, InStreamParameter);
}

//-----------------------------------------------------------------------------
/**
 * Create a decoder instance.
 *
 * @return true if successful, false on error
 */
bool FVideoDecoderH265_PC::InternalDecoderCreate()
{
	MFT_REGISTER_TYPE_INFO		MediaInputInfo { MFMediaType_Video , MFVideoFormat_HEVC };
	TRefCountPtr<IMFAttributes>	Attributes;
	TRefCountPtr<IMFTransform>	Decoder;
	IMFActivate**				ActivateObjects = nullptr;
	UINT32						NumActivateObjects = 0;
	HRESULT						res;

	if (Electra::IsWindows8Plus())
	{
		// Check if there is any reason for a "device lost" - if not we know all is stil well; otherwise we bail without creating a decoder
		if (Electra::FDXDeviceInfo::s_DXDeviceInfo->DxDevice && (res = Electra::FDXDeviceInfo::s_DXDeviceInfo->DxDevice->GetDeviceRemovedReason()) != S_OK)
		{
			PostError(res, "Device lost detected.", ERRCODE_INTERNAL_COULD_NOT_SET_OUTPUT_DESIRED_MEDIA_TYPE);
			return false;
		}
	}

	VERIFY_HR(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT, &MediaInputInfo, nullptr, &ActivateObjects, &NumActivateObjects), "MFTEnumEx failed", ERRCODE_INTERNAL_COULD_NOT_CREATE_DECODER);
	if (NumActivateObjects == 0)
	{
		PostError(S_OK, "MFTEnumEx returned zero activation objects", ERRCODE_INTERNAL_COULD_NOT_CREATE_DECODER);
		return false;
	}

	IMFTransform* NewDecoder = nullptr;
	res = ActivateObjects[0]->ActivateObject(IID_PPV_ARGS(&NewDecoder));
	if (res == S_OK)
	{
		*Decoder.GetInitReference() = NewDecoder;
	}
	for(UINT32 i=0; i<NumActivateObjects; ++i)
	{
		ActivateObjects[i]->Release();
	}
	CoTaskMemFree(ActivateObjects);
	ActivateObjects = nullptr;
	if (res != S_OK)
	{
		PostError(res, "HEVC decoder transform activation failed", ERRCODE_INTERNAL_COULD_NOT_CREATE_DECODER);
		return false;
	}

	VERIFY_HR(Decoder->GetAttributes(Attributes.GetInitReference()), "Failed to get video decoder transform attributes", ERRCODE_INTERNAL_COULD_GET_TRANSFORM_ATTRIBUTES);

	// Check if the transform is D3D aware
	if (Electra::IsWindows8Plus())
	{
		if (!Electra::FDXDeviceInfo::s_DXDeviceInfo->DxDeviceManager)
		{
			// Win8+ with no DX11. No need to check for any DX11 awareness...
			return true;
		}

		uint32 IsDX11Aware = 0;
		res = Attributes->GetUINT32(MF_SA_D3D11_AWARE, &IsDX11Aware);
		if (FAILED(res))
		{
			PostError(res, TEXT("Failed to get MF_SA_D3D11_AWARE"), ERRCODE_INTERNAL_COULD_NOT_CREATE_DECODER);
			return false;
		}
		else if (IsDX11Aware == 0)
		{
			PostError(res, TEXT("Not MF_SA_D3D11_AWARE"), ERRCODE_INTERNAL_COULD_NOT_CREATE_DECODER);
			return false;
		}
		else if (FAILED(res = Decoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(Electra::FDXDeviceInfo::s_DXDeviceInfo->DxDeviceManager.GetReference()))))
		{
			PostError(res, FString::Printf(TEXT("Failed to set MFT_MESSAGE_SET_D3D_MANAGER: 0x%X %s"), res, *GetComErrorDescription(res)), ERRCODE_INTERNAL_COULD_NOT_CREATE_DECODER);
			return false;
		}
	}
	else // Windows 7
	{
		return false;
	}

	// Prepare for the maximum resolution
	VERIFY_HR(Attributes->SetUINT32(MF_MT_DECODER_USE_MAX_RESOLUTION, 1), "Failed to allocate for maximum resolution", ERRCODE_INTERNAL_COULD_NOT_CREATE_DECODER);

	if (FAILED(res = Attributes->SetUINT32(CODECAPI_AVLowLatencyMode, 1)))
	{
		// Not an error. If it doesn't work it just doesn't.
	}

	// Create successful, take on the decoder.
	DecoderTransform = Decoder;

	return true;
}


bool FVideoDecoderH265_PC::CreateDecoderOutputBuffer()
{
	HRESULT									res;
	TUniquePtr<FDecoderOutputBuffer>		NewDecoderOutputBuffer(new FDecoderOutputBuffer());

	VERIFY_HR(DecoderTransform->GetOutputStreamInfo(0, &NewDecoderOutputBuffer->mOutputStreamInfo), "Failed to get video decoder output stream information", ERRCODE_INTERNAL_COULD_NOT_GET_OUTPUT_STREAM_INFO);
	// Do we need to provide the sample output buffer or does the decoder create it for us?
	if ((NewDecoderOutputBuffer->mOutputStreamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0)
	{
		return false;
	}
	CurrentDecoderOutputBuffer = MoveTemp(NewDecoderOutputBuffer);
	return true;
}


void FVideoDecoderH265_PC::PreInitDecodeOutputForSW(const FIntPoint& Dim)
{
	TSharedPtr<FElectraPlayerVideoDecoderOutputH265PC, ESPMode::ThreadSafe> DecoderOutput = CurrentRenderOutputBuffer->GetBufferProperties().GetValue("texture").GetSharedPointer<FElectraPlayerVideoDecoderOutputH265PC>();
	check(DecoderOutput);
	DecoderOutput->PreInitForDecode(Dim, [this](int32 ApiReturnValue, const FString& Message, uint16 Code, UEMediaError Error) { PostError(ApiReturnValue, Message, Code, Error); });
}


bool FVideoDecoderH265_PC::SetupDecodeOutputData(const FIntPoint & ImageDim, const TRefCountPtr<IMFSample>& DecodedOutputSample, FParamDict* OutputBufferSampleProperties)
{
	bool bCopyResult = CopyTexture(DecodedOutputSample, OutputBufferSampleProperties, ImageDim);
	if (!bCopyResult)
	{
		// Failed for some reason. Let's best not render this then, but put in the buffers nonetheless to maintain the normal data flow.
		OutputBufferSampleProperties->Set("is_dummy", FVariantValue(true));
	}
	return true;
}


bool FVideoDecoderH265_PC::CopyTexture(const TRefCountPtr<IMFSample>& Sample, Electra::FParamDict* ParamDict, FIntPoint OutputDim)
{
#if !UE_SERVER
	TSharedPtr<FElectraPlayerVideoDecoderOutputH265PC, ESPMode::ThreadSafe> DecoderOutput = CurrentRenderOutputBuffer->GetBufferProperties().GetValue("texture").GetSharedPointer<FElectraPlayerVideoDecoderOutputH265PC>();
	check(DecoderOutput);

	DWORD BuffersNum = 0;
	CHECK_HR(Sample->GetBufferCount(&BuffersNum));

	if (BuffersNum != 1)
	{
		return false;
	}

	LONGLONG SampleTime = 0;
	CHECK_HR(Sample->GetSampleTime(&SampleTime));
	LONGLONG SampleDuration = 0;
	CHECK_HR(Sample->GetSampleDuration(&SampleDuration));

	TRefCountPtr<IMFMediaBuffer> Buffer;
	CHECK_HR(Sample->GetBufferByIndex(0, Buffer.GetInitReference()));

	if (FDXDeviceInfo::s_DXDeviceInfo->DxVersion == FDXDeviceInfo::ED3DVersion::Version11Win8)
	{
		check(FDXDeviceInfo::s_DXDeviceInfo->DxDevice);

		DecoderOutput->InitializeWithSharedTexture(
			FDXDeviceInfo::s_DXDeviceInfo->DxDevice,
			FDXDeviceInfo::s_DXDeviceInfo->DxDeviceContext,
			Sample,
			OutputDim,
			ParamDict);

		if (!DecoderOutput->GetTexture())
		{
			UE_LOG(LogElectraPlayer, Error, TEXT("FVideoDecoderH265: InD3D11Device->CreateTexture2D() failed!"));
			return false;
		}
	}
	else if (FDXDeviceInfo::s_DXDeviceInfo->DxVersion == FDXDeviceInfo::ED3DVersion::Version12Win10)
	{
		// Gather some info from the DX11 texture in the buffer...
		TRefCountPtr<IMFDXGIBuffer> DXGIBuffer;
		CHECK_HR(Buffer->QueryInterface(__uuidof(IMFDXGIBuffer), (void**)DXGIBuffer.GetInitReference()));

		TRefCountPtr<ID3D11Texture2D> Texture2D;
		CHECK_HR(DXGIBuffer->GetResource(IID_PPV_ARGS(Texture2D.GetInitReference())));
		D3D11_TEXTURE2D_DESC TextureDesc;
		Texture2D->GetDesc(&TextureDesc);
		if (TextureDesc.Format != DXGI_FORMAT_NV12)
		{
			LogMessage(IInfoLog::ELevel::Error, "FVideoDecoderH265::CopyTexture(): Decoded texture is not in NV12 format");
			return false;
		}

		// Retrieve frame data and store it in Buffer for rendering later
#if ELECTRA_USE_IMF2DBUFFER
		TRefCountPtr<IMF2DBuffer> Buffer2D;
		CHECK_HR(Buffer->QueryInterface(__uuidof(IMF2DBuffer), (void**)Buffer2D.GetInitReference()));

		uint8* Data = nullptr;
		LONG Pitch;
		CHECK_HR(Buffer2D->Lock2D(&Data, &Pitch));

		DecoderOutput->InitializeWithBuffer(Data, Pitch * (TextureDesc.Height * 3 / 2),
			Pitch,															// Buffer stride
			FIntPoint(TextureDesc.Width, TextureDesc.Height * 3 / 2),		// Buffer dimensions
			ParamDict);

		CHECK_HR(Buffer2D->Unlock2D());
#else
		DWORD BufferSize = 0;
		CHECK_HR(Buffer->GetCurrentLength(&BufferSize));
		// Not really trusting the size here. Seen crash reports where the returned buffer size is larger than what is contained,
		// as if the texture was wider (a multiple of 256 actually). The crash then happens on the memcpy() trying to _read_ more
		// data than was actually mapped to the buffer (access violation on exactly the start of the next page (4096 byte aligned)).
		// https://docs.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format stipulates the format to be aligned
		// to 'rowPitch' (whatever that is) and 'SysMemPitch' (https://docs.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_subresource_data)
		// which as far as I can tell is a property set when the texture was created. Which we have NO control over since the video decoder creates this
		// texture for us. There does not seem to be any API to query this pitch at all.
		// NV12 format as described here https://docs.microsoft.com/en-us/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering
		// does not suggest there to be any padding/alignment/pitch.
		// What makes the crashes so weird is that they do not happen all the time right on the first frame. It takes a bit before wrong values are returned.
		// For instance, for a 1152*656 texture 1259520 (1280*656) is returned when really it should be only 1133568,
		// or for a 864*480 texture instead of 622080 bytes we are told there'd be 737280 bytes (1024*480).
		// Since we are not using any "actual pitch" in the Initialize() call but the actual width of the texture we must only copy as much data anyway.
		// Even if there was padding in the texture that we would be copying over into our buffer it would not be skipped on rendering later on account
		// of us specifying the width for pitch.
		// It seems prudent at this point not to trust the returned buffer size but to calculate it from the actual dimensions ourselves.
		// NOTE: this value is probably dependent on the GPU and driver implementing the decoder and may therefore not be a general issue!
		BufferSize = TextureDesc.Width * (TextureDesc.Height * 3 / 2);

		uint8* Data = nullptr;
		CHECK_HR(Buffer->Lock(&Data, NULL, NULL));

		DecoderOutput->InitializeWithBuffer(Data, BufferSize,
			TextureDesc.Width,												// Buffer stride
			FIntPoint(TextureDesc.Width, TextureDesc.Height * 3 / 2),		// Buffer dimensions
			ParamDict);

		CHECK_HR(Buffer->Unlock());
#endif

		return true;
	}
	else
	{
		LogMessage(IInfoLog::ELevel::Error, "FVideoDecoderH265::CopyTexture(): Unhandled D3D version");
		return false;
	}
	return true;
#else
	return false;
#endif
}

} // namespace Electra

#endif
