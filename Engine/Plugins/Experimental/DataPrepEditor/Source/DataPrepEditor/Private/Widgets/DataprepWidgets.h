// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class UDataprepContentConsumer;

class SDataprepConsumerWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDataprepConsumerWidget)
	{}

	SLATE_ARGUMENT(UDataprepContentConsumer*, DataprepConsumer)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	void SetDataprepConsumer( UDataprepContentConsumer* DataprepConsumer );

private:
	TSharedRef<SWidget> BuildWidget();
	TSharedRef<SWidget> BuildNullWidget();
	void OnLevelNameChanged( const FText& NewLevelName, ETextCommit::Type CommitType );
	void OnBrowseContentFolder();
	void UpdateContentFolderText();

private:
	TWeakObjectPtr<UDataprepContentConsumer> DataprepConsumer;
	TSharedPtr< SEditableTextBox > ContentFolderTextBox;
	TSharedPtr< SEditableTextBox > LevelTextBox;
};

struct FDataprepDetailsViewColumnSizeData
{
	TAttribute<float> LeftColumnWidth;
	TAttribute<float> RightColumnWidth;
	SSplitter::FOnSlotResized OnWidthChanged;

	void SetColumnWidth(float InWidth) { OnWidthChanged.ExecuteIfBound(InWidth); }
};

class SDataprepDetailsView : public SCompoundWidget
{
	using Super = SCompoundWidget;

public:
	SLATE_BEGIN_ARGS(SDataprepDetailsView)
		: _Object( nullptr )
		, _Class( UObject::StaticClass() )
	{}
	SLATE_ATTRIBUTE(UObject*, Object)
	SLATE_ATTRIBUTE(UClass*, Class)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	~SDataprepDetailsView();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;	

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
	void AddWidgets( const TArray< TSharedRef< class IDetailTreeNode > >& DetailTree, TSharedPtr< class SGridPanel >& GridPanel, int32& Index, float LeftPadding);

	/** Create generic widget */
	TSharedRef< SWidget > CreateDefaultWidget( TSharedPtr< SWidget >& NameWidget, TSharedPtr< SWidget >& ValueWidget, float LeftPadding, EHorizontalAlignment HAlign, EVerticalAlignment VAlign );

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

private:
	/** Row generator applied on detailed object */
	TSharedPtr< class IPropertyRowGenerator > Generator;

	/** Object to be detailed */
	UObject* DetailedObject;

	/** Class of the object to be detailed */
	UClass* DetailedClass;

	TAttribute<UObject*> ObjectAttribute;

	/** Array properties tracked for changes */
	TSet< class UProperty* > TrackedProperties;
	
	/** Delegate handle to track property changes on array properties */
	FDelegateHandle OnPropertyChangedHandle;

	/** Container used by all splitters in the details view, so that they move in sync */
	FDataprepDetailsViewColumnSizeData ColumnSizeData;

	/** Relative width to control splitters */
	float ColumnWidth;
};
