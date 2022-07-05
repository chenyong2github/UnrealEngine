// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingInputFrame.h"
#include "IPixelStreamingAdaptedOutputFrame.h"
#include "Containers/TripleBuffer.h"

/**
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

	/**
	 * Called when an input frame needs processing.
	 * @param InputFrame The input frame to be processed.
	 */
	void Process(const IPixelStreamingInputFrame& InputFrame);

	/**
	 * Returns true if Initialize() has been called.
	 * Output data can depend on the incoming frames so we do lazy initialization when we first consume.
	 * @return True if this process has been initialized correctly.
	 */
	bool IsInitialized() const { return bInitialized; }

	/**
	 * Returns true when this process is actively working on adapting frame data.
	 * @return True when this process is busy.
	 */
	bool IsBusy() const { return bBusy; }

	/**
	 * Returns true if this process has a frame in the output buffer ready to be read.
	 * @return True when this process has output data.
	 */
	bool HasOutput() const { return bHasOutput; }

	/**
	 * Gets the output frame from the output buffer.
	 * @return The output data of this process.
	 */
	TSharedPtr<IPixelStreamingAdaptedOutputFrame> ReadOutput();

	/**
	 * Gets the width of the output in pixels.
	 * @return The pixel width of the output frame.
	 */
	int32 GetOutputLayerWidth();

	/**
	 * Gets the height of the output in pixels.
	 * @return The pixel height of the output frame.
	 */
	int32 GetOutputLayerHeight();

	DECLARE_MULTICAST_DELEGATE(FOnComplete);
	FOnComplete OnComplete;

protected:
	/**
	 * Gets the human readable name for this adapt process. This name will be used in stats readouts so the shorter
	 * the better.
	 * @return A human readable name for this adapt process.
	 */
	virtual FString GetProcessName() const = 0;

	/**
	 * Initializes the process to be ready for work. Called once at startup and any time the
	 * source resolution changes.
	 * @param InputWidth The pixel count of the input frame width
	 * @param InputHeight The pixel count of the input frame height
	 */
	virtual void Initialize(int32 InputWidth, int32 InputHeight);

	/**
	 * Implement this to create a buffer for the output.
	 * @param InputWidth The pixel width of the input frame.
	 * @param InputHeight The pixel height of the input frame.
	 * @return An empty output structure that the process can store the output of its process on.
	 */
	virtual TSharedPtr<IPixelStreamingAdaptedOutputFrame> CreateOutputBuffer(int32 InputWidth, int32 InputHeight) = 0;

	/**
	 * Implement this with your specific process to adapt the incoming frame.
	 * @param InputFrame The input frame data for the process to begin working on.
	 * @param OutputBuffer The destination buffer for the process. Is guaranteed to be of the type created in CreateOutputBuffer()
	 */
	virtual void BeginProcess(const IPixelStreamingInputFrame& InputFrame, TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer) = 0;

	/**
	 * Marks when the processing for this adapt process has started. Marks timestamps in
	 * output metadata.
	 */
	void MarkAdaptProcessStarted();

	/**
	 * Marks when the adapt process starts finalizing. Usually used to mark the point where GPU operations have
	 * Completed. Marks timestamps in output metadata.
	 */
	void MarkAdaptProcessFinalizing();

	/**
	 * Call this to mark the end of processing. Will commit the current write buffer into the read buffer.
	 */
	void EndProcess();

private:
	bool bInitialized = false;
	bool bBusy = false;
	bool bHasOutput = false;
	bool bWasFinalized = false;

	int32 ExpectedInputWidth = 0;
	int32 ExpectedInputHeight = 0;

	TTripleBuffer<TSharedPtr<IPixelStreamingAdaptedOutputFrame>> Buffer;
};
