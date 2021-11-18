// Copyright Epic Games, Inc. All Rights Reserved.

/**
* Control Rig Edit Mode Toolkit
*/
#include "ControlRigEditModeToolkit.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "EditorModes.h"
#include "Toolkits/BaseToolkit.h"
#include "EditorModeManager.h"
#include "SControlRigEditModeTools.h"
#include "ControlRigEditMode.h"
#include "Modules/ModuleManager.h"
#include "EditMode/SControlRigBaseListWidget.h"
#include "EditMode/SControlRigTweenWidget.h"
#include "EditMode/SControlRigSnapper.h"
#include "Tools/SMotionTrailOptions.h"
#include "Editor/SControlRigProfilingView.h"
#include "Toolkits/AssetEditorModeUILayer.h"
#include "Widgets/Docking/SDockTab.h"
#include "ControlRigEditModeSettings.h"

#define LOCTEXT_NAMESPACE "FControlRigEditModeToolkit"

namespace 
{
	static const FName AnimationName(TEXT("Animation")); 
	const TArray<FName> AnimationPaletteNames = { AnimationName };
}

const FName FControlRigEditModeToolkit::PoseTabName = FName(TEXT("PoseTab"));
const FName FControlRigEditModeToolkit::MotionTrailTabName = FName(TEXT("MotionTrailTab"));
const FName FControlRigEditModeToolkit::SnapperTabName = FName(TEXT("SnapperTab"));
const FName FControlRigEditModeToolkit::TweenOverlayName = FName(TEXT("TweenOverlay"));

void FControlRigEditModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	SAssignNew(ModeTools, SControlRigEditModeTools, SharedThis(this), EditMode, EditMode.GetWorld());

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;

	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.bSearchInitialKeyFocus = false;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	FModeToolkit::Init(InitToolkitHost);
}

FControlRigEditModeToolkit::~FControlRigEditModeToolkit()
{
	if (FSlateApplication::IsInitialized())
	{
		RemoveAndDestroyTweenOverlay();
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("ControlRigProfiler");
	}
}


void FControlRigEditModeToolkit::GetToolPaletteNames(TArray<FName>& InPaletteName) const
{
	InPaletteName = AnimationPaletteNames;
}

FText FControlRigEditModeToolkit::GetToolPaletteDisplayName(FName PaletteName) const
{
	if (PaletteName == AnimationName)
	{
		FText::FromName(AnimationName);
	}
	return FText();
}

void FControlRigEditModeToolkit::BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolBarBuilder)
{
	if (PaletteName == AnimationName)
	{
		ModeTools->CustomizeToolBarPalette(ToolBarBuilder);
	}
}

void FControlRigEditModeToolkit::OnToolPaletteChanged(FName PaletteName)
{

}

void FControlRigEditModeToolkit::TryInvokeToolkitUI(const FName InName)
{
	if (InName == MotionTrailTabName)
	{
		FTabId MotionTrailTabID(MotionTrailTabName);
		FGlobalTabmanager::Get()->TryInvokeTab(MotionTrailTabID);
	}
	if (InName == PoseTabName)
	{
		TryInvokePoseTab();
	}
	if (InName == SnapperTabName)
	{
		TryInvokeSnapperTab();
	}
	else if (InName == TweenOverlayName)
	{
		if(TweenWidget)
		{ 
			RemoveAndDestroyTweenOverlay();
		}
		else
		{
			CreateAndShowTweenOverlay();
		}
	}
}

FText FControlRigEditModeToolkit::GetActiveToolDisplayName() const
{
	return ModeTools->GetActiveToolName();
}

FText FControlRigEditModeToolkit::GetActiveToolMessage() const
{

	return ModeTools->GetActiveToolMessage();
}

TSharedRef<SDockTab> SpawnPoseTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		[
			SNew(SControlRigBaseListWidget)
		];
}

TSharedRef<SDockTab> SpawnSnapperTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		[
			SNew(SControlRigSnapper)
		];
}

TSharedRef<SDockTab> SpawnMotionTrailTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		[
			SNew(SMotionTrailOptions)
		];
}

TSharedRef<SDockTab> SpawnRigProfiler(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SControlRigProfilingView)
		];
}

void FControlRigEditModeToolkit::CreateAndShowTweenOverlay()
{
	FVector2D NewTweenWidgetLocation = GetDefault<UControlRigEditModeSettings>()->LastInViewportTweenWidgetLocation;

	if (NewTweenWidgetLocation.IsZero())
	{
		const FVector2D ActiveViewportSize = GetToolkitHost()->GetActiveViewportSize();
		NewTweenWidgetLocation.X = ActiveViewportSize.X / 2.0f;
		NewTweenWidgetLocation.Y = ActiveViewportSize.Y - 100.0f;
		
	}
	UpdateTweenWidgetLocation(NewTweenWidgetLocation);

	SAssignNew(TweenWidget, SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		.Padding(TAttribute<FMargin>(this, &FControlRigEditModeToolkit::GetTweenWidgetPadding))
		[
			SNew(SControlRigTweenWidget)
			.InOwningToolkit(SharedThis(this))
		];

	TryShowTweenOverlay();
}

void FControlRigEditModeToolkit::TryShowTweenOverlay()
{
	if (TweenWidget)
	{
		GetToolkitHost()->AddViewportOverlayWidget(TweenWidget.ToSharedRef());
	}
}

void FControlRigEditModeToolkit::RemoveAndDestroyTweenOverlay()
{
	TryRemoveTweenOverlay();
	if (TweenWidget)
	{
		TweenWidget.Reset();
	}
}

void FControlRigEditModeToolkit::TryRemoveTweenOverlay()
{
	if (IsHosted() && TweenWidget)
	{
		GetToolkitHost()->RemoveViewportOverlayWidget(TweenWidget.ToSharedRef());
	}
}

void FControlRigEditModeToolkit::UpdateTweenWidgetLocation(const FVector2D InLocation)
{
	const FVector2D ActiveViewportSize = GetToolkitHost()->GetActiveViewportSize();
	FVector2D ScreenPos = InLocation;

	const float EdgeFactor = 0.97f;
	const float MinX = ActiveViewportSize.X * (1 - EdgeFactor);
	const float MinY = ActiveViewportSize.Y * (1 - EdgeFactor);
	const float MaxX = ActiveViewportSize.X * EdgeFactor;
	const float MaxY = ActiveViewportSize.Y * EdgeFactor;
	const bool bOutside = ScreenPos.X < MinX || ScreenPos.X > MaxX || ScreenPos.Y < MinY || ScreenPos.Y > MaxY;
	if (bOutside)
	{
		// reset the location if it was placed out of bounds
		ScreenPos.X = ActiveViewportSize.X / 2.0f;
		ScreenPos.Y = ActiveViewportSize.Y - 100.0f;
	}
	InViewportTweenWidgetLocation = ScreenPos;
	UControlRigEditModeSettings* ControlRigEditModeSettings = GetMutableDefault<UControlRigEditModeSettings>();
	ControlRigEditModeSettings->LastInViewportTweenWidgetLocation = ScreenPos;
	ControlRigEditModeSettings->SaveConfig();
}

FMargin FControlRigEditModeToolkit::GetTweenWidgetPadding() const
{
	return FMargin(InViewportTweenWidgetLocation.X, InViewportTweenWidgetLocation.Y, 0, 0);
}

void FControlRigEditModeToolkit::RequestModeUITabs()
{
	FModeToolkit::RequestModeUITabs();
	if (ModeUILayer.IsValid())
	{
		TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
		TSharedRef<FWorkspaceItem> MenuGroup = ModeUILayerPtr->GetModeMenuCategory().ToSharedRef();
	
		FMinorTabConfig PoseTabInfo;
		PoseTabInfo.OnSpawnTab = FOnSpawnTab::CreateStatic(&SpawnPoseTab);
		PoseTabInfo.TabLabel = LOCTEXT("ControlRigPoseTab", "Control Rig Pose");
		PoseTabInfo.TabTooltip = LOCTEXT("ControlRigPoseTabTooltip", "Show Poses.");
		ModeUILayerPtr->SetModePanelInfo(UAssetEditorUISubsystem::BottomRightTabID, PoseTabInfo);

		FMinorTabConfig SnapperTabInfo;
		SnapperTabInfo.OnSpawnTab = FOnSpawnTab::CreateStatic(&SpawnSnapperTab);
		SnapperTabInfo.TabLabel = LOCTEXT("ControlRigSnapperTab", "Control Rig Snapper");
		SnapperTabInfo.TabTooltip = LOCTEXT("ControlRigSnapperTabTooltip", "Snap child objects to a parent object over a set of frames.");
		ModeUILayerPtr->SetModePanelInfo(UAssetEditorUISubsystem::TopRightTabID, SnapperTabInfo);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(MotionTrailTabName, FOnSpawnTab::CreateStatic(&SpawnMotionTrailTab))
			.SetDisplayName(LOCTEXT("MotionTrailTab", "Motion Trail"))
			.SetTooltipText(LOCTEXT("MotionTrailTabTooltip", "Display motion trails for animated objects."))
			.SetGroup(MenuGroup)
			.SetIcon(FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("HierarchicalProfiler.TabIcon")));

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner("HierarchicalProfiler", FOnSpawnTab::CreateStatic(&SpawnRigProfiler))
			.SetDisplayName(LOCTEXT("HierarchicalProfilerTab", "Hierarchical Profiler"))
			.SetTooltipText(LOCTEXT("HierarchicalProfilerTooltip", "Open the Hierarchical Profiler tab."))
			.SetGroup(MenuGroup)
			.SetIcon(FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("HierarchicalProfiler.TabIcon")));
	}
};

void FControlRigEditModeToolkit::InvokeUI()
{
	FModeToolkit::InvokeUI();

// TODO: any future default tabs will go here
}

void FControlRigEditModeToolkit::TryInvokeSnapperTab()
{
	TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
	ModeUILayerPtr->GetTabManager()->TryInvokeTab(UAssetEditorUISubsystem::TopRightTabID);
}

void FControlRigEditModeToolkit::TryInvokePoseTab()
{
	TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
	ModeUILayerPtr->GetTabManager()->TryInvokeTab(UAssetEditorUISubsystem::BottomRightTabID);
}


#undef LOCTEXT_NAMESPACE