// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IImgMediaReader.h"

class FRgbaInputFile;
class IImageWrapperModule;


/**
 * Implements a reader for various image sequence formats.
 */
class FGenericImgMediaReader
	: public IImgMediaReader
{
public:

	/**
	 * Create and initialize a new instance.
	 *
	 * @param InImageWrapperModule The image wrapper module to use.
	 */
	FGenericImgMediaReader(IImageWrapperModule& InImageWrapperModule);

	/** Virtual destructor. */
	virtual ~FGenericImgMediaReader() { }

public:

	//~ IImgMediaReader interface

	virtual bool GetFrameInfo(const FString& ImagePath, FImgMediaFrameInfo& OutInfo) override;
	virtual bool ReadFrame(const FString& ImagePath, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame, int32 FrameId) override;
	virtual void CancelFrame(int32 FrameNumber) override {};

private:

	/** The image wrapper module. */
	IImageWrapperModule& ImageWrapperModule;
};
