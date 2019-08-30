// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "IContentSource.h"
#include "ViewModels/CategoryViewModel.h"

/** A view model for displaying and interacting with an IContentSource in the FAddContentDialog. */
class FContentSourceViewModel : public TSharedFromThis<FContentSourceViewModel>
{
public:
	/** Creates a view model for a supplied content source. */
	FContentSourceViewModel(TSharedPtr<IContentSource> ContentSourceIn);

	/** Gets the content source represented by this view model. */
	TSharedPtr<IContentSource> GetContentSource();

	/** Gets the display name for this content source. */
	FText GetName();

	/** Gets the description of this content source. */
	FText GetDescription();

	/** Gets the asset types used in this content source. */
	FText GetAssetTypes();

	/** Gets the class types used in this content source. */
	FString GetClassTypes();

	/** Gets the view model for the category for this content source. */
	FCategoryViewModel GetCategory();

	/** Gets the brush which should be used to draw the icon representation of this content source. */
	TSharedPtr<FSlateBrush> GetIconBrush();

	/** Gets an array or brushes which should be used to display screenshots for this content source. */
	TArray<TSharedPtr<FSlateBrush>>* GetScreenshotBrushes();

private:
	/** Sets up brushes from the images data supplied by the IContentSource. */
	void SetupBrushes();

	/** Creates a slate brush from raw binary PNG formatted image data and the supplied prefix. */
	TSharedPtr<FSlateDynamicImageBrush> CreateBrushFromRawData(FString ResourceNamePrefix, const TArray<uint8>& RawData) const;

	/** Selects the text from an array which matches the given language. */
	FText ChooseLocalizedText(const TArray<FLocalizedText>& Choices, const FString& InCurrentLanguage);

private:
	struct FCachedContentText
	{
		FString Language;
		FText Text;
	};

	/** The content source represented by this view model. */
	TSharedPtr<IContentSource> ContentSource;

	/** The brush which should be used to draw the icon representation of this content source. */
	TSharedPtr<FSlateBrush> IconBrush;

	/** An array or brushes which should be used to display screenshots for this content source. */
	TArray<TSharedPtr<FSlateBrush>> ScreenshotBrushes;

	/** The view model for the category for this content source. */
	FCategoryViewModel Category;

	/** The information used/returned the last time the name of the content source was requested. */
	FCachedContentText CachedNameText;

	/** The information used/returned the last time the description of the content source was requested. */
	FCachedContentText CachedDescriptionText;

	/** The information used/returned the last time the asset types of the content source was requested. */
	FCachedContentText CachedAssetTypeText;

	/** Keeps track of a unique increasing id which is appended to each brush name.  This avoids an issue
		where two brushes are created with the same name, and then both brushes texture data gets deleted
		when either brush is destructed. */
	static uint32 ImageID;
};
