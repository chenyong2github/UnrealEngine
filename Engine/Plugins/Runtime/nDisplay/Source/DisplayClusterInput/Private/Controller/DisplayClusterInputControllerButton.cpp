// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterInputControllerButton.h"
#include "IDisplayClusterInputModule.h"
#include "Misc/DisplayClusterInputLog.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Input/IDisplayClusterInputManager.h"

#include "IDisplayClusterInputModule.h"

#include "DisplayClusterConfigurationTypes.h"

#define LOCTEXT_NAMESPACE "DisplayClusterInput"


// Add vrpn buttons to UE4 global name-space
void FButtonController::Initialize()
{
	static const FName nDisplayClusterInputCategoryName(TEXT("nDisplayButtons"));
	EKeys::AddMenuCategoryDisplayInfo(nDisplayClusterInputCategoryName, LOCTEXT("nDisplayInputSubCateogry", "nDisplay"), TEXT("GraphEditor.KeyEvent_16x"));

	// Register all names in UE4 namespace by macros. Purpose: easy add new channels and reduce code size
	for (int32 idx = 0; idx < FButtonKey::TotalCount; ++idx)
	{
		FText ButtonLocaleText = FText::Format(LOCTEXT("nDisplayButtonHintFmt", "nDisplay Button {0}"), idx);
		UE_LOG(LogDisplayClusterInputButton, Verbose, TEXT("Registering %s%d..."), *nDisplayClusterInputCategoryName.ToString(), idx);
		EKeys::AddKey(FKeyDetails(*FButtonKey::ButtonKeys[idx], ButtonLocaleText, FKeyDetails::GamepadKey, nDisplayClusterInputCategoryName));
	}
}

void FButtonController::ProcessStartSession()
{
	ResetAllBindings();

	IDisplayClusterInputManager&  InputMgr  = *IDisplayCluster::Get().GetInputMgr();
	IDisplayClusterConfigManager& ConfigMgr = *IDisplayCluster::Get().GetConfigMgr();

	const UDisplayClusterConfigurationData* ConfigData = ConfigMgr.GetConfig();
	if (!ConfigData)
	{
		return;
	}

	TArray<FString> DeviceNames;
	InputMgr.GetButtonDeviceIds(DeviceNames);
	for (const FString& DeviceName : DeviceNames)
	{
		AddDevice(DeviceName);

		for (auto& it : ConfigData->Input->InputBinding)
		{
			if (DeviceName.Equals(it.DeviceId, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogDisplayClusterInputButton, Verbose, TEXT("Binding %s:%d to %s..."), *DeviceName, it.Channel, *it.BindTo);
				BindChannel(DeviceName, it.Channel, it.BindTo);
			}
		}
	}
}

void FButtonController::ProcessEndSession()
{
	UE_LOG(LogDisplayClusterInputButton, Verbose, TEXT("Removing all button bindings..."));

	ResetAllBindings();
}

void FButtonController::ProcessPreTick()
{
	IDisplayClusterInputManager& InputMgr = *IDisplayCluster::Get().GetInputMgr();

	// Get data from VRPN devices
	for (auto& DeviceIt : BindMap)
	{
		// Update all binded vrpn channels:
		for (auto& ChannelIt : DeviceIt.Value)
		{
			bool BtnState;
			if (InputMgr.GetButtonState(DeviceIt.Key, ChannelIt.Key, BtnState))
			{
				UE_LOG(LogDisplayClusterInputButton, Verbose, TEXT("Obtained button data %s:%d => %d"), *DeviceIt.Key, ChannelIt.Key, BtnState ? 1 : 0);
				ChannelIt.Value.SetData(BtnState);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
