// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "IPixelStreamingFrameSource.h"

class FPixelStreamingVideoInput;
class FPixelStreamingFrameAdapterProcess;
class FPixelStreamingSourceFrame;

/*
 * Wraps the output of the adapt process. Extend this for your own result types.
 * You must implement GetWidth and GetHeight to return the width and height of the
 * frame. Add your own method to extract the adapted data.
 */
class IPixelStreamingAdaptedVideoFrameLayer
{
public:
	virtual ~IPixelStreamingAdaptedVideoFrameLayer() = default;

	virtual int32 GetWidth() const = 0;
	virtual int32 GetHeight() const = 0;
};

/*
 * Takes the input frame and adapts it to the input type of the selected encoder.
 * Extend this and implement CreateAdaptProcess to adapt frames to your encoder.
 * CreateAdaptProcess should create your own processes that implements FPixelStreamingFrameAdapterProcess.
 */
class PIXELSTREAMING_API FPixelStreamingFrameAdapter : public IPixelStreamingFrameSource
{
public:
	FPixelStreamingFrameAdapter(TSharedPtr<FPixelStreamingVideoInput> VideoInput);
	FPixelStreamingFrameAdapter(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales);
	virtual ~FPixelStreamingFrameAdapter();

	/*
	 * Returns true when the adapter has frames ready to read.
	 */
	virtual bool IsReady() const override;

	/*
	 * Gets the number of layers in the adapted output.
	 */
	virtual int32 GetNumLayers() const override { return LayerAdapters.Num(); }

	/*
	 * Gets the output frame width of the given index.
	 */
	virtual int32 GetWidth(int LayerIndex) const override;

	/*
	 * Gets the output frame height of the given index.
	 */
	virtual int32 GetHeight(int LayerIndex) const override;

	/*
	 * Gets a single frame of output for the given index.
	 */
	TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> ReadOutput(int32 LayerIndex);

protected:
	/*
	 * Create the appropriate adapter process for the adapter
	 */
	virtual TSharedPtr<FPixelStreamingFrameAdapterProcess> CreateAdaptProcess(float Scale) = 0;

protected:
	TWeakPtr<FPixelStreamingVideoInput> VideoInputPtr;
	FDelegateHandle OnFrameDelegateHandle;
	TArray<float> LayerScales;
	TArray<TSharedPtr<FPixelStreamingFrameAdapterProcess>> LayerAdapters;
	mutable FCriticalSection LayersGuard;

private:
	void AddLayer(float Scale);
	void OnFrame(const FPixelStreamingSourceFrame& SourceFrame);
};
