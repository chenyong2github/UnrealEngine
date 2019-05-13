// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Insights/IoProfilerCommands.h"

#include "DesktopPlatformModule.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/IoProfilerManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "FIoProfilerCommands"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FIoProfilerMenuBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

void FIoProfilerMenuBuilder::AddMenuEntry(FMenuBuilder& MenuBuilder, const TSharedPtr< FUICommandInfo >& UICommandInfo, const FUIAction& UIAction)
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
// FIoProfilerCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

FIoProfilerCommands::FIoProfilerCommands()
	: TCommands<FIoProfilerCommands>(
		TEXT("IoProfilerCommand"), // Context name for fast lookup
		NSLOCTEXT("Contexts", "IoProfilerCommand", "Timing Profiler Command"), // Localized context name for displaying
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // Icon Style Set
	)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// UI_COMMAND takes long for the compile to optimize
PRAGMA_DISABLE_OPTIMIZATION
void FIoProfilerCommands::RegisterCommands()
{
	UI_COMMAND(ToggleFramesTrackVisibility, "Frames", "Toggles the visibility of the Frames track", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Control, EKeys::F));
	UI_COMMAND(ToggleGraphTrackVisibility, "Graph", "Toggles the visibility of the Overview Graph track", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Control, EKeys::G));
	UI_COMMAND(ToggleTimingTrackVisibility, "Timing", "Toggles the visibility of the main Timing view", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Control, EKeys::T));
	UI_COMMAND(ToggleTimersViewVisibility, "Timers", "Toggles the visibility of the Timers view", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::T));
	UI_COMMAND(ToggleLogViewVisibility, "Log", "Toggles the visibility of the Log view", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::L));
}
PRAGMA_ENABLE_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Toggle Commands
////////////////////////////////////////////////////////////////////////////////////////////////////

#define IMPLEMENT_TOGGLE_COMMAND(CmdName, IsEnabled, SetIsEnabled) \
	\
	void FIoProfilerActionManager::Map_##CmdName##_Global()\
	{\
		This->CommandList->MapAction(This->GetCommands().CmdName, CmdName##_Custom());\
	}\
	\
	const FUIAction FIoProfilerActionManager::CmdName##_Custom() const\
	{\
		FUIAction UIAction;\
		UIAction.ExecuteAction = FExecuteAction::CreateRaw(this, &FIoProfilerActionManager::CmdName##_Execute);\
		UIAction.CanExecuteAction = FCanExecuteAction::CreateRaw(this, &FIoProfilerActionManager::CmdName##_CanExecute);\
		UIAction.GetActionCheckState = FGetActionCheckState::CreateRaw(this, &FIoProfilerActionManager::CmdName##_GetCheckState);\
		return UIAction;\
	}\
	\
	void FIoProfilerActionManager::CmdName##_Execute()\
	{\
		const bool b##IsEnabled = !This->IsEnabled();\
		This->SetIsEnabled(b##IsEnabled);\
	}\
	\
	bool FIoProfilerActionManager::CmdName##_CanExecute() const\
	{\
		return FInsightsManager::Get()->GetSession().IsValid();\
	}\
	\
	ECheckBoxState FIoProfilerActionManager::CmdName##_GetCheckState() const\
	{\
		const bool b##IsEnabled = This->IsEnabled();\
		return b##IsEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;\
	}

IMPLEMENT_TOGGLE_COMMAND(ToggleFramesTrackVisibility, IsFramesTrackVisible, SetFramesTrackVisible)
IMPLEMENT_TOGGLE_COMMAND(ToggleGraphTrackVisibility, IsGraphTrackVisible, SetGraphTrackVisible)
IMPLEMENT_TOGGLE_COMMAND(ToggleTimingTrackVisibility, IsTimingTrackVisible, SetTimingTrackVisible)
IMPLEMENT_TOGGLE_COMMAND(ToggleTimersViewVisibility, IsTimersViewVisible, SetTimersViewVisible)
IMPLEMENT_TOGGLE_COMMAND(ToggleLogViewVisibility, IsLogViewVisible, SetLogViewVisible)

#undef IMPLEMENT_TOGGLE_COMMAND

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
