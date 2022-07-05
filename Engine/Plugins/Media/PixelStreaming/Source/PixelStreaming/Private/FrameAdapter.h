// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingAdaptedFrameSource.h"
#include "IPixelStreamingVideoInput.h"

namespace UE::PixelStreaming
{
	/**
	 * Takes input frame data and passes it off to child adapt processes that adapts frame
	 * layers for the selected encoder. Owned by a FVideoSource which will pass this class
	 * frame data that is then passed to the adapt processes. These child adapt processes
	 * will then act as a frame source to a frame buffer. When the frame buffer gets queried
	 * for frames, it will pull from the adapt processes output.
	 */
	class FFrameAdapter : public IPixelStreamingAdaptedFrameSource, public TSharedFromThis<FFrameAdapter>
	{
	public:
		static TSharedPtr<FFrameAdapter> Create(TSharedPtr<IPixelStreamingVideoInput> InVideoInput, TArray<float> LayerScales);
		virtual ~FFrameAdapter() = default;

		// Begin IPixelStreamingAdaptedFrameSource
		virtual bool IsReady() const override;
		virtual int32 GetNumLayers() const override { return LayerAdapters.Num(); }
		virtual int32 GetWidth(int LayerIndex) const override;
		virtual int32 GetHeight(int LayerIndex) const override;
		// End IPixelStreamingAdaptedFrameSource

		void Process(const IPixelStreamingInputFrame& SourceFrame);
		TSharedPtr<IPixelStreamingAdaptedOutputFrame> ReadOutput(int32 LayerIndex);

		DECLARE_MULTICAST_DELEGATE(FOnComplete);
		FOnComplete OnComplete;

	protected:
		FFrameAdapter(TSharedPtr<IPixelStreamingVideoInput> InVideoInput, TArray<float> LayerScales);

		void AddLayer(float Scale);
		void OnLayerAdaptComplete();

		TSharedPtr<IPixelStreamingVideoInput> VideoInput;
		TArray<float> LayerScales;
		TArray<TSharedPtr<FPixelStreamingFrameAdapterProcess>> LayerAdapters;
		mutable FCriticalSection LayersGuard;
		int PendingLayers = 0;
	};
} // namespace UE::PixelStreaming
