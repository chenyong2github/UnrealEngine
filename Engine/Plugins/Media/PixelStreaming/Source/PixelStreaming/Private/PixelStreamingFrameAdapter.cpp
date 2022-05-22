// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingFrameAdapter.h"
#include "PixelStreamingFrameAdapterProcess.h"
#include "PixelStreamingVideoInput.h"

FPixelStreamingFrameAdapter::FPixelStreamingFrameAdapter(TSharedPtr<FPixelStreamingVideoInput> VideoInput)
	: VideoInputPtr(VideoInput)
{
	LayerScales.Add(1.0f);
	OnFrameDelegateHandle = VideoInput->OnFrame.AddRaw(this, &FPixelStreamingFrameAdapter::OnFrame);
}

FPixelStreamingFrameAdapter::FPixelStreamingFrameAdapter(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> InLayerScales)
	: VideoInputPtr(VideoInput)
	, LayerScales(InLayerScales)
{
	OnFrameDelegateHandle = VideoInput->OnFrame.AddRaw(this, &FPixelStreamingFrameAdapter::OnFrame);
}

FPixelStreamingFrameAdapter::~FPixelStreamingFrameAdapter()
{
	if (TSharedPtr<FPixelStreamingVideoInput> VideoInput = VideoInputPtr.Pin())
	{
		VideoInput->OnFrame.Remove(OnFrameDelegateHandle);
	}
}

bool FPixelStreamingFrameAdapter::IsReady() const
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

int32 FPixelStreamingFrameAdapter::GetWidth(int LayerIndex) const
{
	FScopeLock LayersLock(&LayersGuard);
	return LayerAdapters[LayerIndex]->GetOutputLayerWidth();
}

int32 FPixelStreamingFrameAdapter::GetHeight(int LayerIndex) const
{
	FScopeLock LayersLock(&LayersGuard);
	return LayerAdapters[LayerIndex]->GetOutputLayerHeight();
}

TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> FPixelStreamingFrameAdapter::ReadOutput(int32 LayerIndex)
{
	FScopeLock LayersLock(&LayersGuard);
	return LayerAdapters[LayerIndex]->ReadOutput();
}

void FPixelStreamingFrameAdapter::AddLayer(float Scale)
{
	LayerAdapters.Add(CreateAdaptProcess(Scale));
}

void FPixelStreamingFrameAdapter::OnFrame(const FPixelStreamingSourceFrame& SourceFrame)
{
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

	// adapt the frame for encoder use
	for (auto& LayerAdapter : LayerAdapters)
	{
		LayerAdapter->Process(SourceFrame);
	}
}
