// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class UDataprepAsset;
class UDataprepContentConsumer;
class UDataprepParameterizableObject;

struct FDataprepParameterizationContext;
struct FDataprepPropertyLink;

struct FDataprepDetailsViewColumnSizeData
{
	TAttribute<float> LeftColumnWidth;
	TAttribute<float> RightColumnWidth;
	SSplitter::FOnSlotResized OnWidthChanged;

	void SetColumnWidth(float InWidth) { OnWidthChanged.ExecuteIfBound(InWidth); }
};

class SDataprepConsumerWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDataprepConsumerWidget)
		: _DataprepConsumer(nullptr)
	{}

	SLATE_ARGUMENT(TSharedPtr< FDataprepDetailsViewColumnSizeData >, ColumnSizeData)
	SLATE_ARGUMENT(UDataprepContentConsumer*, DataprepConsumer)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs );

	/** Update DataprepConsumerPtr and content folder's and level name's text boxes */
	void SetDataprepConsumer( UDataprepContentConsumer* DataprepConsumer );

private:
	/** Create complete widget from valid UDataprepContentConsumer object */
	TSharedRef<SWidget> BuildWidget();
	/** Create empty widget from invalid UDataprepContentConsumer object */
	TSharedRef<SWidget> BuildNullWidget();
	/** Callback when content of level name's text box has changed */
	void OnLevelNameChanged( const FText& NewLevelName, ETextCommit::Type CommitType );
	/** Callback when browser's button is clicked */
	void OnBrowseContentFolder();
	/** Callback when content of content folder's text box has changed */
	void OnTextCommitted( const FText&, ETextCommit::Type );

	/** Update content folder's and level name's text boxes */
	void OnConsumerChanged();
	/** Update content folder's text boxes */
	void UpdateContentFolderText();

	/** Callbacks to update splitter */
	void OnLeftColumnResized(float InNewWidth)
	{
		// This has to be bound or the splitter will take it upon itself to determine the size
		// We do nothing here because it is handled by the column size data
	}

	float OnGetLeftColumnWidth() const { return 1.0f - ColumnWidth; }
	float OnGetRightColumnWidth() const { return ColumnWidth; }
	void OnSetColumnWidth(float InWidth) { ColumnWidth = InWidth; }

private:
	/** Weak pointer on edited consumer */
	TWeakObjectPtr<UDataprepContentConsumer> DataprepConsumerPtr;
	/** Content folder's text box */
	TSharedPtr< SEditableTextBox > ContentFolderTextBox;
	/** Level name's text box */
	TSharedPtr< SEditableTextBox > LevelTextBox;
	/** Helps sync column resizing with another part of the UI (producers widget) */
	TSharedPtr< FDataprepDetailsViewColumnSizeData > ColumnSizeData;
	/** Relative width to control splitters. */
	float ColumnWidth;
};

class SDataprepDetailsView : public SCompoundWidget, public FGCObject
{
	using Super = SCompoundWidget;

public:
	SLATE_BEGIN_ARGS(SDataprepDetailsView)
		: _Object( nullptr )
	{}
	SLATE_ARGUMENT(TSharedPtr<FDataprepDetailsViewColumnSizeData>, ColumnSizeData)
	SLATE_ARGUMENT(UObject*, Object)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	~SDataprepDetailsView();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;	

	void SetObjectToDisplay(UObject& Object);

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// End of FGCObject interface

protected:
	// Begin SWidget overrides.
	// #ueent_todo: This is temporary until we find a better solution to the splitter issue
	//				See SConstrainedBox's trick in cpp file
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const float MaxWidth = 400.0f;
		const FVector2D ChildSize = ChildSlot.GetWidget()->GetDesiredSize();
		return FVector2D( MaxWidth, ChildSize.Y );
	}
	// End SWidget overrides.

private:
	/** Fills up the details view with the detail nodes created by the property row manager */
	void Construct();

	/** Add widgets held by array of DetailTreeNode objects */
	void AddWidgets( const TArray< TSharedRef< class IDetailTreeNode > >& DetailTree, TSharedPtr< class SGridPanel >& GridPanel, int32& Index, float LeftPadding, const FDataprepParameterizationContext& ParameterizationContext);

	/**
	 * Create generic widget for a property row
	 * @param NameWidget The widget used in the name column
	 * @param ValueWidget The widget used in the value column
	 * @param LeftPadding The Padding on the left of the row
	 * @param PropertyChain The chain to the property from the class of the detailed object
	 * @return A row for the details view
	 */
	TSharedRef< SWidget > CreateDefaultWidget(TSharedPtr< SWidget >& NameWidget, TSharedPtr< SWidget >& ValueWidget, float LeftPadding, EHorizontalAlignment HAlign, EVerticalAlignment VAlign, const FDataprepParameterizationContext& ParameterizationContext);

	/** Callback used by all splitters in the details view, so that they move in sync */
	void OnLeftColumnResized(float InNewWidth)
	{
		// This has to be bound or the splitter will take it upon itself to determine the size
		// We do nothing here because it is handled by the column size data
	}

	float OnGetLeftColumnWidth() const { return 1.0f - ColumnWidth; }
	float OnGetRightColumnWidth() const { return ColumnWidth; }
	void OnSetColumnWidth(float InWidth) { ColumnWidth = InWidth; }

	/** Callback to track property changes on array properties */
	void OnPropertyChanged( const struct FPropertyChangedEvent& InEvent );

	/** Callback used to detect the existence of a new object to display after a reinstancing process */
	void OnObjectReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap);

	void ForceRefresh();

private:
	/** Row generator applied on detailed object */
	TSharedPtr< class IPropertyRowGenerator > Generator;

	/** Object to be detailed */
	UObject* DetailedObject;

	/** Not null if the detailed object is parameterizable */
	UDataprepParameterizableObject* DetailedObjectAsParameterizable;

	/** Array properties tracked for changes */
	TSet< UProperty* > TrackedProperties;
	
	/** Delegate handle to track property changes on array properties */
	FDelegateHandle OnPropertyChangedHandle;

	/** Delegate to track new object after a reinstancing process */
	FDelegateHandle OnObjectReplacedHandle;

	/** Relative width to control splitters. */
	float ColumnWidth;

	// Points to the currently used colum size data. Can be provided via argument as well.
	TSharedPtr< FDataprepDetailsViewColumnSizeData > ColumnSizeData;

	// If there is a new object to display on the next tick
	uint8 bRefreshObjectToDisplay : 1;

	// This pointer to a dataprep asset is used by the parameterization system. It should be nullptr when the parameterization shouldn't be shown
	TWeakObjectPtr<UDataprepAsset> DataprepAssetForParameterization;

	FDelegateHandle OnDataprepParameterizationWasModifiedHandle;
};
