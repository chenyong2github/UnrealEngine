// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepAsset.h"
#include "DataPrepContentProducer.h"
#include "DataPrepEditor.h"

#include "Widgets/Input/STextComboBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

class IDetailsView;
class SDataprepAssetView;
class SDataprepConsumerWidget;

class FProducerStackEntry
{
public:
	FProducerStackEntry(int32 ProducerIndex, const FDataprepAssetProducer& Producer, FDataprepEditor* InDataprepEditor)
		: Label( Producer.Producer->GetLabel().ToString() )
		, ProducerIndex( ProducerIndex )
		, bIsEnabled( Producer.bIsEnabled )
		, bIsSelected( false )
		, DataprepEditorPtr( InDataprepEditor )
	{
	}

	bool HasValidData()
	{
		return DataprepEditorPtr->GetDataprepAsset()->Producers.IsValidIndex( ProducerIndex );
	}

	UDataprepContentProducer* GetProducer()
	{
		return HasValidData() ? DataprepEditorPtr->GetDataprepAsset()->Producers[ProducerIndex].Producer : nullptr;
	}

	FDataprepAssetProducer& GetDataprepAssetProducer()
	{
		// #ueent_todo: Add defensive code
		return DataprepEditorPtr->GetDataprepAsset()->Producers[ProducerIndex];
	}

	FString Label;
	int32 ProducerIndex;
	bool bIsEnabled;
	bool bIsSelected;
	FDataprepEditor* DataprepEditorPtr;
};

typedef TSharedRef<FProducerStackEntry> FProducerStackEntryRef;
typedef TSharedPtr<FProducerStackEntry> FProducerStackEntryPtr;

/** Represents a row in the VariantManager's tree views and list views */
class SProducerStackEntryTableRow : public STableRow<FProducerStackEntryRef>
{
public:
	SLATE_BEGIN_ARGS(SProducerStackEntryTableRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedRef<FProducerStackEntry>& InNode);

	TSharedRef<SWidget> GetInputMainWidget();

	TSharedPtr<FProducerStackEntry> GetDisplayNode() const
	{
		return Node.Pin();
	}

private:
	mutable TWeakPtr<FProducerStackEntry> Node;
	TSharedPtr< STextBlock > CheckBox;
};

class SProducerStackEntryTreeView : public STreeView<FProducerStackEntryRef>
{
public:

	SLATE_BEGIN_ARGS(SProducerStackEntryTreeView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, SDataprepAssetView* InDataprepAssetView, FDataprepEditor* InDataprepEditorPtr);

	int32 GetDisplayIndexOfNode(FProducerStackEntryRef InNode);

	// Caches the nodes the VariantManagerNodeTree is using and refreshes the display
	void Refresh();

protected:
	void OnExpansionChanged(FProducerStackEntryRef InItem, bool bIsExpanded);
	TSharedRef<ITableRow> OnGenerateRow(FProducerStackEntryRef InDisplayNode, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(FProducerStackEntryRef InParent, TArray<FProducerStackEntryRef>& OutChildren) const;

private:
	void BuildProducerEntries();
	void OnDataprepAssetProducerChanged();

private:
	FDataprepEditor* DataprepEditorPtr;
	TArray<FProducerStackEntryRef> RootNodes;
};

class SDataprepAssetView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDataprepAssetView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FDataprepEditor* InDataprepEditor);

	~SDataprepAssetView()
	{
	}

	/** Set the renderer when we change selection */
	void OnSelectionChanged( TSharedPtr< FProducerStackEntry > InItem, ESelectInfo::Type InSeletionInfo, FDataprepEditor* DataprepEditor );

private:
	TSharedRef<SWidget> CreateAddProducerMenuWidget();
	void OnConsumerChanged( TSharedPtr<FString> NewConsumer, ESelectInfo::Type SelectInfo);

private:
	FDataprepEditor* DataprepEditorPtr;
	TSharedPtr<SProducerStackEntryTreeView> TreeView;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr< STextBlock > CheckBox;
	TArray< TSharedPtr< FString > > ConsumerList;
	TSharedPtr< FString > SelectedProducer;
	TSharedPtr< STextComboBox > ProducerSelector;
	bool bIsChecked;
	TSharedPtr< FProducerStackEntry > SelectedEntry;
	TSharedPtr< SDataprepConsumerWidget > ConsumerWidget;
};

// Inspired from SKismetInspector class
// #ueent_todo: Revisit SGraphNodeDetailsWidget class based on tpm's feedback
class SGraphNodeDetailsWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeDetailsWidget) {}
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	void ShowDetailsObjects(const TArray<UObject*>& Objects);

	void SetCanEditProperties(bool bInCanEditProperties) { bCanEditProperties = bInCanEditProperties; };
	bool GetCanEditProperties() const { return bCanEditProperties; };
	const TArray< TWeakObjectPtr<UObject> >& GetObjectsShowInDetails() const { return SelectedObjects; };

	// SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	// End of SWidget interface

private:
	/** Update the inspector window to show information on the supplied objects */
	void UpdateFromObjects(const TArray<UObject*>& PropertyObjects);

	/** Add this property and all its child properties to SelectedObjectProperties */
	void AddPropertiesRecursive(UProperty* Property);

private:
	/** Property viewing widget */
	TSharedPtr<IDetailsView> PropertyView;

	/** Border widget that wraps a dynamic context-sensitive widget for editing objects that the property window is displaying */
	TSharedPtr<SBorder> ContextualEditingBorderWidget;

	/** Selected objects for this detail view */
	TArray< TWeakObjectPtr<UObject> > SelectedObjects;

	/** Set of object properties that should be visible */
	TSet<TWeakObjectPtr<UProperty> > SelectedObjectProperties;

	/** When TRUE, the SGraphNodeDetailsWidget needs to refresh the details view on Tick */
	bool bRefreshOnTick;

	bool bCanEditProperties;

	/** Holds the property objects that need to be displayed by the inspector starting on the next tick */
	TArray<UObject*> RefreshPropertyObjects;
};
