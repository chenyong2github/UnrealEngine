// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IPropertyTypeCustomization.h"
#include "Misc/Optional.h"
#include "Templates/SharedPointer.h"

class IImageWrapper;
class IPropertyHandle;
class SNotificationItem;
class UImgMediaSource;

/**
 * Implements a customization for the FImgMediaSourceImportData class.
 */
class FImgMediaSourceCustomizationImportInfo
	: public IPropertyTypeCustomization
{
public:

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	/**
	 * Creates an instance of this class.
	 *
	 * @return The new instance.
	 */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FImgMediaSourceCustomizationImportInfo());
	}

private:
	/**
	 * Gets the current tile width of each image in pixels.
	 *
	 * @return Width.
	 */
	TOptional<int32> GetTileWidth() const;

	/**
	 * Sets the tile width of each image in pixels.
	 * 
	 * @param InWidth Width to set.
	 */
	void SetTileWidth(int32 InWidth);

	/**
	 * Gets the current tile height of each image in pixels.
	 *
	 * @return Height.
	 */
	TOptional<int32> GetTileHeight() const;

	/**
	 * Sets the tile height of each image in pixels.
	 *
	 * @param InHeight Height to set.
	 */
	void SetTileHeight(int32 InHeight);

	/**
	 * Gets where the imported files should go.
	 */
	FString GetDestinationPath();

	/**
	 * Sets where the imported files should go.
	 */
	void SetDestinationPath(const FString& InPath);

	/**
	 * Query if the destination path has been overriden.
	 */
	bool IsDestinationPathOverriden();

	/**
	 * Set if the destination path has been overriden.
	 */
	void SetIsDestinationPathOverriden(bool bInIsOverriden);

	/**
	 * Query if we should use the imported images.
	 */
	bool IsUsableProperty();

	/**
	 * Set if we should use the imported images.
	 */
	void SetIsUsableProperty(bool bInIsUsable);

	/**
	 * Generic query function for boolean properties.
	 */
	bool GetGenericBoolProperty(const TSharedPtr<IPropertyHandle>& InProperty);

	/**
	 * Generic setter for boolean properties.
	 */
	void SetGenericBoolProperty(const TSharedPtr<IPropertyHandle>& InProperty, bool bInIsTrue);

	/**
	 * Called when DestinationPath changes.
	 */
	void OnDestinationPathChanged(const FString& Directory);

	/**
	 * Called when the import sequence button is clicked.
	 * 
	 * @return Whether this handled the event or not.
	 */
	FReply OnImportClicked();

	/**
	 * Async function to import all files in the sequence and generate tiles/mips.
	 *
	 * @param SequencePath			Path to look for files.
	 * @param ConfirmNotification	Notification that will be updated with progress and closed when done.
	 * @param InTileWidth			Desired width of tiles. 
	 * @param InTileHeight			Desired height of tiles.
	 */
	void ImportFiles(const FString& SequencePath,
		const FString& DestinationPath,
		TSharedPtr<SNotificationItem> ConfirmNotification,
		int32 InTileWidth, int32 InTileHeight);

	/**
	 * Imports a single image and writes out 1 or more files.
	 * Tiles and mips may be generated.
	 * This does NOT run on the game thread.
	 * 
	 * @param InImageWrapper	ImageWrapper to read/write the image.
	 * @param InTileWidth		Desired width of tiles. 
	 * @param InTileHeight		Desired height of tiles.
	 * @param InName			Full path and name of file to write to WITHOUT the extension (e.g. no .exr)
	 * @param FileExtension		Extension to append to the file name.
	 */
	static void ImportImage(TSharedPtr<IImageWrapper>& InImageWrapper,
		int32 InTileWidth, int32 InTileHeight, const FString& InName, const FString& FileExtension);

	/** Stores our property. */
	TSharedPtr<IPropertyHandle> PropertyHandle;
	/** Storess the property to the destination path.*/
	TSharedPtr<IPropertyHandle> DestinationPathPropertyHandle;
	/** Stores the property that says if the destination path is overriden. */
	TSharedPtr<IPropertyHandle> IsDestinationPathOverridenPropertyHandle;
	/** Stores the property that says if the imported files are usable. */
	TSharedPtr<IPropertyHandle> IsUsablePropertyHandle;
	/** Stores the ImgMediaSource that we are editing. */
	TWeakObjectPtr<UImgMediaSource> ImgMediaSource;

	/** Tile width for each image in pixels. */
	int32 TileWidth = 0;
	/** Tile height for each image in pixels. */
	int32 TileHeight = 0;
};
