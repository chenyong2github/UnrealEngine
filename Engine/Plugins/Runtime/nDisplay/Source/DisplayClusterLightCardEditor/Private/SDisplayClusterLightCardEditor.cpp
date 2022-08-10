// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterLightCardEditor.h"

#include "DisplayClusterLightCardEditorCommands.h"
#include "DisplayClusterLightCardEditorStyle.h"
#include "IDisplayClusterLightCardEditor.h"
#include "SDisplayClusterLightCardList.h"
#include "LightCardTemplates/SDisplayClusterLightCardTemplateList.h"
#include "LightCardTemplates/DisplayClusterLightCardTemplate.h"
#include "LightCardTemplates/DisplayClusterLightCardTemplateHelpers.h"
#include "Settings/DisplayClusterLightCardEditorSettings.h"

#include "Viewport/DisplayClusterLightcardEditorViewport.h"
#include "Viewport/DisplayClusterLightCardEditorViewportClient.h"

#include "IDisplayClusterOperator.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Components/DisplayClusterCameraComponent.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "IContentBrowserSingleton.h"
#include "Misc/FileHelper.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Misc/TransactionObjectEvent.h"
#include "Selection.h"
#include "Styling/SlateIconFinder.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Workflow/SWizard.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterLightCardEditor"

const FName SDisplayClusterLightCardEditor::TabName = TEXT("DisplayClusterLightCardEditorTab");

void SDisplayClusterLightCardEditor::RegisterTabSpawner()
{
	IDisplayClusterOperator::Get().OnRegisterLayoutExtensions().AddStatic(&SDisplayClusterLightCardEditor::RegisterLayoutExtension);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateStatic(&SDisplayClusterLightCardEditor::SpawnInTab))
		.SetDisplayName(LOCTEXT("TabDisplayName", "Light Cards Editor"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Editing tools for nDisplay light cards."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void SDisplayClusterLightCardEditor::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

void SDisplayClusterLightCardEditor::RegisterLayoutExtension(FLayoutExtender& InExtender)
{
	FTabManager::FTab NewTab(FTabId(TabName, ETabIdFlags::SaveLayout), ETabState::OpenedTab);
	InExtender.ExtendStack(IDisplayClusterOperator::Get().GetOperatorExtensionId(), ELayoutExtensionPosition::After, NewTab);
}

TSharedRef<SDockTab> SDisplayClusterLightCardEditor::SpawnInTab(const FSpawnTabArgs& SpawnTabArgs)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		// Prevent close until we can add a menu item in the operator panel to spawn this tab.
		.OnCanCloseTab(SDockTab::FCanCloseTab::CreateLambda([]() -> bool { return false; }));
	
	MajorTab->SetContent(SNew(SDisplayClusterLightCardEditor, MajorTab, SpawnTabArgs.GetOwnerWindow()));

	return MajorTab;
}

SDisplayClusterLightCardEditor::~SDisplayClusterLightCardEditor()
{
	IDisplayClusterOperator::Get().OnActiveRootActorChanged().Remove(ActiveRootActorChangedHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);

	if (GEngine != nullptr)
	{
		GEngine->OnLevelActorDeleted().RemoveAll(this);
	}

	if (OnObjectTransactedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectTransacted.Remove(OnObjectTransactedHandle);
	}

	RemoveCompileDelegates();
}

void SDisplayClusterLightCardEditor::PostUndo(bool bSuccess)
{
	FEditorUndoClient::PostUndo(bSuccess);
	RefreshLabels();
}

void SDisplayClusterLightCardEditor::PostRedo(bool bSuccess)
{
	FEditorUndoClient::PostRedo(bSuccess);
	RefreshLabels();
}

void SDisplayClusterLightCardEditor::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& MajorTabOwner, const TSharedPtr<SWindow>& WindowOwner)
{
	ActiveRootActorChangedHandle = IDisplayClusterOperator::Get().OnActiveRootActorChanged().AddSP(this, &SDisplayClusterLightCardEditor::OnActiveRootActorChanged);
	if (GEngine != nullptr)
	{
		GEngine->OnLevelActorDeleted().AddSP(this, &SDisplayClusterLightCardEditor::OnLevelActorDeleted);
	}
	
	OnObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddSP(this, &SDisplayClusterLightCardEditor::OnObjectTransacted);

	BindCommands();
	RegisterToolbarExtensions();
	
	TabManager = FGlobalTabmanager::Get()->NewTabManager(MajorTabOwner);

	const FName LightCardTab = TEXT("LightCards");
	const FName ViewportTab = TEXT("Viewport"); 
	
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("LightCardEditor")
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		
		->Split
		(
			FTabManager::NewSplitter()
			->SetSizeCoefficient(0.25f)
			->SetOrientation(Orient_Vertical)
			->Split
			(
			FTabManager::NewStack()
				->AddTab(LightCardTab, ETabState::OpenedTab)
				->SetHideTabWell(true)
				->SetForegroundTab(LightCardTab)
			)
		)
		->Split
		(
			FTabManager::NewStack()
			->AddTab(ViewportTab, ETabState::OpenedTab)
			->SetSizeCoefficient(0.75f)
			->SetHideTabWell(true)
			->SetForegroundTab(ViewportTab)
		)
	);

	TabManager->RegisterTabSpawner(LightCardTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
	{
		return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		.CanEverClose(false)
		.OnCanCloseTab(SDockTab::FCanCloseTab::CreateLambda([](){ return false; }))
		.ContentPadding(0)
		[
			CreateLightCardListWidget()
		];
	}));
	
	TabManager->RegisterTabSpawner(ViewportTab, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
	{
		return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		.CanEverClose(false)
		.OnCanCloseTab(SDockTab::FCanCloseTab::CreateLambda([](){ return false; }))
		.ContentPadding(0)
		[
			CreateViewportWidget()
		];
	}));
	
	ChildSlot                     
	[
		TabManager->RestoreFrom(Layout, WindowOwner).ToSharedRef()
	];

	BindCompileDelegates();

	RefreshLabels();

	GEditor->RegisterForUndo(this);
}

void SDisplayClusterLightCardEditor::SelectLightCards(const TArray<ADisplayClusterLightCardActor*>& LightCardsToSelect)
{
	check(LightCardList);
	LightCardList->SelectLightCards(LightCardsToSelect);
}

void SDisplayClusterLightCardEditor::GetSelectedLightCards(TArray<ADisplayClusterLightCardActor*>& OutSelectedLightCards)
{
	check(LightCardList);
	LightCardList->GetSelectedLightCards(OutSelectedLightCards);
}

void SDisplayClusterLightCardEditor::SelectLightCardProxies(const TArray<ADisplayClusterLightCardActor*>& LightCardsToSelect)
{
	check(ViewportView);
	ViewportView->GetLightCardEditorViewportClient()->SelectLightCards(LightCardsToSelect);
}

void SDisplayClusterLightCardEditor::CenterLightCardInView(ADisplayClusterLightCardActor& LightCard)
{
	check(ViewportView);
	ViewportView->GetLightCardEditorViewportClient()->CenterLightCardInView(LightCard);
}

ADisplayClusterLightCardActor* SDisplayClusterLightCardEditor::SpawnLightCard()
{
	if (!ActiveRootActor.IsValid())
	{
		return nullptr;
	}

	const FVector SpawnLocation = ActiveRootActor->GetDefaultCamera()->GetComponentLocation();
	FRotator SpawnRotation = ActiveRootActor->GetDefaultCamera()->GetComponentRotation();
	SpawnRotation.Yaw -= 180.f;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.bNoFail = true;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParameters.Name = TEXT("LightCard");
	SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	SpawnParameters.OverrideLevel = ActiveRootActor->GetWorld()->GetCurrentLevel();

	ADisplayClusterLightCardActor* NewLightCard = CastChecked<ADisplayClusterLightCardActor>(
		ActiveRootActor->GetWorld()->SpawnActor(ADisplayClusterLightCardActor::StaticClass(),
			&SpawnLocation, &SpawnRotation, MoveTemp(SpawnParameters)));

	NewLightCard->SetActorLabel(NewLightCard->GetName());

	if (ViewportView.IsValid())
	{
		const EDisplayClusterMeshProjectionType ProjectionMode = ViewportView->GetLightCardEditorViewportClient()->GetProjectionMode();
		if (ProjectionMode == EDisplayClusterMeshProjectionType::UV)
		{
			NewLightCard->bIsUVLightCard = true;
		}
	}

	TArray<ADisplayClusterLightCardActor*> LightCards { NewLightCard } ;
	AddLightCardsToActor(LightCards);

	return NewLightCard;
}

ADisplayClusterLightCardActor* SDisplayClusterLightCardEditor::SpawnLightCardFromTemplate(
	const UDisplayClusterLightCardTemplate* InTemplate, ULevel* InLevel, bool bIsPreview)
{
	if (!ActiveRootActor.IsValid())
	{
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("SpawnLightCardFromTemplateTransactionMessage", "Spawn Light Card from Template"));
	
	check(InTemplate && InTemplate->LightCardActor);

	ULevel* Level = InLevel ? InLevel : ActiveRootActor->GetWorld()->GetCurrentLevel();
	
	FName UniqueName = *InTemplate->GetName().Replace(TEXT("Template"), TEXT(""));
	if (StaticFindObjectFast(InTemplate->LightCardActor->GetClass(), Level, UniqueName))
	{
		UniqueName = MakeUniqueObjectName(Level, InTemplate->LightCardActor->GetClass(), UniqueName);
	}
	
	// Duplicate, don't copy properties or spawn from a template. Doing so will copy component data incorrectly,
	// specifically the static mesh override textures. They will be parented to the template, not the level instance
	// and prevent the map from saving.
	ADisplayClusterLightCardActor* NewLightCard = CastChecked<ADisplayClusterLightCardActor>(StaticDuplicateObject(InTemplate->LightCardActor.Get(), Level, UniqueName));
	Level->AddLoadedActor(NewLightCard);
	
	if (!bIsPreview)
	{
		NewLightCard->SetActorLabel(NewLightCard->GetName());
	
		const TArray<ADisplayClusterLightCardActor*> LightCards { NewLightCard } ;
		AddLightCardsToActor(LightCards);

		FDisplayClusterLightCardEditorRecentItem RecentlyPlacedItem;
		RecentlyPlacedItem.ObjectPath = InTemplate;
		RecentlyPlacedItem.ItemType = FDisplayClusterLightCardEditorRecentItem::Type_LightCardTemplate;
		
		AddRecentlyPlacedItem(MoveTemp(RecentlyPlacedItem));
	}
	
	return NewLightCard;
}

void SDisplayClusterLightCardEditor::AddNewLightCard()
{
	check(ActiveRootActor.IsValid());

	FScopedTransaction Transaction(LOCTEXT("AddNewLightCardTransactionMessage", "Add New Light Card"));

	ADisplayClusterLightCardActor* NewLightCard = SpawnLightCard();

	// When adding a new lightcard, usually the desired location is in the middle of the viewport
	if (NewLightCard)
	{
		CenterLightCardInView(*NewLightCard);

		FDisplayClusterLightCardEditorRecentItem RecentlyPlacedItem;
		RecentlyPlacedItem.ObjectPath = NewLightCard->GetClass();
		RecentlyPlacedItem.ItemType = FDisplayClusterLightCardEditorRecentItem::Type_LightCard;
		
		AddRecentlyPlacedItem(MoveTemp(RecentlyPlacedItem));
	}
}

void SDisplayClusterLightCardEditor::AddExistingLightCard()
{
	TSharedPtr<SWindow> PickerWindow;
	TWeakObjectPtr<ADisplayClusterLightCardActor> SelectedActorPtr;
	bool bFinished = false;
	
	const TSharedRef<SWidget> ActorPicker = PropertyCustomizationHelpers::MakeActorPickerWithMenu(
		nullptr,
		false,
		FOnShouldFilterActor::CreateLambda([&](const AActor* const InActor) -> bool // ActorFilter
		{
			const bool IsAllowed = InActor != nullptr && !InActor->IsChildActor() && InActor->IsA<ADisplayClusterLightCardActor>() &&
				!InActor->GetClass()->HasAnyClassFlags(CLASS_Interface)	&& !InActor->IsA<ADisplayClusterRootActor>();
			
			return IsAllowed;
		}),
		FOnActorSelected::CreateLambda([&](AActor* InActor) -> void // OnSet
		{
			SelectedActorPtr = Cast<ADisplayClusterLightCardActor>(InActor);
		}),
		FSimpleDelegate::CreateLambda([&]() -> void // OnClose
		{
		}),
		FSimpleDelegate::CreateLambda([&]() -> void // OnUseSelected
		{
			if (ADisplayClusterLightCardActor* Selection = Cast<ADisplayClusterLightCardActor>(GEditor->GetSelectedActors()->GetTop(ADisplayClusterLightCardActor::StaticClass())))
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
		FScopedTransaction Transaction(LOCTEXT("AddExistingLightCardTransactionMessage", "Add Existing Light Card"));

		TArray<ADisplayClusterLightCardActor*> LightCards { SelectedActorPtr.Get() };
		AddLightCardsToActor(LightCards);
	}

	PickerWindow.Reset();
	SelectedActorPtr.Reset();
}

void SDisplayClusterLightCardEditor::AddLightCardsToActor(TArray<ADisplayClusterLightCardActor*> LightCards)
{
	if (ActiveRootActor.IsValid())
	{
		UDisplayClusterConfigurationData* ConfigData = ActiveRootActor->GetConfigData();
		ConfigData->Modify();
		FDisplayClusterConfigurationICVFX_VisibilityList& RootActorLightCards = ConfigData->StageSettings.Lightcard.ShowOnlyList;

		for (ADisplayClusterLightCardActor* LightCard : LightCards)
		{
			check(LightCard);

			if (!RootActorLightCards.Actors.ContainsByPredicate([&](const TSoftObjectPtr<AActor>& Actor)
				{
					// Don't add if a loaded actor is already present.
					return Actor.Get() == LightCard;
				}))
			{
				LightCard->ShowLightCardLabel(ShouldShowLightCardLabels(), *GetLightCardLabelScale(), ActiveRootActor.Get());
				
				const TSoftObjectPtr<AActor> LightCardSoftObject(LightCard);

				// Remove any exact paths to this actor. It's possible invalid actors are present if a light card
				// was force deleted from a level.
				RootActorLightCards.Actors.RemoveAll([&](const TSoftObjectPtr<AActor>& Actor)
					{
						return Actor == LightCardSoftObject;
					});

				RootActorLightCards.Actors.Add(LightCard);
			}
		}

		RefreshPreviewActors(EDisplayClusterLightCardEditorProxyType::LightCards);
	}
}

bool SDisplayClusterLightCardEditor::CanAddLightCard() const
{
	return ActiveRootActor.IsValid() && ActiveRootActor->GetWorld() != nullptr;
}

void SDisplayClusterLightCardEditor::CutLightCards()
{
	CopyLightCards();
	RemoveLightCards(true);
}

bool SDisplayClusterLightCardEditor::CanCutLightCards()
{
	return CanCopyLightCards() && CanRemoveLightCards();
}

void SDisplayClusterLightCardEditor::CopyLightCards()
{
	TArray<ADisplayClusterLightCardActor*> SelectedLightCards;
	LightCardList->GetSelectedLightCards(SelectedLightCards);

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();

	const bool bNoteSelectionChange = false;
	const bool bDeselectBSPSurfs = true;
	const bool bWarnAboutManyActors = false;
	GEditor->SelectNone(bNoteSelectionChange, bDeselectBSPSurfs, bWarnAboutManyActors);

	for (AActor* LightCard : SelectedLightCards)
	{
		const bool bInSelected = true;
		const bool bNotify = false;
		const bool bSelectEvenIfHidden = true;
		GEditor->SelectActor(LightCard, true, bNotify, bSelectEvenIfHidden);
	}

	const bool bShouldCut = false;
	const bool bIsMove = false;
	const bool bWarnAboutReferences = false;
	GEditor->CopySelectedActorsToClipboard(EditorWorld, bShouldCut, bIsMove, bWarnAboutReferences);
}

bool SDisplayClusterLightCardEditor::CanCopyLightCards() const
{
	TArray<ADisplayClusterLightCardActor*> SelectedLightCards;
	LightCardList->GetSelectedLightCards(SelectedLightCards);

	return SelectedLightCards.Num() > 0;
}

void SDisplayClusterLightCardEditor::PasteLightCards(bool bOffsetLightCardPosition)
{
	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	GEditor->PasteSelectedActorsFromClipboard(EditorWorld, LOCTEXT("PasteLightCardsTransactionMessage", "Paste Light Cards"), EPasteTo::PT_OriginalLocation);

	TArray<ADisplayClusterLightCardActor*> PastedLightCards;
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (ADisplayClusterLightCardActor* LightCard = Cast<ADisplayClusterLightCardActor>(*It))
		{
			PastedLightCards.Add(LightCard);
		}
	}

	for (ADisplayClusterLightCardActor* LightCard : PastedLightCards)
	{
		if (bOffsetLightCardPosition)
		{
			// If the light card should be offset from its pasted location, offset its longitude and latitude by a number of
			// degrees equal to an arc length of 10 units (arc length = angle in radians * radius)
			const float AngleOffset = FMath::RadiansToDegrees(10.0f / FMath::Max(LightCard->DistanceFromCenter, 1.0f));
			LightCard->Latitude -= AngleOffset;
			LightCard->Longitude += AngleOffset;
		}
	}

	AddLightCardsToActor(PastedLightCards);

	SelectLightCards(PastedLightCards);
}

bool SDisplayClusterLightCardEditor::CanPasteLightCards() const
{
	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	return GEditor->CanPasteSelectedActorsFromClipboard(EditorWorld);
}

void SDisplayClusterLightCardEditor::DuplicateLightCards()
{
	CopyLightCards();

	const bool bOffsetLightCardPosition = true;
	PasteLightCards(bOffsetLightCardPosition);
}

bool SDisplayClusterLightCardEditor::CanDuplicateLightCards() const
{
	return CanCopyLightCards();
}

void SDisplayClusterLightCardEditor::RemoveLightCards(bool bDeleteLightCardActor)
{
	TArray<ADisplayClusterLightCardActor*> SelectedLightCards;
	LightCardList->GetSelectedLightCards(SelectedLightCards);

	FScopedTransaction Transaction(LOCTEXT("RemoveLightCardTransactionMessage", "Remove Light Card(s)"));

	USelection* EdSelectionManager = GEditor->GetSelectedActors();
	UWorld* WorldToUse = nullptr;

	if (bDeleteLightCardActor)
	{
		EdSelectionManager->BeginBatchSelectOperation();
		EdSelectionManager->Modify();
		EdSelectionManager->DeselectAll();
	}

	if (ActiveRootActor.IsValid())
	{
		UDisplayClusterConfigurationData* ConfigData = ActiveRootActor->GetConfigData();
		ConfigData->Modify();

		FDisplayClusterConfigurationICVFX_VisibilityList& RootActorLightCards = ConfigData->StageSettings.Lightcard.ShowOnlyList;

		for (ADisplayClusterLightCardActor* LightCard : SelectedLightCards)
		{
			RootActorLightCards.Actors.RemoveAll([&](const TSoftObjectPtr<AActor>& Actor)
				{
					return Actor.Get() == LightCard;
				});

			if (bDeleteLightCardActor)
			{
				WorldToUse = LightCard->GetWorld();
				GEditor->SelectActor(LightCard, /*bSelect =*/true, /*bNotifyForActor =*/false, /*bSelectEvenIfHidden =*/true);
			}
			else
			{
				LightCard->ShowLightCardLabel(false, *GetLightCardLabelScale(), ActiveRootActor.Get());
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

	RefreshPreviewActors(EDisplayClusterLightCardEditorProxyType::LightCards);
}

bool SDisplayClusterLightCardEditor::CanRemoveLightCards() const
{
	TArray<ADisplayClusterLightCardActor*> SelectedLightCards;
	LightCardList->GetSelectedLightCards(SelectedLightCards);

	return SelectedLightCards.Num() > 0;
}

void SDisplayClusterLightCardEditor::CreateLightCardTemplate()
{
	TArray<ADisplayClusterLightCardActor*> SelectedLightCards;
	LightCardList->GetSelectedLightCards(SelectedLightCards);

	check(SelectedLightCards.Num() == 1);
	const ADisplayClusterLightCardActor* LightCardActor = SelectedLightCards[0];

	const UDisplayClusterLightCardEditorProjectSettings* Settings = GetDefault<UDisplayClusterLightCardEditorProjectSettings>();
	FString DefaultPath = Settings->LightCardTemplateDefaultPath.Path;
	if (DefaultPath.IsEmpty())
	{
		DefaultPath = TEXT("/Game");
	}
	
	FString DefaultAssetName = LightCardActor->GetActorLabel();
	if (!DefaultAssetName.EndsWith(TEXT("Template")))
	{
		DefaultAssetName += TEXT("Template");
	}

	FString PackageName;
	bool FilenameValid = false;
	while (!FilenameValid)
	{
		FSaveAssetDialogConfig SaveAssetDialogConfig;
		{
			SaveAssetDialogConfig.DefaultPath = MoveTemp(DefaultPath);
			SaveAssetDialogConfig.DefaultAssetName = MoveTemp(DefaultAssetName);
			SaveAssetDialogConfig.AssetClassNames.Add(UDisplayClusterLightCardTemplate::StaticClass()->GetClassPathName());
			SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
			SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SaveAssetDialogTitle", "Save Asset As");
		}

		const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		const FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);

		if (SaveObjectPath.IsEmpty())
		{
			return;
		}

		PackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
		
		FText OutError;
		FilenameValid = FFileHelper::IsFilenameValidForSaving(PackageName, OutError);
	}

	const FString NewAssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UPackage* AssetPackage = CreatePackage(*PackageName);
	check(AssetPackage);

	const EObjectFlags Flags = RF_Public | RF_Standalone;
	UDisplayClusterLightCardTemplate* Template = NewObject<UDisplayClusterLightCardTemplate>(AssetPackage, FName(*NewAssetName), Flags);
	Template->LightCardActor = CastChecked<ADisplayClusterLightCardActor>(StaticDuplicateObject(LightCardActor, Template));

	FAssetRegistryModule::AssetCreated(Template);

	AssetPackage->MarkPackageDirty();

	FEditorFileUtils::PromptForCheckoutAndSave({AssetPackage}, true, false);
}

bool SDisplayClusterLightCardEditor::CanCreateLightCardTemplate() const
{
	TArray<ADisplayClusterLightCardActor*> SelectedLightCards;
	LightCardList->GetSelectedLightCards(SelectedLightCards);

	return SelectedLightCards.Num() == 1;
}

void SDisplayClusterLightCardEditor::ToggleLightCardLabels()
{
	ShowLightCardLabels(!ShouldShowLightCardLabels());
}

void SDisplayClusterLightCardEditor::ShowLightCardLabels(bool bVisible)
{
	if (!GetActiveRootActor().IsValid())
	{
		return;
	}
	
	FScopedTransaction Transaction(LOCTEXT("ToggleLightCardLabelsTransactionMessage", "Toggle Light Card Labels"), !GIsTransacting);
	
	IDisplayClusterLightCardEditor& LightCardEditorModule = FModuleManager::GetModuleChecked<IDisplayClusterLightCardEditor>(IDisplayClusterLightCardEditor::ModuleName);

	IDisplayClusterLightCardEditor::FLabelArgs Args;
	Args.bVisible = bVisible;
	Args.Scale = *GetLightCardLabelScale();
	Args.RootActor = GetActiveRootActor().Get();
	
	LightCardEditorModule.ShowLabels(MoveTemp(Args));

	RefreshPreviewActors(EDisplayClusterLightCardEditorProxyType::LightCards);
}

bool SDisplayClusterLightCardEditor::ShouldShowLightCardLabels() const
{
	return GetDefault<UDisplayClusterLightCardEditorProjectSettings>()->bDisplayLightCardLabels;
}

TOptional<float> SDisplayClusterLightCardEditor::GetLightCardLabelScale() const
{
	return GetDefault<UDisplayClusterLightCardEditorProjectSettings>()->LightCardLabelScale;
}

void SDisplayClusterLightCardEditor::SetLightCardLabelScale(float NewValue)
{
	if (!GetActiveRootActor().IsValid())
	{
		return;
	}
	
	FScopedTransaction Transaction(LOCTEXT("ScaleLightCardLabelsTransactionMessage", "Scale Light Card Labels"), !GIsTransacting);
	
	IDisplayClusterLightCardEditor& LightCardEditorModule = FModuleManager::GetModuleChecked<IDisplayClusterLightCardEditor>(IDisplayClusterLightCardEditor::ModuleName);

	IDisplayClusterLightCardEditor::FLabelArgs Args;
	Args.bVisible = ShouldShowLightCardLabels();
	Args.Scale = NewValue;
	Args.RootActor = GetActiveRootActor().Get();
	
	LightCardEditorModule.ShowLabels(MoveTemp(Args));
}

void SDisplayClusterLightCardEditor::OnActiveRootActorChanged(ADisplayClusterRootActor* NewRootActor)
{
	if (NewRootActor == ActiveRootActor.Get())
	{
		return;
	}
	
	RemoveCompileDelegates();
	
	// The new root actor pointer could be null, indicating that it was deleted or the user didn't select a valid root actor
	ActiveRootActor = NewRootActor;
	LightCardList->SetRootActor(NewRootActor);
	ViewportView->SetRootActor(NewRootActor);
	
	BindCompileDelegates();

	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SDisplayClusterLightCardEditor::OnActorPropertyChanged);

	RefreshLabels();
}

TSharedRef<SWidget> SDisplayClusterLightCardEditor::CreateLightCardListWidget()
{
	return SAssignNew(LightCardList, SDisplayClusterLightCardList, SharedThis(this), CommandList);
}

TSharedRef<SWidget> SDisplayClusterLightCardEditor::CreateViewportWidget()
{
	return SAssignNew(ViewportView, SDisplayClusterLightCardEditorViewport, SharedThis(this), CommandList);
}

TSharedRef<SWidget> SDisplayClusterLightCardEditor::GeneratePlaceActorsMenu()
{
	const bool bInShouldCloseWindowAfterMenuSelection = true;

	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, CommandList);
	MenuBuilder.BeginSection("PlaceActors", LOCTEXT("PlaceActorsMenuHeader", "Place Actors"));
	{
		FSlateIcon LightCardIcon = FSlateIconFinder::FindIconForClass(ADisplayClusterLightCardActor::StaticClass());
		
		MenuBuilder.AddMenuEntry(FDisplayClusterLightCardEditorCommands::Get().AddNewLightCard,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(), LightCardIcon);
		MenuBuilder.AddMenuEntry(FDisplayClusterLightCardEditorCommands::Get().AddExistingLightCard,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(), LightCardIcon);
		
		MenuBuilder.AddSubMenu(LOCTEXT("AllTemplatesLabel", "All Templates"),
		LOCTEXT("AllTemplatesTooltip", "Select a template"),
		FNewMenuDelegate::CreateSP(this, &SDisplayClusterLightCardEditor::GenerateTemplateSubMenu),
		false,
		FSlateIcon(),
		true);
	}
	MenuBuilder.EndSection();
	MenuBuilder.BeginSection("Favorites", LOCTEXT("FavoritesMenuHeader", "Favorites"));
	{
		TArray<UDisplayClusterLightCardTemplate*> LightCardTemplates =
		UE::DisplayClusterLightCardTemplateHelpers::GetLightCardTemplates(/*bFavoritesOnly*/ true);

		TemplateBrushes.Empty();
		
		for (UDisplayClusterLightCardTemplate* Template : LightCardTemplates)
		{
			// Create a brush if this template is using a custom texture.
			if (Template->LightCardActor != nullptr && Template->LightCardActor->Texture.Get() != nullptr)
			{
				TSharedPtr<FSlateBrush> SlateBrush = MakeShared<FSlateBrush>();

				SlateBrush->SetResourceObject(Template->LightCardActor->Texture.Get());
				SlateBrush->ImageSize = FVector2D(16.f, 16.f);
				TemplateBrushes.Add(Template, SlateBrush);
			}

			TWeakObjectPtr<UDisplayClusterLightCardTemplate> TemplateWeakPtr = MakeWeakObjectPtr(Template);

			const TSharedPtr<SWidget> TemplateWidget = SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.HeightOverride(16.f)
				.WidthOverride(16.f)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(this, &SDisplayClusterLightCardEditor::GetLightCardTemplateIcon, TemplateWeakPtr)
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Template->GetName()))
			];

			FMenuEntryParams EntryParams;
			EntryParams.EntryWidget = TemplateWidget;
			EntryParams.LabelOverride = FText::FromString(Template->GetName());
			EntryParams.ToolTipOverride = LOCTEXT("FavoriteEntryTooltip", "Spawn this favorite");
			EntryParams.DirectActions.ExecuteAction = FExecuteAction::CreateLambda([this, TemplateWeakPtr]()
			{
				if (TemplateWeakPtr.IsValid())
				{
					SpawnLightCardFromTemplate(TemplateWeakPtr.Get());
				}
			});
			
			MenuBuilder.AddMenuEntry(EntryParams);
		}
	}
	MenuBuilder.EndSection();

	CleanupRecentlyPlacedItems();
	
	MenuBuilder.BeginSection("RecentlyPlaced", LOCTEXT("RecentlyPlacedMenuHeader", "Recently Placed"));
	{
		const UDisplayClusterLightCardEditorSettings* Settings = GetDefault<UDisplayClusterLightCardEditorSettings>();
		for (const FDisplayClusterLightCardEditorRecentItem& RecentlyPlacedItem : Settings->RecentlyPlacedItems)
		{
			TWeakObjectPtr<UObject> ObjectWeakPtr = MakeWeakObjectPtr(RecentlyPlacedItem.ObjectPath.LoadSynchronous());
			check(ObjectWeakPtr.IsValid());
			
			const TSharedPtr<SWidget> RecentItemWidget = SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.HeightOverride(16.f)
				.WidthOverride(16.f)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(RecentlyPlacedItem.GetSlateBrush())
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.f, 0.f)
			[
				SNew(STextBlock)
				.Text(RecentlyPlacedItem.GetItemDisplayName())
			];

			FMenuEntryParams EntryParams;
			EntryParams.EntryWidget = RecentItemWidget;
			EntryParams.LabelOverride = FText::FromString(ObjectWeakPtr->GetName());
			EntryParams.ToolTipOverride = LOCTEXT("RecentItemEntryTooltip", "Spawn this recently placed item");
			EntryParams.DirectActions.ExecuteAction = FExecuteAction::CreateLambda([this, RecentlyPlacedItem, ObjectWeakPtr]()
			{
				if (ObjectWeakPtr.IsValid())
				{
					if (RecentlyPlacedItem.ItemType == FDisplayClusterLightCardEditorRecentItem::Type_LightCard)
					{
						AddNewLightCard();
					}
					else if (RecentlyPlacedItem.ItemType == FDisplayClusterLightCardEditorRecentItem::Type_LightCardTemplate)
					{
						if (const UDisplayClusterLightCardTemplate* Template = Cast<UDisplayClusterLightCardTemplate>(ObjectWeakPtr.Get()))
						{
							SpawnLightCardFromTemplate(Template);
						}
					}
				}
			});
			
			MenuBuilder.AddMenuEntry(EntryParams);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

const FSlateBrush* SDisplayClusterLightCardEditor::GetLightCardTemplateIcon(const TWeakObjectPtr<UDisplayClusterLightCardTemplate> InTemplate) const
{
	if (!InTemplate.IsValid())
	{
		return nullptr;
	}
		
	if (const TSharedPtr<FSlateBrush>* Brush = TemplateBrushes.Find(InTemplate))
	{
		return (*Brush).Get();
	}
	
	return FSlateIconFinder::FindIconBrushForClass(InTemplate->GetClass());
}

void SDisplayClusterLightCardEditor::GenerateTemplateSubMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.BeginSection("Templates", LOCTEXT("TemplatesMenuHeader", "Templates"));
	{
		InMenuBuilder.AddWidget(CreateLightCardTemplateWidget(), FText::GetEmpty(), true);
	}
	InMenuBuilder.EndSection();
}

TSharedRef<SWidget> SDisplayClusterLightCardEditor::GenerateLabelsMenu()
{
	const bool bInShouldCloseWindowAfterMenuSelection = true;

	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection("Labels", LOCTEXT("LabelsMenuHeader", "Labels"));
	{
		MenuBuilder.AddMenuEntry(FDisplayClusterLightCardEditorCommands::Get().ToggleLightCardLabels);
	
		MenuBuilder.AddWidget(
		SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SBox)
			.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
				.Padding(FMargin(1.0f))
				[
					SNew(SBox)
					.MinDesiredWidth(64)
					[
						SNew(SNumericEntryBox<float>)
						.Value(this, &SDisplayClusterLightCardEditor::GetLightCardLabelScale)
						.OnValueChanged(this, &SDisplayClusterLightCardEditor::SetLightCardLabelScale)
						.MinValue(0)
						.MaxValue(FLT_MAX)
						.MinSliderValue(0)
						.MaxSliderValue(10)
						.AllowSpin(true)
					]
				]
			]
		],
		LOCTEXT("LightCardLabelScale_Label", "Label Scale"));
	}
	MenuBuilder.EndSection();
	
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SDisplayClusterLightCardEditor::CreateLightCardTemplateWidget()
{
	return SNew(SDisplayClusterLightCardTemplateList, SharedThis(this))
	.HideHeader(true)
	.SpawnOnSelection(true);
}

void SDisplayClusterLightCardEditor::BindCommands()
{
	CommandList = MakeShareable(new FUICommandList);

	CommandList->MapAction(
		FDisplayClusterLightCardEditorCommands::Get().AddNewLightCard,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::AddNewLightCard),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CanAddLightCard));

	CommandList->MapAction(
		FDisplayClusterLightCardEditorCommands::Get().AddExistingLightCard,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::AddExistingLightCard),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CanAddLightCard));

	CommandList->MapAction(
		FDisplayClusterLightCardEditorCommands::Get().RemoveLightCard,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::RemoveLightCards, false),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CanRemoveLightCards));

	CommandList->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CutLightCards),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CanCutLightCards));

	CommandList->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CopyLightCards),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CanCopyLightCards));

	CommandList->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::PasteLightCards, false),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CanPasteLightCards));

	CommandList->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::DuplicateLightCards),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CanDuplicateLightCards));

	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::RemoveLightCards, true),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CanRemoveLightCards));

	CommandList->MapAction(
		FDisplayClusterLightCardEditorCommands::Get().SaveLightCardTemplate,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CreateLightCardTemplate),
		FCanExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::CanCreateLightCardTemplate));

	CommandList->MapAction(
		FDisplayClusterLightCardEditorCommands::Get().ToggleLightCardLabels,
		FExecuteAction::CreateSP(this, &SDisplayClusterLightCardEditor::ToggleLightCardLabels),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SDisplayClusterLightCardEditor::ShouldShowLightCardLabels));
}

void SDisplayClusterLightCardEditor::RegisterToolbarExtensions()
{
	const TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension("General", EExtensionHook::After, nullptr,
		FToolBarExtensionDelegate::CreateSP(this, &SDisplayClusterLightCardEditor::ExtendToolbar));
	IDisplayClusterOperator::Get().GetOperatorToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
}

void SDisplayClusterLightCardEditor::ExtendToolbar(FToolBarBuilder& ToolbarBuilder)
{
	// TODO: Any toolbar buttons needed for the lightcards editor can be added to the operator panel's toolbar using this toolbar extender

	ToolbarBuilder.AddSeparator();
	{
		const FUIAction DefaultAction;
		ToolbarBuilder.AddComboButton(
			DefaultAction,
			FOnGetContent::CreateSP(this, &SDisplayClusterLightCardEditor::GeneratePlaceActorsMenu),
			TAttribute<FText>(),
			LOCTEXT("PlaceActors_ToolTip", "Place actors in the scene"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlaceActors"));
	}
	
	ToolbarBuilder.AddSeparator();
	{
		const FUIAction DefaultAction;
		ToolbarBuilder.AddComboButton(
			DefaultAction,
			FOnGetContent::CreateSP(this, &SDisplayClusterLightCardEditor::GenerateLabelsMenu),
			TAttribute<FText>(),
			LOCTEXT("Labels_ToolTip", "Configure options for labels"),
			FSlateIcon(FDisplayClusterLightCardEditorStyle::Get().GetStyleSetName(), "DisplayClusterLightCardEditor.Labels"));
	}
	
}

void SDisplayClusterLightCardEditor::RefreshPreviewActors(EDisplayClusterLightCardEditorProxyType ProxyType)
{
	RemoveCompileDelegates();
	
	if (ADisplayClusterRootActor* RootActor = GetActiveRootActor().Get())
	{
		if (LightCardList.IsValid())
		{
			LightCardList->SetRootActor(RootActor);
		}
			
		if (ViewportView.IsValid())
		{
			const bool bForce = true;
			ViewportView->GetLightCardEditorViewportClient()->UpdatePreviewActor(RootActor, bForce, ProxyType);
		}
	}
	
	BindCompileDelegates();
}

void SDisplayClusterLightCardEditor::RefreshLabels()
{
	ShowLightCardLabels(ShouldShowLightCardLabels());
}

bool SDisplayClusterLightCardEditor::IsOurObject(UObject* InObject,
	EDisplayClusterLightCardEditorProxyType& OutProxyType) const
{
	auto IsOurActor = [InObject] (UObject* ObjectToCompare) -> bool
	{
		if (ObjectToCompare)
		{
			if (InObject == ObjectToCompare)
			{
				return true;
			}

			if (const UObject* RootActorOuter = InObject->GetTypedOuter(ObjectToCompare->GetClass()))
			{
				return RootActorOuter == ObjectToCompare;
			}
		}

		return false;
	};

	EDisplayClusterLightCardEditorProxyType ProxyType = EDisplayClusterLightCardEditorProxyType::All;
	
	bool bIsOurActor = IsOurActor(GetActiveRootActor().Get());
	if (!bIsOurActor && LightCardList.IsValid())
	{
		for (const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>& LightCard : LightCardList->GetLightCardActors())
		{
			bIsOurActor = IsOurActor(LightCard->LightCardActor.Get());
			if (bIsOurActor)
			{
				ProxyType = EDisplayClusterLightCardEditorProxyType::LightCards;
				break;
			}
		}
	}

	OutProxyType = ProxyType;
	return bIsOurActor;
}

void SDisplayClusterLightCardEditor::BindCompileDelegates()
{
	if (LightCardList.IsValid())
	{
		for (const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>& LightCardActor : LightCardList->GetLightCardActors())
		{
			if (LightCardActor.IsValid() && LightCardActor->LightCardActor.IsValid())
			{
				if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(LightCardActor->LightCardActor->GetClass()))
				{
					Blueprint->OnCompiled().AddSP(this, &SDisplayClusterLightCardEditor::OnBlueprintCompiled);
				}
			}
		}
	}
}

void SDisplayClusterLightCardEditor::RemoveCompileDelegates()
{
	if (LightCardList.IsValid())
	{
		for (const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>& LightCardActor : LightCardList->GetLightCardActors())
		{
			if (LightCardActor.IsValid() && LightCardActor->LightCardActor.IsValid())
			{
				if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(LightCardActor->LightCardActor->GetClass()))
				{
					Blueprint->OnCompiled().RemoveAll(this);
				}
			}
		}
	}
}

void SDisplayClusterLightCardEditor::OnActorPropertyChanged(UObject* ObjectBeingModified,
                                                            FPropertyChangedEvent& PropertyChangedEvent)
{
	EDisplayClusterLightCardEditorProxyType ProxyType;
	if (IsOurObject(ObjectBeingModified, ProxyType))
	{
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
		{
			// Real-time & efficient update when dragging a slider.
			if (ViewportView.IsValid())
			{
				ViewportView->GetLightCardEditorViewportClient()->UpdateProxyTransforms();
			}
		}
		else
		{
			// Full destroy and refresh.
			RefreshPreviewActors(ProxyType);
		}
	}
}

void SDisplayClusterLightCardEditor::OnLevelActorDeleted(AActor* Actor)
{
	if (LightCardList.IsValid() &&
		LightCardList->GetLightCardActors().ContainsByPredicate([Actor](const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>& LightCardTreeItem)
	{
		return LightCardTreeItem.IsValid() && Actor == LightCardTreeItem->LightCardActor.Get();
	}))
	{
		if (Actor && Actor->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			// When a blueprint class is regenerated instances are deleted and replaced.
			// In this case the OnCompiled() delegate will fire and refresh the actor.
			return;
		}
		
		if (ViewportView.IsValid())
		{
			ViewportView->GetLightCardEditorViewportClient()->GetWorld()->GetTimerManager().SetTimerForNextTick([=]()
			{
				// Schedule for next tick so available selections are properly updated once the
				// actor is fully deleted.
				RefreshPreviewActors(EDisplayClusterLightCardEditorProxyType::LightCards);
			});
		}
	}
}

void SDisplayClusterLightCardEditor::OnBlueprintCompiled(UBlueprint* Blueprint)
{
	// Right now only LightCard blueprints are handled here.
	RefreshPreviewActors(EDisplayClusterLightCardEditorProxyType::LightCards);
}

void SDisplayClusterLightCardEditor::OnObjectTransacted(UObject* Object,
	const FTransactionObjectEvent& TransactionObjectEvent)
{
	if (TransactionObjectEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		// Always refresh on undo because the light card actor may not inherit our C++ class
		// so we can't easily distinguish it. This supports the case where the user deletes
		// a LightCard actor from the level manually then undoes it.
		RefreshPreviewActors();
	}
}

void SDisplayClusterLightCardEditor::OnLightCardListChanged()
{
	RefreshPreviewActors(EDisplayClusterLightCardEditorProxyType::LightCards);
}

void SDisplayClusterLightCardEditor::AddRecentlyPlacedItem(const FDisplayClusterLightCardEditorRecentItem& InItem)
{
	UDisplayClusterLightCardEditorSettings* Settings = GetMutableDefault<UDisplayClusterLightCardEditorSettings>();
	
	Settings->RecentlyPlacedItems.RemoveAll([&](const FDisplayClusterLightCardEditorRecentItem& Compare)
	{
		return Compare == InItem;
	});
	Settings->RecentlyPlacedItems.Insert(InItem, 0);

	const int32 MaxRecentlyPlaced = 5;
	
	if (Settings->RecentlyPlacedItems.Num() > MaxRecentlyPlaced)
	{
		Settings->RecentlyPlacedItems.RemoveAt(MaxRecentlyPlaced - 1, Settings->RecentlyPlacedItems.Num() - MaxRecentlyPlaced);
	}

	Settings->PostEditChange();
	Settings->SaveConfig();
}

void SDisplayClusterLightCardEditor::CleanupRecentlyPlacedItems()
{
	UDisplayClusterLightCardEditorSettings* Settings = GetMutableDefault<UDisplayClusterLightCardEditorSettings>();
	Settings->RecentlyPlacedItems.RemoveAll([&](const FDisplayClusterLightCardEditorRecentItem& Compare)
	{
		return !Compare.ObjectPath.LoadSynchronous();
	});

	Settings->PostEditChange();
	Settings->SaveConfig();
}

#undef LOCTEXT_NAMESPACE
