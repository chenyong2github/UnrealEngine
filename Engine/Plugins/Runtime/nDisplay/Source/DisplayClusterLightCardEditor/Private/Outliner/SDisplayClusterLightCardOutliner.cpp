// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterLightCardOutliner.h"

#include "DisplayClusterLightCardEditorCommands.h"
#include "DisplayClusterLightCardOutlinerMode.h"
#include "SDisplayClusterLightCardEditor.h"

#include "IDisplayClusterOperator.h"

#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterLightCardActor.h"
#include "DisplayClusterRootActor.h"

#include "ActorTreeItem.h"
#include "ClassIconFinder.h"
#include "FolderTreeItem.h"
#include "SceneOutlinerMenuContext.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerTextInfoColumn.h"
#include "Selection.h"
#include "SSceneOutliner.h"
#include "ToolMenus.h"
#include "WorldTreeItem.h"
#include "Framework/Commands/GenericCommands.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterLightCardOutliner"

SDisplayClusterLightCardOutliner::~SDisplayClusterLightCardOutliner()
{
	if (SceneOutliner.IsValid() && SceneOutlinerSelectionChanged.IsValid())
	{
		SceneOutliner->GetOnItemSelectionChanged().Remove(SceneOutlinerSelectionChanged);
	}
}

void SDisplayClusterLightCardOutliner::Construct(const FArguments& InArgs, TSharedPtr<SDisplayClusterLightCardEditor> InLightCardEditor, TSharedPtr<FUICommandList> InCommandList)
{
	LightCardEditorPtr = InLightCardEditor;
	CommandList = InCommandList;
	
	CreateWorldOutliner();
}

void SDisplayClusterLightCardOutliner::SetRootActor(ADisplayClusterRootActor* NewRootActor)
{
	TArray<ADisplayClusterLightCardActor*> PreviouslySelectedLightCards;
	GetSelectedLightCards(PreviouslySelectedLightCards);
	
	RootActor = NewRootActor;
	FillLightCardList();

	CreateWorldOutliner();
	
	for (const TSharedPtr<FLightCardTreeItem>& LightCardTreeItem : LightCardTree)
	{
		if (LightCardTreeItem->IsActorLayer())
		{
			LightCardTreeView->SetItemExpansion(LightCardTreeItem, true);
		}
	}
	
	// Select previously selected light cards. This fixes an issue where the details panel may clear in some cases
	// and also supports maintaining the selection of shared light cards across different root actors.
	SelectLightCards(MoveTemp(PreviouslySelectedLightCards));
}

void SDisplayClusterLightCardOutliner::GetSelectedLightCards(TArray<ADisplayClusterLightCardActor*>& OutSelectedLightCards) const
{
	TArray<FSceneOutlinerTreeItemPtr> SelectedOutlinerLightCards = SceneOutliner->GetSelectedItems();
	for (const FSceneOutlinerTreeItemPtr& SelectedLightCard : SelectedOutlinerLightCards)
	{
		TSharedPtr<FLightCardTreeItem> MatchingLightCard = GetLightCardTreeItemFromOutliner(SelectedLightCard);
		if (MatchingLightCard.IsValid() && MatchingLightCard->LightCardActor.IsValid())
		{
			OutSelectedLightCards.Add(MatchingLightCard->LightCardActor.Get());
		}
	}
}

void SDisplayClusterLightCardOutliner::SelectLightCards(const TArray<ADisplayClusterLightCardActor*>& LightCardsToSelect)
{
	TArray<ADisplayClusterLightCardActor*> ActualSelectedLightCards;
	TArray<FSceneOutlinerTreeItemPtr> SelectedTreeItems;
	CachedSelectedActors.Reset();
	
	for (const TSharedPtr<FLightCardTreeItem>& TreeItem : LightCardActors)
	{
		if (LightCardsToSelect.Contains(TreeItem->LightCardActor))
		{
			if (FSceneOutlinerTreeItemPtr OutlinerItem = SceneOutliner->GetTreeItem(TreeItem->LightCardActor.Get()))
			{
				SelectedTreeItems.Add(OutlinerItem);
				ActualSelectedLightCards.Add(TreeItem->LightCardActor.Get());
				CachedSelectedActors.Add(TreeItem->LightCardActor);
			}
		}
	}
	
	SceneOutliner->SetItemSelection(SelectedTreeItems, true);
	
	IDisplayClusterOperator::Get().ShowDetailsForObjects(*reinterpret_cast<TArray<UObject*>*>(&ActualSelectedLightCards));
}

void SDisplayClusterLightCardOutliner::RestoreCachedSelection()
{
	TArray<ADisplayClusterLightCardActor*> ActorsToSelect;
	for (const TWeakObjectPtr<ADisplayClusterLightCardActor>& CachedActor : CachedSelectedActors)
	{
		if (CachedActor.IsValid())
		{
			ActorsToSelect.Add(CachedActor.Get());
		}
	}
	
	SelectLightCards(MoveTemp(ActorsToSelect));
}

void SDisplayClusterLightCardOutliner::CreateWorldOutliner()
{
	auto OutlinerFilterPredicate = [this](const AActor* InActor)
	{
		return TrackedActors.Contains(InActor);
	};

	const FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	if (SceneOutliner.IsValid() && SceneOutlinerSelectionChanged.IsValid())
	{
		SceneOutliner->GetOnItemSelectionChanged().Remove(SceneOutlinerSelectionChanged);
	}
	
	FSceneOutlinerInitializationOptions SceneOutlinerOptions;

	// Explicitly add in the columns we want. Otherwise all column visibility can be toggled and this outliner only needs several.
	
	SceneOutlinerOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Gutter(),
		FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0, FCreateSceneOutlinerColumn(), false, TOptional<float>(),
				FSceneOutlinerBuiltInColumnTypes::Gutter_Localized()));
	
	SceneOutlinerOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(),
		FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10, FCreateSceneOutlinerColumn(), false, TOptional<float>(),
				FSceneOutlinerBuiltInColumnTypes::Label_Localized()));

	// Add a custom class/type column. The default ActorInfo column returns the class FName only, where as we want to display a friendly
	// display for the class.
	{
		// Based on struct FGetInfo from SceneOutlinerActorInfoColumn
		const FGetTextForItem InternalNameInfoText = FGetTextForItem::CreateLambda([](const ISceneOutlinerTreeItem& Item) -> FString
		{
			if (const FActorTreeItem* ActorItem = Item.CastTo<FActorTreeItem>())
			{
				if (const AActor* Actor = ActorItem->Actor.Get())
				{
					return Actor->GetClass()->GetDisplayNameText().ToString();
				}
			
				return FString();
			}
			if (Item.IsA<FFolderTreeItem>())
			{
				return LOCTEXT("FolderTypeName", "Folder").ToString();
			}
			if (Item.IsA<FWorldTreeItem>())
			{
				return LOCTEXT("WorldTypeName", "World").ToString();
			}

			return FString();
		});
		
		SceneOutlinerOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::ActorInfo(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 20,
			FCreateSceneOutlinerColumn::CreateStatic(&FTextInfoColumn::CreateTextInfoColumn, FSceneOutlinerBuiltInColumnTypes::ActorInfo(), InternalNameInfoText, FText::GetEmpty()),
			true, TOptional<float>(),
				FSceneOutlinerBuiltInColumnTypes::ActorInfo_Localized()));
	}
	
	SceneOutlinerOptions.ModifyContextMenu = FSceneOutlinerModifyContextMenu::CreateSP(this, &SDisplayClusterLightCardOutliner::RegisterContextMenu);
	SceneOutlinerOptions.OutlinerIdentifier = TEXT("LightCardEditorOutliner");
	SceneOutlinerOptions.Filters->AddFilterPredicate<FActorTreeItem>(FActorTreeItem::FFilterPredicate::CreateLambda(OutlinerFilterPredicate));

	TWeakPtr<SDisplayClusterLightCardOutliner> WeakPtrThis = SharedThis(this);
	UWorld* OutlinerWorld = RootActor.IsValid() ? RootActor->GetWorld() : nullptr;
	SceneOutlinerOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda([WeakPtrThis, SpecifiedWorld = MakeWeakObjectPtr(OutlinerWorld)](SSceneOutliner* Outliner)
		{
			return new FDisplayClusterLightCardOutlinerMode(Outliner, WeakPtrThis, SpecifiedWorld);
		});
	
	SceneOutliner = StaticCastSharedRef<SSceneOutliner>(SceneOutlinerModule.CreateSceneOutliner(SceneOutlinerOptions));
	SceneOutlinerSelectionChanged = SceneOutliner->GetOnItemSelectionChanged().AddRaw(this, &SDisplayClusterLightCardOutliner::OnOutlinerSelectionChanged);

	ChildSlot
	[
		SceneOutliner.ToSharedRef()
	];
}

bool SDisplayClusterLightCardOutliner::FillLightCardList()
{
	TArray<TSharedPtr<FLightCardTreeItem>> OriginalTree = MoveTemp(LightCardTree);
	LightCardActors.Empty();

	if (RootActor.IsValid())
	{
		FDisplayClusterConfigurationICVFX_VisibilityList& RootActorLightCards = RootActor->GetConfigData()->StageSettings.Lightcard.ShowOnlyList;

		for (const FActorLayer& ActorLayer : RootActorLightCards.ActorLayers)
		{
			TSharedPtr<FLightCardTreeItem> LightCardTreeItem = MakeShared<FLightCardTreeItem>();
			LightCardTreeItem->ActorLayer = ActorLayer.Name;
			LightCardTree.Add(LightCardTreeItem);
		}

		for (const TSoftObjectPtr<AActor>& LightCardActor : RootActorLightCards.Actors)
		{
			if (LightCardActor.IsValid() && LightCardActor->IsA<ADisplayClusterLightCardActor>())
			{
				TSharedPtr<FLightCardTreeItem> LightCardTreeItem = MakeShared<FLightCardTreeItem>();
				LightCardTreeItem->LightCardActor = CastChecked<ADisplayClusterLightCardActor>(LightCardActor.Get());
				LightCardTree.Add(LightCardTreeItem);
				LightCardActors.Add(LightCardTreeItem);
			}
		}

		// If there are any layers that are specified as light card layers, iterate over all actors in the world and 
		// add any that are members of any of the light card layers to the list. Only add an actor once, even if it is
		// in multiple layers
		if (RootActorLightCards.ActorLayers.Num())
		{
			if (UWorld* World = RootActor->GetWorld())
			{
				for (const TWeakObjectPtr<ADisplayClusterLightCardActor> WeakActor : TActorRange<ADisplayClusterLightCardActor>(World))
				{
					if (WeakActor.IsValid())
					{
						for (const FActorLayer& ActorLayer : RootActorLightCards.ActorLayers)
						{
							if (WeakActor->Layers.Contains(ActorLayer.Name))
							{
								TSharedPtr<FLightCardTreeItem> LightCardReference = MakeShared<FLightCardTreeItem>();
								LightCardReference->LightCardActor = WeakActor.Get();
								LightCardReference->ActorLayer = ActorLayer.Name;
								LightCardActors.Add(LightCardReference);
								break;
							}
						}
					}
				}
			}
		}
	}

	TrackedActors.Empty(LightCardActors.Num());

	for (const TSharedPtr<FLightCardTreeItem>& LightCard : LightCardActors)
	{
		TrackedActors.Add(LightCard->LightCardActor.Get(), LightCard);
	}
	
	// Check if the tree items have changed.
	{
		if (OriginalTree.Num() != LightCardTree.Num())
		{
			return true;
		}
	
		for (const TSharedPtr<FLightCardTreeItem>& OriginalTreeItem : OriginalTree)
		{
			if (!LightCardTree.ContainsByPredicate([OriginalTreeItem](const TSharedPtr<FLightCardTreeItem>& LightCardTreeItem)
			{
				return OriginalTreeItem.IsValid() && LightCardTreeItem.IsValid() &&
					OriginalTreeItem->LightCardActor.Get() == LightCardTreeItem->LightCardActor.Get() &&
						OriginalTreeItem->ActorLayer == LightCardTreeItem->ActorLayer;
			}))
			{
				return true;
			}
		}
	}

	return false;
}

void SDisplayClusterLightCardOutliner::OnOutlinerSelectionChanged(FSceneOutlinerTreeItemPtr TreeItem,
	ESelectInfo::Type Type)
{
	MostRecentSelectedItem = TreeItem;

	TArray<ADisplayClusterLightCardActor*> SelectedLightCards;
	GetSelectedLightCards(SelectedLightCards);

	IDisplayClusterOperator::Get().ShowDetailsForObjects(*reinterpret_cast<TArray<UObject*>*>(&SelectedLightCards));

	if (LightCardEditorPtr.IsValid())
	{
		LightCardEditorPtr.Pin()->SelectLightCardProxies(SelectedLightCards);
	}
}

TSharedPtr<SDisplayClusterLightCardOutliner::FLightCardTreeItem> SDisplayClusterLightCardOutliner::
GetLightCardTreeItemFromOutliner(FSceneOutlinerTreeItemPtr InOutlinerTreeItem) const
{
	if (const FActorTreeItem* ActorTreeItem = InOutlinerTreeItem->CastTo<FActorTreeItem>())
	{
		if (const TSharedPtr<FLightCardTreeItem>* MatchingLightCard = TrackedActors.Find(
			ActorTreeItem->Actor.Get()))
		{
			return *MatchingLightCard;
		}
	}

	return nullptr;
}

AActor* SDisplayClusterLightCardOutliner::GetActorFromTreeItem(FSceneOutlinerTreeItemPtr InOutlinerTreeItem) const
{
	if (InOutlinerTreeItem != nullptr)
	{
		if (const FActorTreeItem* ActorTreeItem = InOutlinerTreeItem->CastTo<FActorTreeItem>())
		{
			return ActorTreeItem->Actor.Get();
		}
	}

	return nullptr;
}

FReply SDisplayClusterLightCardOutliner::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SDisplayClusterLightCardOutliner::RegisterContextMenu(FName& InName, FToolMenuContext& InContext)
{
	static const FName LightCardEditorSectionName = "DynamicLightCardSection";
	
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ensure(ToolMenus->IsMenuRegistered(InName)))
	{
		UToolMenu* Menu = ToolMenus->FindMenu(InName);

		InContext.AddCleanup([Menu]()
		{
			Menu->RemoveSection(LightCardEditorSectionName);
		});

		check(LightCardEditorPtr.IsValid());
		InContext.AppendCommandList(LightCardEditorPtr.Pin()->GetCommandList());
		
		Menu->AddDynamicSection(LightCardEditorSectionName, FNewToolMenuDelegate::CreateLambda([this](UToolMenu* InMenu)
		{
			USceneOutlinerMenuContext* Context = InMenu->FindContext<USceneOutlinerMenuContext>();
			if (!Context || !Context->SceneOutliner.IsValid())
			{
				return;
			}
			
			AActor* Actor = GetActorFromTreeItem(MostRecentSelectedItem.Pin());

			if (Actor == nullptr && Context->SceneOutliner.Pin()->GetSelection().Num() == 1)
			{
				// If a delete operation was undone the most recent selection will be invalid.
				// Assume a single entry light card is correct.
				TArray<ADisplayClusterLightCardActor*> SelectedLightCardActors;
				GetSelectedLightCards(SelectedLightCardActors);
				if (SelectedLightCardActors.Num() == 1)
				{
					Actor = SelectedLightCardActors[0];
				}
			}
			
			bool bAddFullEditMenu = false;
			
			if (ADisplayClusterLightCardActor* LightCardActor = Cast<ADisplayClusterLightCardActor>(Actor))
			{
				FToolMenuSection& Section = InMenu->AddSection("LightCardDefaultSection", LOCTEXT("LightCardSectionName", "Light Cards"));

				Section.AddMenuEntry(FDisplayClusterLightCardEditorCommands::Get().RemoveLightCard);
				Section.AddMenuEntry(FDisplayClusterLightCardEditorCommands::Get().SaveLightCardTemplate);
				bAddFullEditMenu = true;
			}
			
			FToolMenuSection& Section = InMenu->AddSection("LightCardEditSection", LOCTEXT("LightCardEditSectionName", "Edit"));

			if (bAddFullEditMenu)
			{
				Section.AddMenuEntry(FGenericCommands::Get().Cut);
				Section.AddMenuEntry(FGenericCommands::Get().Copy);
			}
			
			Section.AddMenuEntry(FGenericCommands::Get().Paste);
			
			if (bAddFullEditMenu)
			{	
				Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
				Section.AddMenuEntry(FGenericCommands::Get().Delete);
			}
		}));
	}
}

#undef LOCTEXT_NAMESPACE
