// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertyEditorDelegates.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FDMXEditor;
class UDMXEntity;

class SBorder;

/** DMX inspector details view base class */
class SDMXEntityInspector
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXEntityInspector)
		: _SetNotifyHook(true)
		, _ShowSearch(false)
		, _ShowTitleArea(false)
		, _HideFilterArea(false)
		, _HideNameArea(true)
		, _ViewIdentifier(FName(TEXT("DMXEntityInspector")))
		, _OnFinishedChangingProperties()
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)
	SLATE_ARGUMENT(bool, SetNotifyHook)
	SLATE_ARGUMENT(bool, ShowSearch)
	SLATE_ARGUMENT(bool, ShowTitleArea)
	SLATE_ARGUMENT(bool, HideFilterArea)

	SLATE_ARGUMENT(bool, HideNameArea)
	SLATE_ARGUMENT(FName, ViewIdentifier)

	SLATE_EVENT(FOnFinishedChangingProperties::FDelegate, OnFinishedChangingProperties)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~End of SWidget interface

	/** Update the inspector window to show information on the supplied Entity */
	void ShowDetailsForSingleEntity(UDMXEntity* Entity);

	/** Update the inspector window to show information on the supplied objects */
	void ShowDetailsForEntities(const TArray<UDMXEntity*>& Entities);

	/** Get current property view widget */
	TSharedPtr<class IDetailsView> GetPropertyView() const { return PropertyView; }

protected:
	/** Update the inspector window to show information on the supplied objects */
	void UpdateFromObjects(const TArray<UObject*>& PropertyObjects);

	/**
	 * Generates a widget that is used to edit the specified object array.  This widget
	 * will be displayed along with a property view in the level editor
	 */
	TSharedRef<SWidget> MakeEditingWidget(const TArray<UObject*>& Objects);

protected:
	/** Pointer back to the DMXEditor tool that owns us */
	TWeakPtr<FDMXEditor> DMXEditor;

	/** Property viewing widget */
	TSharedPtr<IDetailsView> PropertyView;

	/** Show Search for property view */
	bool bIsShowSearch;

	/** Show title area for property view */
	bool bIsShowTitleArea;	
	
	/** Hide filter area for property view */
	bool bIsHideFilterArea;

	/** User defined delegate for OnFinishedChangingProperties */
	FOnFinishedChangingProperties::FDelegate UserOnFinishedChangingProperties;

	/** When TRUE, the inspector needs to refresh the details view on Tick */
	bool bRefreshOnTick;

	/** Holds the property objects that need to be displayed by the inspector starting on the next tick */
	TArray<UObject*> RefreshPropertyObjects;

	/** Border widget that wraps a dynamic context-sensitive widget for editing objects that the property window is displaying */
	TSharedPtr<SBorder> EditingWidget;
};

/** Enum specifying how Fixture Type Details should be layed out */
enum class EDMXFixtureTypeLayout : uint8
{
	Default,
	FixtureSettings,
	Modes,
	ModeProperties,
	Functions,
	FunctionProperties
};

/** DMX fixture types inspector */
class SDMXEntityInspectorFixtureTypes
	: public SDMXEntityInspector
{
public:
	SLATE_BEGIN_ARGS(SDMXEntityInspectorFixtureTypes)
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)
	SLATE_EVENT(FOnFinishedChangingProperties::FDelegate, OnFinishedChangingProperties)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, EDMXFixtureTypeLayout DetailsView);
};

/** DMX Component inspector */
class SDMXComponentInspector
	: public SDMXEntityInspector
{
public:
	SLATE_BEGIN_ARGS(SDMXComponentInspector)
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)
		SLATE_EVENT(FOnFinishedChangingProperties::FDelegate, OnFinishedChangingProperties)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);
};
