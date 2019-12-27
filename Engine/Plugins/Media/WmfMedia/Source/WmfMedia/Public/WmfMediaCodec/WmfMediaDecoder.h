// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WmfMediaCommon.h"

#include "Containers/Queue.h"

#if WMFMEDIA_SUPPORTED_PLATFORM

struct ID3D11DeviceContext;
struct ID3D11Device;

class WMFMEDIA_API WmfMediaDecoder : public IMFTransform
{
public:

	struct DataBuffer
	{
		TArray<unsigned char> Color;
		TArray<unsigned char> Alpha;
		LONGLONG TimeStamp;
	};

public:
	WmfMediaDecoder();
	virtual ~WmfMediaDecoder() = default;

	static GUID GetMajorType() { return MFMediaType_Video; }

	virtual ULONG STDMETHODCALLTYPE AddRef() override;
	virtual ULONG STDMETHODCALLTYPE Release() override;
	virtual HRESULT STDMETHODCALLTYPE GetStreamLimits(DWORD* pdwInputMinimum, DWORD* pdwInputMaximum, DWORD* pdwOutputMinimum, DWORD* pdwOutputMaximum) override;
	virtual HRESULT STDMETHODCALLTYPE GetStreamCount(DWORD* pcInputStreams, DWORD* pcOutputStreams) override;
	virtual HRESULT STDMETHODCALLTYPE GetStreamIDs(DWORD dwInputIDArraySize, DWORD* pdwInputIDs, DWORD dwOutputIDArraySize, DWORD* pdwOutputIDs) override;
	virtual HRESULT STDMETHODCALLTYPE GetInputStreamInfo(DWORD dwInputStreamID, MFT_INPUT_STREAM_INFO* pStreamInfo) override;
	virtual HRESULT STDMETHODCALLTYPE GetOutputStreamInfo(DWORD dwOutputStreamID, MFT_OUTPUT_STREAM_INFO* pStreamInfo) override;
	virtual HRESULT STDMETHODCALLTYPE GetAttributes(IMFAttributes** pAttributes) override;
	virtual HRESULT STDMETHODCALLTYPE GetInputStreamAttributes(DWORD dwInputStreamID, IMFAttributes** ppAttributes) override;
	virtual HRESULT STDMETHODCALLTYPE GetOutputStreamAttributes(DWORD dwOutputStreamID, IMFAttributes **ppAttributes) override;
	virtual HRESULT STDMETHODCALLTYPE DeleteInputStream(DWORD dwStreamID) override;
	virtual HRESULT STDMETHODCALLTYPE AddInputStreams(DWORD cStreams, DWORD* adwStreamIDs) override;
	virtual HRESULT STDMETHODCALLTYPE GetInputAvailableType(DWORD dwInputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType) override;
	virtual HRESULT STDMETHODCALLTYPE SetInputType(DWORD dwInputStreamID, IMFMediaType* pType, DWORD dwFlags) override;
	virtual HRESULT STDMETHODCALLTYPE SetOutputType(DWORD dwOutputStreamID, IMFMediaType* pType, DWORD dwFlags) override;
	virtual HRESULT STDMETHODCALLTYPE GetInputCurrentType(DWORD dwInputStreamID, IMFMediaType** ppType) override;
	virtual HRESULT STDMETHODCALLTYPE GetOutputCurrentType(DWORD dwOutputStreamID, IMFMediaType** ppType) override;
	virtual HRESULT STDMETHODCALLTYPE GetInputStatus(DWORD dwInputStreamID, DWORD* pdwFlags) override;
	virtual HRESULT STDMETHODCALLTYPE GetOutputStatus(DWORD* pdwFlags) override;
	virtual HRESULT STDMETHODCALLTYPE SetOutputBounds(LONGLONG hnsLowerBound, LONGLONG hnsUpperBound) override;
	virtual HRESULT STDMETHODCALLTYPE ProcessEvent(DWORD dwInputStreamID, IMFMediaEvent* pEvent) override;
	virtual HRESULT STDMETHODCALLTYPE ProcessInput(DWORD dwInputStreamID, IMFSample* pSample, DWORD dwFlags) override;

	virtual HRESULT OnSetOutputType(IMFMediaType *InMediaType);
	virtual HRESULT OnFlush();
	virtual HRESULT OnDiscontinuity();

private:

	virtual HRESULT OnCheckInputType(IMFMediaType *InMediaType) = 0;
	virtual HRESULT OnSetInputType(IMFMediaType* InMediaType) = 0;
	virtual bool HasPendingOutput() const = 0;
	virtual HRESULT InternalProcessInput(LONGLONG InTimeStamp, BYTE* InData, DWORD InDataSize) = 0;

	void EmplyQueues();
	HRESULT OnCheckOutputType(IMFMediaType *InMediaType);

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

	TComPtr<IMFDXGIDeviceManager> DXGIManager;
	TComPtr<ID3D11Device> D3D11Device;
	TComPtr<ID3D11DeviceContext> D3DImmediateContext;

	LONGLONG InternalTimeStamp;
	UINT64 SampleDuration;

	TQueue<DataBuffer> InputQueue;
	TQueue<DataBuffer> OutputQueue;
};

#endif // WMFMEDIA_SUPPORTED_PLATFORM