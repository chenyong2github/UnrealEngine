// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureDataDetailsCustomization.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Notifications/SErrorText.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "GameFeatureData.h"
#include "../../GameFeatures/Private/GameFeaturePluginStateMachine.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

//////////////////////////////////////////////////////////////////////////
// FGameFeatureDataDetailsCustomization

TSharedRef<IDetailCustomization> FGameFeatureDataDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FGameFeatureDataDetailsCustomization);
}

void FGameFeatureDataDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	ErrorTextWidget = SNew(SErrorText)
		.ToolTipText(LOCTEXT("ErrorTooltip", "The error raised while attempting to change the state of this feature"));

	// Create a category so this is displayed early in the properties
	IDetailCategoryBuilder& TopCategory = DetailBuilder.EditCategory("Plugin Settings", FText::GetEmpty(), ECategoryPriority::Important);

	PluginURL.Reset();
	ObjectsBeingCustomized.Empty();
	DetailBuilder.GetObjectsBeingCustomized(/*out*/ ObjectsBeingCustomized);

	if (ObjectsBeingCustomized.Num() == 1)
	{
		const UGameFeatureData* GameFeature = CastChecked<const UGameFeatureData>(ObjectsBeingCustomized[0]);

		TArray<FString> PathParts;
		GameFeature->GetOutermost()->GetName().ParseIntoArray(PathParts, TEXT("/"));

		UGameFeaturesSubsystem& Subsystem = UGameFeaturesSubsystem::Get();
		Subsystem.GetPluginURLForBuiltInPluginByName(PathParts[0], /*out*/ PluginURL);

		TSharedRef<SWidget> CurrentStateSwitcher = SNew(SSegmentedControl<EGameFeaturePluginState>)
			.Value(this, &FGameFeatureDataDetailsCustomization::GetCurrentState)
			.OnValueChanged(this, &FGameFeatureDataDetailsCustomization::ChangeDesiredState)
			.ToolTipText(LOCTEXT("StateSwitcherTooltip", "Attempt to change the current state of this game feature"))
			+SSegmentedControl<EGameFeaturePluginState>::Slot(EGameFeaturePluginState::Installed)
				.Text(GetDisplayNameOfState(EGameFeaturePluginState::Installed))
			+ SSegmentedControl<EGameFeaturePluginState>::Slot(EGameFeaturePluginState::Registered)
				.Text(GetDisplayNameOfState(EGameFeaturePluginState::Registered))
			+ SSegmentedControl<EGameFeaturePluginState>::Slot(EGameFeaturePluginState::Loaded)
				.Text(GetDisplayNameOfState(EGameFeaturePluginState::Loaded))
			+ SSegmentedControl<EGameFeaturePluginState>::Slot(EGameFeaturePluginState::Active)
				.Text(GetDisplayNameOfState(EGameFeaturePluginState::Active))
			;

		const float Padding = 8.0f;

		FDetailWidgetRow& ControlRow = TopCategory.AddCustomRow(LOCTEXT("ControlSearchText", "Plugin State Control"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CurrentState", "Current State"))
				.Font(DetailBuilder.GetDetailFont())
			]

			.ValueContent()
			.MinDesiredWidth(400.0f)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						CurrentStateSwitcher
					]
					+SHorizontalBox::Slot()
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &FGameFeatureDataDetailsCustomization::GetStateStatusDisplay)
						.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("ButtonText"))
						.ToolTipText(LOCTEXT("OtherStateToolTip", "The current state of this game feature plugin"))
						.ColorAndOpacity(FAppStyle::Get().GetSlateColor(TEXT("Colors.AccentYellow")))
					]
				]
				+SVerticalBox::Slot()
				.HAlign(HAlign_Left)
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([=]() { return (GetCurrentState() == EGameFeaturePluginState::Active) ? EVisibility::Visible : EVisibility::Collapsed; })
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(Padding)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.Lock"))
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(FMargin(0.f, Padding, Padding, Padding))
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.WrapTextAt(300.0f)
						.Text(LOCTEXT("Active_PreventingEditing", "Deactivate the feature before editing the Game Feature Data"))
						.Font(DetailBuilder.GetDetailFont())
						.ColorAndOpacity(FAppStyle::Get().GetSlateColor(TEXT("Colors.AccentYellow")))
					]
				]
				+SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				[
					ErrorTextWidget.ToSharedRef()
				]
			];


//@TODO: This disables the mode switcher widget too (and it's a const cast hack...)
// 		if (IDetailsView* ConstHackDetailsView = const_cast<IDetailsView*>(DetailBuilder.GetDetailsView()))
// 		{
// 			ConstHackDetailsView->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateLambda([CapturedThis = this] { return CapturedThis->GetCurrentState() != EGameFeaturePluginState::Active; }));
// 		}
	}
}

void FGameFeatureDataDetailsCustomization::ChangeDesiredState(EGameFeaturePluginState DesiredState)
{
	ErrorTextWidget->SetError(FText::GetEmpty());
	const TWeakPtr<FGameFeatureDataDetailsCustomization> WeakThisPtr = StaticCastSharedRef<FGameFeatureDataDetailsCustomization>(AsShared());


	UGameFeaturesSubsystem& Subsystem = UGameFeaturesSubsystem::Get();

	const EGameFeaturePluginState CurrentState = GetCurrentState();
	if (DesiredState == EGameFeaturePluginState::Active)
	{
		Subsystem.LoadAndActivateGameFeaturePlugin(PluginURL, FGameFeaturePluginLoadComplete::CreateStatic(&FGameFeatureDataDetailsCustomization::OnOperationCompletedOrFailed, WeakThisPtr));
	}
	else if (DesiredState == EGameFeaturePluginState::Loaded)
	{
		if (CurrentState < EGameFeaturePluginState::Loaded)
		{
			Subsystem.LoadGameFeaturePlugin(PluginURL, FGameFeaturePluginLoadComplete::CreateStatic(&FGameFeatureDataDetailsCustomization::OnOperationCompletedOrFailed, WeakThisPtr));
		}
		else
		{
			Subsystem.DeactivateGameFeaturePlugin(PluginURL, FGameFeaturePluginDeactivateComplete::CreateStatic(&FGameFeatureDataDetailsCustomization::OnOperationCompletedOrFailed, WeakThisPtr));
		}
	}
	else if (DesiredState == EGameFeaturePluginState::Registered)
	{
		if (CurrentState >= EGameFeaturePluginState::Loaded)
		{
			Subsystem.UnloadGameFeaturePlugin(PluginURL, FGameFeaturePluginDeactivateComplete::CreateStatic(&FGameFeatureDataDetailsCustomization::OnOperationCompletedOrFailed, WeakThisPtr), /*bKeepRegistered=*/ true);
		}
		else
		{
			//@TODO: No public transition from Installed..Registered is exposed yet
		}
	}
	else if (DesiredState == EGameFeaturePluginState::Installed)
	{
		//@TODO: No public transition from something greater than Installed to Installed is exposed yet
		//@TODO: Do we need to support unregistering?  If not, should remove this button
		Subsystem.UnloadGameFeaturePlugin(PluginURL, FGameFeaturePluginDeactivateComplete::CreateStatic(&FGameFeatureDataDetailsCustomization::OnOperationCompletedOrFailed, WeakThisPtr), /*bKeepRegistered=*/ false);
	}
}

FText FGameFeatureDataDetailsCustomization::GetDisplayNameOfState(EGameFeaturePluginState State)
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

EGameFeaturePluginState FGameFeatureDataDetailsCustomization::GetCurrentState() const
{
	return UGameFeaturesSubsystem::Get().GetPluginState(PluginURL);
}

FText FGameFeatureDataDetailsCustomization::GetStateStatusDisplay() const
{
	// Display the current state/transition for anything but the four acceptable destination states (which are already covered by the switcher)
	const EGameFeaturePluginState State = GetCurrentState();
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

void FGameFeatureDataDetailsCustomization::OnOperationCompletedOrFailed(const UE::GameFeatures::FResult& Result, const TWeakPtr<FGameFeatureDataDetailsCustomization> WeakThisPtr)
{
	if (Result.HasError())
	{
		TSharedPtr<FGameFeatureDataDetailsCustomization> StrongThis = WeakThisPtr.Pin();
		if (StrongThis.IsValid())
		{
			StrongThis->ErrorTextWidget->SetError(FText::AsCultureInvariant(Result.GetError()));
		}
	}
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
