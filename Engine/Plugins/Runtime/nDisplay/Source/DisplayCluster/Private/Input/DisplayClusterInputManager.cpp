// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/DisplayClusterInputManager.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Config/IPDisplayClusterConfigManager.h"

#include "Devices/VRPN/Analog/DisplayClusterVrpnAnalogInputDevice.h"
#include "Devices/VRPN/Button/DisplayClusterVrpnButtonInputDevice.h"
#include "Devices/VRPN/Tracker/DisplayClusterVrpnTrackerInputDevice.h"
#include "Devices/VRPN/Keyboard/DisplayClusterVrpnKeyboardInputDevice.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


FDisplayClusterInputManager::FDisplayClusterInputManager()
{
}

FDisplayClusterInputManager::~FDisplayClusterInputManager()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterInputManager::Init(EDisplayClusterOperationMode OperationMode)
{
	return true;
}

void FDisplayClusterInputManager::Release()
{
}

bool FDisplayClusterInputManager::StartSession(const FString& InConfigPath, const FString& InNodeId)
{
	ConfigPath = InConfigPath;
	ClusterNodeId = InNodeId;

	if (!InitDevices())
	{
		UE_LOG(LogDisplayClusterInput, Error, TEXT("Couldn't initialize input devices"));
		return false;
	}

	return true;
}

void FDisplayClusterInputManager::EndSession()
{
	ConfigPath.Reset();
	ClusterNodeId.Reset();

	ReleaseDevices();
}

bool FDisplayClusterInputManager::StartScene(UWorld* InWorld)
{
	check(InWorld);
	CurrentWorld = InWorld;

	return true;
}

void FDisplayClusterInputManager::EndScene()
{

	CurrentWorld = nullptr;
}

void FDisplayClusterInputManager::StartFrame(uint64 FrameNum)
{
	// Update input state
	Update();
}

void FDisplayClusterInputManager::PreTick(float DeltaSeconds)
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterInputManager
//////////////////////////////////////////////////////////////////////////////////////////////
// Device API
const IDisplayClusterInputDevice* FDisplayClusterInputManager::GetDevice(EDisplayClusterInputDeviceType DeviceType, const FString& DeviceID) const
{
	if (Devices.Contains(DeviceType))
	{
		if (Devices[DeviceType].Contains(DeviceID))
		{
			return Devices[DeviceType][DeviceID].Get();
		}
	}

	return nullptr;
}

// Basic functionality (device amount)
uint32 FDisplayClusterInputManager::GetAxisDeviceAmount() const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetDeviceAmount_impl<EDisplayClusterInputDeviceType::VrpnAnalog>();
}

uint32 FDisplayClusterInputManager::GetButtonDeviceAmount() const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetDeviceAmount_impl<EDisplayClusterInputDeviceType::VrpnButton>();
}

uint32 FDisplayClusterInputManager::GetKeyboardDeviceAmount() const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetDeviceAmount_impl<EDisplayClusterInputDeviceType::VrpnKeyboard>();
}


uint32 FDisplayClusterInputManager::GetTrackerDeviceAmount() const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetDeviceAmount_impl<EDisplayClusterInputDeviceType::VrpnTracker>();
}


// Access to the device lists
void FDisplayClusterInputManager::GetAxisDeviceIds(TArray<FString>& DeviceIDs) const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetDeviceIds_impl<EDisplayClusterInputDeviceType::VrpnAnalog>(DeviceIDs);
}

void FDisplayClusterInputManager::GetButtonDeviceIds(TArray<FString>& DeviceIDs) const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetDeviceIds_impl<EDisplayClusterInputDeviceType::VrpnButton>(DeviceIDs);
}

void FDisplayClusterInputManager::GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetDeviceIds_impl<EDisplayClusterInputDeviceType::VrpnKeyboard>(DeviceIDs);
}

void FDisplayClusterInputManager::GetTrackerDeviceIds(TArray<FString>& DeviceIDs) const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetDeviceIds_impl<EDisplayClusterInputDeviceType::VrpnTracker>(DeviceIDs);
}


// Button data access
bool FDisplayClusterInputManager::GetButtonState(const FString& DeviceID, const uint8 Channel, bool& CurrentState) const
{
	FDisplayClusterVrpnButtonChannelData Data;
	if (GetButtonData(DeviceID, Channel, Data))
	{
		CurrentState = Data.BtnStateNew;
		return true;
	}

	return false;
}

bool FDisplayClusterInputManager::IsButtonPressed(const FString& DeviceID, const uint8 Channel, bool& IsPressedCurrently) const
{
	bool BtnState;
	if (GetButtonState(DeviceID, Channel, BtnState))
	{
		IsPressedCurrently = (BtnState == true);
		return true;
	}

	return false;
}

bool FDisplayClusterInputManager::IsButtonReleased(const FString& DeviceID, const uint8 Channel, bool& IsReleasedCurrently) const
{
	bool BtnState;
	if (GetButtonState(DeviceID, Channel, BtnState))
	{
		IsReleasedCurrently = (BtnState == false);
		return true;
	}

	return false;
}

bool FDisplayClusterInputManager::WasButtonPressed(const FString& DeviceID, const uint8 Channel, bool& WasPressed) const
{
	FDisplayClusterVrpnButtonChannelData Data;
	if (GetButtonData(DeviceID, Channel, Data))
	{
		WasPressed = (Data.BtnStateOld == false && Data.BtnStateNew == true);
		return true;
	}

	return false;
}

bool FDisplayClusterInputManager::WasButtonReleased(const FString& DeviceID, const uint8 Channel, bool& WasReleased) const
{
	FDisplayClusterVrpnButtonChannelData Data;
	if (GetButtonData(DeviceID, Channel, Data))
	{
		WasReleased = (Data.BtnStateOld == true && Data.BtnStateNew == false);
		return true;
	}

	return false;
}

// Keyboard data access
bool FDisplayClusterInputManager::GetKeyboardState(const FString& DeviceID, const uint8 Channel, bool& CurrentState) const
{
	FDisplayClusterVrpnKeyboardChannelData Data;
	if (GetKeyboardData(DeviceID, Channel, Data))
	{
		CurrentState = Data.BtnStateNew;
		return true;
	}

	return false;
}

bool FDisplayClusterInputManager::IsKeyboardPressed(const FString& DeviceID, const uint8 Channel, bool& IsPressedCurrently) const
{
	bool BtnState;
	if (GetKeyboardState(DeviceID, Channel, BtnState))
	{
		IsPressedCurrently = (BtnState == true);
		return true;
	}

	return false;
}

bool FDisplayClusterInputManager::IsKeyboardReleased(const FString& DeviceID, const uint8 Channel, bool& IsReleasedCurrently) const
{
	bool BtnState;
	if (GetKeyboardState(DeviceID, Channel, BtnState))
	{
		IsReleasedCurrently = (BtnState == false);
		return true;
	}

	return false;
}

bool FDisplayClusterInputManager::WasKeyboardPressed(const FString& DeviceID, const uint8 Channel, bool& WasPressed) const
{
	FDisplayClusterVrpnKeyboardChannelData Data;
	if (GetKeyboardData(DeviceID, Channel, Data))
	{
		WasPressed = (Data.BtnStateOld == false && Data.BtnStateNew == true);
		return true;
	}

	return false;
}

bool FDisplayClusterInputManager::WasKeyboardReleased(const FString& DeviceID, const uint8 Channel, bool& WasReleased) const
{
	FDisplayClusterVrpnKeyboardChannelData Data;
	if (GetKeyboardData(DeviceID, Channel, Data))
	{
		WasReleased = (Data.BtnStateOld == true && Data.BtnStateNew == false);
		return true;
	}

	return false;
}

// Axes data access
bool FDisplayClusterInputManager::GetAxis(const FString& DeviceID, const uint8 Channel, float& Value) const
{
	FDisplayClusterVrpnAnalogChannelData Data;
	if (GetAxisData(DeviceID, Channel, Data))
	{
		Value = Data.AxisValue;
		return true;
	}

	return false;
}

// Tracking data access
bool FDisplayClusterInputManager::GetTrackerLocation(const FString& DeviceID, const uint8 Channel, FVector& Location) const
{
	FDisplayClusterVrpnTrackerChannelData Data;
	if (GetTrackerData(DeviceID, Channel, Data))
	{
		Location = Data.TrackerLoc;
		return true;
	}

	return false;
}

bool FDisplayClusterInputManager::GetTrackerQuat(const FString& DeviceID, const uint8 Channel, FQuat& Rotation) const
{
	FDisplayClusterVrpnTrackerChannelData Data;
	if (GetTrackerData(DeviceID, Channel, Data))
	{
		Rotation = Data.TrackerQuat;
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterInputManager
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterInputManager::Update()
{
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return;
	}

	// Perform input update on master only. Slaves' state will be replicated later.
	if (GDisplayCluster->GetPrivateClusterMgr()->IsMaster())
	{
		UE_LOG(LogDisplayClusterInput, Verbose, TEXT("Input update started"));
		{
			FScopeLock ScopeLock(&InternalsSyncScope);

			// Pre-Update
			UE_LOG(LogDisplayClusterInput, Verbose, TEXT("Input pre-update..."));
			for (auto classIt = Devices.CreateIterator(); classIt; ++classIt)
			{
				for (auto devIt = classIt->Value.CreateConstIterator(); devIt; ++devIt)
				{
					devIt->Value->PreUpdate();
				}
			}

			// Update
			UE_LOG(LogDisplayClusterInput, Verbose, TEXT("Input update..."));
			for (auto classIt = Devices.CreateIterator(); classIt; ++classIt)
			{
				for (auto devIt = classIt->Value.CreateConstIterator(); devIt; ++devIt)
				{
					devIt->Value->Update();
				}
			}

			// Post-Update
			for (auto classIt = Devices.CreateIterator(); classIt; ++classIt)
			{
				for (auto devIt = classIt->Value.CreateConstIterator(); devIt; ++devIt)
				{
					devIt->Value->PostUpdate();
				}
			}
		}

		UE_LOG(LogDisplayClusterInput, Verbose, TEXT("Input update finished"));
	}
}

void FDisplayClusterInputManager::ExportInputData(FDisplayClusterMessage::DataType& InputData) const
{
	FScopeLock ScopeLock(&InternalsSyncScope);

	// Clear container before put some data
	InputData.Empty(InputData.Num() | 0x07);

	for (auto DevClassIt = Devices.CreateConstIterator(); DevClassIt; ++DevClassIt)
	{
		for (auto DevIt = DevClassIt->Value.CreateConstIterator(); DevIt; ++DevIt)
		{
			const FString key = FString::Printf(TEXT("%d%s%s"), DevClassIt->Key, SerializationDeviceTypeNameDelimiter, *DevIt->Key);
			const FString val = DevIt->Value->SerializeToString();
			UE_LOG(LogDisplayClusterInput, VeryVerbose, TEXT("Input device %d:%s serialized: <%s, %s>"), DevClassIt->Key, *DevIt->Key, *key, *val);
			InputData.Add(key, val);
		}
	}
}

void FDisplayClusterInputManager::ImportInputData(const FDisplayClusterMessage::DataType& InputData)
{
	FScopeLock ScopeLock(&InternalsSyncScope);

	for (const auto& DataItem : InputData)
	{
		FString DevClassId;
		FString DevId;
		if (DataItem.Key.Split(FString(SerializationDeviceTypeNameDelimiter), &DevClassId, &DevId))
		{
			UE_LOG(LogDisplayClusterInput, VeryVerbose, TEXT("Deserializing input device: <%s, %s>"), *DataItem.Key, *DataItem.Value);

			int classId = FCString::Atoi(*DevClassId);
			if (Devices.Contains(classId))
			{
				if (Devices[classId].Contains(DevId))
				{
					Devices[classId][DevId]->DeserializeFromString(DataItem.Value);
				}
			}
		}
	}
}

bool FDisplayClusterInputManager::GetAxisData(const FString& DeviceID, const uint8 Channel, FDisplayClusterVrpnAnalogChannelData& Data) const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetChannelData_impl<EDisplayClusterInputDeviceType::VrpnAnalog>(DeviceID, Channel, Data);
}

bool FDisplayClusterInputManager::GetButtonData(const FString& DeviceID, const uint8 Channel, FDisplayClusterVrpnButtonChannelData& Data) const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetChannelData_impl<EDisplayClusterInputDeviceType::VrpnButton>(DeviceID, Channel, Data);
}

bool FDisplayClusterInputManager::GetKeyboardData(const FString& DeviceID, const uint8 Channel, FDisplayClusterVrpnKeyboardChannelData& Data) const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetChannelData_impl<EDisplayClusterInputDeviceType::VrpnKeyboard>(DeviceID, Channel, Data);
}

bool FDisplayClusterInputManager::GetTrackerData(const FString& DeviceID, const uint8 Channel, FDisplayClusterVrpnTrackerChannelData& Data) const
{
	FScopeLock ScopeLock(&InternalsSyncScope);
	return GetChannelData_impl<EDisplayClusterInputDeviceType::VrpnTracker>(DeviceID, Channel, Data);
}

template<int DevTypeID>
uint32 FDisplayClusterInputManager::GetDeviceAmount_impl() const
{
	if (!Devices.Contains(DevTypeID))
	{
		return 0;
	}

	return static_cast<uint32>(Devices[DevTypeID].Num());
}

template<int DevTypeID>
void FDisplayClusterInputManager::GetDeviceIds_impl(TArray<FString>& IDs) const
{
	if (!Devices.Contains(DevTypeID))
	{
		return;
	}

	Devices[DevTypeID].GenerateKeyArray(IDs);
	return;
}

template<int DevTypeID>
bool FDisplayClusterInputManager::GetChannelData_impl(const FString& DeviceID, const uint8 Channel, typename display_cluster_input_device_traits<DevTypeID>::dev_channel_data_type& Data) const
{
	if (!Devices.Contains(DevTypeID))
	{
		return false;
	}

	if (!Devices[DevTypeID].Contains(DeviceID))
	{
		return false;
	}

	return static_cast<FDisplayClusterInputDeviceBase<DevTypeID>*>(Devices[DevTypeID][DeviceID].Get())->GetChannelData(Channel, Data);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterInputManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterInputManager::InitDevices()
{
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return false;
	}

	FScopeLock ScopeLock(&InternalsSyncScope);

	UE_LOG(LogDisplayClusterInput, Log, TEXT("Initializing input devices..."));

	const TArray<FDisplayClusterConfigInput> cfgInputDevs = GDisplayCluster->GetPrivateConfigMgr()->GetInputDevices();

	for (auto& cfgDev : cfgInputDevs)
	{
		UE_LOG(LogDisplayClusterInput, Verbose, TEXT("Creating input device: %s"), *cfgDev.ToString());

		IDisplayClusterInputDevice* pDev = nullptr;

		if (cfgDev.Type.Compare(FString(DisplayClusterStrings::cfg::data::input::DeviceAnalog), ESearchCase::IgnoreCase) == 0)
		{
			if (GDisplayCluster->GetPrivateClusterMgr()->IsMaster())
			{
				pDev = new FDisplayClusterVrpnAnalogInputDevice(cfgDev);
			}
			else
			{
				pDev = new FDisplayClusterVrpnAnalogInputDataHolder(cfgDev);
			}
		}
		else if (cfgDev.Type.Compare(FString(DisplayClusterStrings::cfg::data::input::DeviceButtons), ESearchCase::IgnoreCase) == 0)
		{
			if (GDisplayCluster->GetPrivateClusterMgr()->IsMaster())
			{
				pDev = new FDisplayClusterVrpnButtonInputDevice(cfgDev);
			}
			else
			{
				pDev = new FDisplayClusterVrpnButtonInputDataHolder(cfgDev);
			}
		}
		else if (cfgDev.Type.Compare(FString(DisplayClusterStrings::cfg::data::input::DeviceTracker), ESearchCase::IgnoreCase) == 0)
		{
			if (GDisplayCluster->GetPrivateClusterMgr()->IsMaster())
			{
				pDev = new FDisplayClusterVrpnTrackerInputDevice(cfgDev);
			}
			else
			{
				pDev = new FDisplayClusterVrpnTrackerInputDataHolder(cfgDev);
			}
		}
		else if (cfgDev.Type.Compare(FString(DisplayClusterStrings::cfg::data::input::DeviceKeyboard), ESearchCase::IgnoreCase) == 0)
		{
			if (GDisplayCluster->GetPrivateClusterMgr()->IsMaster())
			{
				pDev = new FDisplayClusterVrpnKeyboardInputDevice(cfgDev);
			}
			else
			{
				pDev = new FDisplayClusterVrpnKeyboardInputDataHolder(cfgDev);
			}
		}
		else
		{
			UE_LOG(LogDisplayClusterInput, Error, TEXT("Unsupported device type: %s"), *cfgDev.Type);
			continue;
		}

		if (pDev && pDev->Initialize())
		{
			UE_LOG(LogDisplayClusterInput, Log, TEXT("Adding device: %s"), *pDev->ToString());
			
			auto pDevMap = Devices.Find(pDev->GetTypeId());
			if (!pDevMap)
			{
				pDevMap = &Devices.Add(pDev->GetTypeId());
			}

			pDevMap->Add(cfgDev.Id, TDevice(pDev));
		}
		else
		{
			UE_LOG(LogDisplayClusterInput, Warning, TEXT("Neither data holder nor true device was instantiated for item id: %s"), *cfgDev.Id);

			// It's safe to delete nullptr so no checking performed
			delete pDev;

			//@note: Allow other devices to be initialized. User will locate the problem from logs.
			//return false;
		}
	}

	return true;
}

void FDisplayClusterInputManager::ReleaseDevices()
{
	FScopeLock ScopeLock(&InternalsSyncScope);

	UE_LOG(LogDisplayClusterInput, Log, TEXT("Releasing input subsystem..."));

	UE_LOG(LogDisplayClusterInput, Log, TEXT("Releasing input devices..."));
	Devices.Empty();
}
