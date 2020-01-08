// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WmfMediaCodec/WmfMediaDecoder.h"

#if WMFMEDIA_SUPPORTED_PLATFORM

struct ID3D11Texture2D;
struct D3D11_TEXTURE2D_DESC;
struct ID3D11InputLayout;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;

class WmfMediaHAPDecoder 
	: public WmfMediaDecoder
{

public:

	WmfMediaHAPDecoder();
	virtual ~WmfMediaHAPDecoder();
	
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override;
	virtual HRESULT STDMETHODCALLTYPE GetOutputAvailableType(DWORD dwOutputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType) override;
	virtual HRESULT STDMETHODCALLTYPE ProcessMessage(MFT_MESSAGE_TYPE eMessage, ULONG_PTR ulParam) override;
	virtual HRESULT STDMETHODCALLTYPE ProcessOutput(DWORD dwFlags, DWORD cOutputBufferCount, MFT_OUTPUT_DATA_BUFFER* pOutputSamples, DWORD* pdwStatus) override;

	static bool IsSupported(const GUID& InGuid);
	static bool SetOutputFormat(const GUID& InGuid, GUID& OutVideoFormat);

private:

	virtual  bool HasPendingOutput() const override;

	HRESULT InternalProcessOutput(IMFSample *InSample);
	virtual HRESULT InternalProcessInput(LONGLONG InTimeStamp, BYTE* InData, DWORD InDataSize) override;
	virtual HRESULT OnCheckInputType(IMFMediaType *InMediaType) override;
	virtual HRESULT OnSetInputType(IMFMediaType *InMediaType) override;

	bool InitPipeline(void);
	void CreateSRV(ID3D11Texture2D* InTexture, D3D11_TEXTURE2D_DESC& InTextureDesc, ID3D11ShaderResourceView** InSRV);
	void BindSRV(ID3D11ShaderResourceView* InSRV);
	void BindSRV(ID3D11ShaderResourceView* InSRVA, ID3D11ShaderResourceView* InSRVB);

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


	GUID InputSubType;
	TComPtr<ID3D11InputLayout> InputLayout;
	TComPtr<ID3D11Buffer> VertexBuffer;
	TComPtr<ID3D11Buffer> PixelBuffer;

	ConstantBuffer FrameData;

	TUniquePtr<FOutputTextures> OutputTexture;
	TUniquePtr<FWorkTextures> WorkTextures;

};

#endif // WMFMEDIA_SUPPORTED_PLATFORM
