// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "Templates/SharedPointer.h"

class IDetailCategoryBuilder;
class IDetailGroup;
class IDetailLayoutBuilder;
class IPropertyHandle;
class SEditableTextBox;

class FImgMediaMipMapInfo;
struct FImgMediaMipMapCameraInfo;
struct FImgMediaMipMapObjectInfo;

/**
 * Implements a details view customization for the UImgMediaSource class.
 */
class FImgMediaSourceCustomization
	: public IPropertyTypeCustomization
{
public:

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

public:

	/**
	 * Creates an instance of this class.
	 *
	 * @return The new instance.
	 */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FImgMediaSourceCustomization());
	}

protected:

	/**
	 * Adds custom UI for mipmap info.
	 *
	 * @param DetailBuilder Adds to this UI.
	 */
	void CustomizeMipMapInfo(IDetailLayoutBuilder& DetailBuilder);

	/**
	 * Adds info on objects to the UI.
	 *
	 * @param InCategory UI will be added here.
	 * @param MipMapInfo Object info will be retrieved from this.
	 */
	void AddMipMapObjects(IDetailCategoryBuilder& InCategory, const FImgMediaMipMapInfo* MipMapInfo);

	/**
	 * Adds object info for a camera to the UI.
	 * 
	 * @param InCameraGroup UI to add to.
	 * @param InCameraInfo Camera to use.
	 * @param InMipLevelDistances Distances for each mip map level in world space.
	 * @param Objects List of objects to show info for.
	 */
	void AddCameraObjects(IDetailGroup& InCameraGroup, const FImgMediaMipMapCameraInfo& InCameraInfo, const TArray<float>& InMipLevelDistances, const TArray<FImgMediaMipMapObjectInfo*>& Objects);
	
	/**
	 * Adds mip level distances for a camera to the UI.
	 *
	 * @param InCameraGroup UI to add to.
	 * @param InCameraInfo Camera to use.
	 * @param InMipLevelDistances Distances for each mip map level in world space.
	 */
	void AddCameraMipDistances(IDetailGroup& InCameraGroup, const FImgMediaMipMapCameraInfo& InCameraInfo, const TArray<float>& InMipLevelDistances);

	/**
	 * Get the path to the currently selected image sequence.
	 *
	 * @return Sequence path string.
	 */
	FString GetSequencePath() const;

	/**
	 * Get the root path we are using for a relative Sequence Path.
	 *
	 * @return Root path.
	 */
	FString GetRelativePathRoot() const;

private:

	/** Callback for picking a path in the source directory picker. */
	void HandleSequencePathPickerPathPicked(const FString& PickedPath);

	/** Callback for getting the visibility of warning icon for invalid SequencePath paths. */
	EVisibility HandleSequencePathWarningIconVisibility() const;

	/** Returns the property for SequencePath. */
	TSharedPtr<IPropertyHandle> GetSequencePathProperty() const;
	/** Returns the property for SequencePath->Path. */
	TSharedPtr<IPropertyHandle> GetSequencePathPathProperty() const;
	/** Returns the property for IsPathRelativeToProjectRoot. */
	TSharedPtr<IPropertyHandle> GetPathRelativeToRootProperty() const;
	/** Returns the value of IsPathRelativeToProjectRoot. */
	bool IsPathRelativeToRoot() const;
	/** Sets the value of IsPathRelativeToProjectRoot. */
	void SetPathRelativeToRoot(bool bIsPathRelativeToRoot);

private:

	/** Text block widget showing the found proxy directories. */
	TSharedPtr<SEditableTextBox> ProxiesTextBlock;

	/** Stores our property. */
	TSharedPtr<IPropertyHandle> PropertyHandle;
};
