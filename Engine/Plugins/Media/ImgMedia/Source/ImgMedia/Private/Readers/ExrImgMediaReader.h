// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImgMediaPrivate.h"

#if IMGMEDIA_EXR_SUPPORTED_PLATFORM

#include "IImgMediaReader.h"

class FImgMediaLoader;
class FOpenExrHeaderReader;
class FRgbaInputFile;


/**
 * Implements a reader for EXR image sequences.
 */
class FExrImgMediaReader
	: public IImgMediaReader
{
public:

	/** Default constructor. */
	FExrImgMediaReader(const TSharedRef<FImgMediaLoader, ESPMode::ThreadSafe>& InLoader);
	virtual ~FExrImgMediaReader();
public:

	//~ IImgMediaReader interface

	virtual bool GetFrameInfo(const FString& ImagePath, FImgMediaFrameInfo& OutInfo) override;
	virtual bool ReadFrame(int32 FrameId, const TMap<int32, FImgMediaTileSelection>& InMipTiles, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame) override;
	virtual void CancelFrame(int32 FrameNumber) override;

public:
	/** Gets reader type (GPU vs CPU) depending on size of EXR and its compression. */
	static TSharedPtr<IImgMediaReader, ESPMode::ThreadSafe> GetReader(const TSharedRef<FImgMediaLoader, ESPMode::ThreadSafe>& InLoader, FString FirstImageInSequencePath);

	/** Query if our images are in our custom format. */
	bool IsCustomFormat() const { return bIsCustomFormat; }
	/** Gets the tile size of our custom format. */
	FIntPoint GetCustomFormatTileSize() { return CustomFormatTileSize; }

protected:

	/**
	 * Get the frame information from the given input file.
	 *
	 * @param InputFile The input file containing the frame.
	 * @param OutInfo Will contain the frame information.
	 * @return true on success, false otherwise.
	 */
	static bool GetInfo(const FString& FilePath, FImgMediaFrameInfo& OutInfo);

	/**
	 * Sets parameters of our custom format images.
	 *
	 * @param bInIsCustomFormat		True if we are using custom format images.
	 * @param TileSize				Size of our tiles. If (0, 0) then we are not using tiles.
	 */
	void SetCustomFormatInfo(bool bInIsCustomFormat, const FIntPoint& InTileSize);

	/**
	 * Gets the total size needed for all mips.
	 *
	 * @param Dim Dimensions of the largest mip.
	 * @return Total size of all mip levels.
	 */
	SIZE_T GetMipBufferTotalSize(FIntPoint Dim);

protected:
	TSet<int32> CanceledFrames;
	FCriticalSection CanceledFramesCriticalSection;
	TMap<int32, FRgbaInputFile*> PendingFrames;
	TWeakPtr<FImgMediaLoader, ESPMode::ThreadSafe> LoaderPtr;
	/** True if we are using our custom format. */
	bool bIsCustomFormat;
	/** True if our custom format images are tiled. */
	bool bIsCustomFormatTiled;
	/** Tile size of our custom format images. */
	FIntPoint CustomFormatTileSize;
};


#endif //IMGMEDIA_EXR_SUPPORTED_PLATFORM
