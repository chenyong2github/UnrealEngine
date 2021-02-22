// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImgMediaMipMapInfo.h"
#include "Misc/IQueuedWork.h"

class FImgMediaLoader;
class IImgMediaReader;

struct FImgMediaFrame;


/**
 * Loads a single image frame from disk.
 */
class FImgMediaLoaderWork
	: public IQueuedWork
{
public:

	/**
	 * Create and initialize a new instance.
	 *
	 * @param InOwner The loader that created this work item.
	 * @param InReader The image reader to use.
	 */
	FImgMediaLoaderWork(const TSharedRef<FImgMediaLoader, ESPMode::ThreadSafe>& InOwner, const TSharedRef<IImgMediaReader, ESPMode::ThreadSafe>& InReader);

public:

	/**
	 * Initialize this work item.
	 *
	 * @param InFrameNumber The number of the image frame.
	 * @param InMipLevel Will read in this level and all higher levels.
	 * @param InTileSelection Which tiles to read.
	 * @param InExistingFrame If set, then use this frame to load into.
	 * @see Shutdown
	 */
	void Initialize(int32 InFrameNumber, int32 InMipLevel, const FImgMediaTileSelection& InTileSelection, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> InExistingFrame);

public:

	//~ IQueuedWork interface

	virtual void Abandon() override;
	virtual void DoThreadedWork() override;

protected:

	/**
	 * Notify the owner of this work item, or self destruct.
	 *
	 * @param Frame The frame that was loaded by this work item.
	 */
	void Finalize(TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> Frame);

private:

	/** The number of the image frame. */
	int32 FrameNumber;

	/** Mip level of the image frame. */
	int32 MipLevel;

	/** Which tiles we should load. */
	FImgMediaTileSelection TileSelection;
	
	/** If set, then load the image into this frame instead of making a new one. */
	TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> ExistingFrame;

	/** The loader that created this reader task. */
	TWeakPtr<FImgMediaLoader, ESPMode::ThreadSafe> OwnerPtr;

	/** The image sequence reader to use. */
	TSharedPtr<IImgMediaReader, ESPMode::ThreadSafe> Reader;
};
