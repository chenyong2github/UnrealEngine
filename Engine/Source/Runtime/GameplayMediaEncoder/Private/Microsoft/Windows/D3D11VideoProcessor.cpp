// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "D3D11VideoProcessor.h"

GAMEPLAYMEDIAENCODER_START

CSV_DECLARE_CATEGORY_EXTERN(GameplayMediaEncoder);

bool FD3D11VideoProcessor::Initialize(uint32 Width, uint32 Height)
{
	CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, D3D11VideoProcessor_Initialize);

	ID3D11Device* DX11Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
	check(DX11Device);
	ID3D11DeviceContext* DX11DeviceContext = nullptr;
	DX11Device->GetImmediateContext(&DX11DeviceContext);
	check(DX11DeviceContext);

	CHECK_HR(DX11Device->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&VideoDevice));
	CHECK_HR(DX11DeviceContext->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&VideoContext));

	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc =
	{
		D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
		{ 1, 1 }, Width, Height,
		{ 1, 1 }, Width, Height,
		D3D11_VIDEO_USAGE_PLAYBACK_NORMAL
	};
	CHECK_HR(VideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, VideoProcessorEnumerator.GetInitReference()));
	CHECK_HR(VideoDevice->CreateVideoProcessor(VideoProcessorEnumerator, 0, VideoProcessor.GetInitReference()));

	return true;
}

bool FD3D11VideoProcessor::ConvertTexture(const FTexture2DRHIRef& InTexture, const FTexture2DRHIRef& OutTexture)
{
	CSV_SCOPED_TIMING_STAT(GameplayMediaEncoder, D3D11VideoProcessor_ConvertTexture);

	// TODO: clean up dead entries of InputViews and OutputViews which happen after resolutions change
	ID3D11Texture2D* InTextureDX11 = (ID3D11Texture2D*)(GetD3D11TextureFromRHITexture(InTexture)->GetResource());
	TRefCountPtr<ID3D11VideoProcessorInputView>& InputView = InputViews.FindOrAdd(InTextureDX11);
	if (!InputView)
	{
		D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC InputViewDesc = { 0, D3D11_VPIV_DIMENSION_TEXTURE2D, { 0, 0 } };
		CHECK_HR(VideoDevice->CreateVideoProcessorInputView(InTextureDX11, VideoProcessorEnumerator, &InputViewDesc, InputView.GetInitReference()));
	}

	ID3D11Texture2D* OutTextureDX11 = (ID3D11Texture2D*)(GetD3D11TextureFromRHITexture(OutTexture)->GetResource());
	TRefCountPtr<ID3D11VideoProcessorOutputView>& OutputView = OutputViews.FindOrAdd(OutTextureDX11);
	if (!OutputView)
	{
		D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputViewDesc = { D3D11_VPOV_DIMENSION_TEXTURE2D };
		CHECK_HR(VideoDevice->CreateVideoProcessorOutputView(OutTextureDX11, VideoProcessorEnumerator, &OutputViewDesc, OutputView.GetInitReference()));
	}

	D3D11_VIDEO_PROCESSOR_STREAM Stream = { true, 0, 0, 0, 0, nullptr, InputView, nullptr };
	CHECK_HR(VideoContext->VideoProcessorBlt(VideoProcessor, OutputView, 0, 1, &Stream));
	return true;
}

GAMEPLAYMEDIAENCODER_END

