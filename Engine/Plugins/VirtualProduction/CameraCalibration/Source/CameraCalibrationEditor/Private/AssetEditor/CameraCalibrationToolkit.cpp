// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationToolkit.h"

#include "LensFile.h"
#include "SLensEvaluation.h"
#include "SLensFilePanel.h"
#include "SNodalOffsetToolPanel.h"
#include "UI/CameraCalibrationEditorStyle.h"
#include "Widgets/Docking/SDockTab.h"


#define LOCTEXT_NAMESPACE "CameraCalibrationToolkit"


namespace CameraCalibrationToolkitUtils
{
	const FName CameraCalibrationIdentifier(TEXT("CameraCalibrationTools"));
	const FName LensTabId(TEXT("LensFileEditorTab"));
	const FName LensEvaluationTabId(TEXT("LensEvaluationTab"));
	const FName NodalOffsetTabId(TEXT("NodalOffsetTab"));
	const FName LensDetailsTabId(TEXT("LensFileDetailsTab"));
}


TSharedRef<FCameraCalibrationToolkit> FCameraCalibrationToolkit::CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULensFile* InLensFile)
{
	TSharedRef<FCameraCalibrationToolkit> NewEditor = MakeShared<FCameraCalibrationToolkit>();
	NewEditor->InitCameraCalibrationTool(Mode, InitToolkitHost, InLensFile);

	return NewEditor;
}

void FCameraCalibrationToolkit::InitCameraCalibrationTool(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULensFile* InLensFile)
{
	LensFile = InLensFile;
	LensEvaluationWidget = SNew(SLensEvaluation, InLensFile);
	NodalOffsetToolTab = SNew(SNodalOffsetToolPanel, LensFile);
	LensEditorTab = SNew(SLensFilePanel, LensFile)
		.CachedFIZData(TAttribute<FCachedFIZData>::Create(TAttribute<FCachedFIZData>::FGetter::CreateSP(LensEvaluationWidget.ToSharedRef(), &SLensEvaluation::GetLastEvaluatedData)));
		
	TSharedRef<FTabManager::FLayout> NewLayout = FTabManager::NewLayout("CameraCalibrationToolLayout_v0.6")
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
			->Split
			(
				FTabManager::NewSplitter()
				->SetSizeCoefficient(0.85f)
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->AddTab(CameraCalibrationToolkitUtils::LensTabId, ETabState::OpenedTab)
					->AddTab(CameraCalibrationToolkitUtils::NodalOffsetTabId, ETabState::OpenedTab)
					->AddTab(CameraCalibrationToolkitUtils::LensDetailsTabId, ETabState::ClosedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.15f)
				->SetHideTabWell(true)
				->AddTab(CameraCalibrationToolkitUtils::LensEvaluationTabId, ETabState::OpenedTab)
			)
		);

	constexpr bool bCreateDefaultStandaloneMenu = true;
	constexpr bool bCreateDefaultToolbar = true;
	constexpr bool bToolbarFocusable = false;
	constexpr bool bUseSmallIcons = true;
	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		CameraCalibrationToolkitUtils::CameraCalibrationIdentifier,
		NewLayout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		InLensFile,
		bToolbarFocusable,
		bUseSmallIcons);

	RegenerateMenusAndToolbars();
}

void FCameraCalibrationToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_CameraCalibrationTools", "Camera Calibration Panel"));

	Super::RegisterTabSpawners(InTabManager);
	
	InTabManager->RegisterTabSpawner(CameraCalibrationToolkitUtils::LensTabId, FOnSpawnTab::CreateSP(this, &FCameraCalibrationToolkit::HandleSpawnLensEditorTab))
		.SetDisplayName(LOCTEXT("LensEditorTab", "Lens File Editor"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings.Small"));

	InTabManager->RegisterTabSpawner(CameraCalibrationToolkitUtils::LensDetailsTabId, FOnSpawnTab::CreateSP(this, &FCameraCalibrationToolkit::HandleSpawnLensDetailsPanelTab))
		.SetDisplayName(LOCTEXT("LensDetailsTab", "Lens DetailsPanel"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings.Small"));
	
	InTabManager->RegisterTabSpawner(CameraCalibrationToolkitUtils::NodalOffsetTabId, FOnSpawnTab::CreateSP(this, &FCameraCalibrationToolkit::HandleSpawnNodalOffsetTab))
		.SetDisplayName(LOCTEXT("NodalOffsetTab", "Nodal Offset"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings.Small"));

	InTabManager->RegisterTabSpawner(CameraCalibrationToolkitUtils::LensEvaluationTabId, FOnSpawnTab::CreateSP(this, &FCameraCalibrationToolkit::HandleSpawnLensEvaluationTab))
		.SetDisplayName(LOCTEXT("NodalOffsetTab", "Nodal Offset"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings.Small"));
}

void FCameraCalibrationToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(CameraCalibrationToolkitUtils::LensTabId);
	InTabManager->UnregisterTabSpawner(CameraCalibrationToolkitUtils::NodalOffsetTabId);
	InTabManager->UnregisterTabSpawner(CameraCalibrationToolkitUtils::LensDetailsTabId);
	InTabManager->UnregisterTabSpawner(CameraCalibrationToolkitUtils::LensEvaluationTabId);
	Super::UnregisterTabSpawners(InTabManager);
}

bool FCameraCalibrationToolkit::OnRequestClose()
{
	return true;
}

FText FCameraCalibrationToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("PanelToolkitName", "Camera Calibration Tools");
}

FName FCameraCalibrationToolkit::GetToolkitFName() const
{
	static const FName PanelName("CameraCalibrationTools");
	return PanelName;
}

FLinearColor FCameraCalibrationToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(1.0, 0.0f, 0.0f, 1.0f);
}

FString FCameraCalibrationToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("CameraCalibrationTabPrefix", "CameraCalibrationTools").ToString();
}

TSharedRef<SDockTab> FCameraCalibrationToolkit::HandleSpawnNodalOffsetTab(const FSpawnTabArgs& Args) const
{
	check(Args.GetTabId() == CameraCalibrationToolkitUtils::NodalOffsetTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("NodalOffsetToolPanel", "Nodal Offset Tool"))
		.TabColorScale(GetTabColorScale())
		[
			NodalOffsetToolTab.ToSharedRef()
		];
}

TSharedRef<SDockTab> FCameraCalibrationToolkit::HandleSpawnLensEditorTab(const FSpawnTabArgs& Args) const
{
	check(Args.GetTabId() == CameraCalibrationToolkitUtils::LensTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("LensFilePanelLabel", "Lens File Panel"))
		.TabColorScale(GetTabColorScale())
		[
			LensEditorTab.ToSharedRef()
		];
}

TSharedRef<SDockTab> FCameraCalibrationToolkit::HandleSpawnLensDetailsPanelTab(const FSpawnTabArgs& Args) const
{
	check(Args.GetTabId() == CameraCalibrationToolkitUtils::LensDetailsTabId);

	FDetailsViewArgs DetailsViewArg;
	DetailsViewArg.bHideSelectionTip = true;
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedPtr<IDetailsView> DetailsView = PropertyModule.CreateDetailView(DetailsViewArg);
	DetailsView->SetObject(LensFile);

	return SNew(SDockTab)
		.Label(LOCTEXT("LensDetailsViewTab", "Lens DetailsPanel"))
		.TabColorScale(GetTabColorScale())
		[
			DetailsView.ToSharedRef()
		];
}

FCachedFIZData FCameraCalibrationToolkit::GetFIZData() const
{
	if (LensEvaluationWidget)
	{
		return LensEvaluationWidget->GetLastEvaluatedData();
	}

	return FCachedFIZData();
}

TSharedRef<SDockTab> FCameraCalibrationToolkit::HandleSpawnLensEvaluationTab(const FSpawnTabArgs& Args) const
{
	check(Args.GetTabId() == CameraCalibrationToolkitUtils::LensEvaluationTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("LensEvaluationTabLabel", "Lens Evaluation"))
		.TabColorScale(GetTabColorScale())
		[
			LensEvaluationWidget.ToSharedRef()
		];
}

#undef LOCTEXT_NAMESPACE