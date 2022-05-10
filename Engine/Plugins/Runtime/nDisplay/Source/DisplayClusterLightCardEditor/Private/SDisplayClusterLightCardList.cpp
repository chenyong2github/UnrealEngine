// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterLightCardList.h"

#include "SDisplayClusterLightCardEditor.h"
#include "DisplayClusterLightCardEditorCommands.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterLightCardActor.h"
#include "DisplayClusterConfigurationTypes.h"
#include "IDisplayClusterOperator.h"
#include "Components/DisplayClusterCameraComponent.h"

#include "ClassIconFinder.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "SPositiveActionButton.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Workflow/SWizard.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterLightCardList"

namespace DisplayClusterLightCardListColumnNames
{
	const static FName LightCardName(TEXT("LightCardName"));
};

class SLightCardTreeItemRow : public SMultiColumnTableRow<TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>>
{
	SLATE_BEGIN_ARGS(SLightCardTreeItemRow) {}
	SLATE_END_ARGS()

public:

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem> InLightCardTreeItem)
	{
		LightCardTreeItem = InLightCardTreeItem;

		SMultiColumnTableRow<TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>>::Construct(
			SMultiColumnTableRow<TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>>::FArguments()
			.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SceneOutliner.TableViewRow"))
		, InOwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == DisplayClusterLightCardListColumnNames::LightCardName)
		{
			return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6, 0, 0, 0)
			[
				SNew(SExpanderArrow, SharedThis(this))
				.IndentAmount(12)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0, 1, 6, 1))
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(16)
				.HeightOverride(16)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(this, &SLightCardTreeItemRow::GetLightCardIcon)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(this, &SLightCardTreeItemRow::GetLightCardDisplayName)
			];
		}

		return SNullWidget::NullWidget;
	}

private:
	FText GetLightCardDisplayName() const
	{
		FString ItemName = TEXT("");
		if (LightCardTreeItem.IsValid())
		{
			TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem> LightCardTreeItemPin = LightCardTreeItem.Pin();

			if (LightCardTreeItemPin->IsActorLayer())
			{
				ItemName = LightCardTreeItemPin->ActorLayer.ToString();
			}
			else if (LightCardTreeItemPin->LightCardActor.IsValid())
			{
				ItemName = LightCardTreeItemPin->LightCardActor->GetActorNameOrLabel();
			}
		}

		return FText::FromString(*ItemName);
	}

	const FSlateBrush* GetLightCardIcon() const
	{
		FString ActorName = TEXT("");
		if (LightCardTreeItem.IsValid())
		{
			TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem> LightCardTreeItemPin = LightCardTreeItem.Pin();
			if (LightCardTreeItemPin->IsActorLayer())
			{
				return FAppStyle::Get().GetBrush(TEXT("Layer.Icon16x"));
			}
			else if (LightCardTreeItemPin->LightCardActor.IsValid())
			{
				return FClassIconFinder::FindIconForActor(LightCardTreeItemPin->LightCardActor.Get());
			}
		}

		return nullptr;
	}

private:
	TWeakPtr<SDisplayClusterLightCardList::FLightCardTreeItem> LightCardTreeItem;
};


void SDisplayClusterLightCardList::Construct(const FArguments& InArgs, TSharedPtr<SDisplayClusterLightCardEditor> InLightCardEditor)
{
	LightCardEditorPtr = InLightCardEditor;
	OnLightCardChanged = InArgs._OnLightCardListChanged;
	
	BindCommands();

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Padding(0)
			.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.Padding(3.0f, 3.0f)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
						
					SNew(SPositiveActionButton)
					.Text(LOCTEXT("AddNewItem", "Add"))
					.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
					.ToolTipText(LOCTEXT("AddNewLightCardTooltip", "Adds a new Light Card to the display cluster"))
					.OnGetMenuContent(this, &SDisplayClusterLightCardList::CreateAddNewMenuContent)
				]
			]
		]
		+SVerticalBox::Slot()
		[
			SAssignNew(LightCardTreeView, STreeView<TSharedPtr<FLightCardTreeItem>>)
			.TreeItemsSource(&LightCardTree)
			.ItemHeight(28)
			.SelectionMode(ESelectionMode::Multi)
			.OnGenerateRow(this, &SDisplayClusterLightCardList::GenerateTreeItemRow)
			.OnGetChildren(this, &SDisplayClusterLightCardList::GetChildrenForTreeItem)
			.OnSelectionChanged(this, &SDisplayClusterLightCardList::OnTreeItemSelected)
			.OnContextMenuOpening(this, &SDisplayClusterLightCardList::CreateContextMenu)
			.HighlightParentNodesForSelection(true)
			.HeaderRow
			(
				SNew(SHeaderRow)

				+ SHeaderRow::Column(DisplayClusterLightCardListColumnNames::LightCardName)
				.DefaultLabel(LOCTEXT("LightCardName", "Light Card"))
				.FillWidth(0.8f)
			)
		]
	];
}

void SDisplayClusterLightCardList::SetRootActor(ADisplayClusterRootActor* NewRootActor)
{
	TArray<TSharedPtr<FLightCardTreeItem>> PreviouslySelectedLightCards;
	LightCardTreeView->GetSelectedItems(PreviouslySelectedLightCards);
	
	RootActor = NewRootActor;
	if (FillLightCardList())
	{
		LightCardTreeView->RebuildList();
	}
	
	for (const TSharedPtr<FLightCardTreeItem>& LightCardTreeItem : LightCardTree)
	{
		if (LightCardTreeItem->IsActorLayer())
		{
			LightCardTreeView->SetItemExpansion(LightCardTreeItem, true);
		}
	}

	// Select previously selected light cards. This fixes an issue where the details panel may clear in some cases
	// and also supports maintaining the selection of shared light cards across different root actors.
	{
		TArray<AActor*> LightCardActorsToSelect;
		for (const TSharedPtr<FLightCardTreeItem>& LightCard : PreviouslySelectedLightCards)
		{
			if (LightCard.IsValid() && LightCard->LightCardActor.IsValid())
			{
				LightCardActorsToSelect.Add(LightCard.Get()->LightCardActor.Get());
			}
		}

		SelectLightCards(MoveTemp(LightCardActorsToSelect));
	}
}

void SDisplayClusterLightCardList::SelectLightCards(const TArray<AActor*>& LightCardsToSelect)
{
	LightCardTreeView->ClearSelection();

	TArray<TSharedPtr<FLightCardTreeItem>> SelectedTreeItems;
	for (const TSharedPtr<FLightCardTreeItem>& TreeItem : LightCardActors)
	{
		if (LightCardsToSelect.Contains(TreeItem->LightCardActor))
		{
			SelectedTreeItems.Add(TreeItem);
		}
	}

	LightCardTreeView->SetItemSelection(SelectedTreeItems, true);
}

void SDisplayClusterLightCardList::Refresh()
{
	SetRootActor(RootActor.Get());
}

void SDisplayClusterLightCardList::BindCommands()
{
	const FDisplayClusterLightCardEditorCommands& Commands = FDisplayClusterLightCardEditorCommands::Get();

	CommandList = MakeShareable(new FUICommandList);
	
	CommandList->MapAction(
		Commands.AddNewLightCard,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardList::AddNewLightCard),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardList::CanAddLightCard),
		FCanExecuteAction());

	CommandList->MapAction(
		Commands.AddExistingLightCard,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardList::AddExistingLightCard),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardList::CanAddLightCard),
		FCanExecuteAction());
	
	CommandList->MapAction(
		Commands.RemoveLightCard,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardList::RemoveLightCard, false),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardList::CanRemoveLightCard),
		FCanExecuteAction());

	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardList::RemoveLightCard, true),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardList::CanRemoveLightCard),
		FCanExecuteAction());
}

bool SDisplayClusterLightCardList::FillLightCardList()
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
			if (LightCardActor.IsValid())
			{
				TSharedPtr<FLightCardTreeItem> LightCardTreeItem = MakeShared<FLightCardTreeItem>();
				LightCardTreeItem->LightCardActor = LightCardActor.Get();
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
				for (const TWeakObjectPtr<AActor> WeakActor : FActorRange(World))
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

void SDisplayClusterLightCardList::AddNewLightCard()
{
	check(RootActor.IsValid());
	
	FScopedTransaction Transaction(LOCTEXT("AddNewLightCard", "Add New Light Card"));
	
	const FVector SpawnLocation = RootActor->GetDefaultCamera()->GetComponentLocation();
	FRotator SpawnRotation = RootActor->GetDefaultCamera()->GetComponentRotation();
	SpawnRotation.Yaw -= 180.f;
	
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.bNoFail = true;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParameters.Name = TEXT("LightCard");
	SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	SpawnParameters.OverrideLevel = RootActor->GetLevel();
	
	ADisplayClusterLightCardActor* NewActor = CastChecked<ADisplayClusterLightCardActor>(
		RootActor->GetWorld()->SpawnActor(ADisplayClusterLightCardActor::StaticClass(),
		&SpawnLocation, &SpawnRotation, MoveTemp(SpawnParameters)));

	ensure(NewActor->GetLevel() == RootActor->GetLevel());
	NewActor->SetActorLabel(NewActor->GetName());

	// Parent to the root actor, since the typical intention is for the LC to move with the stage.
	NewActor->AttachToActor(RootActor.Get(), FAttachmentTransformRules::KeepWorldTransform);

	AddLightCardToActor(NewActor);

	// When adding a new lightcard, usually the desired location is in the middle of the viewport
	if (LightCardEditorPtr.IsValid())
	{
		LightCardEditorPtr.Pin()->CenterLightCardInView(*NewActor);
	}
}

void SDisplayClusterLightCardList::AddExistingLightCard()
{
	TSharedPtr<SWindow> PickerWindow;
	TWeakObjectPtr<AActor> SelectedActorPtr;
	bool bFinished = false;
	
	const TSharedRef<SWidget> ActorPicker = PropertyCustomizationHelpers::MakeActorPickerWithMenu(
		nullptr,
		false,
		FOnShouldFilterActor::CreateLambda([&](const AActor* const InActor) -> bool // ActorFilter
		{
			const bool IsAllowed = InActor != nullptr && !InActor->IsChildActor() && InActor->IsA<AActor>() &&
				!InActor->GetClass()->HasAnyClassFlags(CLASS_Interface)	&& !InActor->IsA<ADisplayClusterRootActor>();
			
			return IsAllowed;
		}),
		FOnActorSelected::CreateLambda([&](AActor* InActor) -> void // OnSet
		{
			SelectedActorPtr = InActor;
		}),
		FSimpleDelegate::CreateLambda([&]() -> void // OnClose
		{
		}),
		FSimpleDelegate::CreateLambda([&]() -> void // OnUseSelected
		{
			if (AActor* Selection = Cast<AActor>(GEditor->GetSelectedActors()->GetTop(AActor::StaticClass())))
			{
				SelectedActorPtr = Selection;
			}
		}));
	
	PickerWindow = SNew(SWindow)
	.Title(LOCTEXT("AddExistingLightCard", "Select an existing Light Card actor"))
	.ClientSize(FVector2D(500, 525))
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		[
			SNew(SWizard)
			.FinishButtonText(LOCTEXT("FinishAddingExistingLightCard", "Add Actor"))
			.OnCanceled(FSimpleDelegate::CreateLambda([&]()
			{
				if (PickerWindow.IsValid())
				{
					PickerWindow->RequestDestroyWindow();
				}
			}))
			.OnFinished(FSimpleDelegate::CreateLambda([&]()
			{
				bFinished = true;
				if (PickerWindow.IsValid())
				{
					PickerWindow->RequestDestroyWindow();
				}
			}))
			.CanFinish(TAttribute<bool>::CreateLambda([&]()
			{
				return SelectedActorPtr.IsValid();
			}))
			.ShowPageList(false)
			+SWizard::Page()
			.CanShow(true)
			[
				SNew(SBorder)
				.VAlign(VAlign_Fill)
				.BorderImage(FAppStyle::GetBrush("Menu.Background"))
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.AutoHeight()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					[
						ActorPicker
					]
					+SVerticalBox::Slot()
					.AutoHeight()
					.VAlign(VAlign_Bottom)
					.Padding(0.f, 8.f)
					[
						SNew(STextBlock)
						.TextStyle(FAppStyle::Get(), "NormalText.Important")
						.Text_Lambda([&]
						{
							const FString Result = FString::Printf(TEXT("Selected Actor: %s"),
								SelectedActorPtr.IsValid() ? *SelectedActorPtr->GetActorLabel() : TEXT(""));
							return FText::FromString(Result);
						})
					]
				]
			]
		]
	];

	GEditor->EditorAddModalWindow(PickerWindow.ToSharedRef());
	if (bFinished && SelectedActorPtr.IsValid())
	{
		FScopedTransaction Transaction(LOCTEXT("AddExistingLightCard", "Add Existing Light Card"));
		AddLightCardToActor(SelectedActorPtr.Get());
	}

	PickerWindow.Reset();
	SelectedActorPtr.Reset();
}

void SDisplayClusterLightCardList::AddLightCardToActor(AActor* LightCard)
{
	check(LightCard);
	
	UDisplayClusterConfigurationData* ConfigData = RootActor->GetConfigData();
	ConfigData->Modify();
	FDisplayClusterConfigurationICVFX_VisibilityList& RootActorLightCards = ConfigData->StageSettings.Lightcard.ShowOnlyList;

	if (!RootActorLightCards.Actors.ContainsByPredicate([&](const TSoftObjectPtr<AActor>& Actor)
	{
		// Don't add if a loaded actor is already present.
		return Actor.Get() == LightCard;
	}))
	{
		const TSoftObjectPtr<AActor> LightCardSoftObject(LightCard);
		
		// Remove any exact paths to this actor. It's possible invalid actors are present if a light card
		// was force deleted from a level.
		RootActorLightCards.Actors.RemoveAll([&] (const TSoftObjectPtr<AActor>& Actor)
		{
			return Actor == LightCardSoftObject;
		});
	
		RootActorLightCards.Actors.Add(LightCard);
	}
	Refresh();
	OnLightCardChanged.ExecuteIfBound();
}

bool SDisplayClusterLightCardList::CanAddLightCard() const
{
	return RootActor.IsValid() && RootActor->GetWorld() != nullptr;
}

void SDisplayClusterLightCardList::RemoveLightCard(bool bDeleteLightCardActor)
{
	TArray<TSharedPtr<FLightCardTreeItem>> SelectedTreeItems;
	LightCardTreeView->GetSelectedItems(SelectedTreeItems);

	FScopedTransaction Transaction(LOCTEXT("RemoveLightCard", "Remove Light Card(s)"));

	USelection* EdSelectionManager = GEditor->GetSelectedActors();
	UWorld* WorldToUse = nullptr;
	
	if (bDeleteLightCardActor)
	{
		EdSelectionManager->BeginBatchSelectOperation();
		EdSelectionManager->Modify();
		EdSelectionManager->DeselectAll();
	}
	
	for (const TSharedPtr<FLightCardTreeItem>& Item : SelectedTreeItems)
	{
		if (Item.IsValid() && Item->LightCardActor.IsValid())
		{
			if (RootActor.IsValid())
			{
				UDisplayClusterConfigurationData* ConfigData = RootActor->GetConfigData();
				ConfigData->Modify();
				
				FDisplayClusterConfigurationICVFX_VisibilityList& RootActorLightCards = ConfigData->StageSettings.Lightcard.ShowOnlyList;
				RootActorLightCards.Actors.RemoveAll([&](const TSoftObjectPtr<AActor>& Actor)
				{
					return Actor.Get() == Item->LightCardActor.Get();
				});
			}

			if (bDeleteLightCardActor)
			{
				WorldToUse = Item->LightCardActor->GetWorld();
				GEditor->SelectActor(Item->LightCardActor.Get(), /*bSelect =*/true, /*bNotifyForActor =*/false, /*bSelectEvenIfHidden =*/true);
			}
		}
	}

	if (bDeleteLightCardActor)
	{
		EdSelectionManager->EndBatchSelectOperation();

		if (WorldToUse)
		{
			GEditor->edactDeleteSelected(WorldToUse);
		}
	}

	Refresh();
	OnLightCardChanged.ExecuteIfBound();
}

bool SDisplayClusterLightCardList::CanRemoveLightCard() const
{
	TArray<TSharedPtr<FLightCardTreeItem>> SelectedTreeItems;
	LightCardTreeView->GetSelectedItems(SelectedTreeItems);
	return SelectedTreeItems.Num() > 0;
}

TSharedRef<ITableRow> SDisplayClusterLightCardList::GenerateTreeItemRow(TSharedPtr<FLightCardTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SLightCardTreeItemRow, OwnerTable, Item);
}

void SDisplayClusterLightCardList::GetChildrenForTreeItem(TSharedPtr<FLightCardTreeItem> InItem, TArray<TSharedPtr<FLightCardTreeItem>>& OutChildren)
{
	if (InItem.IsValid())
	{
		if (InItem->IsActorLayer())
		{
			for (const TSharedPtr<FLightCardTreeItem>& LightCardActor : LightCardActors)
			{
				if (LightCardActor->IsInActorLayer() && LightCardActor->ActorLayer == InItem->ActorLayer)
				{
					OutChildren.Add(LightCardActor);
				}
			}
		}
	}
}

void SDisplayClusterLightCardList::OnTreeItemSelected(TSharedPtr<FLightCardTreeItem> InItem, ESelectInfo::Type SelectInfo)
{
	TArray<TSharedPtr<FLightCardTreeItem>> SelectedTreeItems;
	LightCardTreeView->GetSelectedItems(SelectedTreeItems);

	TArray<AActor*> SelectedLightCards;
	for (const TSharedPtr<FLightCardTreeItem>& SelectedTreeItem : SelectedTreeItems)
	{
		if (SelectedTreeItem->LightCardActor.IsValid())
		{
			SelectedLightCards.Add(SelectedTreeItem->LightCardActor.Get());
		}
	}

	IDisplayClusterOperator::Get().ShowDetailsForObjects(*reinterpret_cast<TArray<UObject*>*>(&SelectedLightCards));

	if (LightCardEditorPtr.IsValid())
	{
		LightCardEditorPtr.Pin()->SelectLightCardProxies(SelectedLightCards);
	}
}

FReply SDisplayClusterLightCardList::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SDisplayClusterLightCardList::CreateAddNewMenuContent()
{
	const FDisplayClusterLightCardEditorCommands& Commands = FDisplayClusterLightCardEditorCommands::Get();
	
	const bool bCloseAfterSelection = true;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, CommandList, Extenders);
	MenuBuilder.AddMenuEntry(Commands.AddNewLightCard);
	MenuBuilder.AddMenuEntry(Commands.AddExistingLightCard);
	
	return MenuBuilder.MakeWidget();
}

TSharedPtr<SWidget> SDisplayClusterLightCardList::CreateContextMenu()
{
	const FDisplayClusterLightCardEditorCommands& Commands = FDisplayClusterLightCardEditorCommands::Get();
	
	const bool bCloseAfterSelection = true;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, CommandList, Extenders);
	MenuBuilder.AddMenuEntry(Commands.RemoveLightCard);
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
