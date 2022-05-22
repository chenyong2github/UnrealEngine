// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/TripleBuffer.h"
#include "RHI.h"

class FPixelStreamingSourceFrame;
class IPixelStreamingAdaptedVideoFrameLayer;

/*
 * Extend this class to add your own adaption/conversion logic for incoming frames
 * to adapt the data to the type your selected encoder requires.
 * Implement Consume with your logic to convert the frame data.
 * This is implemented as a parallel process. Call OnBegin to mark this process as busy
 * and call OnComplete once the work is complete.
 */
class PIXELSTREAMING_API FPixelStreamingFrameAdapterProcess : public TSharedFromThis<FPixelStreamingFrameAdapterProcess>
{
public:
	FPixelStreamingFrameAdapterProcess() = default;
	virtual ~FPixelStreamingFrameAdapterProcess() = default;

	/*
	 * Called when an input frame needs processing.
	 */
	void Process(const FPixelStreamingSourceFrame& SourceFrame);

	/*
	 * Returns true if Initialize() has been called.
	 * Output data can depend on the incoming frames so we do lazy initialization when we first consume.
	 */
	bool IsInitialized() const { return bInitialized; }

	/*
	 * Returns true when this process is actively working on adapting frame data.
	 */
	bool IsBusy() const { return bBusy; }

	/*
	 * Returns true if this process has a frame in the output buffer ready to be read.
	 */
	bool HasOutput() const { return bHasOutput; }

	/*
	 * Gets the output frame from the output buffer.
	 */
	TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> ReadOutput();

	/*
	 * Gets the width of the output in pixels.
	 */
	int32 GetOutputLayerWidth();

	/*
	 * Gets the height of the output in pixels.
	 */
	int32 GetOutputLayerHeight();

protected:
	/*
	 * Initializes the process to be ready for work. Called once at startup and any time the
	 * source resolution changes.
	 * SourceWidth and SourceHeight are the pixel dimensions of the frame to work on.
	 */
	virtual void Initialize(int32 SourceWidth, int32 SourceHeight);

	/*
	 * A callback that can be implemented if work needs to be done when the source
	 * resolution changes.
	 */
	virtual void OnSourceResolutionChanged(int32 OldWidth, int32 OldHeight, int32 NewWidth, int32 NewHeight) {}

	/*
	 * Implement this to create a buffer for the output.
	 */
	virtual TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> CreateOutputBuffer(int32 SourceWidth, int32 SourceHeight) = 0;

	/*
	 * Gets a buffer ready for writing to for the output.
	 */
	TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> GetWriteBuffer();

	/*
	 * Implement this with your specific process to adapt the incoming frame.
	 */
	virtual void BeginProcess(const FPixelStreamingSourceFrame& SourceFrame) = 0;

	/*
	 * Call this to mark the end of processing. Will commit the current write buffer into the read buffer.
	 */
	void EndProcess();

private:
	bool bInitialized = false;
	bool bBusy = false;
	bool bHasOutput = false;

	int32 ExpectedSourceWidth = 0;
	int32 ExpectedSourceHeight = 0;

	uint64 LastWriteSwap = 0;
	uint64 LastReadSwap = 0;

	TTripleBuffer<TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer>> Buffer;

	bool ResolutionChanged(int32 SourceWidth, int32 SourceHeight) const;
};
