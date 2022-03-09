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

	/** Holds our details view. */
	TSharedPtr<class IDetailsView> DetailsView;
	/** Object that holds our options. */
	TStrongObjectPtr<UImgMediaProcessImagesOptions> Options;
};
