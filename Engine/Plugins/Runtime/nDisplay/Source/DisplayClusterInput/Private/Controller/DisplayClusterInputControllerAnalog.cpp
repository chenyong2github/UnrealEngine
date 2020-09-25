// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterInputControllerAnalog.h"
#include "IDisplayClusterInputModule.h"
#include "Misc/DisplayClusterInputLog.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Input/IDisplayClusterInputManager.h"

#include "DisplayClusterConfigurationTypes.h"

#define LOCTEXT_NAMESPACE "DisplayClusterInput"


// Add vrpn analog to UE4 global name-space
void FAnalogController::Initialize()
{
	static const FName nDisplayClusterInputCategoryName(TEXT("nDisplayAnalogs"));
	EKeys::AddMenuCategoryDisplayInfo(nDisplayClusterInputCategoryName, LOCTEXT("nDisplayInputSubCateogry", "nDisplay"), TEXT("GraphEditor.KeyEvent_16x"));

	uint8 Flags = FKeyDetails::GamepadKey | FKeyDetails::Axis1D;

	for (int32 idx = 0; idx < FAnalogKey::TotalCount; ++idx)
	{
		FText AnalogLocaleText = FText::Format(LOCTEXT("nDisplayAnalogHintFmt", "nDisplay Analog {0}"), idx);
		UE_LOG(LogDisplayClusterInputAnalog, Verbose, TEXT("Registering %s%d..."), *nDisplayClusterInputCategoryName.ToString(), idx);
		EKeys::AddKey(FKeyDetails(*FAnalogKey::AnalogKeys[idx], AnalogLocaleText, Flags, nDisplayClusterInputCategoryName));
	}

	UE_LOG(LogDisplayClusterInputAnalog, Log, TEXT("nDisplay input controller has been initialized <Analog>"));
}

void FAnalogController::ProcessStartSession()
{
	ResetAllBindings();

	const UDisplayClusterConfigurationData* ConfigData = IDisplayCluster::Get().GetConfigMgr()->GetConfig();
	if (!ConfigData)
	{
		return;
	}

	TArray<FString> DeviceIds;
	IDisplayCluster::Get().GetInputMgr()->GetAxisDeviceIds(DeviceIds);
	for (const FString& DeviceId : DeviceIds)
	{
		AddDevice(DeviceId);

		for (auto& it : ConfigData->Input->InputBinding)
		{
			if (DeviceId.Equals(it.DeviceId, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogDisplayClusterInputAnalog, Verbose, TEXT("Binding %s:%d to %s..."), *DeviceId, it.Channel, *it.BindTo);
				BindChannel(DeviceId, it.Channel, it.BindTo);
			}
		}
	}
}

void FAnalogController::ProcessEndSession()
{
	UE_LOG(LogDisplayClusterInputAnalog, Verbose, TEXT("Removing all analog bindings..."));

	ResetAllBindings();
}

void FAnalogController::ProcessPreTick()
{
	IDisplayClusterInputManager& InputMgr = *IDisplayCluster::Get().GetInputMgr();
	for (auto& DeviceIt : BindMap)
	{
		for (auto& ChannelIt : DeviceIt.Value)
		{
			float AxisValue;
			if (InputMgr.GetAxis(DeviceIt.Key, ChannelIt.Key, AxisValue))
			{
				UE_LOG(LogDisplayClusterInputAnalog, Verbose, TEXT("Obtained analog data %s:%d => %f"), *DeviceIt.Key, ChannelIt.Key, AxisValue);
				ChannelIt.Value.SetData(AxisValue);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
