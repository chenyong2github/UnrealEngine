// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TemplateSequenceEditorToolkit.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "DragAndDrop/ActorDragDropGraphEdOp.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/ClassDragDropOp.h"
#include "Engine/Selection.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelEditor.h"
#include "LevelEditorSequencerIntegration.h"
#include "Misc/TemplateSequenceEditorPlaybackContext.h"
#include "Misc/TemplateSequenceEditorSpawnRegister.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "SequencerSettings.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TemplateSequenceEditor"

const FName FTemplateSequenceEditorToolkit::SequencerMainTabId(TEXT("Sequencer_SequencerMain"));

namespace SequencerDefs
{
	static const FName SequencerAppIdentifier(TEXT("SequencerApp"));
}

FTemplateSequenceEditorToolkit::FTemplateSequenceEditorToolkit(const TSharedRef<ISlateStyle>& InStyle)
	: TemplateSequence(nullptr)
	, Style(InStyle)
{
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	int32 NewIndex = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().Add(
		FAssetEditorExtender::CreateRaw(this, &FTemplateSequenceEditorToolkit::HandleMenuExtensibilityGetExtender));
	SequencerExtenderHandle = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates()[NewIndex].GetHandle();
}

FTemplateSequenceEditorToolkit::~FTemplateSequenceEditorToolkit()
{
	FLevelEditorSequencerIntegration::Get().RemoveSequencer(Sequencer.ToSharedRef());

	Sequencer->Close();

	if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		auto& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.OnMapChanged().RemoveAll(this);
	}

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().RemoveAll([this](const FAssetEditorExtender& Extender)
	{
		return SequencerExtenderHandle == Extender.GetHandle();
	});
}

void FTemplateSequenceEditorToolkit::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UTemplateSequence* InTemplateSequence, const FTemplateSequenceToolkitParams& ToolkitParams)
{
	// create tab layout
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_TemplateSequenceEditor")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->Split
			(
				FTabManager::NewStack()
				->AddTab(SequencerMainTabId, ETabState::OpenedTab)
			)
		);

	TemplateSequence = InTemplateSequence;
	PlaybackContext = MakeShared<FTemplateSequenceEditorPlaybackContext>();

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = false;

	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, SequencerDefs::SequencerAppIdentifier, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, TemplateSequence);

	TSharedRef<FTemplateSequenceEditorSpawnRegister> SpawnRegister = MakeShareable(new FTemplateSequenceEditorSpawnRegister());
	SpawnRegister->SetSequencer(Sequencer);

	// Initialize sequencer.
	FSequencerInitParams SequencerInitParams;
	{
		SequencerInitParams.RootSequence = TemplateSequence;
		SequencerInitParams.bEditWithinLevelEditor = true;
		SequencerInitParams.ToolkitHost = InitToolkitHost;
		SequencerInitParams.SpawnRegister = SpawnRegister;
		SequencerInitParams.HostCapabilities.bSupportsCurveEditor = true;
		SequencerInitParams.HostCapabilities.bSupportsSaveMovieSceneAsset = true;

		SequencerInitParams.PlaybackContext.Bind(PlaybackContext.ToSharedRef(), &FTemplateSequenceEditorPlaybackContext::GetPlaybackContext);

		SequencerInitParams.ViewParams.UniqueName = "TemplateSequenceEditor";
		SequencerInitParams.ViewParams.ScrubberStyle = ESequencerScrubberStyle::FrameBlock;
		SequencerInitParams.ViewParams.OnReceivedFocus.BindRaw(this, &FTemplateSequenceEditorToolkit::OnSequencerReceivedFocus);

		if (ToolkitParams.bCanChangeBinding)
		{
			// Callbacks for changing the root binding by drag-and-dropping stuff on the Sequencer.
			SequencerInitParams.ViewParams.OnAssetsDrop.BindRaw(this, &FTemplateSequenceEditorToolkit::OnSequencerAssetsDrop);
			SequencerInitParams.ViewParams.OnClassesDrop.BindRaw(this, &FTemplateSequenceEditorToolkit::OnSequencerClassesDrop);
			SequencerInitParams.ViewParams.OnActorsDrop.BindRaw(this, &FTemplateSequenceEditorToolkit::OnSequencerActorsDrop);

			// Extended for showing toolbar controls to change the root binding.
			TSharedRef<FExtender> ToolbarExtender = MakeShared<FExtender>();
			ToolbarExtender->AddToolBarExtension("Base Commands", EExtensionHook::After, nullptr, FToolBarExtensionDelegate::CreateSP(this, &FTemplateSequenceEditorToolkit::ExtendSequencerToolbar));
			SequencerInitParams.ViewParams.ToolbarExtender = ToolbarExtender;
		}
		else
		{
			// Bind drag-and-drop callbacks to suppress them from doing anything.
			SequencerInitParams.ViewParams.OnAssetsDrop.BindLambda([](const TArray<UObject*>&, const FAssetDragDropOp&) -> bool { return false; });
			SequencerInitParams.ViewParams.OnClassesDrop.BindLambda([](const TArray<TWeakObjectPtr<UClass>>&, const FClassDragDropOp&) -> bool { return false; });
			SequencerInitParams.ViewParams.OnActorsDrop.BindLambda([](const TArray<TWeakObjectPtr<AActor>>&, const FActorDragDropGraphEdOp&) -> bool { return false; });
		}
	}

	Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(SequencerInitParams);

	Sequencer->OnActorAddedToSequencer().AddSP(this, &FTemplateSequenceEditorToolkit::HandleActorAddedToSequencer);

	if (ToolkitParams.InitialBindingClass != nullptr)
	{
		ChangeActorBinding(*ToolkitParams.InitialBindingClass);
	}

	FLevelEditorSequencerIntegrationOptions Options;
	Options.bRequiresLevelEvents = true;
	Options.bRequiresActorEvents = true;
	Options.bCanRecord = true;
	FLevelEditorSequencerIntegration::Get().AddSequencer(Sequencer.ToSharedRef(), Options);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	// Reopen the scene outliner so that is refreshed with the sequencer info column
	if (Sequencer->GetSequencerSettings()->GetShowOutlinerInfoColumn())
	{
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		if (LevelEditorTabManager->FindExistingLiveTab(FName("LevelEditorSceneOutliner")).IsValid())
		{
			LevelEditorTabManager->InvokeTab(FName("LevelEditorSceneOutliner"))->RequestCloseTab();
			LevelEditorTabManager->InvokeTab(FName("LevelEditorSceneOutliner"));
		}
	}

	LevelEditorModule.AttachSequencer(Sequencer->GetSequencerWidget(), SharedThis(this));
	LevelEditorModule.OnMapChanged().AddRaw(this, &FTemplateSequenceEditorToolkit::HandleMapChanged);
}

FText FTemplateSequenceEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Template Sequence Editor");
}

FName FTemplateSequenceEditorToolkit::GetToolkitFName() const
{
	static FName SequencerName("TemplateSequenceEditor");
	return SequencerName;
}

FString FTemplateSequenceEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Sequencer ").ToString();
}

FLinearColor FTemplateSequenceEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.7, 0.0f, 0.0f, 0.5f);
}

void FTemplateSequenceEditorToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	if (IsWorldCentricAssetEditor())
	{
		return;
	}
}


void FTemplateSequenceEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	if (!IsWorldCentricAssetEditor())
	{
		InTabManager->UnregisterTabSpawner(SequencerMainTabId);
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.AttachSequencer(SNullWidget::NullWidget, nullptr);
}

void FTemplateSequenceEditorToolkit::ExtendSequencerToolbar(FToolBarBuilder& ToolbarBuilder)
{
	TSharedRef<SHorizontalBox> Widget = SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BoundActorClassPicker", "Bound Actor Class"))
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.OnGetMenuContent(this, &FTemplateSequenceEditorToolkit::GetBoundActorClassMenuContent)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &FTemplateSequenceEditorToolkit::GetBoundActorClassName)
			]
		];
	
	ToolbarBuilder.AddWidget(Widget);
}

FText FTemplateSequenceEditorToolkit::GetBoundActorClassName() const
{
	const UClass* BoundActorClass = TemplateSequence ? TemplateSequence->BoundActorClass.Get() : NULL;
	return BoundActorClass ? BoundActorClass->GetDisplayNameText() : FText::FromName(NAME_None);
}

TSharedRef<SWidget> FTemplateSequenceEditorToolkit::GetBoundActorClassMenuContent()
{
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.bIsActorsOnly = true;

	TSharedRef<SWidget> ClassPicker = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateSP(this, &FTemplateSequenceEditorToolkit::OnBoundActorClassPicked));

	return SNew(SBox)
		.WidthOverride(350.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.MaxHeight(400.0f)
			.AutoHeight()
			[
				ClassPicker
			]
		];
}

void FTemplateSequenceEditorToolkit::OnBoundActorClassPicked(UClass* ChosenClass)
{
	FSlateApplication::Get().DismissAllMenus();

	if (TemplateSequence != nullptr)
	{
		ChangeActorBinding(*ChosenClass);

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}
}

TSharedRef<FExtender> FTemplateSequenceEditorToolkit::HandleMenuExtensibilityGetExtender(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects)
{
	TSharedRef<FExtender> AddTrackMenuExtender(new FExtender());
	AddTrackMenuExtender->AddMenuExtension(
		SequencerMenuExtensionPoints::AddTrackMenu_PropertiesSection,
		EExtensionHook::Before,
		CommandList,
		FMenuExtensionDelegate::CreateRaw(this, &FTemplateSequenceEditorToolkit::HandleTrackMenuExtensionAddTrack, ContextSensitiveObjects));

	return AddTrackMenuExtender;
}

void FTemplateSequenceEditorToolkit::HandleTrackMenuExtensionAddTrack(FMenuBuilder& AddTrackMenuBuilder, TArray<UObject*> ContextObjects)
{
	// TODO-lchabant: stolen from level sequence.
	if (ContextObjects.Num() != 1)
	{
		return;
	}

	AActor* Actor = Cast<AActor>(ContextObjects[0]);
	if (Actor == nullptr)
	{
		return;
	}

	AddTrackMenuBuilder.BeginSection("Components", LOCTEXT("ComponentsSection", "Components"));
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component)
			{
				FUIAction AddComponentAction(FExecuteAction::CreateSP(this, &FTemplateSequenceEditorToolkit::HandleAddComponentActionExecute, Component));
				FText AddComponentLabel = FText::FromString(Component->GetName());
				FText AddComponentToolTip = FText::Format(LOCTEXT("ComponentToolTipFormat", "Add {0} component"), FText::FromString(Component->GetName()));
				AddTrackMenuBuilder.AddMenuEntry(AddComponentLabel, AddComponentToolTip, FSlateIcon(), AddComponentAction);
			}
		}
	}
	AddTrackMenuBuilder.EndSection();
}

void FTemplateSequenceEditorToolkit::HandleAddComponentActionExecute(UActorComponent* Component)
{
	// TODO-lchabant: stolen from level sequence.
	const FScopedTransaction Transaction(LOCTEXT("AddComponent", "Add Component"));

	FString ComponentName = Component->GetName();

	TArray<UActorComponent*> ActorComponents;
	ActorComponents.Add(Component);

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (SelectedActors && SelectedActors->Num() > 0)
	{
		for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
		{
			AActor* Actor = CastChecked<AActor>(*Iter);

			TArray<UActorComponent*> OutActorComponents;
			Actor->GetComponents(OutActorComponents);

			for (UActorComponent* ActorComponent : OutActorComponents)
			{
				if (ActorComponent->GetName() == ComponentName)
				{
					ActorComponents.AddUnique(ActorComponent);
				}
			}
		}
	}

	for (UActorComponent* ActorComponent : ActorComponents)
	{
		Sequencer->GetHandleToObject(ActorComponent);
	}
}

void FTemplateSequenceEditorToolkit::HandleActorAddedToSequencer(AActor* Actor, const FGuid Binding)
{
	// TODO-lchabant: add default tracks (re-use level sequence toolkit code).
}

void FTemplateSequenceEditorToolkit::HandleMapChanged(UWorld* NewWorld, EMapChangeType MapChangeType)
{
	if ((MapChangeType == EMapChangeType::LoadMap || MapChangeType == EMapChangeType::NewMap || MapChangeType == EMapChangeType::TearDownWorld))
	{
		Sequencer->GetSpawnRegister().CleanUp(*Sequencer);
		CloseWindow();
	}
}

bool FTemplateSequenceEditorToolkit::OnRequestClose()
{
	return true;
}

bool FTemplateSequenceEditorToolkit::CanFindInContentBrowser() const
{
	// False so that sequencer doesn't take over Find In Content Browser functionality and always find the level sequence asset.
	return false;
}

void FTemplateSequenceEditorToolkit::OnSequencerReceivedFocus()
{
	if (Sequencer.IsValid())
	{
		FLevelEditorSequencerIntegration::Get().OnSequencerReceivedFocus(Sequencer.ToSharedRef());
	}
}

bool FTemplateSequenceEditorToolkit::OnSequencerReceivedDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, FReply& OutReply)
{
	bool bIsDragSupported = false;

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (Operation.IsValid() && (
		(Operation->IsOfType<FAssetDragDropOp>() && StaticCastSharedPtr<FAssetDragDropOp>(Operation)->GetAssetPaths().Num() <= 1) ||
		(Operation->IsOfType<FClassDragDropOp>() && StaticCastSharedPtr<FClassDragDropOp>(Operation)->ClassesToDrop.Num() <= 1) ||
		(Operation->IsOfType<FActorDragDropGraphEdOp>() && StaticCastSharedPtr<FActorDragDropGraphEdOp>(Operation)->Actors.Num() <= 1)))
	{
		bIsDragSupported = true;
	}

	OutReply = (bIsDragSupported ? FReply::Handled() : FReply::Unhandled());
	return true;
}

bool FTemplateSequenceEditorToolkit::OnSequencerAssetsDrop(const TArray<UObject*>& Assets, const FAssetDragDropOp& DragDropOp)
{
	if (Assets.Num() > 0)
	{
		// Only drop the first asset.
		UObject* CurObject = Assets[0];

		// TODO: check for dropping a sequence?

		ChangeActorBinding(*CurObject, DragDropOp.GetActorFactory());

		return true;
	}

	return true;
}

bool FTemplateSequenceEditorToolkit::OnSequencerClassesDrop(const TArray<TWeakObjectPtr<UClass>>& Classes, const FClassDragDropOp& DragDropOp)
{
	if (Classes.Num() > 0 && Classes[0].IsValid())
	{
		// Only drop the first class.
		UClass* CurClass = Classes[0].Get();

		ChangeActorBinding(*CurClass);

		return true;
	}
	return false;
}

bool FTemplateSequenceEditorToolkit::OnSequencerActorsDrop(const TArray<TWeakObjectPtr<AActor>>& Actors, const FActorDragDropGraphEdOp& DragDropOp)
{
	return false;
}

void FTemplateSequenceEditorToolkit::ChangeActorBinding(UObject& Object, UActorFactory* ActorFactory, bool bSetupDefaults)
{
	// See if we have anything to do in the first place.
	if (UClass* ChosenClass = Cast<UClass>(&Object))
	{
		if (ChosenClass == TemplateSequence->BoundActorClass)
		{
			return;
		}
	}

	UMovieScene* MovieScene = TemplateSequence->GetMovieScene();
	check(MovieScene != nullptr);

	// See if we previously had a main object binding.
	FGuid PreviousSpawnableGuid;
	if (MovieScene->GetSpawnableCount() > 0)
	{
		PreviousSpawnableGuid = MovieScene->GetSpawnable(0).GetGuid();
	}

	// Make the new spawnable object binding.
	FGuid NewSpawnableGuid = Sequencer->MakeNewSpawnable(Object, ActorFactory, bSetupDefaults);
	FMovieSceneSpawnable* NewSpawnable = MovieScene->FindSpawnable(NewSpawnableGuid);

	if (Object.IsA<UClass>())
	{
		UClass* ChosenClass = StaticCast<UClass*>(&Object);
		TemplateSequence->BoundActorClass = ChosenClass;
	}
	else
	{
		const UObject* SpawnableTemplate = NewSpawnable->GetObjectTemplate();
		TemplateSequence->BoundActorClass = SpawnableTemplate->GetClass();
	}

	// If we had a previous one, move everything under it to the new binding, and clean up.
	if (PreviousSpawnableGuid.IsValid())
	{
		MovieScene->MoveBindingContents(PreviousSpawnableGuid, NewSpawnableGuid);

		if (MovieScene->RemoveSpawnable(PreviousSpawnableGuid))
		{
			FMovieSceneSpawnRegister& SpawnRegister = Sequencer->GetSpawnRegister();
			SpawnRegister.DestroySpawnedObject(PreviousSpawnableGuid, Sequencer->GetFocusedTemplateID(), *Sequencer);
		}
	}
}

#undef LOCTEXT_NAMESPACE
