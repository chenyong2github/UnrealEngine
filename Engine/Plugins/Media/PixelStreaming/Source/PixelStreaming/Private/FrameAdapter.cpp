// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAdapter.h"
#include "PixelStreamingFrameAdapterProcess.h"
#include "Settings.h"

namespace UE::PixelStreaming
{
	TSharedPtr<FFrameAdapter> FFrameAdapter::Create(TSharedPtr<IPixelStreamingVideoInput> InVideoInput, TArray<float> LayerScales)
	{
		return TSharedPtr<FFrameAdapter>(new FFrameAdapter(InVideoInput, LayerScales));
	}

	FFrameAdapter::FFrameAdapter(TSharedPtr<IPixelStreamingVideoInput> InVideoInput, TArray<float> InLayerScales)
		: VideoInput(InVideoInput)
		, LayerScales(InLayerScales)
	{
	}

	bool FFrameAdapter::IsReady() const
	{
		FScopeLock LayersLock(&LayersGuard);

		for (auto& LayerAdapter : LayerAdapters)
		{
			if (!LayerAdapter->HasOutput())
			{
				return false;
			}
		}
		return !LayerAdapters.IsEmpty();
	}

	int32 FFrameAdapter::GetWidth(int LayerIndex) const
	{
		FScopeLock LayersLock(&LayersGuard);
		return LayerAdapters[LayerIndex]->GetOutputLayerWidth();
	}

	int32 FFrameAdapter::GetHeight(int LayerIndex) const
	{
		FScopeLock LayersLock(&LayersGuard);
		return LayerAdapters[LayerIndex]->GetOutputLayerHeight();
	}

	TSharedPtr<IPixelStreamingAdaptedOutputFrame> FFrameAdapter::ReadOutput(int32 LayerIndex)
	{
		FScopeLock LayersLock(&LayersGuard);
		return LayerAdapters[LayerIndex]->ReadOutput();
	}

	void FFrameAdapter::AddLayer(float Scale)
	{
		EPixelStreamingFrameBufferFormat RequestFormat = EPixelStreamingFrameBufferFormat::Unknown;
		switch (Settings::GetSelectedCodec())
		{
			case EPixelStreamingCodec::VP8:
			case EPixelStreamingCodec::VP9:
				RequestFormat = EPixelStreamingFrameBufferFormat::IYUV420;
				break;
			case EPixelStreamingCodec::H264:
				RequestFormat = EPixelStreamingFrameBufferFormat::RHITexture;
				break;
		}
		TSharedPtr<FPixelStreamingFrameAdapterProcess> AdaptProcess = VideoInput->CreateAdaptProcess(RequestFormat, Scale);
		AdaptProcess->OnComplete.AddSP(AsShared(), &FFrameAdapter::OnLayerAdaptComplete);
		LayerAdapters.Add(AdaptProcess);
	}

	void FFrameAdapter::OnLayerAdaptComplete()
	{
		--PendingLayers;
		if (PendingLayers == 0)
		{
			OnComplete.Broadcast();
		}
	}

	void FFrameAdapter::Process(const IPixelStreamingInputFrame& SourceFrame)
	{
		if (PendingLayers > 0)
		{
			// still adapting previous frame. drop new one.
			return;
		}
		
		FScopeLock LayersLock(&LayersGuard);

		// initial setup. Should only be called on the first frame.
		// We do this lazily because CreateAdaptProcess is a pure virtual function
		// so we cannot call it in our constructor. Another option would be to
		// require the user to call a SetupLayers method or something but that
		// could be prone to errors.
		if (LayerAdapters.IsEmpty())
		{
			for (auto& Scale : LayerScales)
			{
				AddLayer(Scale);
			}
		}

		// set this before calling process since the process might immediately complete
		PendingLayers = LayerAdapters.Num();

		// adapt the frame for encoder use
		for (auto& LayerAdapter : LayerAdapters)
		{
			LayerAdapter->Process(SourceFrame);
		}
	}
} // namespace UE::PixelStreaming
