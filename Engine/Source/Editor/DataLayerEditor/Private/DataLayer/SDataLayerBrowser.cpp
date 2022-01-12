// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDataLayerBrowser.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "SDataLayerOutliner.h"
#include "DataLayersActorDescTreeItem.h"
#include "DataLayerActorTreeItem.h"
#include "DataLayerTreeItem.h"
#include "DataLayerMode.h"
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "DataLayerOutlinerIsVisibleColumn.h"
#include "DataLayerOutlinerIsLoadedInEditorColumn.h"
#include "DataLayerOutlinerDeleteButtonColumn.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "SceneOutlinerTextInfoColumn.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

void SDataLayerBrowser::Construct(const FArguments& InArgs)
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Args.bAllowSearch = true;
	Args.bAllowFavoriteSystem = true;
	Args.bHideSelectionTip = true;
	Args.bShowObjectLabel = true;
	Args.NameAreaSettings = FDetailsViewArgs::ObjectsUseNameArea;
	Args.ColumnWidth = 0.5f;
	DetailsWidget = PropertyModule.CreateDetailView(Args);
	DetailsWidget->SetVisibility(EVisibility::Visible);

	//////////////////////////////////////////////////////////////////////////
	//	DataLayer Contents Header
	SAssignNew(DataLayerContentsHeader, SBorder)
	.BorderImage(FEditorStyle::GetBrush("DataLayerBrowser.DataLayerContentsQuickbarBackground"))
	.Visibility(EVisibility::Visible);

	//////////////////////////////////////////////////////////////////////////
	//	DataLayer Contents Section

	FGetTextForItem InternalNameInfoText = FGetTextForItem::CreateLambda([](const ISceneOutlinerTreeItem& Item) -> FString
	{
		if (const FDataLayerTreeItem* DataLayerItem = Item.CastTo<FDataLayerTreeItem>())
		{
			if (const UDataLayer* DataLayer = DataLayerItem->GetDataLayer())
			{
				return DataLayer->GetFName().ToString();
			}
		}
		else if (const FDataLayerActorTreeItem* DataLayerActorTreeItem = Item.CastTo<FDataLayerActorTreeItem>())
		{
			if (const AActor* Actor = DataLayerActorTreeItem->GetActor())
			{
				return Actor->GetFName().ToString();
			}
		}
		else if (const FDataLayerActorDescTreeItem* ActorDescItem = Item.CastTo<FDataLayerActorDescTreeItem>())
		{
			if (const FWorldPartitionActorDesc* ActorDesc = ActorDescItem->ActorDescHandle.Get())
			{
				return ActorDesc->GetActorName().ToString();
			}
		}
		return FString();
	});

	FGetTextForItem InternalInitialRuntimeStateInfoText = FGetTextForItem::CreateLambda([](const ISceneOutlinerTreeItem& Item) -> FString
	{
		if (const FDataLayerTreeItem* DataLayerItem = Item.CastTo<FDataLayerTreeItem>())
		{
			if (const UDataLayer* DataLayer = DataLayerItem->GetDataLayer())
			{
				if (DataLayer->IsRuntime())
				{
					return GetDataLayerRuntimeStateName(DataLayer->GetInitialRuntimeState());
				}
			}
		}
		return FString();
	});

	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.bShowHeaderRow = true;
	InitOptions.bShowParentTree = true;
	InitOptions.bShowCreateNewFolder = false;
	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda([this](SSceneOutliner* Outliner) { return new FDataLayerMode(FDataLayerModeParams(Outliner, this, nullptr)); });
	InitOptions.ColumnMap.Add(FDataLayerOutlinerIsVisibleColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShareable(new FDataLayerOutlinerIsVisibleColumn(InSceneOutliner)); })));
	InitOptions.ColumnMap.Add(FDataLayerOutlinerIsLoadedInEditorColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 1, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShareable(new FDataLayerOutlinerIsLoadedInEditorColumn(InSceneOutliner)); })));
	InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 2));
	InitOptions.ColumnMap.Add(FDataLayerOutlinerDeleteButtonColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShareable(new FDataLayerOutlinerDeleteButtonColumn(InSceneOutliner)); })));
	InitOptions.ColumnMap.Add("ID Name", FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Invisible, 20, FCreateSceneOutlinerColumn::CreateStatic(&FTextInfoColumn::CreateTextInfoColumn, FName("ID Name"), InternalNameInfoText, FText::GetEmpty())));
	InitOptions.ColumnMap.Add("Initial State", FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Invisible, 20, FCreateSceneOutlinerColumn::CreateStatic(&FTextInfoColumn::CreateTextInfoColumn, FName("Initial State"), InternalInitialRuntimeStateInfoText, FText::FromString("Initial Runtime State"))));
	DataLayerOutliner = SNew(SDataLayerOutliner, InitOptions).IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute());

	SAssignNew(DataLayerContentsSection, SBorder)
	.Padding(5)
	.BorderImage(FEditorStyle::GetBrush("NoBrush"))
	.Content()
	[
		// Data Layer Outliner
		SNew(SSplitter)
		.Orientation(Orient_Vertical)
		.Style(FEditorStyle::Get(), "DetailsView.Splitter")
		+ SSplitter::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				DataLayerOutliner.ToSharedRef()
			]
		]
		// Details
		+SSplitter::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(2, 4, 0, 0)
			[
				DetailsWidget.ToSharedRef()
			]
		]
	];

	//////////////////////////////////////////////////////////////////////////
	//	DataLayer Browser
	ChildSlot
	[
		SAssignNew(ContentAreaBox, SVerticalBox)
		.IsEnabled_Lambda([]() { return GWorld ? UWorld::HasSubsystem<UWorldPartitionSubsystem>(GWorld) : false; })
	];

	InitializeDataLayerBrowser();
}

void SDataLayerBrowser::SyncDataLayerBrowserToDataLayer(const UDataLayer* DataLayer)
{
	FSceneOutlinerTreeItemPtr Item = DataLayerOutliner->GetTreeItem(DataLayer);
	if (Item.IsValid())
	{
		DataLayerOutliner->SetItemSelection(Item, true, ESelectInfo::OnMouseClick);
		FSceneOutlinerTreeItemPtr Parent = Item->GetParent();
		while(Parent.IsValid())
		{
			DataLayerOutliner->SetItemExpansion(Parent, true);
			Parent = Parent->GetParent();
		};
	}
}

void SDataLayerBrowser::OnSelectionChanged(TSet<TWeakObjectPtr<const UDataLayer>>& InSelectedDataLayersSet)
{
	SelectedDataLayersSet = InSelectedDataLayersSet;
	TArray<UObject*> SelectedDataLayers;
	for (const auto& WeakDataLayer : SelectedDataLayersSet)
	{
		if (WeakDataLayer.IsValid())
		{
			UDataLayer* DataLayer = const_cast<UDataLayer*>(WeakDataLayer.Get());
			SelectedDataLayers.Add(DataLayer);
		}
	}
	DetailsWidget->SetObjects(SelectedDataLayers, /*bForceRefresh*/ true);
}


void SDataLayerBrowser::InitializeDataLayerBrowser()
{
	ContentAreaBox->ClearChildren();
	ContentAreaBox->AddSlot()
	.AutoHeight()
	.FillHeight(1.0f)
	[
		DataLayerContentsSection.ToSharedRef()
	];

	ContentAreaBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Bottom)
	.MaxHeight(23)
	[
		DataLayerContentsHeader.ToSharedRef()
	];
}

#undef LOCTEXT_NAMESPACE