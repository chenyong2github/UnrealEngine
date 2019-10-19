// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WmfMediaHAPDecoder.h"

#if WMFMEDIA_SUPPORTED_PLATFORM

#include "hap.h"

#include "HAPMediaModule.h"

#include "GenericPlatform/GenericPlatformAtomics.h"

#include "Windows/AllowWindowsPlatformTypes.h"

#include <d3d11.h>
#include <d3dcompiler.h>

void MyHapDecodeCallback(HapDecodeWorkFunction InFunction, void* InParameter, unsigned int InCount, void* InInfo)
{
	unsigned int i;
	for (i = 0; i < InCount; i++)
	{
		InFunction(InParameter, i);
	}
}

const GUID DecoderGUID_HAP = { 0x48617031, 0x767A, 0x494D, { 0xB4, 0x78, 0xF2, 0x9D, 0x25, 0xDC, 0x90, 0x37 } };
const GUID DecoderGUID_HAP_ALPHA = { 0x48617035, 0x767A, 0x494D, { 0xB4, 0x78, 0xF2, 0x9D, 0x25, 0xDC, 0x90, 0x37 } };
const GUID DecoderGUID_HAP_Q = { 0x48617059, 0x767A, 0x494D, { 0xB4, 0x78, 0xF2, 0x9D, 0x25, 0xDC, 0x90, 0x37 } };
const GUID DecoderGUID_HAP_Q_ALPHA = { 0x4861704D, 0x767A, 0x494D, { 0xB4, 0x78, 0xF2, 0x9D, 0x25, 0xDC, 0x90, 0x37 } };

bool WmfMediaHAPDecoder::IsSupported(const GUID& InGuid)
{
	return ((InGuid == DecoderGUID_HAP) ||
			(InGuid == DecoderGUID_HAP_ALPHA) ||
			(InGuid == DecoderGUID_HAP_Q) ||
			(InGuid == DecoderGUID_HAP_Q_ALPHA));
}


bool WmfMediaHAPDecoder::SetOutputFormat(const GUID& InGuid, GUID& OutVideoFormat)
{

	if ((InGuid == DecoderGUID_HAP) || (InGuid == DecoderGUID_HAP_Q))
	{
		OutVideoFormat =  MFVideoFormat_NV12;
		return true;
	}
	else if ((InGuid == DecoderGUID_HAP_ALPHA) || (InGuid == DecoderGUID_HAP_Q_ALPHA))
	{
		OutVideoFormat = MFVideoFormat_ARGB32;
		return true;
	}
	else
	{
		return false;
	}
}

WmfMediaHAPDecoder::WmfMediaHAPDecoder()
	: WmfMediaDecoder(),
	InputSubType{ 0 },
	FrameData{ 0 }
{
}


WmfMediaHAPDecoder::~WmfMediaHAPDecoder()
{
}


#pragma warning(push)
#pragma warning(disable:4838)
HRESULT WmfMediaHAPDecoder::QueryInterface(REFIID riid, void** ppv)
{
	static const QITAB qit[] = 
	{
		QITABENT(WmfMediaHAPDecoder, IMFTransform),
		{ 0 }
	};

	return QISearch(this, qit, riid, ppv);
}
#pragma warning(pop)



HRESULT WmfMediaHAPDecoder::GetOutputAvailableType(DWORD dwOutputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType)
{
	if (ppType == NULL)
	{
		return E_INVALIDARG;
	}

	if (dwOutputStreamID != 0)
	{
		return MF_E_INVALIDSTREAMNUMBER;
	}

	if (dwTypeIndex != 0)
	{
		return MF_E_NO_MORE_TYPES;
	}

	FScopeLock Lock(&CriticalSection);

	HRESULT hr = S_OK;

	TComPtr<IMFMediaType> TempOutputType;

	if (InputType == NULL)
	{
		hr = MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	if (SUCCEEDED(hr))
	{
		hr = MFCreateMediaType(&TempOutputType);
	}

	if (SUCCEEDED(hr))
	{
		hr = TempOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	}

	if (SUCCEEDED(hr))
	{
		if (InputSubType == DecoderGUID_HAP_ALPHA || InputSubType == DecoderGUID_HAP_Q_ALPHA)
		{
			hr = TempOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
		}
		else
		{
			hr = TempOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		}
	}

	if (SUCCEEDED(hr))
	{
		hr = TempOutputType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
	}

	if (SUCCEEDED(hr))
	{
		hr = TempOutputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	}

	if (SUCCEEDED(hr))
	{
		hr = TempOutputType->SetUINT32(MF_MT_SAMPLE_SIZE, OutputImageSize);
	}

	if (SUCCEEDED(hr))
	{
		hr = MFSetAttributeSize(TempOutputType, MF_MT_FRAME_SIZE, ImageWidthInPixels, ImageHeightInPixels);
	}

	if (SUCCEEDED(hr))
	{
		hr = MFSetAttributeRatio(TempOutputType, MF_MT_FRAME_RATE, FrameRate.Numerator, FrameRate.Denominator);
	}

	if (SUCCEEDED(hr))
	{
		hr = TempOutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	}

	if (SUCCEEDED(hr))
	{
		hr = MFSetAttributeRatio(TempOutputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	}

	if (SUCCEEDED(hr))
	{
		*ppType = TempOutputType;
		(*ppType)->AddRef();
	}

	return hr;
}


HRESULT WmfMediaHAPDecoder::ProcessMessage(MFT_MESSAGE_TYPE eMessage, ULONG_PTR ulParam)
{
	FScopeLock Lock(&CriticalSection);

	HRESULT hr = S_OK;

	switch (eMessage)
	{
	case MFT_MESSAGE_COMMAND_FLUSH:
		hr = OnFlush();
		break;

	case MFT_MESSAGE_COMMAND_DRAIN:
	case MFT_MESSAGE_NOTIFY_BEGIN_STREAMING:
		hr = OnDiscontinuity();
		break;

	case MFT_MESSAGE_SET_D3D_MANAGER:
		{
			IMFDXGIDeviceManager *pDeviceManager = (IMFDXGIDeviceManager*)ulParam;
			DXGIManager = TComPtr<IMFDXGIDeviceManager>(pDeviceManager);

			if (DXGIManager.IsValid())
			{
				HANDLE DeviceHandle = 0;
				if (DXGIManager->OpenDeviceHandle(&DeviceHandle) == S_OK)
				{
					DXGIManager->GetVideoService(DeviceHandle, __uuidof(ID3D11Device), (void**)&D3D11Device);
					if (D3D11Device.IsValid())
					{
						D3D11Device->GetImmediateContext(&D3DImmediateContext);
						if (D3DImmediateContext.IsValid())
						{
							UE_LOG(LogHAPMedia, Verbose, TEXT("D3D11Device from Device manager: %p"), D3D11Device.Get());
							if (!InitPipeline())
							{
								UE_LOG(LogHAPMedia, Error, TEXT("Unable to initialize 3D pipeline for Hap Decoding"));
								hr = E_NOTIMPL;
							}
						}
					}
				}
			}
		}
		break;
	default:
		break;
	}

	return hr;
}


HRESULT WmfMediaHAPDecoder::ProcessOutput(DWORD dwFlags, DWORD cOutputBufferCount, MFT_OUTPUT_DATA_BUFFER* pOutputSamples, DWORD* pdwStatus)
{
	if (dwFlags != 0)
	{
		return E_INVALIDARG;
	}

	if (pOutputSamples == NULL || pdwStatus == NULL)
	{
		return E_POINTER;
	}

	if (cOutputBufferCount != 1)
	{
		return E_INVALIDARG;
	}

	if (OutputQueue.IsEmpty())
	{
		return MF_E_TRANSFORM_NEED_MORE_INPUT;
	}

	if (!D3D11Device.IsValid())
	{
		UE_LOG(LogHAPMedia, Error, TEXT("DX11 Device is not initialized!"));
		return E_NOTIMPL;
	}

	if (!OutputTexture.IsValid())
	{
		TComPtr<ID3D11Texture2D> Texture;
		TComPtr<ID3D11RenderTargetView> RenderTarget_A;
		TComPtr<ID3D11RenderTargetView> RenderTarget_B;
		{
			D3D11_TEXTURE2D_DESC TextureDesc;
			memset(&TextureDesc, 0, sizeof(TextureDesc));
			TextureDesc.Width = ImageWidthInPixels;
			TextureDesc.Height = ImageHeightInPixels;
			TextureDesc.MipLevels = 1;
			TextureDesc.ArraySize = 1;

			if (InputSubType == DecoderGUID_HAP || InputSubType == DecoderGUID_HAP_Q)
			{
				TextureDesc.Format = DXGI_FORMAT_NV12;
			}
			else // if (InputSubType == DecoderGUID_HAP_ALPHA || InputSubType == DecoderGUID_HAP_ALPHA)
			{
				TextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			}

			TextureDesc.SampleDesc.Count = 1;
			TextureDesc.SampleDesc.Quality = 0;
			TextureDesc.Usage = D3D11_USAGE_DEFAULT;
			TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
			TextureDesc.CPUAccessFlags = 0;
			TextureDesc.MiscFlags = 0;
			HRESULT Result = D3D11Device->CreateTexture2D(&TextureDesc, nullptr, &Texture);

			if (InputSubType == DecoderGUID_HAP || InputSubType == DecoderGUID_HAP_Q)
			{
				D3D11_RENDER_TARGET_VIEW_DESC RTViewDesc;
				memset(&RTViewDesc, 0, sizeof(RTViewDesc));
				RTViewDesc.Format = DXGI_FORMAT_R8_UNORM;
				RTViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				RTViewDesc.Texture2D.MipSlice = 0;
				Result = D3D11Device->CreateRenderTargetView(Texture, &RTViewDesc, &RenderTarget_A);

				memset(&RTViewDesc, 0, sizeof(RTViewDesc));
				RTViewDesc.Format = DXGI_FORMAT_R8G8_UNORM;
				RTViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				RTViewDesc.Texture2D.MipSlice = 0;
				Result = D3D11Device->CreateRenderTargetView(Texture, &RTViewDesc, &RenderTarget_B);
			}
			else // if (InputSubType == DecoderGUID_HAP_ALPHA || InputSubType == DecoderGUID_HAP_Q_ALPHA)
			{
				D3D11_RENDER_TARGET_VIEW_DESC RTViewDesc;
				memset(&RTViewDesc, 0, sizeof(RTViewDesc));
				RTViewDesc.Format = TextureDesc.Format;
				RTViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				RTViewDesc.Texture2D.MipSlice = 0;
				Result = D3D11Device->CreateRenderTargetView(Texture, &RTViewDesc, &RenderTarget_A);
			}
		}

		OutputTexture = MakeUnique<FOutputTextures>();
		OutputTexture->Texture = MoveTemp(Texture);
		OutputTexture->RTV_A = MoveTemp(RenderTarget_A);
		OutputTexture->RTV_B = MoveTemp(RenderTarget_B);
	}

	if (!WorkTextures.IsValid())
	{
		TComPtr<ID3D11Texture2D> InputTextureColor;
		TComPtr<ID3D11ShaderResourceView> SRV_InputColor;
		{
			D3D11_TEXTURE2D_DESC TextureDesc;
			memset(&TextureDesc, 0, sizeof(TextureDesc));
			TextureDesc.Width = ImageWidthInPixels;
			TextureDesc.Height = ImageHeightInPixels;
			TextureDesc.MipLevels = 1;
			TextureDesc.ArraySize = 1;

			if (InputSubType == DecoderGUID_HAP)
			{
				TextureDesc.Format = DXGI_FORMAT_BC1_UNORM;
			}
			else // if (InputSubType == DecoderGUID_HAP_ALPHA || InputSubType == DecoderGUID_HAP_Q || InputSubType == DecoderGUID_HAP_Q_ALPHA)
			{
				TextureDesc.Format = DXGI_FORMAT_BC3_UNORM;
			}

			TextureDesc.SampleDesc.Count = 1;
			TextureDesc.SampleDesc.Quality = 0;
			TextureDesc.Usage = D3D11_USAGE_DYNAMIC;
			TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			TextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			TextureDesc.MiscFlags = 0;
			D3D11Device->CreateTexture2D(&TextureDesc, nullptr, &InputTextureColor);
			CreateSRV(InputTextureColor, TextureDesc, &SRV_InputColor);
		}

		TComPtr<ID3D11Texture2D> InputTextureAlpha;
		TComPtr<ID3D11ShaderResourceView> SRV_InputAlpha;

		if (InputSubType == DecoderGUID_HAP_Q_ALPHA)
		{
			D3D11_TEXTURE2D_DESC TextureDesc;
			memset(&TextureDesc, 0, sizeof(TextureDesc));
			TextureDesc.Width = ImageWidthInPixels;
			TextureDesc.Height = ImageHeightInPixels;
			TextureDesc.MipLevels = 1;
			TextureDesc.ArraySize = 1;
			TextureDesc.Format = DXGI_FORMAT_BC4_UNORM;
			TextureDesc.SampleDesc.Count = 1;
			TextureDesc.SampleDesc.Quality = 0;
			TextureDesc.Usage = D3D11_USAGE_DYNAMIC;
			TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			TextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			TextureDesc.MiscFlags = 0;
			D3D11Device->CreateTexture2D(&TextureDesc, nullptr, &InputTextureAlpha);
			CreateSRV(InputTextureAlpha, TextureDesc, &SRV_InputAlpha);
		}

		FWorkTextures WT;
		WT.InputTextureColor = MoveTemp(InputTextureColor);
		WT.InputTextureAlpha = MoveTemp(InputTextureAlpha);
		WT.SRV_InputColor = MoveTemp(SRV_InputColor);
		WT.SRV_InputAlpha = MoveTemp(SRV_InputAlpha);
		WorkTextures = MakeUnique<FWorkTextures>(MoveTemp(WT));
	}

	FScopeLock Lock(&CriticalSection);

	HRESULT hr = S_OK;

	TComPtr<IMFMediaBuffer> pOutput;

	if (SUCCEEDED(hr))
	{
		TComPtr<IMFSample> Sample;

		MFCreateSample(&Sample);

		pOutputSamples[0].pSample = Sample.Get();
		Sample->AddRef();

		TComPtr<IMFMediaBuffer> MediaBuffer;
		MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), OutputTexture->Texture, 0, FALSE, &MediaBuffer);

		Sample->AddBuffer(MediaBuffer);
	}

	hr = pOutputSamples[0].pSample->GetBufferByIndex(0, &pOutput);

	if (pOutput == nullptr)
	{
		return S_OK;
	}

	if (SUCCEEDED(hr))
	{
		hr = InternalProcessOutput(pOutputSamples[0].pSample);
	}

	pOutputSamples[0].dwStatus = 0;
	*pdwStatus = 0;

	return hr;
}


HRESULT WmfMediaHAPDecoder::InternalProcessOutput(IMFSample* InSample)
{
	if (OutputQueue.IsEmpty())
	{
		return MF_E_TRANSFORM_NEED_MORE_INPUT;
	}

	HRESULT hr = S_OK;

	BYTE *pbData = NULL;
	LONG lActualStride = 0;

	DWORD dwTimeCode = 0;
	LONGLONG rt = 0;

	DWORD cBuffers = 0;
	TComPtr<IMFMediaBuffer> pBuffer = nullptr;
	TComPtr<IMFDXGIBuffer> pDXGIBuffer = nullptr;
	UINT dwViewIndex = 0;
	TComPtr<ID3D11Texture2D> TextureNV12_Y = nullptr;

	HRESULT Result = InSample->GetBufferCount(&cBuffers);
	if (FAILED(Result))
	{
		return Result;
	}
	if (1 == cBuffers)
	{
		Result = InSample->GetBufferByIndex(0, &pBuffer);
		if (FAILED(Result))
		{
			return Result;
		}
	}
	Result = pBuffer->QueryInterface(__uuidof(IMFDXGIBuffer), (LPVOID*)&pDXGIBuffer);
	if (FAILED(Result))
	{
		return Result;
	}

	Result = pDXGIBuffer->GetResource(__uuidof(ID3D11Texture2D), (LPVOID*)&TextureNV12_Y);
	if (FAILED(Result))
	{
		return Result;
	}

	Result = pDXGIBuffer->GetSubresourceIndex(&dwViewIndex);
	if (FAILED(Result))
	{
		return Result;
	}

	DataBuffer OuputDataBuffer;
	OutputQueue.Dequeue(OuputDataBuffer);

	D3D11_MAPPED_SUBRESOURCE MappedResourceColor;
	Result = D3DImmediateContext->Map(WorkTextures->InputTextureColor, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResourceColor);
	memcpy(MappedResourceColor.pData, OuputDataBuffer.Color.GetData(), MappedResourceColor.DepthPitch);
	D3DImmediateContext->Unmap(WorkTextures->InputTextureColor, 0);

	if (InputSubType == DecoderGUID_HAP_Q_ALPHA)
	{
		D3D11_MAPPED_SUBRESOURCE MappedResourceAlpha;
		Result = D3DImmediateContext->Map(WorkTextures->InputTextureAlpha, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResourceAlpha);
		memcpy(MappedResourceAlpha.pData, OuputDataBuffer.Alpha.GetData(), MappedResourceAlpha.DepthPitch);
		D3DImmediateContext->Unmap(WorkTextures->InputTextureAlpha, 0);
	}


	if (InputSubType == DecoderGUID_HAP_Q_ALPHA)
	{
		BindSRV(WorkTextures->SRV_InputColor, WorkTextures->SRV_InputAlpha);
	}
	else
	{
		BindSRV(WorkTextures->SRV_InputColor);
	}

	UINT stride = sizeof(VertexDescription);
	UINT offset = 0;

	if (InputSubType == DecoderGUID_HAP)
	{
		FrameData.Mode = 0;
	}
	else if (InputSubType == DecoderGUID_HAP_ALPHA)
	{
		FrameData.Mode = 1;
	}
	else if (InputSubType == DecoderGUID_HAP_Q)
	{
		FrameData.Mode = 2;
	}
	else // if (InputSubType == DecoderGUID_HAP_Q_ALPHA)
	{
		FrameData.Mode = 3;
	}

	D3D11_VIEWPORT viewport;
	ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;

	ID3D11RenderTargetView* m_pRenderViews[1];

	int StartPass = 0;

	if (InputSubType == DecoderGUID_HAP_ALPHA || InputSubType == DecoderGUID_HAP_Q_ALPHA)
	{
		StartPass = 1;
	}

	for (int PassIndex = StartPass; PassIndex < 2; PassIndex++)
	{
		if (PassIndex == 0)
		{
			m_pRenderViews[0] = OutputTexture->RTV_B;
			FrameData.WriteY = 0;
			viewport.Width = float(ImageWidthInPixels / 2);
			viewport.Height = float(ImageHeightInPixels / 2);
		}
		else
		{
			m_pRenderViews[0] = OutputTexture->RTV_A;
			FrameData.WriteY = 1;
			viewport.Width = float(ImageWidthInPixels);
			viewport.Height = float(ImageHeightInPixels);
		}

		D3DImmediateContext->RSSetViewports(1, &viewport);
		D3DImmediateContext->UpdateSubresource(PixelBuffer, 0, nullptr, &FrameData, 0, 0);
		D3DImmediateContext->OMSetRenderTargets(1, &m_pRenderViews[0], NULL);
		D3DImmediateContext->IASetVertexBuffers(0, 1, &VertexBuffer, &stride, &offset);
		D3DImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		D3DImmediateContext->Draw(4, 0);
	}

	LONGLONG TimeStamp = OuputDataBuffer.TimeStamp;

	InputQueue.Enqueue(MoveTemp(OuputDataBuffer));

	if (SUCCEEDED(hr))
	{
		hr = InSample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
	}

	if (SUCCEEDED(hr))
	{
		hr = InSample->SetSampleTime(TimeStamp);
	}

	if (SUCCEEDED(hr))
	{
		hr = InSample->SetSampleDuration(SampleDuration);
	}

	return hr;
}


HRESULT WmfMediaHAPDecoder::OnCheckInputType(IMFMediaType* InMediaType)
{
	HRESULT hr = S_OK;

	if (InputType)
	{
		DWORD dwFlags = 0;
		if (S_OK == InputType->IsEqual(InMediaType, &dwFlags))
		{
			return S_OK;
		}
		else
		{
			return MF_E_INVALIDTYPE;
		}
	}

	GUID MajorType = { 0 };
	GUID SubType = { 0 };
	UINT32 Width = 0;
	UINT32 Height = 0;
	MFRatio Fps = { 0 };

	hr = InMediaType->GetMajorType(&MajorType);

	if (SUCCEEDED(hr))
	{
		if (MajorType != MFMediaType_Video)
		{
			hr = MF_E_INVALIDTYPE;
		}
	}

	if (SUCCEEDED(hr))
	{
		hr = InMediaType->GetGUID(MF_MT_SUBTYPE, &SubType);
	}

	if (SUCCEEDED(hr))
	{
		if (SubType != DecoderGUID_HAP &&
			SubType != DecoderGUID_HAP_ALPHA &&
			SubType != DecoderGUID_HAP_Q &&
			SubType != DecoderGUID_HAP_Q_ALPHA)
		{
			hr = MF_E_INVALIDTYPE;
		}

	}

	if (SUCCEEDED(hr))
	{
		hr = MFGetAttributeSize(InMediaType, MF_MT_FRAME_SIZE, &Width, &Height);
	}

	if (SUCCEEDED(hr))
	{
		hr = MFGetAttributeRatio(InMediaType, MF_MT_FRAME_RATE, (UINT32*)&Fps.Numerator, (UINT32*)&Fps.Denominator);
	}

	return hr;
}


HRESULT WmfMediaHAPDecoder::OnSetInputType(IMFMediaType* InMediaType)
{
	HRESULT hr = S_OK;

	hr = MFGetAttributeSize(InMediaType, MF_MT_FRAME_SIZE, &ImageWidthInPixels, &ImageHeightInPixels);

	if (SUCCEEDED(hr))
	{
		hr = MFGetAttributeRatio(InMediaType, MF_MT_FRAME_RATE, (UINT32*)&FrameRate.Numerator, (UINT32*)&FrameRate.Denominator);
	}

	if (SUCCEEDED(hr))
	{
		hr = MFFrameRateToAverageTimePerFrame(
			FrameRate.Numerator, 
			FrameRate.Denominator, 
			&SampleDuration
			);
	}

	if (SUCCEEDED(hr))
	{
		InputImageSize = ImageWidthInPixels * ImageHeightInPixels * 4;
		OutputImageSize = ImageWidthInPixels * ImageHeightInPixels * 3 / 2;

		InputType = InMediaType;
		InputType->AddRef();

		InMediaType->GetGUID(MF_MT_SUBTYPE, &InputSubType);
	}

	return hr;
}


bool WmfMediaHAPDecoder::HasPendingOutput() const
{
	return !InputQueue.IsEmpty();
}


HRESULT WmfMediaHAPDecoder::InternalProcessInput(LONGLONG InTimeStamp, BYTE* InData, DWORD InDataSize)
{
	check(InData)

	unsigned OutputTextureCount = 0;
	unsigned int Result = HapGetFrameTextureCount(InData, InDataSize, &OutputTextureCount);

	int ChunkCount = 0;
	Result = HapGetFrameTextureChunkCount(InData, InDataSize, 0, &ChunkCount);

	unsigned int ColorOutputBufferTextureFormat = 0;
	unsigned long ColorOutputBufferBytesUsed = 0;

	unsigned int AlphaOutputBufferTextureFormat = 0;
	unsigned long AlphaOutputBufferBytesUsed = 0;

	DataBuffer InputDataBuffer;

	if (!InputQueue.IsEmpty())
	{
		InputQueue.Dequeue(InputDataBuffer);
	}
	else
	{
		InputDataBuffer.Color.AddUninitialized(InputImageSize);
		if (InputSubType == DecoderGUID_HAP_Q_ALPHA)
		{
			InputDataBuffer.Alpha.AddUninitialized(InputImageSize);
		}
	}
		
	Result = HapDecode(InData, InDataSize, 0, MyHapDecodeCallback, nullptr, InputDataBuffer.Color.GetData(), InputImageSize, &ColorOutputBufferBytesUsed, &ColorOutputBufferTextureFormat);

	if (OutputTextureCount == 2 && InputSubType == DecoderGUID_HAP_Q_ALPHA)
	{
		Result = HapDecode(InData, InDataSize, 1, MyHapDecodeCallback, nullptr, InputDataBuffer.Alpha.GetData(), InputImageSize, &AlphaOutputBufferBytesUsed, &AlphaOutputBufferTextureFormat);
	}

	if (Result == HapResult_No_Error)
	{
		InputDataBuffer.TimeStamp = InTimeStamp;
		OutputQueue.Enqueue(MoveTemp(InputDataBuffer));
	}

	return S_OK;
}


bool WmfMediaHAPDecoder::InitPipeline()
{
	static const char PixelShader[] = R"SHADER(

	Texture2D TextureColor : register(t0);
	Texture2D TextureAlpha : register(t1);

	cbuffer cbPerFrameData : register(b0)
	{
		int Mode;
		int Index;
		int WriteY;
		int Padding;
	};

	SamplerState TextureSampler : register(s0);

	float4 PShader(float4 position : SV_POSITION, float2 textureUV : TEXCOORD0) : SV_TARGET
	{
		float4 ColorValue = float4(0.0, 0.0, 0.0, 0.0);

		if (Mode == 0)
		{
			float4 Value = TextureColor.Sample(TextureSampler, textureUV);
			float3x3 fMatrix = { 
				0.299, 0.587, 0.114,
				-0.14713, -0.28886, 0.436, 
				0.615, -0.51499, -0.10001 };

			ColorValue = Value;
			float3 YUV = mul(fMatrix, ColorValue.xyz);
			YUV += float3(0.06274509803921568627f, 0.5019607843137254902f, 0.5019607843137254902f);
			ColorValue.rgb = saturate(YUV);
		}
		else if (Mode == 1)
		{
			float4 Value = TextureColor.Sample(TextureSampler, textureUV);
			Value = Value * Value;
			return float4(Value.rgba);
		}		
		else
		{
			float4 offsets = float4(-0.50196078431373, -0.50196078431373, 0.0, 0.0);
			float4 CoCgSY = TextureColor.Sample(TextureSampler, textureUV);
			CoCgSY += offsets;

			float scale = ( CoCgSY.z * ( 255.0 / 8.0 ) ) + 1.0;

			float Co = CoCgSY.x / scale;
			float Cg = CoCgSY.y / scale;
			float Y = CoCgSY.w;

			float4 Value = float4(Y + Co - Cg, Y + Cg, Y - Co - Cg, 1.0);

			float3x3 fMatrix = { 
				0.299, 0.587, 0.114,
				-0.14713, -0.28886, 0.436, 
				0.615, -0.51499, -0.10001 };

			ColorValue = Value;

			if (Mode == 3)
			{
				float Alpha = TextureAlpha.Sample(TextureSampler, textureUV).r;
				return float4(ColorValue.rgb*ColorValue.rgb, Alpha);
			}

			float3 YUV = mul(fMatrix, ColorValue.xyz);
			YUV += float3(0.06274509803921568627f, 0.5019607843137254902f, 0.5019607843137254902f);
			ColorValue.rgb = saturate(YUV);
		}

		if (WriteY == 1)
		{
			return float4(ColorValue.x, 0.0, 0.0, 1.0);
		}
		else
		{
			return float4(ColorValue.y, ColorValue.z, 0.0, 1.0);
		}
	}
	)SHADER";

	static const char VertexShader[] = R"SHADER(
	struct VOut
	{
		float4 position : SV_POSITION;
		float2 textureUV : TEXCOORD0;
	};

	VOut VShader(float4 position : POSITION, float2 textureUV : TEXCOORD0)
	{
		VOut output;
		output.position = position;
		output.textureUV = textureUV;
		return output;
	}
	)SHADER";

	TComPtr<ID3DBlob> PSCode;
	TComPtr<ID3DBlob> VSCode;
	TComPtr<ID3DBlob> ErrorMsgs;

	HRESULT Result = D3DCompile(PixelShader, sizeof(PixelShader), NULL, NULL, NULL, "PShader", "ps_5_0", 0, 0, &PSCode, &ErrorMsgs);
	if (ErrorMsgs)
	{
		char *pMessage = (char*)ErrorMsgs->GetBufferPointer();
		UE_LOG(LogHAPMedia, Error, TEXT("D3DCompile Error/warning: %s"), *FString(pMessage));
		if (!PSCode.IsValid())
		{
			return false;
		}
	}

	Result = D3DCompile(VertexShader, sizeof(VertexShader), NULL, NULL, NULL, "VShader", "vs_5_0", 0, 0, &VSCode, &ErrorMsgs);
	if (ErrorMsgs)
	{
		char *pMessage = (char*)ErrorMsgs->GetBufferPointer();
		UE_LOG(LogHAPMedia, Error, TEXT("D3DCompile Error/warning: %s"), *FString(pMessage));
		if (!VSCode.IsValid())
		{
			return false;
		}
	}

	TComPtr<ID3D11PixelShader> ps;
	TComPtr<ID3D11VertexShader> vs;

	Result = D3D11Device->CreatePixelShader(PSCode->GetBufferPointer(), PSCode->GetBufferSize(), nullptr, &ps);
	if (FAILED(Result))
	{
		return false;
	}

	Result = D3D11Device->CreateVertexShader(VSCode->GetBufferPointer(), VSCode->GetBufferSize(), nullptr, &vs);
	if (FAILED(Result))
	{
		return false;
	}

	D3D11_BUFFER_DESC PixelConstantBufferDesc;
	PixelConstantBufferDesc.ByteWidth = sizeof(ConstantBuffer);
	PixelConstantBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	PixelConstantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	PixelConstantBufferDesc.CPUAccessFlags = 0;
	PixelConstantBufferDesc.MiscFlags = 0;

	Result = D3D11Device->CreateBuffer(&PixelConstantBufferDesc, nullptr, &PixelBuffer);
	if (FAILED(Result))
	{
		return false;
	}

	D3DImmediateContext->PSSetConstantBuffers(0, 1, &PixelBuffer);

	FrameData.Mode = 0;
	D3DImmediateContext->UpdateSubresource(PixelBuffer, 0, nullptr, &FrameData, 0, 0);

	D3DImmediateContext->VSSetShader(vs, 0, 0);
	D3DImmediateContext->PSSetShader(ps, 0, 0);

	D3D11_INPUT_ELEMENT_DESC InputElementDescription[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	};

	Result = D3D11Device->CreateInputLayout(InputElementDescription, 2, VSCode->GetBufferPointer(), VSCode->GetBufferSize(), &InputLayout);
	if (FAILED(Result))
	{
		return false;
	}

	D3DImmediateContext->IASetInputLayout(InputLayout);

	D3D11_BLEND_DESC BlendStateDesc;
	ZeroMemory(&BlendStateDesc, sizeof(D3D11_BLEND_DESC));
	BlendStateDesc.RenderTarget[0].BlendEnable = FALSE;
	BlendStateDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	ID3D11BlendState* BlendState = NULL;
	Result = D3D11Device->CreateBlendState(&BlendStateDesc, &BlendState);
	if (FAILED(Result))
	{
		return false;
	}

	float BlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	UINT SampleMask = 0xffffffff;
	D3DImmediateContext->OMSetBlendState(BlendState, BlendFactor, SampleMask);

	VertexDescription Vertices[] =
	{
		{-1.0f,  1.0f, 0.0f, 0.0f, 0.0f },
		{ 1.0f,  1.0f, 0.0f, 1.0f, 0.0f },
		{-1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
		{ 1.0f, -1.0f, 0.0f, 1.0f, 1.0f },
	};

	D3D11_BUFFER_DESC BufferDesc;
	ZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	BufferDesc.ByteWidth = sizeof(VertexDescription) * 4;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	Result = D3D11Device->CreateBuffer(&BufferDesc, NULL, &VertexBuffer);
	if (FAILED(Result))
	{
		return false;
	}

	D3D11_MAPPED_SUBRESOURCE MappedSubresource;
	Result = D3DImmediateContext->Map(VertexBuffer, NULL, D3D11_MAP_WRITE_DISCARD, NULL, &MappedSubresource);
	if (FAILED(Result))
	{
		return false;
	}

	memcpy(MappedSubresource.pData, Vertices, sizeof(Vertices));
	D3DImmediateContext->Unmap(VertexBuffer, NULL);

	return true;
}


void WmfMediaHAPDecoder::CreateSRV(ID3D11Texture2D* InTexture, D3D11_TEXTURE2D_DESC& InTextureDesc, ID3D11ShaderResourceView** InSRV)
{
	D3D11_SHADER_RESOURCE_VIEW_DESC ViewDesc;
	memset(&ViewDesc, 0, sizeof(ViewDesc));

	ViewDesc.Format = InTextureDesc.Format;
	ViewDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
	ViewDesc.Texture2D.MipLevels = 1;
	ViewDesc.Texture2D.MostDetailedMip = 0;

	D3D11Device->CreateShaderResourceView(InTexture, &ViewDesc, InSRV);

	D3D11_SAMPLER_DESC SamplerDesc;
	SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.MipLODBias = 0.0f;
	SamplerDesc.MaxAnisotropy = 1;
	SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD = -FLT_MAX;
	SamplerDesc.MaxLOD = FLT_MAX;

	ID3D11SamplerState* myLinearWrapSampler;
	auto hr = D3D11Device->CreateSamplerState(&SamplerDesc, &myLinearWrapSampler);

	ID3D11SamplerState *SamplerArray[2];
	SamplerArray[0] = myLinearWrapSampler;

	D3DImmediateContext->PSSetSamplers(0, 1, SamplerArray);
}


void WmfMediaHAPDecoder::BindSRV(ID3D11ShaderResourceView* InSRV)
{
	ID3D11ShaderResourceView *Array[2];
	Array[0] = InSRV;
	D3DImmediateContext->PSSetShaderResources(0, 1, Array);
}


void WmfMediaHAPDecoder::BindSRV(ID3D11ShaderResourceView* InSRVA, ID3D11ShaderResourceView* InSRVB)
{
	ID3D11ShaderResourceView *Array[2];
	Array[0] = InSRVA;
	Array[1] = InSRVB;
	D3DImmediateContext->PSSetShaderResources(0, 2, Array);
}


#include "Windows/HideWindowsPlatformTypes.h"

#endif // WMFMEDIA_SUPPORTED_PLATFORM

