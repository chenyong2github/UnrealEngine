// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "PropertyPath.h"
#include "DiffUtils.h"

class IDetailsView;

/** Struct to handle showing details for an object and provide an interface for listing all differences */
class KISMET_API FDetailsDiff
{
public:
	DECLARE_DELEGATE( FOnDisplayedPropertiesChanged );

	FDetailsDiff( const UObject* InObject, FOnDisplayedPropertiesChanged InOnDisplayedPropertiesChanged );
	~FDetailsDiff();

	/** Attempt to highlight the property with the given path, may not always succeed */
	void HighlightProperty( const FPropertySoftPath& PropertyName );

	/** Returns actual widget that is used to display details */
	TSharedRef< SWidget > DetailsWidget();

	/** Returns object being displayed */
	const UObject* GetDisplayedObject() const { return DisplayedObject; }

	/** Returns a list of all properties that would be diffed */
	TArray<FPropertySoftPath> GetDisplayedProperties() const;

	/** Perform a diff against another view, ordering either by display order or by remove/add/change */
	void DiffAgainst(const FDetailsDiff& Newer, TArray<FSingleObjectDiffEntry>& OutDifferences, bool bSortByDisplayOrder = false) const;

private:
	void HandlePropertiesChanged();

	FOnDisplayedPropertiesChanged OnDisplayedPropertiesChanged;

	TArray< FPropertyPath > DifferingProperties;
	const UObject* DisplayedObject;

	TSharedPtr< class IDetailsView > DetailsView;
};
