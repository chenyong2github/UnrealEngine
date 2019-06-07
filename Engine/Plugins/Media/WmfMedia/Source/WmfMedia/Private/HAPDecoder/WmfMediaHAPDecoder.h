// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Queue.h"

#include "WmfMediaPrivate.h"

#if WMFMEDIA_SUPPORTED_PLATFORM

#if HAP_SUPPORTED

struct ID3D11DeviceContext;
struct ID3D11Device;
struct ID3D11Texture2D;
struct D3D11_TEXTURE2D_DESC;
struct ID3D11InputLayout;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;

struct FWorkTextures
{
	TComPtr<ID3D11Texture2D> InputTextureColor;
	TComPtr<ID3D11Texture2D> InputTextureAlpha;
	TComPtr<ID3D11ShaderResourceView> SRV_InputColor;
	TComPtr<ID3D11ShaderResourceView> SRV_InputAlpha;
};

struct FOutputTextures
{
	TComPtr<ID3D11Texture2D> Texture;
	TComPtr<ID3D11RenderTargetView> RTV_A;
	TComPtr<ID3D11RenderTargetView> RTV_B;
};

struct VertexDescription
{ 
	float X, Y, Z; 
	float U, V; 
};

struct ConstantBuffer
{
	int Mode;
	int Index;
	int WriteY;
	int Padding;
};

struct DataBuffer
{
	TArray<unsigned char> Color;
	TArray<unsigned char> Alpha;
	LONGLONG TimeStamp;
};

class WmfMediaHAPDecoder 
	: public IMFTransform
{

public:

public:

	WmfMediaHAPDecoder();
	virtual ~WmfMediaHAPDecoder();
	
	STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	STDMETHODIMP GetStreamLimits(DWORD* pdwInputMinimum, DWORD* pdwInputMaximum, DWORD* pdwOutputMinimum, DWORD* pdwOutputMaximum);
	STDMETHODIMP GetStreamCount(DWORD* pcInputStreams, DWORD* pcOutputStreams);
	STDMETHODIMP GetStreamIDs(DWORD dwInputIDArraySize, DWORD* pdwInputIDs, DWORD dwOutputIDArraySize, DWORD* pdwOutputIDs);
	STDMETHODIMP GetInputStreamInfo(DWORD dwInputStreamID, MFT_INPUT_STREAM_INFO* pStreamInfo);
	STDMETHODIMP GetOutputStreamInfo(DWORD dwOutputStreamID, MFT_OUTPUT_STREAM_INFO* pStreamInfo);
	STDMETHODIMP GetAttributes(IMFAttributes** pAttributes);
	STDMETHODIMP GetInputStreamAttributes(DWORD dwInputStreamID, IMFAttributes** ppAttributes);
	STDMETHODIMP GetOutputStreamAttributes(DWORD dwOutputStreamID, IMFAttributes **ppAttributes);
	STDMETHODIMP DeleteInputStream(DWORD dwStreamID);

	STDMETHODIMP AddInputStreams(DWORD cStreams, DWORD* adwStreamIDs);
	STDMETHODIMP GetInputAvailableType(DWORD dwInputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType);
	STDMETHODIMP GetOutputAvailableType(DWORD dwOutputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType);

	STDMETHODIMP SetInputType(DWORD dwInputStreamID, IMFMediaType* pType, DWORD dwFlags);
	STDMETHODIMP SetOutputType(DWORD dwOutputStreamID, IMFMediaType* pType, DWORD dwFlags);

	STDMETHODIMP GetInputCurrentType(DWORD dwInputStreamID, IMFMediaType** ppType);
	STDMETHODIMP GetOutputCurrentType(DWORD dwOutputStreamID, IMFMediaType** ppType);

	STDMETHODIMP GetInputStatus(DWORD dwInputStreamID, DWORD* pdwFlags);
	STDMETHODIMP GetOutputStatus(DWORD* pdwFlags);

	STDMETHODIMP SetOutputBounds(LONGLONG hnsLowerBound, LONGLONG hnsUpperBound);

	STDMETHODIMP ProcessEvent(DWORD dwInputStreamID, IMFMediaEvent* pEvent);

	STDMETHODIMP ProcessMessage(MFT_MESSAGE_TYPE eMessage, ULONG_PTR ulParam);
 
	STDMETHODIMP ProcessInput(DWORD dwInputStreamID, IMFSample* pSample, DWORD dwFlags);
	STDMETHODIMP ProcessOutput(DWORD dwFlags, DWORD cOutputBufferCount, MFT_OUTPUT_DATA_BUFFER* pOutputSamples, DWORD* pdwStatus);

private:

	HRESULT InternalProcessOutput(IMFSample *InSample);
	HRESULT InternalProcessInput(LONGLONG InTimeStamp, BYTE* InData, DWORD InDataSize);
	HRESULT OnCheckInputType(IMFMediaType *InMediaType);
	HRESULT OnCheckOutputType(IMFMediaType *InMediaType);
	HRESULT OnSetInputType(IMFMediaType *InMediaType);
	HRESULT OnSetOutputType(IMFMediaType *InMediaType);
	HRESULT OnDiscontinuity();
	HRESULT OnFlush();

	void EmplyQueues();
	bool InitPipeline(void);
	void CreateSRV(ID3D11Texture2D* InTexture, D3D11_TEXTURE2D_DESC& InTextureDesc, ID3D11ShaderResourceView** InSRV);
	void BindSRV(ID3D11ShaderResourceView* InSRV);
	void BindSRV(ID3D11ShaderResourceView* InSRVA, ID3D11ShaderResourceView* InSRVB);
	bool HasPendingOutput() const;

protected:

	int32 RefCount;
	
	FCriticalSection CriticalSection;

	TComPtr<IMFMediaType> InputType;
	TComPtr<IMFMediaType> OutputType;

	UINT32 ImageWidthInPixels;
	UINT32 ImageHeightInPixels;
	MFRatio FrameRate;
	DWORD InputImageSize;
	DWORD OutputImageSize;
	GUID InputSubType;

	TComPtr<IMFDXGIDeviceManager> DXGIManager;
	TComPtr<ID3D11Device> D3D11Device;
	TComPtr<ID3D11DeviceContext> D3DImmediateContext;

	TComPtr<ID3D11InputLayout> InputLayout;
	TComPtr<ID3D11Buffer> VertexBuffer;
	TComPtr<ID3D11Buffer> PixelBuffer;

	ConstantBuffer FrameData;

	TQueue<DataBuffer> InputQueue;
	TQueue<DataBuffer> OutputQueue;

	TUniquePtr<FOutputTextures> OutputTexture;
	TUniquePtr<FWorkTextures> WorkTextures;

	LONGLONG InternalTimeStamp;
	UINT64 SampleDuration;
};

#endif // HAP_SUPPORTED

#endif // WMFMEDIA_SUPPORTED_PLATFORM
