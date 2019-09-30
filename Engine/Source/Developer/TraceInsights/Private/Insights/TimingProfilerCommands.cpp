// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Insights/TimingProfilerCommands.h"

#include "DesktopPlatformModule.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/TimingProfilerManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "FTimingProfilerCommands"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingProfilerMenuBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingProfilerMenuBuilder::AddMenuEntry(FMenuBuilder& MenuBuilder, const TSharedPtr< FUICommandInfo >& UICommandInfo, const FUIAction& UIAction)
{
	MenuBuilder.AddMenuEntry
	(
		UICommandInfo->GetLabel(),
		UICommandInfo->GetDescription(),
		UICommandInfo->GetIcon(),
		UIAction,
		NAME_None,
		UICommandInfo->GetUserInterfaceType()
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingProfilerCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingProfilerCommands::FTimingProfilerCommands()
	: TCommands<FTimingProfilerCommands>(
		TEXT("TimingProfilerCommand"), // Context name for fast lookup
		NSLOCTEXT("Contexts", "TimingProfilerCommand", "Timing Insights"), // Localized context name for displaying
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // Icon Style Set
	)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// UI_COMMAND takes long for the compiler to optimize
PRAGMA_DISABLE_OPTIMIZATION
void FTimingProfilerCommands::RegisterCommands()
{
	UI_COMMAND(ToggleFramesTrackVisibility, "Frames", "Toggles the visibility of the Frames track", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Control, EKeys::F));
	UI_COMMAND(ToggleGraphTrackVisibility, "Graph", "Toggles the visibility of the Overview Graph track", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Control, EKeys::G));
	UI_COMMAND(ToggleTimingViewVisibility, "Timing", "Toggles the visibility of the main Timing view", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Control, EKeys::T));
	UI_COMMAND(ToggleTimersViewVisibility, "Timers", "Toggles the visibility of the Timers view", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleCallersTreeViewVisibility, "Callers", "Toggles the visibility of the Callers tree view", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleCalleesTreeViewVisibility, "Callees", "Toggles the visibility of the Callees tree view", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleStatsCountersViewVisibility, "Counters", "Toggles the visibility of the Counters view", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleLogViewVisibility, "Log", "Toggles the visibility of the Log view", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::L));
}
PRAGMA_ENABLE_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingViewCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingViewCommands::FTimingViewCommands()
	: TCommands<FTimingViewCommands>(
		TEXT("TimingViewCommand"), // Context name for fast lookup
		NSLOCTEXT("Contexts", "TimingViewCommand", "Timing Insights"), // Localized context name for displaying
		NAME_None, // Parent
		FInsightsStyle::GetStyleSetName() // Icon Style Set
	)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// UI_COMMAND takes long for the compiler to optimize
PRAGMA_DISABLE_OPTIMIZATION
void FTimingViewCommands::RegisterCommands()
{
	UI_COMMAND(ShowAllGpuTracks, "GPU Track", "Show/hide the GPU track", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::Y));
	UI_COMMAND(ShowAllCpuTracks, "CPU Thread Tracks", "Show/hide all CPU tracks (and all CPU thread groups)", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::U));
}
PRAGMA_ENABLE_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Toggle Commands
////////////////////////////////////////////////////////////////////////////////////////////////////

#define IMPLEMENT_TOGGLE_COMMAND(CmdName, IsEnabled, SetIsEnabled) \
	\
	void FTimingProfilerActionManager::Map_##CmdName##_Global()\
	{\
		This->CommandList->MapAction(This->GetCommands().CmdName, CmdName##_Custom());\
	}\
	\
	const FUIAction FTimingProfilerActionManager::CmdName##_Custom() \
	{\
		FUIAction UIAction;\
		UIAction.ExecuteAction = FExecuteAction::CreateRaw(this, &FTimingProfilerActionManager::CmdName##_Execute);\
		UIAction.CanExecuteAction = FCanExecuteAction::CreateRaw(this, &FTimingProfilerActionManager::CmdName##_CanExecute);\
		UIAction.GetActionCheckState = FGetActionCheckState::CreateRaw(this, &FTimingProfilerActionManager::CmdName##_GetCheckState);\
		return UIAction;\
	}\
	\
	void FTimingProfilerActionManager::CmdName##_Execute()\
	{\
		const bool b##IsEnabled = !This->IsEnabled();\
		This->SetIsEnabled(b##IsEnabled);\
	}\
	\
	bool FTimingProfilerActionManager::CmdName##_CanExecute() const\
	{\
		return FInsightsManager::Get()->GetSession().IsValid();\
	}\
	\
	ECheckBoxState FTimingProfilerActionManager::CmdName##_GetCheckState() const\
	{\
		const bool b##IsEnabled = This->IsEnabled();\
		return b##IsEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;\
	}

IMPLEMENT_TOGGLE_COMMAND(ToggleFramesTrackVisibility, IsFramesTrackVisible, ShowHideFramesTrack)
IMPLEMENT_TOGGLE_COMMAND(ToggleGraphTrackVisibility, IsGraphTrackVisible, ShowHideGraphTrack)
IMPLEMENT_TOGGLE_COMMAND(ToggleTimingViewVisibility, IsTimingViewVisible, ShowHideTimingView)
IMPLEMENT_TOGGLE_COMMAND(ToggleTimersViewVisibility, IsTimersViewVisible, ShowHideTimersView)
IMPLEMENT_TOGGLE_COMMAND(ToggleCallersTreeViewVisibility, IsCallersTreeViewVisible, ShowHideCallersTreeView)
IMPLEMENT_TOGGLE_COMMAND(ToggleCalleesTreeViewVisibility, IsCalleesTreeViewVisible, ShowHideCalleesTreeView)
IMPLEMENT_TOGGLE_COMMAND(ToggleStatsCountersViewVisibility, IsStatsCountersViewVisible, ShowHideStatsCountersView)
IMPLEMENT_TOGGLE_COMMAND(ToggleLogViewVisibility, IsLogViewVisible, ShowHideLogView)

#undef IMPLEMENT_TOGGLE_COMMAND

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
