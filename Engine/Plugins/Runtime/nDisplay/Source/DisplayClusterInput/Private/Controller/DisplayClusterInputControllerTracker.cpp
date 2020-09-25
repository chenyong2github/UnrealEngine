// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterInputControllerTracker.h"
#include "IDisplayClusterInputModule.h"
#include "Misc/DisplayClusterInputLog.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Input/IDisplayClusterInputManager.h"

#include "IDisplayClusterInputModule.h"

#include "DisplayClusterConfigurationTypes.h"


void FTrackerController::Initialize()
{
	//todo: Register new trackers for nDisplayCluster
}

void FTrackerController::ProcessStartSession()
{
	// Clear old binds
	ResetAllBindings();

	IDisplayClusterInputManager&  InputMgr  = *IDisplayCluster::Get().GetInputMgr();
	IDisplayClusterConfigManager& ConfigMgr = *IDisplayCluster::Get().GetConfigMgr();

	const UDisplayClusterConfigurationData* ConfigData = ConfigMgr.GetConfig();
	if (!ConfigData)
	{
		return;
	}

	TArray<FString> DeviceNames;
	InputMgr.GetTrackerDeviceIds(DeviceNames);
	for (const FString& DeviceName : DeviceNames)
	{
		AddDevice(DeviceName);

		for (auto& it : ConfigData->Input->InputBinding)
		{
			if (DeviceName.Equals(it.DeviceId, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogDisplayClusterInputTracker, Verbose, TEXT("Binding %s:%d to %s..."), *DeviceName, it.Channel, *it.BindTo);
				BindTracker(DeviceName, it.Channel, it.BindTo);
			}
		}
	}
}

void FTrackerController::ProcessEndSession()
{
	UE_LOG(LogDisplayClusterInputTracker, Verbose, TEXT("Removing all tracker bindings..."));

	ResetAllBindings();
}

void FTrackerController::ProcessPreTick()
{
	// Get data from VRPN devices
	IDisplayClusterInputManager& InputMgr = *IDisplayCluster::Get().GetInputMgr();
	for (auto& DeviceIt : BindMap)
	{
		for (auto& ChannelIt : DeviceIt.Value)
		{
			FQuat NewQuat;
			FVector NewPosition;
			if (InputMgr.GetTrackerLocation(DeviceIt.Key, ChannelIt.Key, NewPosition) &&
				InputMgr.GetTrackerQuat(DeviceIt.Key, ChannelIt.Key, NewQuat))
			{
				UE_LOG(LogDisplayClusterInputTracker, Verbose, TEXT("Obtained tracker data %s:%d => %s / %s"), *DeviceIt.Key, ChannelIt.Key, *NewPosition.ToString(), *NewQuat.ToString());
				ChannelIt.Value.SetData(NewQuat.Rotator(), NewPosition);
			}
		}
	}
}

bool FTrackerController::BindTracker(const FString& DeviceID, uint32 VrpnChannel, const FString& TargetName)
{
	// Find target TargetName analog value from user-friendly TargetName:
	EControllerHand TargetHand;
	if (!FControllerDeviceHelper::FindTrackerByName(TargetName, TargetHand))
	{
		// Bad target name, handle error details:
		UE_LOG(LogDisplayClusterInputTracker, Error, TEXT("Unknown bind tracker name <%s> for device <%s> channel <%i>"), *TargetName, *DeviceID, VrpnChannel);
		return false;
	}

	// Add new bind for tracker:
	return BindTracker(DeviceID, VrpnChannel, TargetHand);
}
bool FTrackerController::BindTracker(const FString& DeviceID, uint32 VrpnChannel, const EControllerHand TargetHand)
{
	// Create new bind:
	dev_channel_data_type& BindData = AddDeviceChannelBind(DeviceID, VrpnChannel);
	return BindData.BindTarget(TargetHand);
}

const FTrackerState* FTrackerController::GetDeviceBindData(const EControllerHand DeviceHand) const
{
	for (const auto& DeviceIt : BindMap)
	{
		const FChannelBinds& ChannelBinds = DeviceIt.Value;
		for (const auto& ChannelIt : ChannelBinds)
		{
			const FTrackerState& ChannelBindData = ChannelIt.Value;
			if (ChannelBindData.FindTracker(DeviceHand) != INDEX_NONE)
			{
				//Found, return tracker data
				return &ChannelBindData;
			}
		}
	}

	// Not found any bind for this tracker type
	return nullptr; 
}

bool FTrackerController::IsTrackerConnected(const EControllerHand DeviceHand) const
{
	return GetDeviceBindData(DeviceHand) != nullptr;
}

void FTrackerController::ApplyTrackersChanges()
{
	for (auto& DeviceIt : BindMap)
	{
		for (auto& ChannelIt : DeviceIt.Value)
		{
			ChannelIt.Value.ApplyChanges();
		}
	}
}

int FTrackerController::GetTrackersCount() const
{
	int Result = 0;
	for (auto& DeviceIt : BindMap)
	{
		for (auto& ChannelIt : DeviceIt.Value)
		{
			Result += ChannelIt.Value.GetTrackersNum();
		}
	}

	return Result;
}

bool FTrackerController::GetTrackerOrientationAndPosition(const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition) const
{
	const FTrackerState* TrackerData = GetDeviceBindData(DeviceHand);
	if (TrackerData!=nullptr)
	{
		TrackerData->GetCurrentData(OutOrientation, OutPosition);
		return true;
	}

	return false;
}
