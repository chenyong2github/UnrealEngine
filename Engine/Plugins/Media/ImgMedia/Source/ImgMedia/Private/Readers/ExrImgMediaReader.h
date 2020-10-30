// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImgMediaPrivate.h"

#if IMGMEDIA_EXR_SUPPORTED_PLATFORM

#include "IImgMediaReader.h"

class FRgbaInputFile;


/**
 * Implements a reader for EXR image sequences.
 */
class FExrImgMediaReader
	: public IImgMediaReader
{
public:

	/** Default constructor. */
	FExrImgMediaReader();
	virtual ~FExrImgMediaReader();
public:

	//~ IImgMediaReader interface

	virtual bool GetFrameInfo(const FString& ImagePath, FImgMediaFrameInfo& OutInfo) override;
	virtual bool ReadFrame(const FString& ImagePath, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame, int32 FrameId) override;
	virtual void CancelFrame(int32 FrameNumber) override;

public:
	/** Gets reader type (GPU vs CPU) depending on size of EXR and its compression. */
	static TSharedPtr<IImgMediaReader, ESPMode::ThreadSafe> GetReader(FString FirstImageInSequencePath);

protected:

	/**
	 * Get the frame information from the given input file.
	 *
	 * @param InputFile The input file containing the frame.
	 * @param OutInfo Will contain the frame information.
	 * @return true on success, false otherwise.
	 */
	static bool GetInfo(FRgbaInputFile& InputFile, FImgMediaFrameInfo& OutInfo);

protected:
	TSet<int32> CanceledFrames;
	FCriticalSection CanceledFramesCriticalSection;
	TMap<int32, FRgbaInputFile*> PendingFrames;
};


#endif //IMGMEDIA_EXR_SUPPORTED_PLATFORM
