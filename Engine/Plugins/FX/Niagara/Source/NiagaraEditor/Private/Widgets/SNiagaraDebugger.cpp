// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraDebugger.h"
#include "NiagaraWorldManager.h"

#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "UObject/UObjectIterator.h"
#include "EditorWidgetsModule.h"
#include "PropertyEditorModule.h"
#include "PlatformInfo.h"

#define LOCTEXT_NAMESPACE "SNiagaraDebugger"

namespace NiagaraDebuggerLocal
{
	static FText GetLocalDeviceText()
	{
		return LOCTEXT("LocalDevice", "<Local Device : This Application>");
	}

	static bool IsSupportedPlatform(ITargetPlatform* Platform)
	{
		check(Platform);
		return Platform->SupportsFeature(ETargetPlatformFeatures::DeviceOutputLog);
	}
}

#if WITH_EDITOR
void UNiagaraDebugHUDSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	OnChangedDelegate.Broadcast();

	SaveConfig();
}
#endif

SNiagaraDebugger::SNiagaraDebugger()
{
}

SNiagaraDebugger::~SNiagaraDebugger()
{
	DestroyDeviceList();
}

void SNiagaraDebugger::Construct(const FArguments& InArgs)
{
	InitDeviceList();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	TArray<SNumericDropDown<float>::FNamedValue> PlaybackRateValues;
	PlaybackRateValues.Add(SNumericDropDown<float>::FNamedValue(1.000f, LOCTEXT("PlaybackRate_Full", "1 x"), LOCTEXT("PlaybackRateDesc_Full", "Set playback rate to normal")));
	PlaybackRateValues.Add(SNumericDropDown<float>::FNamedValue(0.500f, LOCTEXT("PlaybackRate_Half", "1/2 x"), LOCTEXT("PlaybackRateDesc_Half", "Set playback rate to half rate")));
	PlaybackRateValues.Add(SNumericDropDown<float>::FNamedValue(0.250f, LOCTEXT("PlaybackRate_Quarter", "1/4 x"), LOCTEXT("PlaybackRateDesc_Quarter", "Set playback rate to quarter rate")));
	PlaybackRateValues.Add(SNumericDropDown<float>::FNamedValue(0.125f, LOCTEXT("PlaybackRate_Eighth", "1/8 x"), LOCTEXT("PlaybackRateDesc_Eighth", "Set playback rate to eighth rate")));
	PlaybackRateValues.Add(SNumericDropDown<float>::FNamedValue(0.062f, LOCTEXT("PlaybackRate_Sixteenth", "1/16 x"), LOCTEXT("PlaybackRateDesc_Sixteenth", "Set playback rate to sixteenth rate")));

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bHideSelectionTip = true;
	TSharedPtr<IDetailsView> DebuggerSettingsDetails = PropertyModule.CreateDetailView(DetailsArgs);
	this->ChildSlot
	[
		SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			// Target device selection
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
					.Text(LOCTEXT("SelectedDevice", "Selected Device: "))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SComboButton)
					.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
					.ForegroundColor(FLinearColor::White)
					.OnGetMenuContent(this, &SNiagaraDebugger::MakeDeviceComboButtonMenu)
					.ContentPadding(FMargin(4.0f, 0.0f))
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(16)
							.HeightOverride(16)
							[
								SNew(SImage).Image(this, &SNiagaraDebugger::GetSelectedTargetDeviceBrush)
							]
						]
						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
							.Text(this, &SNiagaraDebugger::GetSelectedTargetDeviceText)
						]
					]
				]
			]
			// Refresh button / play / pause / step
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(FOnClicked::CreateLambda(
						[&]() -> FReply
						{
							ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.PlaybackMode %d"), ENiagaraDebugPlaybackMode::Play), true);
							return FReply::Handled();
						})
					)
					.Text(FText::FromString(TEXT("Play")))
					.ToolTipText(FText::FromString(TEXT("Set simulations to play")))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(FOnClicked::CreateLambda(
						[&]() -> FReply
						{
							ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.PlaybackMode %d"), ENiagaraDebugPlaybackMode::Paused), true);
							return FReply::Handled();
						})
					)
					.Text(FText::FromString(TEXT("Pause")))
					.ToolTipText(FText::FromString(TEXT("Pause all simulations")))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(FOnClicked::CreateLambda(
						[&]() -> FReply
						{
							ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.PlaybackMode %d"), ENiagaraDebugPlaybackMode::Step), true);
							return FReply::Handled();
						})
					)
					.Text(FText::FromString(TEXT("Step")))
					.ToolTipText(FText::FromString(TEXT("Step a single frame and pause all simulations")))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SNumericDropDown<float>)
					.DropDownValues(PlaybackRateValues)
					.LabelText(LOCTEXT("PlaybackRate", "Playback Rate"))
					.Value(this, &SNiagaraDebugger::GetPlaybackRate)
					.OnValueChanged(this, &SNiagaraDebugger::SetPlaybackRate)
					//SNew(SNumericEntryBox<float>)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(FOnClicked::CreateLambda(
						[&]() -> FReply
						{
							ExecHUDConsoleCommand();
							SetPlaybackRate(PlaybackRate);
							return FReply::Handled();
						})
					)
					.Text(FText::FromString(TEXT("RefreshHUD")))
					.ToolTipText(FText::FromString(TEXT("Force resend the HUD command in case we are out of date")))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(FOnClicked::CreateLambda(
						[&]() -> FReply
						{
							ExecConsoleCommand(TEXT("stat particleperf"), true);
							return FReply::Handled();
						})
					)
					.Text(FText::FromString(TEXT("Toggle ParticlePerf")))
					.ToolTipText(FText::FromString(TEXT("Toggles particle performance stat reporting on / off")))
				]
			]
			// Debug HUD display
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				DebuggerSettingsDetails.ToSharedRef()
			]
		]
	];

	UNiagaraDebugHUDSettings* DebugHUDSettings = GetMutableDefault<UNiagaraDebugHUDSettings>();
	DebuggerSettingsDetails->SetObject(DebugHUDSettings);

	DebugHUDSettings->OnChangedDelegate.AddSP(this, &SNiagaraDebugger::ExecHUDConsoleCommand);
}

void SNiagaraDebugger::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if ( bWasDeviceConnected == false )
	{
		if ( SelectedTargetDevice != nullptr )
		{
			if ( ITargetDevicePtr DevicePtr = SelectedTargetDevice->DeviceWeakPtr.Pin() )
			{
				if ( DevicePtr->IsConnected() )
				{
					bWasDeviceConnected = true;
					ExecHUDConsoleCommand();
				}
			}
		}
	}
}

void SNiagaraDebugger::AddReferencedObjects(FReferenceCollector& Collector)
{
	//if (DebugHUDSettings != nullptr)
	//{
	//	Collector.AddReferencedObject(DebugHUDSettings);
	//}
}

TSharedRef<SWidget> SNiagaraDebugger::MakeDeviceComboButtonMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	// Entry for local application
	{
		TSharedRef<SWidget> MenuEntryWidget =
			SNew(SBox)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(24)
					.HeightOverride(24)
					[
						SNew(SImage).Image(GetTargetDeviceBrush(nullptr))
					]
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.0f, 0.0f))
				[
					SNew(STextBlock).Text(NiagaraDebuggerLocal::GetLocalDeviceText())
				]
			];

		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateSP(this, &SNiagaraDebugger::SelectDevice, FTargetDeviceEntryPtr())),
			MenuEntryWidget
		);
	}

	// Create entry per device
	for (const auto& DeviceEntry : TargetDeviceList)
	{
		TSharedRef<SWidget> MenuEntryWidget =
			SNew(SBox)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(24)
					.HeightOverride(24)
					[
						SNew(SImage).Image(GetTargetDeviceBrush(DeviceEntry))
					]
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.0f, 0.0f))
				[
					SNew(STextBlock).Text(this, &SNiagaraDebugger::GetTargetDeviceText, DeviceEntry)
				]
			];

		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateSP(this, &SNiagaraDebugger::SelectDevice, DeviceEntry)),
			MenuEntryWidget
		);
	}
	return MenuBuilder.MakeWidget();
}

void SNiagaraDebugger::InitDeviceList()
{
	if (ITargetPlatformManagerModule* TPM = FModuleManager::GetModulePtr<ITargetPlatformManagerModule>("TargetPlatform"))
	{
		for (ITargetPlatform* TargetPlatform : TPM->GetTargetPlatforms())
		{
			if (NiagaraDebuggerLocal::IsSupportedPlatform(TargetPlatform))
			{
				TargetPlatform->OnDeviceDiscovered().AddRaw(this, &SNiagaraDebugger::AddTargetDevice);
				TargetPlatform->OnDeviceLost().AddRaw(this, &SNiagaraDebugger::RemoveTargetDevice);

				TArray<ITargetDevicePtr> TargetDevices;
				TargetPlatform->GetAllDevices(TargetDevices);
				
				for (const ITargetDevicePtr& TargetDevice : TargetDevices)
				{
					if (TargetDevice.IsValid())
					{
						AddTargetDevice(TargetDevice.ToSharedRef());
					}
				}
			}
		}
	}
}

void SNiagaraDebugger::DestroyDeviceList()
{
	if ( ITargetPlatformManagerModule* TPM = FModuleManager::GetModulePtr<ITargetPlatformManagerModule>("TargetPlatform") )
	{
		for (ITargetPlatform* TargetPlatform : TPM->GetTargetPlatforms())
		{
			TargetPlatform->OnDeviceDiscovered().RemoveAll(this);
			TargetPlatform->OnDeviceLost().RemoveAll(this);
		}
	}
}

void SNiagaraDebugger::AddTargetDevice(ITargetDeviceRef TargetDevice)
{
	// Check it doesn't already exist
	for ( const auto& DevicePtr : TargetDeviceList )
	{
		if ( DevicePtr->DeviceId == TargetDevice->GetId() )
		{
			return;
		}
	}

	// Add device
	FName DeviceIconStyleName = TargetDevice->GetTargetPlatform().GetPlatformInfo().GetIconStyleName(PlatformInfo::EPlatformIconSize::Normal);

	FTargetDeviceEntryPtr NewDevice = MakeShareable(new FTargetDeviceEntry);
	NewDevice->DeviceId = TargetDevice->GetId();
	NewDevice->DeviceWeakPtr = TargetDevice;
	NewDevice->DeviceIconBrush = FEditorStyle::GetBrush(DeviceIconStyleName);

	TargetDeviceList.Add(NewDevice);
}

void SNiagaraDebugger::RemoveTargetDevice(ITargetDeviceRef TargetDevice)
{
	if (SelectedTargetDevice && (SelectedTargetDevice->DeviceId == TargetDevice->GetId()))
	{
		SelectDevice(nullptr);
	}

	TargetDeviceList.RemoveAllSwap(
		[&](const FTargetDeviceEntryPtr& DevicePtr)
		{
			return DevicePtr->DeviceId == TargetDevice->GetId();
		}
	);
}

void SNiagaraDebugger::SelectDevice(FTargetDeviceEntryPtr DeviceEntry)
{
	SelectedTargetDevice = DeviceEntry;
	bWasDeviceConnected = true;
	if ( SelectedTargetDevice != nullptr )
	{
		if ( ITargetDevicePtr DevicePtr = SelectedTargetDevice->DeviceWeakPtr.Pin() )
		{
			bWasDeviceConnected = DevicePtr->IsConnected();
		}
	}

	ExecHUDConsoleCommand();
}

const FSlateBrush* SNiagaraDebugger::GetTargetDeviceBrush(FTargetDeviceEntryPtr DeviceEntry) const
{
	if (DeviceEntry.IsValid())
	{
		return DeviceEntry->DeviceIconBrush;
	}
	else
	{
		return FEditorStyle::GetBrush("NoBorder");
	}
}

FText SNiagaraDebugger::GetTargetDeviceText(FTargetDeviceEntryPtr DeviceEntry) const
{
	if (DeviceEntry.IsValid())
	{
		ITargetDevicePtr PinnedPtr = DeviceEntry->DeviceWeakPtr.Pin();
		if (PinnedPtr.IsValid() && PinnedPtr->IsConnected())
		{
			FString DeviceName = PinnedPtr->GetName();
			return FText::FromString(DeviceName);
		}
		else
		{
			FString DeviceName = DeviceEntry->DeviceId.GetDeviceName();
			return FText::Format(LOCTEXT("TargetDeviceOffline", "{0} (Offline)"), FText::FromString(DeviceName));
		}
	}
	else
	{
		return NiagaraDebuggerLocal::GetLocalDeviceText();
	}
}

const FSlateBrush* SNiagaraDebugger::GetSelectedTargetDeviceBrush() const
{
	return GetTargetDeviceBrush(SelectedTargetDevice);
}

FText SNiagaraDebugger::GetSelectedTargetDeviceText() const
{
	return GetTargetDeviceText(SelectedTargetDevice);
}

void SNiagaraDebugger::ExecConsoleCommand(const TCHAR* Cmd, bool bRequiresWorld)
{
	if (SelectedTargetDevice)
	{
		ITargetDevicePtr DevicePtr = SelectedTargetDevice->DeviceWeakPtr.Pin();
		if (DevicePtr && DevicePtr->IsConnected())
		{
			DevicePtr->ExecuteConsoleCommand(Cmd);
		}
	}
	else
	{
		if (bRequiresWorld)
		{
			for (TObjectIterator<UWorld> WorldIt; WorldIt; ++WorldIt)
			{
				UWorld* World = *WorldIt;
				if ( (World != nullptr) &&
					 (World->WorldType == EWorldType::PIE) &&
					 (World->PersistentLevel != nullptr) &&
					 (World->PersistentLevel->OwningWorld == World) &&
					 ((World->GetNetMode() == ENetMode::NM_Client) || (World->GetNetMode() == ENetMode::NM_Standalone)) )
				{
					GEngine->Exec(*WorldIt, Cmd);
				}
			}
		}
		else
		{
			GEngine->Exec(nullptr, Cmd);
		}
	}
}

void SNiagaraDebugger::ExecHUDConsoleCommand()
{
	const auto BuildVariableString =
		[](const TArray<FNiagaraDebugHUDVariable>& Variables) -> FString
		{
			FString Output;
			for (const FNiagaraDebugHUDVariable& Variable : Variables)
			{
				if (Variable.bEnabled && Variable.Name.Len() > 0)
				{
					if (Output.Len() > 0)
					{
						Output.Append(TEXT(","));
					}
					Output.Append(Variable.Name);
				}
			}
			return Output;
		};

	// Some platforms have limits on the size of the remote command, so split into a series of several commands to send
	if ( const UNiagaraDebugHUDSettings* Settings = GetDefault<UNiagaraDebugHUDSettings>() )
	{
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud Enabled=%d"), Settings->bEnabled ? 1 : 0), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud GpuReadback=%d"), Settings->bEnableGpuReadback ? 1 : 0), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud DisplayLocation=%d,%d"), Settings->HUDLocation.X, Settings->HUDLocation.Y), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemVerbosity=%d"), (int32)Settings->SystemVerbosity), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemShowBounds=%d "), Settings->bSystemShowBounds ? 1 : 0), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemShowActiveOnlyInWorld=%d"), Settings->bSystemShowActiveOnlyInWorld ? 1 : 0), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemFilter=%s"), *Settings->SystemFilter), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud ComponentFilter=%s"), *Settings->ComponentFilter), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud SystemVariables=%s"), Settings->bShowSystemVariables ? *BuildVariableString(Settings->SystemVariables) : TEXT("")), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud ParticleVariables=%s"), Settings->bShowParticleVariables ? *BuildVariableString(Settings->ParticlesVariables) : TEXT("")), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud MaxParticlesToDisplay=%d"), Settings->MaxParticlesToDisplay), false);
		ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.Hud ShowParticlesInWorld=%d"), Settings->bShowParticlesInWorld ? 1 : 0), false);
	}
}

float SNiagaraDebugger::GetPlaybackRate() const
{
	return PlaybackRate;
}

void SNiagaraDebugger::SetPlaybackRate(float Rate)
{
	PlaybackRate = Rate;
	ExecConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.PlaybackRate %.3f"), PlaybackRate), true);
}

#undef LOCTEXT_NAMESPACE
