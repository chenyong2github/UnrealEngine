// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundEffectPresetEditor.h"

#include "Blueprint/UserWidget.h"
#include "Containers/Set.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "WidgetBlueprint.h"
#include "AudioEditorModule.h"


#define LOCTEXT_NAMESPACE "SoundEffectPresetEditor"


const FName FSoundEffectPresetEditor::AppIdentifier("SoundEffectPresetEditorApp");
const FName FSoundEffectPresetEditor::PropertiesTabId("SoundEffectPresetEditor_Properties");
const FName FSoundEffectPresetEditor::UserWidgetTabId("SoundEffectPresetEditor_UserWidget");

FSoundEffectPresetEditor::FSoundEffectPresetEditor()
	: SoundEffectPreset(nullptr)
{
}

void FSoundEffectPresetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_SoundEffectPresetEditor", "Sound Effect Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FSoundEffectPresetEditor::SpawnTab_Properties))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));


	for (int32 i = 0; i < UserWidgets.Num(); i++)
	{
		TStrongObjectPtr<UUserWidget> UserWidget = UserWidgets[i];
		const FString ClassName = SoundEffectPreset->GetClass()->GetName();
		FSlateIcon BPIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.CreateClassBlueprint");

		const FName UserWidgetTabIdIndexed = FName(UserWidgetTabId.ToString() + FString(TEXT("_")) + FString::FromInt(i));
		InTabManager->RegisterTabSpawner(UserWidgetTabIdIndexed, FOnSpawnTab::CreateLambda([this, i](const FSpawnTabArgs& Args) { return SpawnTab_UserWidgetEditor(Args, i); }))
			.SetDisplayName(FText::Format(LOCTEXT("UserEditorTabFormat", "{0} Editor"), FText::FromString(ClassName)))
			.SetGroup(WorkspaceMenuCategory.ToSharedRef())
			.SetIcon(BPIcon);
	}
}

void FSoundEffectPresetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(PropertiesTabId);

	for (int32 i = 0; i < UserWidgets.Num(); i++)
	{
		const FName UserWidgetTabIdIndexed = FName(UserWidgetTabId.ToString() + FString(TEXT("_")) + FString::FromInt(i));
		InTabManager->UnregisterTabSpawner(UserWidgetTabIdIndexed);
	}
}

void FSoundEffectPresetEditor::Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USoundEffectPreset* InPresetToEdit, const TArray<UUserWidget*>& InWidgetBlueprints)
{
	if (!ensure(InPresetToEdit))
	{
		return;
	}

	SoundEffectPreset = TStrongObjectPtr<USoundEffectPreset>(InPresetToEdit);
	InitPresetWidgets(InWidgetBlueprints);
	
	// Support undo/redo
	InPresetToEdit->SetFlags(RF_Transactional);

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NotifyHook = this;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertiesView = PropertyModule.CreateDetailView(Args);
	PropertiesView->SetObject(InPresetToEdit);

	TSharedRef<FTabManager::FSplitter> TabSplitter = FTabManager::NewSplitter()
		->SetSizeCoefficient(0.9f)
		->SetOrientation(Orient_Horizontal)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.225f)
			->AddTab(PropertiesTabId, ETabState::OpenedTab)
		);

	if (!UserWidgets.IsEmpty())
	{
		TabSplitter->Split
		(
			FTabManager::NewSplitter()
			->SetSizeCoefficient(0.775f)
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetHideTabWell(true)
				->SetSizeCoefficient(0.33f)
				->AddTab(UserWidgetTabId, ETabState::OpenedTab)
			)
		);
	}

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_SoundEffectPresetEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split(TabSplitter)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	const bool bToolbarFocusable = false;
	const bool bUseSmallIcons = true;
	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		AppIdentifier,
		StandaloneDefaultLayout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		InPresetToEdit,
		bToolbarFocusable,
		bUseSmallIcons);
}

FName FSoundEffectPresetEditor::GetEditorName() const
{
	return "Preset Editor";
}

FName FSoundEffectPresetEditor::GetToolkitFName() const
{
	return FName("SoundEffectPresetEditor");
}

FText FSoundEffectPresetEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Sound Effect Preset Editor");
}

void FSoundEffectPresetEditor::InitPresetWidgets(const TArray<UUserWidget*>& InWidgets)
{
	if (!SoundEffectPreset)
	{
		return;
	}

	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		for (UUserWidget* Widget : InWidgets)
		{
			if (Widget)
			{
				UserWidgets.Add(TStrongObjectPtr<UUserWidget>(Widget));
				IAudioWidgetInterface::Execute_OnConstructed(Widget, Cast<UObject>(SoundEffectPreset.Get()));
			}
		}
	}
}

FString FSoundEffectPresetEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "SoundEffect ").ToString();
}

FLinearColor FSoundEffectPresetEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.0f, 0.0f, 0.2f, 0.5f );
}

EOrientation FSoundEffectPresetEditor::GetSnapLabelOrientation() const
{
	return FMultiBoxSettings::UseSmallToolBarIcons.Get()
		? EOrientation::Orient_Horizontal
		: EOrientation::Orient_Vertical;
}

void FSoundEffectPresetEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (SoundEffectPreset)
	{
		for (TStrongObjectPtr<UUserWidget>& UserWidget : UserWidgets)
		{
			const FName PropertyName = PropertyThatChanged->GetFName();
			IAudioWidgetInterface::Execute_OnPropertyChanged(UserWidget.Get(), SoundEffectPreset.Get(), PropertyName);
		}
	}
}

void FSoundEffectPresetEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged)
{
	if (SoundEffectPreset)
	{
		for (TStrongObjectPtr<UUserWidget>& UserWidget : UserWidgets)
		{
			auto Node = PropertyThatChanged->GetHead();
			while(Node)
			{
				if (FProperty* Property = Node->GetValue())
				{
					const FName PropertyName = Property->GetFName();
					IAudioWidgetInterface::Execute_OnPropertyChanged(UserWidget.Get(), SoundEffectPreset.Get(), PropertyName);
				}
				Node = Node->GetNextNode();
			}
		}
	}
}

TSharedRef<SDockTab> FSoundEffectPresetEditor::SpawnTab_Properties(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PropertiesTabId);

	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("SoundSoundEffectDetailsTitle", "Details"))
		[
			PropertiesView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FSoundEffectPresetEditor::SpawnTab_UserWidgetEditor(const FSpawnTabArgs& Args, int32 WidgetIndex)
{
	FName IconBrushName = IAudioWidgetInterface::Execute_GetIconBrushName(UserWidgets[WidgetIndex].Get());
	if (IconBrushName == FName())
	{
		IconBrushName = ("GenericEditor.Tabs.Properties");
	}

	const FSlateBrush* IconBrush = FEditorStyle::GetBrush("SoundEffectPresetEditor.Tabs.Properties");

	FText Label = FText::FromString(SoundEffectPreset->GetName());
	if (UserWidgets.Num() < WidgetIndex)
	{
		return SNew(SDockTab)
			.Icon(IconBrush)
			.Label(Label)
			.TabColorScale(GetTabColorScale())
			[
				SNew(STextBlock)
					.Text(LOCTEXT("InvalidPresetEditor", "No editor available for SoundEffectPreset.  Widget Blueprint not found."))
			];
	}

	const FText CustomLabel = IAudioWidgetInterface::Execute_GetEditorName(UserWidgets[WidgetIndex].Get());
	if (!CustomLabel.IsEmpty())
	{
		Label = CustomLabel;
	}

	return SNew(SDockTab)
		.Icon(IconBrush)
		.Label(Label)
		.TabColorScale(GetTabColorScale())
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0.0f)
			[
				UserWidgets[WidgetIndex]->TakeWidget()
			]
		];
}

void FSoundEffectPresetEditor::PostUndo(bool bSuccess)
{
}

void FSoundEffectPresetEditor::PostRedo(bool bSuccess)
{
}
#undef LOCTEXT_NAMESPACE
