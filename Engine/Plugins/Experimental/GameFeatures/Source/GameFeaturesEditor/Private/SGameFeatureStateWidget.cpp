// Copyright Epic Games, Inc. All Rights Reserved.

#include "SGameFeatureStateWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "GameFeatureData.h"
#include "../../GameFeatures/Private/GameFeaturePluginStateMachine.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

//////////////////////////////////////////////////////////////////////////
// FGameFeatureDataDetailsCustomization

void SGameFeatureStateWidget::Construct(const FArguments& InArgs)
{
	CurrentState = InArgs._CurrentState;

	ChildSlot
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SSegmentedControl<EGameFeaturePluginState>)
			.Value(CurrentState)
			.OnValueChanged(InArgs._OnStateChanged)
//			.ToolTipText(LOCTEXT("StateSwitcherTooltip", "Attempt to change the current state of this game feature"))
			+SSegmentedControl<EGameFeaturePluginState>::Slot(EGameFeaturePluginState::Installed)
				.Text(GetDisplayNameOfState(EGameFeaturePluginState::Installed))
			+ SSegmentedControl<EGameFeaturePluginState>::Slot(EGameFeaturePluginState::Registered)
				.Text(GetDisplayNameOfState(EGameFeaturePluginState::Registered))
			+ SSegmentedControl<EGameFeaturePluginState>::Slot(EGameFeaturePluginState::Loaded)
				.Text(GetDisplayNameOfState(EGameFeaturePluginState::Loaded))
			+ SSegmentedControl<EGameFeaturePluginState>::Slot(EGameFeaturePluginState::Active)
				.Text(GetDisplayNameOfState(EGameFeaturePluginState::Active))
		]
		+SHorizontalBox::Slot()
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SGameFeatureStateWidget::GetStateStatusDisplay)
			.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("ButtonText"))
//			.ToolTipText(LOCTEXT("OtherStateToolTip", "The current state of this game feature plugin"))
			.ColorAndOpacity(FAppStyle::Get().GetSlateColor(TEXT("Colors.AccentYellow")))
		]
	];
}

FText SGameFeatureStateWidget::GetDisplayNameOfState(EGameFeaturePluginState State)
{
	switch (State)
	{
	case EGameFeaturePluginState::Uninitialized: return LOCTEXT("UninitializedStateDisplayName", "Uninitialized");
	case EGameFeaturePluginState::UnknownStatus: return LOCTEXT("UnknownStatusStateDisplayName", "UnknownStatus");
	case EGameFeaturePluginState::CheckingStatus: return LOCTEXT("CheckingStatusStateDisplayName", "CheckingStatus");
	case EGameFeaturePluginState::StatusKnown: return LOCTEXT("StatusKnownStateDisplayName", "StatusKnown");
	case EGameFeaturePluginState::Uninstalling: return LOCTEXT("UninstallingStateDisplayName", "Uninstalling");
	case EGameFeaturePluginState::Downloading: return LOCTEXT("DownloadingStateDisplayName", "Downloading");
	case EGameFeaturePluginState::Installed: return LOCTEXT("InstalledStateDisplayName", "Installed");
	case EGameFeaturePluginState::Unmounting: return LOCTEXT("UnmountingStateDisplayName", "Unmounting");
	case EGameFeaturePluginState::Mounting: return LOCTEXT("MountingStateDisplayName", "Mounting");
	case EGameFeaturePluginState::WaitingForDependencies: return LOCTEXT("WaitingForDependenciesStateDisplayName", "WaitingForDependencies");
	case EGameFeaturePluginState::Unregistering: return LOCTEXT("UnregisteringStateDisplayName", "Unregistering");
	case EGameFeaturePluginState::Registering: return LOCTEXT("RegisteringStateDisplayName", "Registering");
	case EGameFeaturePluginState::Registered: return LOCTEXT("RegisteredStateDisplayName", "Registered");
	case EGameFeaturePluginState::Unloading: return LOCTEXT("UnloadingStateDisplayName", "Unloading");
	case EGameFeaturePluginState::Loading: return LOCTEXT("LoadingStateDisplayName", "Loading");
	case EGameFeaturePluginState::Loaded: return LOCTEXT("LoadedStateDisplayName", "Loaded");
	case EGameFeaturePluginState::Deactivating: return LOCTEXT("DeactivatingStateDisplayName", "Deactivating");
	case EGameFeaturePluginState::Activating: return LOCTEXT("ActivatingStateDisplayName", "Activating");
	case EGameFeaturePluginState::Active: return LOCTEXT("ActiveStateDisplayName", "Active");
	default:
		check(0);
		return FText::GetEmpty();
	}
}

FText SGameFeatureStateWidget::GetStateStatusDisplay() const
{
	// Display the current state/transition for anything but the four acceptable destination states (which are already covered by the switcher)
	const EGameFeaturePluginState State = CurrentState.Get();
	switch (State)
	{
		case EGameFeaturePluginState::Active:
		case EGameFeaturePluginState::Installed:
		case EGameFeaturePluginState::Loaded:
		case EGameFeaturePluginState::Registered:
			return FText::GetEmpty();
		default:
			return GetDisplayNameOfState(State);
	}
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
