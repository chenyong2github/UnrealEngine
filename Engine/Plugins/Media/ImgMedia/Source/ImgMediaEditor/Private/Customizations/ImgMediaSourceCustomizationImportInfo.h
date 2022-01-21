// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IPropertyTypeCustomization.h"
#include "Misc/Optional.h"
#include "Templates/SharedPointer.h"

class IPropertyHandle;
class SNotificationItem;

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
	 * Called when the import sequence button is clicked.
	 * 
	 * @return Whether this handled the event or not.
	 */
	FReply OnImportClicked();

	/**
	 * Async function to import all files in the sequence and generate tiles/mips.
	 *
	 * @param SequencePath				Path to look for files.
	 * @param ConfirmNotification		Notification that will be updated with progress and closed when done.
	 */
	static void ImportFiles(const FString& SequencePath, TSharedPtr<SNotificationItem> ConfirmNotification);

	/** Stores our property. */
	TSharedPtr<IPropertyHandle> PropertyHandle;

	/** Tile width for each image in pixels. */
	int32 TileWidth = 0;
};
