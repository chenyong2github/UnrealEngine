// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImgMediaProcessImagesOptions.h"
#include "Input/Reply.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class IImageWrapper;
class SNotificationItem;

/**
 * SImgMediaProcessImages provides processing of image sequences.
 */
class SImgMediaProcessImages : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImgMediaProcessImages){}
	SLATE_END_ARGS()

	virtual ~SImgMediaProcessImages();

	void Construct(const FArguments& InArgs);

private:
	/** Called when we click on the process images button. */
	FReply OnProcessImagesClicked();

	/**
	 * Processes all images in the sequence and generate tiles/mips.
	 *
	 * @param ConfirmNotification	Notification that will be updated with progress and closed when done.
	 */
	void ProcessAllImages(TSharedPtr<SNotificationItem> ConfirmNotification);
	
	/**
	  * Checks if this file has an alpha channel.
	  *
	  * @param Ext			File extension.
	  * @param File			Full path to file.
	  * @return				True if the file has an alpha channel.
	  */
	bool HasAlphaChannel(const FString& Ext, const FString& File);

	/**
	 * Processess a single image and writes out 1 or more files.
	 * Tiles and mips may be generated.
	 * This does NOT run on the game thread.
	 *
	 * @param InImageWrapper	ImageWrapper to read/write the image.
	 * @param InTileWidth		Desired width of tiles.
	 * @param InTileHeight		Desired height of tiles.
	 * @param InName			Full path and name of file to write to WITHOUT the extension (e.g. no .exr)
	 * @param FileExtension		Extension to append to the file name.
	 */
	void ProcessImage(TSharedPtr<IImageWrapper>& InImageWrapper,
		int32 InTileWidth, int32 InTileHeight, const FString& InName, const FString& FileExtension);

	/**
	 * Processess a single image and writes out a file.
	 * Tiles and mips may be generated.
	 * This does NOT run on the game thread.
	 *
	 * @param InImageWrapper	ImageWrapper to read/write the image.
	 * @param InTileWidth		Desired width of tiles.
	 * @param InTileHeight		Desired height of tiles.
	 * @param InTileBorder		Number of pixels to duplicate along a tile edge.
	 * @param bInEnableMips		Turn on mip mapping.
	 * @param bHasAlphaChannel	True if there really is an alpha channel.
	 * @param InName			Full path and name of file to write.
	 */
	void ProcessImageCustom(TSharedPtr<IImageWrapper>& InImageWrapper,
		int32 InTileWidth, int32 InTileHeight, int32 InTileBorder, 
		bool bInEnableMips, bool bHasAlphaChannel, const FString& InName);

	/**
	 * Creates tiles from a source and outputs it to a destination.
	 *
	 * @param SourceData		Source image.
	 * @param DestArray			Destination image.
	 * @param SourceWidth		Width of source in pixels.
	 * @param SourceHeight		Height of source in pixels.
	 * @param DestWidth			Width of destination in pixels.
	 * @param DestHeight		Height of destination in pixels.
	 * @param NumTilesX			Number of tiles in X direction.
	 * @param NumTilesY			Number of tiles in Y direction.
	 * @param TileWidth			Width of a tile (without borders) in pixels.
	 * @param TileHeight		Height of a tile (without borders) in pixels.
	 * @param InTileBorder		Size of border in pixels.
	 * @param BytesPerPixel		Number of bytes per pixel.
	 */
	void TileData(uint8* SourceData, TArray64<uint8>& DestArray,
		int32 SourceWidth, int32 SourceHeight, int32 DestWidth, int32 DestHeight,
		int32 NumTilesX, int32 NumTilesY,
		int32 TileWidth, int32 TileHeight, int32 InTileBorder,
		int32 BytesPerPixel);

	/** Holds our details view. */
	TSharedPtr<class IDetailsView> DetailsView;
	/** Object that holds our options. */
	TStrongObjectPtr<UImgMediaProcessImagesOptions> Options;
};
