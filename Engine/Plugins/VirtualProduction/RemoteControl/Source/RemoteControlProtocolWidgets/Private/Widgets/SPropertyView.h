// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IPropertyRowGenerator.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

namespace RemoteControlProtocolWidgetUtils {
	struct FPropertyViewColumnSizeData;
}

enum class EPropertyNameVisibility : uint8
{
	Show = 0, // Always show the name column
    Hide = 1, // Never show the name column
    HideTopLevel = 2 // Hide if either: Property has no children, or is draw in one row; Property is the top-level/root, and has children
};

class SGridPanel;

/** Represents a single property, including a struct. */
class REMOTECONTROLPROTOCOLWIDGETS_API SPropertyView final : public SCompoundWidget, public FGCObject
{
	struct FPropertyWidgetCreationArgs
	{
		FPropertyWidgetCreationArgs(
            const int32 InIndex,
            const TSharedPtr<SWidget>& InNameWidget, 
            const TSharedPtr<SWidget>& InValueWidget,
            const float InLeftPadding,
            const TOptional<float>& InValueMinWidth = {},
            const TOptional<float>& InValueMaxWidth = {})
            : Index(InIndex),
              NameWidget(InNameWidget),
              ValueWidget(InValueWidget),
              LeftPadding(InLeftPadding),
              ValueMinWidth(InValueMinWidth),
              ValueMaxWidth(InValueMaxWidth),
              ColumnSizeData(nullptr),
              Spacing(0.0f),
              bResizableColumn(false)
		{
		}

		FPropertyWidgetCreationArgs(
            const FPropertyWidgetCreationArgs& InOther,
            const TSharedPtr<SWidget>& InNameWidget,
            const TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>& InColumnSizeData,
            const float InSpacing,
            const bool bInResizeableColumn)
            : Index(InOther.Index),
              NameWidget(InNameWidget ? InNameWidget : InOther.NameWidget),
              ValueWidget(InOther.ValueWidget),
              LeftPadding(InOther.LeftPadding),
              ValueMinWidth(InOther.ValueMinWidth),
              ValueMaxWidth(InOther.ValueMaxWidth),
              ColumnSizeData(InColumnSizeData),
              Spacing(InSpacing),
              bResizableColumn(bInResizeableColumn)
		{
		}

		int32 Index;
		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> ValueWidget;
		float LeftPadding;
		TOptional<float> ValueMinWidth;
		TOptional<float> ValueMaxWidth;
		TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> ColumnSizeData;
		float Spacing;
		bool bResizableColumn = true;
		
		bool HasNameWidget() const { return NameWidget.IsValid(); }
	};
	
public:
	SLATE_BEGIN_ARGS(SPropertyView)
		: _Object(nullptr)
		, _Struct(nullptr)
		, _RootPropertyName(NAME_None)
		, _NameVisibility(EPropertyNameVisibility::HideTopLevel)
		, _DisplayName({})
		, _Spacing(0)
		, _ColumnPadding(false)
		, _ResizableColumn(true)
		{}
		SLATE_ARGUMENT(TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>, ColumnSizeData)
	    SLATE_ARGUMENT(UObject*, Object)
		SLATE_ARGUMENT(TSharedPtr<FStructOnScope>, Struct)
		SLATE_ARGUMENT(FName, RootPropertyName)
		SLATE_ARGUMENT(EPropertyNameVisibility, NameVisibility)
		SLATE_ARGUMENT(TOptional<FText>, DisplayName)
	    SLATE_ARGUMENT(float, Spacing)
	    SLATE_ARGUMENT(bool, ColumnPadding)
	    SLATE_ARGUMENT(bool, ResizableColumn)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	~SPropertyView();

	/** Set's the object/property pair */
	void SetProperty(UObject* InObject, const FName InPropertyName);

	/** Set's the object/struct pair */
	void SetStruct(UObject* InObject, TSharedPtr<FStructOnScope>& InStruct);

	/** Force a refresh/rebuild */
	void Refresh();

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& InCollector) override;
	// End of FGCObject interface

	static int32 GetDesiredWidth() { return DesiredWidth; }
	static void SetDesiredWidth(int32 InDesiredWidth) { DesiredWidth = InDesiredWidth; }

	/** Get the property handle for the property on this view */
	TSharedPtr<IPropertyHandle> GetPropertyHandle() const { return Property; }

	/** Get the FOnFinishedChangingProperties delegate from the underlying PropertyRowGenerator */
	FOnFinishedChangingProperties& OnFinishedChangingProperties() const { return Generator->OnFinishedChangingProperties(); }

protected:
	// ueent_hotfix Hack for 4.24 allow to refresh the ui in between two frame without any flickering
	virtual bool CustomPrepass(float LayoutScaleMultiplier) override;
	
	// Begin SWidget overrides.
	// #ueent_todo: This is temporary until we find a better solution to the splitter issue
	//				See SConstrainedBox's trick in cpp file
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const FVector2D ChildSize = ChildSlot.GetWidget()->GetDesiredSize();
		return FVector2D( DesiredWidth, ChildSize.Y );
	}
	// End SWidget overrides.

private:	
	/** Fills up the details view with the detail nodes created by the property row manager */
	void Construct();

	/** Add widgets held by array of DetailTreeNode objects */
	void AddWidgets( const TArray<TSharedRef<class IDetailTreeNode>>& InDetailTree, int32& InIndex, float InLeftPadding);

	TSharedRef<SWidget> CreatePropertyWidget(const FPropertyWidgetCreationArgs& InCreationArgs);
	
	/**
	* Inserts a generic widget for a property row into the grid panel
	* @param Index						Row index in the grid panel
	* @param NameWidget				The widget used in the name column
	* @param ValueWidget				The widget used in the value column
	* @param LeftPadding				The Padding on the left of the row
	*/
	void CreateDefaultWidget(const FPropertyWidgetCreationArgs& InCreationArgs);

	/** Callback used by all splitters in the details view, so that they move in sync */
	void OnLeftColumnResized(float InNewWidth)
	{
		// This has to be bound or the splitter will take it upon itself to determine the size
		// We do nothing here because it is handled by the column size data
	}

	float OnGetLeftColumnWidth() const { return 1.0f - ColumnWidth; }
	float OnGetRightColumnWidth() const { return ColumnWidth; }
	void OnSetColumnWidth(float InWidth) { ColumnWidth = InWidth < 0.5f ? 0.5f : InWidth; }

	/** Callback to track property changes on array properties */
	void OnPropertyChanged(const struct FPropertyChangedEvent& InEvent);

	/** Callback used to detect the existence of a new object to display after a reinstancing process */
	void OnObjectReplaced(const TMap<UObject*, UObject*>& InReplacementObjectMap);

	void OnObjectTransacted(UObject* InObject, const class FTransactionObjectEvent& InTransactionObjectEvent);

private:
	/** Row generator applied on detailed object */
	TSharedPtr<class IPropertyRowGenerator> Generator;

	/** Object to be detailed */
	UObject* Object;

	/** Array properties tracked for changes */
	FName RootPropertyName;

	EPropertyNameVisibility NameVisibility;

	TOptional<FText> DisplayNameOverride;

	/** Array properties tracked for changes */
	TSharedPtr<IPropertyHandle> Property;

	/** */
	TSharedPtr<FStructOnScope> Struct;

	/** Delegate handle to track property changes on array properties */
	FDelegateHandle OnPropertyChangedHandle;

	/** Delegate handle to track new object after a re-instancing process */
	FDelegateHandle OnObjectReplacedHandle;

	/** Delegate handle to track when a object was transacted */
	FDelegateHandle OnObjectTransactedHandle;

	/** Relative width to control splitters. */
	float ColumnWidth;

	/** Points to the currently used column size data. Can be provided via argument as well. */
	TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> ColumnSizeData;

	/** If there is a new object to display on the next tick */
	uint8 bRefreshObjectToDisplay : 1;

	/** Spacing between rows of the SDataprepDetailsView. 0 by default. */
	float Spacing;

	/**
	* Indicates if two columns should be added
	* This is useful when the SDataprepDetailsView widget is used along the Producers widget
	*/
	bool bColumnPadding;

	/**
	* Indicates if the name and value widgets have a splitter and can be resized
	*/
	bool bResizableColumn;

	/** Grid panel storing the row widgets */
	TSharedPtr<SGridPanel> GridPanel;

	static int32 DesiredWidth;
};
