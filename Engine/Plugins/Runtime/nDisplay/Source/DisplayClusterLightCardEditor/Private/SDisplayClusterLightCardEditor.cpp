// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterLightCardEditor.h"

#include "IDisplayClusterOperator.h"
#include "DisplayClusterRootActor.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterLightCardEditor"

const FName SDisplayClusterLightCardEditor::TabName = TEXT("DisplayClusterLightCardEditorTab");

void SDisplayClusterLightCardEditor::RegisterTabSpawner()
{
	IDisplayClusterOperator::Get().OnRegisterLayoutExtensions().AddStatic(&SDisplayClusterLightCardEditor::RegisterLayoutExtension);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateStatic(&SDisplayClusterLightCardEditor::SpawnInTab))
		.SetDisplayName(LOCTEXT("TabDisplayName", "Light Cards Editor"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Editing tools for nDisplay light cards."));

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension("General", EExtensionHook::After, nullptr, FToolBarExtensionDelegate::CreateStatic(&SDisplayClusterLightCardEditor::ExtendToolbar));
	IDisplayClusterOperator::Get().GetOperatorToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
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
		.TabRole(ETabRole::NomadTab);

	MajorTab->SetContent(SNew(SDisplayClusterLightCardEditor, MajorTab, SpawnTabArgs.GetOwnerWindow()));

	return MajorTab;
}

void SDisplayClusterLightCardEditor::ExtendToolbar(FToolBarBuilder& ToolbarBuilder)
{
	// TODO: Any toolbar buttons needed for the lightcards editor can be added to the operator panel's toobar using this toolbar extender
}

SDisplayClusterLightCardEditor::~SDisplayClusterLightCardEditor()
{
	IDisplayClusterOperator::Get().OnActiveRootActorChanged().Remove(ActiveRootActorChangedHandle);
}

void SDisplayClusterLightCardEditor::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& MajorTabOwner, const TSharedPtr<SWindow>& WindowOwner)
{
	ActiveRootActorChangedHandle = IDisplayClusterOperator::Get().OnActiveRootActorChanged().AddSP(this, &SDisplayClusterLightCardEditor::OnActiveRootActorChanged);

    TabManager = FGlobalTabmanager::Get()->NewTabManager(MajorTabOwner);

	// TODO: Register the various tabs for the light card editor here

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("nDisplayLightCardEditorLayout")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(EOrientation::Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetHideTabWell(true)
			)
		);

	ChildSlot
	[
		TabManager->RestoreFrom(Layout, WindowOwner).ToSharedRef()
	];
}

void SDisplayClusterLightCardEditor::OnActiveRootActorChanged(ADisplayClusterRootActor* NewRootActor)
{
	ActiveRootActor = NewRootActor;

	// TODO: Need to pass a refresh command to any child tabs of this widget to inform them that the active root actor has been changed.
	// The new root actor pointer could be null, indicating that it was deleted or the user didn't select a valid root actor
}