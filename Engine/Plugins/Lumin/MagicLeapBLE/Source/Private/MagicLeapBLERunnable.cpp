// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_MLSDK
#include "MagicLeapBLERunnable.h"
#include "MagicLeapBLEPlugin.h"

#define DEBUG_BLE 0
#if DEBUG_BLE
PRAGMA_DISABLE_OPTIMIZATION_ACTUAL
#define MLDEBUG_LOG_FUNC() LogInfo(UTF8_TO_TCHAR(__FUNCTION__))
#define MLDEBUG_LOG_FUNC_CB() This->LogInfo(UTF8_TO_TCHAR(__FUNCTION__))
#define MLDEBUG_CLOG_CB(bConditional, Log) if (bConditional) This->LogInfo(Log)
#else
#define MLDEBUG_LOG_FUNC()
#define MLDEBUG_LOG_FUNC_CB()
#define MLDEBUG_CLOG_CB(bConditional, Log)
#endif // DEBUG_BLE
#define ERROR_TO_STRING_FUNC MLBluetoothGattGetResultString
#define MLGEN_API_ERROR_STR(FuncName) FString::Printf(TEXT("%s failed due to API call %s failing with error '%s'"), UTF8_TO_TCHAR(__FUNCTION__), UTF8_TO_TCHAR(#FuncName), UTF8_TO_TCHAR(ERROR_TO_STRING_FUNC(Result)))
#define MLGEN_FUNC_ERROR_STR(ErrorMsg) FString::Printf(TEXT("%s failed due to the following error: '%s'."), UTF8_TO_TCHAR(__FUNCTION__), ErrorMsg)

constexpr int32 DefaultDescriptorNodePoolSize = 32;
constexpr uint8_t NotificationMarkerLength = 2;
constexpr uint8_t EnableNotification[NotificationMarkerLength] = { 1, 0 };
constexpr uint8_t DisableNotification[NotificationMarkerLength] = { 0, 0 };
const TCHAR* const NotificationDescriptorUUID = TEXT("00002902-0000-1000-8000-00805f9b34fb");
const char* const NotificationDescriptorUUIDAnsi = "00002902-0000-1000-8000-00805f9b34fb";
TArray<MLBluetoothGattDescriptorNode> FBLERunnable::MLDescriptorNodePool;

FBLERunnable::FBLERunnable()
: FMagicLeapRunnable({}, TEXT("FBLERunnable"))
, bInitialized(false)
{
	MLDescriptorNodePool.Reserve(DefaultDescriptorNodePoolSize);
}

bool FBLERunnable::TryInit()
{
	if (bInitialized)
	{
		return true;
	}

	MLBluetoothLeScannerCallbacksInit(&ScannerCallbacks);
	ScannerCallbacks.on_scan_result = OnScanResult;
	MLResult Result = MLBluetoothLeSetScannerCallbacks(&ScannerCallbacks, this);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothLeSetScannerCallbacks));
		return false;
	}

	MLBluetoothLeGattClientCallbacksInit(&GattCallbacks);
	GattCallbacks.on_gatt_connection_state_changed = OnGattConnectionStateChanged;
	GattCallbacks.on_gatt_services_discovered = OnGattServicesDiscovered;
	GattCallbacks.on_gatt_read_remote_rssi = OnGattReadRemoteRSSI;
	GattCallbacks.on_gatt_characteristic_read = OnGattCharacteristicRead;
	GattCallbacks.on_gatt_characteristic_write = OnGattCharacteristicWrite;
	GattCallbacks.on_gatt_descriptor_read = OnGattDescriptorRead;
	GattCallbacks.on_gatt_descriptor_write = OnGattDescriptorWrite;
	GattCallbacks.on_gatt_notify = OnGattNotify;
	GattCallbacks.on_gatt_connection_parameters_updated = OnGattConnectionParametersUpdated;
	GattCallbacks.on_gatt_mtu_changed = OnGattMTUChanged;
	Result = MLBluetoothGattSetClientCallbacks(&GattCallbacks, this);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattSetClientCallbacks));
		return false;
	}

	MLBluetoothAdapterCallbacksInit(&AdapterCallbacks);
	AdapterCallbacks.on_adapter_state_changed = OnAdapterStateChanged;
	AdapterCallbacks.on_bond_state_changed = OnAdapterBondStateChanged;
	Result = MLBluetoothAdapterSetCallbacks(&AdapterCallbacks, this);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothAdapterSetCallbacks));
		return false;
	}

	bInitialized = true;
	return true;
}

bool FBLERunnable::ProcessCurrentTask()
{
	bool bSuccess = false;
	if (!TryInit())
	{
		return bSuccess;
	}
	
	MLBluetoothAdapterState MLState = MLBluetoothAdapterState_Off;
	MLResult Result = MLBluetoothAdapterGetState(&MLState);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothAdapterGetState));
		return false;
	}

	if (CurrentTask.Type != FBLETask::EType::RequestAdapterState)
	{
		if (MLState != MLBluetoothAdapterState_On)
		{
			CurrentTask.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(TEXT("Bluetooth is not enabled")));
			return false;
		}
	}
	else
	{
		CurrentTask.Type = FBLETask::EType::ReceiveAdapterState;
		CurrentTask.AdapterState = MLState == MLBluetoothAdapterState_Off ? EMagicLeapBluetoothAdapterState::Off : EMagicLeapBluetoothAdapterState::On;
		CurrentTask.Result.bSuccess = true;
		return true;
	}

#if WITH_MLSDK
	switch (CurrentTask.Type)
	{
	case FBLETask::EType::None: bSuccess = false; checkf(false, TEXT("Invalid task encountered!")); break;
	case FBLETask::EType::ScanForDevices: bSuccess = StartScan_WorkerThread(); break;
	case FBLETask::EType::StopScan: bSuccess = StopScan_WorkerThread(); break;
	case FBLETask::EType::ConnectToDevice: bSuccess = GattConnect_WorkerThread(); break;
	case FBLETask::EType::DisconnectFromDevice: bSuccess = GattDisconnect_WorkerThread(); break;
	case FBLETask::EType::RequestRemoteRSSI: bSuccess = GattReadRemoteRSSI_WorkerThread(); break;
	case FBLETask::EType::RequestAvailableServices: bSuccess = GattGetAvailableServices_WorkerThread(); break;
	case FBLETask::EType::ReadCharacteristic: bSuccess = GattReadCharacteristic_WorkerThread(); break;
	case FBLETask::EType::WriteCharacteristic: bSuccess = GattWriteCharacteristic_WorkerThread(); break;
	case FBLETask::EType::ReadDescriptor: bSuccess = GattReadDescriptor_WorkerThread(); break;
	case FBLETask::EType::WriteDescriptor: bSuccess = GattWriteDescriptor_WorkerThread(); break;
	case FBLETask::EType::RequestCharacteristicNotifications: bSuccess = GattSetCharacteristicNotification_WorkerThread(); break;
	case FBLETask::EType::RequestConnectionPriority: bSuccess = GattRequestConnectionPriority_WorkerThread(); break;
	case FBLETask::EType::RequestMTU: bSuccess = GattRequestMTU_WorkerThread(); break;
	case FBLETask::EType::RequestAdapterName: bSuccess = AdapterGetName_WorkerThread(); break;
	}
#endif // WITH_MLSDK
	return bSuccess;
}

void FBLERunnable::LogInfo(const FString& Info)
{
	FBLETask Task(FBLETask::EType::Log);
	Task.Log = Info;
	Task.bSuccess = true;
	PushCompletedTask(Task);
	UE_LOG(LogMagicLeapBLE, Log, TEXT("%s"), *Info);
}

void FBLERunnable::LogError(const FString& Info)
{
	FBLETask Task(FBLETask::EType::Log);
	Task.Log = Info;
	Task.bSuccess = true;
	PushCompletedTask(Task);
	UE_LOG(LogMagicLeapBLE, Error, TEXT("%s"), *Info);
}

bool FBLERunnable::StartScan_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLResult Result = MLBluetoothLeStartScan();
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothLeStartScan));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::StopScan_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLResult Result = MLBluetoothLeStopScan();
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothLeStopScan));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattConnect_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	bRequestedConnection = true;
	if (CurrentTask.StringData.Len() > LENGTH_BD_ADDR)
	{
		CurrentTask.Result = FMagicLeapResult(false, TEXT("Address is too long!"));
		return false;
	}

	MLBluetoothAddr BluetoothAddr;
	FMemory::Memzero(BluetoothAddr.address, LENGTH_BD_ADDR + 1);
	FMemory::Memcpy(BluetoothAddr.address, TCHAR_TO_UTF8(*CurrentTask.StringData), LENGTH_BD_ADDR);
	MLResult Result = MLBluetoothAdapterCreateBond(&BluetoothAddr);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothAdapterCreateBond));
		return false;
	}
	
	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattDisconnect_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	bRequestedConnection = false;
	MLResult Result = MLBluetoothGattDisconnect();
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattDisconnect));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattReadRemoteRSSI_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLResult Result = MLBluetoothGattReadRemoteRssi();
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattReadRemoteRssi));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattGetAvailableServices_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLBluetoothGattServiceList ServiceList;
	MLBluetoothGattServiceListInit(&ServiceList);
	MLResult Result = MLBluetoothGattGetServiceRecord(&ServiceList);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattGetServiceRecord));
		return false;
	}

	CurrentTask.Type = FBLETask::EType::ReceiveAvailableServices;
	CurrentTask.GattServices.Reserve(ServiceList.count);
	for (auto ServiceIndex = 0; ServiceIndex < ServiceList.count; ++ServiceIndex)
	{
		MLBluetoothGattService MLService = ServiceList.services[ServiceIndex];
		FMagicLeapBluetoothGattService& UEService = CurrentTask.GattServices.AddZeroed_GetRef();
		UEService.UUID = UTF8_TO_TCHAR(MLService.uuid.uuid);
		UEService.InstanceId = MLService.instance_id;
		UEService.ServiceType = MLService.service_type;
		MLBluetoothGattIncludedServiceNode* IncludedServiceNode = MLService.inc_services;
		for (; IncludedServiceNode != nullptr; IncludedServiceNode = IncludedServiceNode->next)
		{
			MLToUEGattIncludedService(IncludedServiceNode->inc_service, UEService.IncludedServices.AddZeroed_GetRef());
		}
		MLBluetoothGattCharacteristicNode* CharacteristicNode = MLService.characteristics;
		for (; CharacteristicNode != nullptr; CharacteristicNode = CharacteristicNode->next)
		{
			MLToUEGattCharacteristic(CharacteristicNode->characteristic, UEService.Characteristics.AddZeroed_GetRef());
		}
	}

	Result = MLBluetoothGattReleaseServiceList(&ServiceList);
	if (Result != MLResult_Ok)
	{
		LogError(MLGEN_API_ERROR_STR(MLBluetoothGattReleaseServiceList));
	}
	
	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattReadCharacteristic_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLBluetoothGattCharacteristic MLCharacteristic;
	if (!UEToMLGattCharacteristic(CurrentTask.GattCharacteristic, MLCharacteristic))
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(TEXT("UEToMLGattCharacteristic failed")));
		return false;
	}

	MLResult Result = MLBluetoothGattReadCharacteristic(&MLCharacteristic);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattReadCharacteristic));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattWriteCharacteristic_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLBluetoothGattCharacteristic MLCharacteristic;
	if (!UEToMLGattCharacteristic(CurrentTask.GattCharacteristic, MLCharacteristic))
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(TEXT("UEToMLGattCharacteristic failed")));
		return false;
	}

	MLResult Result = MLBluetoothGattWriteCharacteristic(&MLCharacteristic);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattWriteCharacteristic));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattReadDescriptor_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLBluetoothGattDescriptor MLDescriptor;
	if (!UEToMLGattDescriptor(CurrentTask.GattDescriptor, MLDescriptor))
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(TEXT("UEToMLGattDescriptor failed")));
		return false;
	}

	MLResult Result = MLBluetoothGattReadDescriptor(&MLDescriptor);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattWriteCharacteristic));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattWriteDescriptor_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLBluetoothGattDescriptor MLDescriptor;
	if (!UEToMLGattDescriptor(CurrentTask.GattDescriptor, MLDescriptor))
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(TEXT("UEToMLGattDescriptor failed")));
		return false;
	}

	MLResult Result = MLBluetoothGattWriteDescriptor(&MLDescriptor);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattWriteCharacteristic));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattSetCharacteristicNotification_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLBluetoothGattCharacteristic MLCharacteristic;
	if (!UEToMLGattCharacteristic(CurrentTask.GattCharacteristic, MLCharacteristic))
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(TEXT("UEToMLGattCharacteristic failed")));
		return false;
	}

	int NotificationDescriptorIndex = -1;
	for (int DescIndex = 0; DescIndex < CurrentTask.GattCharacteristic.Descriptors.Num(); ++DescIndex)
	{
		if (CurrentTask.GattCharacteristic.Descriptors[DescIndex].UUID == NotificationDescriptorUUID)
		{
			NotificationDescriptorIndex = DescIndex;
			break;
		}
	}

	if (NotificationDescriptorIndex == -1)
	{
		CurrentTask.Result = FMagicLeapResult(false, FString::Printf(TEXT("GattSetCharacteristicNotification_WorkerThread() failed due to the following error: 'Failed to find notification descriptor in %s'"), *CurrentTask.GattCharacteristic.UUID));
		return false;
	}

	MLResult Result = MLBluetoothGattSetCharacteristicNotification(&MLCharacteristic, CurrentTask.bEnable);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattSetCharacteristicNotification));
		return false;
	}

	MLBluetoothGattDescriptorNode* DescriptorNode = MLCharacteristic.descriptors;
	for (; DescriptorNode != nullptr; DescriptorNode = DescriptorNode->next)
	{
		if (FCStringAnsi::Stricmp(NotificationDescriptorUUIDAnsi, DescriptorNode->descriptor.uuid.uuid) == 0)
		{
			FMemory::Memcpy(&DescriptorNode->descriptor.value[0], CurrentTask.bEnable ? EnableNotification : DisableNotification, NotificationMarkerLength);
			DescriptorNode->descriptor.size = NotificationMarkerLength;
			Result = MLBluetoothGattWriteDescriptor(&DescriptorNode->descriptor);
			if (Result != MLResult_Ok)
			{
				CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattWriteDescriptor));
				return false;
			}

			break;
		}
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattRequestConnectionPriority_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLResult Result = MLBluetoothGattRequestConnectionPriority(UEToMLGattConnectionPriority(CurrentTask.ConnectionPriority));
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattRequestConnectionPriority));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::GattRequestMTU_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLResult Result = MLBluetoothGattRequestMtu(CurrentTask.IntData);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattRequestMtu));
		return false;
	}

	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::AdapterGetName_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLBluetoothName MLName;
	MLResult Result = MLBluetoothAdapterGetName(&MLName);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothAdapterGetName));
		return false;
	}

	CurrentTask.Type = FBLETask::EType::ReceiveAdapterName;
	CurrentTask.StringData = UTF8_TO_TCHAR(MLName.name);
	CurrentTask.Result.bSuccess = true;
	return true;
}

bool FBLERunnable::AdapterGetState_WorkerThread()
{
	MLDEBUG_LOG_FUNC();
	MLBluetoothAdapterState MLState = MLBluetoothAdapterState_Off;
	MLResult Result = MLBluetoothAdapterGetState(&MLState);
	if (Result != MLResult_Ok)
	{
		CurrentTask.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothAdapterGetState));
		return false;
	}

	CurrentTask.Type = FBLETask::EType::ReceiveAdapterState;
	CurrentTask.AdapterState = MLState == MLBluetoothAdapterState_Off ? EMagicLeapBluetoothAdapterState::Off : EMagicLeapBluetoothAdapterState::On;
	CurrentTask.Result.bSuccess = true;
	return true;
}

void FBLERunnable::OnScanResult(MLBluetoothLeScanResult* InResult, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::ReportFoundDevice);
	MLToUEDevice(InResult->device, Task.FoundDevice);
	Task.Result.bSuccess = true;
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnAdapterBondStateChanged(const MLBluetoothDevice* Device, MLBluetoothBondState State, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	switch (State)
	{
	case MLBluetoothBondState_None:
	{
		FBLETask Task(FBLETask::EType::DeviceConnectionChanged);
		if (This->bRequestedConnection)
		{
			Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(TEXT("Adapter not bonded")));
		}
		else
		{
			Task.Result.bSuccess = true;
		}
		This->PushCompletedTask(Task);
	}
	break;
	case MLBluetoothBondState_Bonding:
	{
		This->LogInfo(FString::Printf(TEXT("Adapter bonding with %s."), UTF8_TO_TCHAR(Device->bd_addr.address)));
	}
	break;
	case MLBluetoothBondState_Bonded:
	{
		This->LogInfo(FString::Printf(TEXT("Adapter bonded to %s."), UTF8_TO_TCHAR(Device->bd_addr.address)));
		if (This->bRequestedConnection)
		{
			MLResult Result = MLBluetoothGattConnect(&Device->bd_addr);
			if (Result != MLResult_Ok)
			{
				FBLETask Task(FBLETask::EType::DeviceConnectionChanged);
				Task.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattConnect));
				This->PushCompletedTask(Task);
			}
		}
	}
	}
}

void FBLERunnable::OnGattConnectionStateChanged(MLBluetoothGattStatus Status, MLBluetoothGattConnectionState NewState, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	if (NewState == MLBluetoothGattConnectionState_Connected)
	{
		MLResult Result = MLBluetoothGattDiscoverServices();
		if (Result != MLResult_Ok)
		{
			FBLETask Task(FBLETask::EType::DeviceConnectionChanged);
			Task.ConnectionState = EMagicLeapBluetoothGattConnectionState::NotConnected;
			Task.Result = FMagicLeapResult(false, MLGEN_API_ERROR_STR(MLBluetoothGattGetResultString));
			This->PushCompletedTask(Task);
		}
		// otherwise wait until we have discovered services before calling the connect operation a success
	}
	else
	{
		FBLETask Task(FBLETask::EType::DeviceConnectionChanged);
		Task.ConnectionState = EMagicLeapBluetoothGattConnectionState::NotConnected;
		if (This->bRequestedConnection)
		{
			Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(TEXT("Device was forcibly disconnected")));
		}
		else
		{
			Task.Result.bSuccess = true;
		}
		This->PushCompletedTask(Task);
	}
}

void FBLERunnable::OnGattReadRemoteRSSI(int32_t RSSI, MLBluetoothGattStatus Status, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::ReceiveRemoteRSSI);
	Task.IntData = RSSI;
	Task.GattStatus = MLToUEGattStatus(Status);
	Task.bSuccess = Status == MLBluetoothGattStatus_Success;
	if (Status != MLBluetoothGattStatus_Success)
	{
		Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(MLBluetoothGattStatusToString(Status)));
	}
	else
	{
		Task.Result.bSuccess = true;
	}
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnGattServicesDiscovered(MLBluetoothGattStatus Status, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::DeviceConnectionChanged);
	Task.ConnectionState = EMagicLeapBluetoothGattConnectionState::Connected;
	Task.GattStatus = MLToUEGattStatus(Status);
	if (Status != MLBluetoothGattStatus_Success)
	{
		Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(MLBluetoothGattStatusToString(Status)));
	}
	else
	{
		Task.Result.bSuccess = true;
	}
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnGattCharacteristicRead(const MLBluetoothGattCharacteristic* Characteristic, MLBluetoothGattStatus Status, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::CharacteristicRead);
	MLToUEGattCharacteristic(*Characteristic, Task.GattCharacteristic);
	Task.GattStatus = MLToUEGattStatus(Status);
	if (Status != MLBluetoothGattStatus_Success)
	{
		Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(MLBluetoothGattStatusToString(Status)));
	}
	else
	{
		Task.Result.bSuccess = true;
	}
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnGattCharacteristicWrite(const MLBluetoothGattCharacteristic* Characteristic, MLBluetoothGattStatus Status, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::CharacteristicWritten);
	MLToUEGattCharacteristic(*Characteristic, Task.GattCharacteristic);
	Task.GattStatus = MLToUEGattStatus(Status);
	if (Status != MLBluetoothGattStatus_Success)
	{
		Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(MLBluetoothGattStatusToString(Status)));
	}
	else
	{
		Task.Result.bSuccess = true;
	}
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnGattDescriptorRead(const MLBluetoothGattDescriptor* Descriptor, MLBluetoothGattStatus Status, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::DescriptorRead);
	MLToUEGattDescriptor(*Descriptor, Task.GattDescriptor);
	Task.GattStatus = MLToUEGattStatus(Status);
	if (Status != MLBluetoothGattStatus_Success)
	{
		Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(MLBluetoothGattStatusToString(Status)));
	}
	else
	{
		Task.Result.bSuccess = true;
	}
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnGattDescriptorWrite(const MLBluetoothGattDescriptor* Descriptor, MLBluetoothGattStatus Status, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::DescriptorWritten);
	MLToUEGattDescriptor(*Descriptor, Task.GattDescriptor);
	Task.GattStatus = MLToUEGattStatus(Status);
	if (Status != MLBluetoothGattStatus_Success)
	{
		Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(MLBluetoothGattStatusToString(Status)));
	}
	else
	{
		Task.Result.bSuccess = true;
	}
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnGattNotify(const MLBluetoothGattCharacteristic* Characteristic, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::ReceiveCharacteristicNotification);
	MLToUEGattCharacteristic(*Characteristic, Task.GattCharacteristic);
	Task.Result.bSuccess = true;
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnGattConnectionParametersUpdated(int32_t Interval, MLBluetoothGattStatus Status, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::ReceiveConnectionPriority);
	Task.IntData = Interval;
	Task.GattStatus = This->MLToUEGattStatus(Status);
	if (Status != MLBluetoothGattStatus_Success)
	{
		Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(MLBluetoothGattStatusToString(Status)));
	}
	else
	{
		Task.Result.bSuccess = true;
	}
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnGattMTUChanged(int32_t MTU, MLBluetoothGattStatus Status, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::ReceiveMTU);
	Task.IntData = MTU;
	Task.GattStatus = MLToUEGattStatus(Status);
	if (Status != MLBluetoothGattStatus_Success)
	{
		Task.Result = FMagicLeapResult(false, MLGEN_FUNC_ERROR_STR(MLBluetoothGattStatusToString(Status)));
	}
	else
	{
		Task.Result.bSuccess = true;
	}
	This->PushCompletedTask(Task);
}

void FBLERunnable::OnAdapterStateChanged(MLBluetoothAdapterState State, void* InContext)
{
	FBLERunnable* This = static_cast<FBLERunnable*>(InContext);
	MLDEBUG_LOG_FUNC_CB();
	FBLETask Task(FBLETask::EType::ReceiveAdapterState);
	Task.AdapterState = MLToUEAdapterState(State);
	Task.Result.bSuccess = true;
	This->PushCompletedTask(Task);
}

void FBLERunnable::MLToUEDevice(const MLBluetoothDevice& InMLDevice, FMagicLeapBluetoothDevice& OutUEDevice)
{
	OutUEDevice.Version = InMLDevice.version;
	OutUEDevice.Name = UTF8_TO_TCHAR(InMLDevice.bd_name.name);
	OutUEDevice.Address = UTF8_TO_TCHAR(InMLDevice.bd_addr.address);
	OutUEDevice.RSSI = InMLDevice.rssi;
	OutUEDevice.Type = MLToUEDeviceType(InMLDevice.device_type);
}

EMagicLeapBluetoothDeviceType FBLERunnable::MLToUEDeviceType(MLBluetoothDeviceType InMLDeviceType)
{
	EMagicLeapBluetoothDeviceType UEDeviceType = EMagicLeapBluetoothDeviceType::Unknown;
	switch (InMLDeviceType)
	{
	case MLBluetoothDeviceType_Unknown: UEDeviceType = EMagicLeapBluetoothDeviceType::Unknown; break;
	case MLBluetoothDeviceType_LE: UEDeviceType = EMagicLeapBluetoothDeviceType::LE;
	}

	return UEDeviceType;
}

EMagicLeapBluetoothGattStatus FBLERunnable::MLToUEGattStatus(MLBluetoothGattStatus InMLGattStatus)
{
	EMagicLeapBluetoothGattStatus UEGattStatus = EMagicLeapBluetoothGattStatus::Success;
	switch (InMLGattStatus)
	{
	case MLBluetoothGattStatus_Success:	UEGattStatus = EMagicLeapBluetoothGattStatus::Success; break;
	case MLBluetoothGattStatus_Read_Not_Permitted:	UEGattStatus = EMagicLeapBluetoothGattStatus::ReadNotPermitted; break;
	case MLBluetoothGattStatus_Write_Not_Permitted:	UEGattStatus = EMagicLeapBluetoothGattStatus::WriteNotPermitted; break;
	case MLBluetoothGattStatus_Insufficient_Authentication:	UEGattStatus = EMagicLeapBluetoothGattStatus::InsufficientAuthentication; break;
	case MLBluetoothGattStatus_Request_Not_Supported:	UEGattStatus = EMagicLeapBluetoothGattStatus::RequestNot_Supported; break;
	case MLBluetoothGattStatus_Invalid_Offset:	UEGattStatus = EMagicLeapBluetoothGattStatus::InvalidOffset; break;
	case MLBluetoothGattStatus_Invalid_Attribute_Length:	UEGattStatus = EMagicLeapBluetoothGattStatus::InvalidAttributeLength; break;
	case MLBluetoothGattStatus_Insufficient_Encryption:	UEGattStatus = EMagicLeapBluetoothGattStatus::InsufficientEncryption; break;
	case MLBluetoothGattStatus_Connection_Congested:	UEGattStatus = EMagicLeapBluetoothGattStatus::ConnectionCongested; break;
	case MLBluetoothGattStatus_Error:	UEGattStatus = EMagicLeapBluetoothGattStatus::Error; break;
	case MLBluetoothGattStatus_Failure:	UEGattStatus = EMagicLeapBluetoothGattStatus::Failure; break;
	}

	return UEGattStatus;
}

EMagicLeapBluetoothGattConnectionState FBLERunnable::MLToUEGattConnectionState(MLBluetoothGattConnectionState InMLGattConnectionState)
{
	EMagicLeapBluetoothGattConnectionState UEGattConnectionState = EMagicLeapBluetoothGattConnectionState::NotConnected;
	switch (InMLGattConnectionState)
	{
	case MLBluetoothGattConnectionState_Disconnected: UEGattConnectionState = EMagicLeapBluetoothGattConnectionState::NotConnected; break;
	case MLBluetoothGattConnectionState_Connected: UEGattConnectionState = EMagicLeapBluetoothGattConnectionState::Connected; break;
	}

	return UEGattConnectionState;
}

EMagicLeapBluetoothAdapterState FBLERunnable::MLToUEAdapterState(MLBluetoothAdapterState InMLBluetoothAdapterState)
{
	EMagicLeapBluetoothAdapterState UEAdapterState = EMagicLeapBluetoothAdapterState::Off;
	switch (InMLBluetoothAdapterState)
	{
	case MLBluetoothAdapterState_Off: UEAdapterState = EMagicLeapBluetoothAdapterState::Off; break;
	case MLBluetoothAdapterState_On: UEAdapterState = EMagicLeapBluetoothAdapterState::On; break;
	}

	return UEAdapterState;
}

EMagicLeapBluetoothBondState FBLERunnable::MLToUEBondState(MLBluetoothBondState InMLBluetoothBondState)
{
	EMagicLeapBluetoothBondState UEBondState = EMagicLeapBluetoothBondState::None;

	switch (InMLBluetoothBondState)
	{
	case MLBluetoothBondState_None: UEBondState = EMagicLeapBluetoothBondState::None; break;
	case MLBluetoothBondState_Bonding: UEBondState = EMagicLeapBluetoothBondState::Bonding; break;
	case MLBluetoothBondState_Bonded: UEBondState = EMagicLeapBluetoothBondState::Bonded; break;
	}

	return UEBondState;
}

EMagicLeapBluetoothACLState FBLERunnable::MLToUEACLState(MLBluetoothAclState InMLBluetoothACLState)
{
	EMagicLeapBluetoothACLState UEACLState = EMagicLeapBluetoothACLState::Disconnected;
	switch (InMLBluetoothACLState)
	{
	case MLBluetoothAclState_Connected: UEACLState = EMagicLeapBluetoothACLState::Connected; break;
	case MLBluetoothAclState_Disconnected: UEACLState = EMagicLeapBluetoothACLState::Disconnected; break;
	}

	return UEACLState;
}

void FBLERunnable::MLToUEGattIncludedService(const MLBluetoothGattIncludedService& InMLGattIncludedService, FMagicLeapBluetoothGattIncludedService& OutUEGattIncludedService)
{
	OutUEGattIncludedService.UUID = UTF8_TO_TCHAR(InMLGattIncludedService.uuid.uuid);
	OutUEGattIncludedService.InstanceId = InMLGattIncludedService.instance_id;
	OutUEGattIncludedService.ServiceType = InMLGattIncludedService.service_type;
}

void FBLERunnable::MLToUEGattCharacteristic(const MLBluetoothGattCharacteristic& InMLGattCharacteristic, FMagicLeapBluetoothGattCharacteristic& OutUEGattCharacteristic)
{
	OutUEGattCharacteristic.Version = InMLGattCharacteristic.version;
	OutUEGattCharacteristic.UUID = UTF8_TO_TCHAR(InMLGattCharacteristic.uuid.uuid);
	OutUEGattCharacteristic.InstanceId = InMLGattCharacteristic.instance_id;
	BitFieldToUEGattCharacteristicPermissions(InMLGattCharacteristic.permissions, OutUEGattCharacteristic.Permissions);
	BitFieldToUEGattCharacteristicProperties(InMLGattCharacteristic.properties, OutUEGattCharacteristic.Properties);
	BitFieldToUEGattCharacteristicWriteTypes(InMLGattCharacteristic.write_type, OutUEGattCharacteristic.WriteTypes);
	OutUEGattCharacteristic.Value.AddUninitialized(InMLGattCharacteristic.size);
	FMemory::Memcpy(OutUEGattCharacteristic.Value.GetData(), InMLGattCharacteristic.value, InMLGattCharacteristic.size);
	MLBluetoothGattDescriptorNode* DescriptorNode = InMLGattCharacteristic.descriptors;
	for (; DescriptorNode != nullptr; DescriptorNode = DescriptorNode->next)
	{
		MLToUEGattDescriptor(DescriptorNode->descriptor, OutUEGattCharacteristic.Descriptors.AddZeroed_GetRef());
	}
}

bool FBLERunnable::UEToMLGattCharacteristic(const FMagicLeapBluetoothGattCharacteristic& InUEGattCharacteristic, MLBluetoothGattCharacteristic& OutMLGattCharacteristic)
{
	MLBluetoothGattCharacteristicInit(&OutMLGattCharacteristic);
	if (InUEGattCharacteristic.UUID.Len() > LENGTH_UUID)
	{
		LogError(TEXT("UUID length is greater than LENGTH_UUID!"));
		return false;
	}

	if (InUEGattCharacteristic.Value.Num() > MAX_VALUE_BUFFER)
	{
		LogError(TEXT("value array length is greater than MAX_VALUE_BUFFER!"));
		return false;
	}

	OutMLGattCharacteristic.version = InUEGattCharacteristic.Version;
	if (InUEGattCharacteristic.UUID.Len())
	{
		FMemory::Memcpy(OutMLGattCharacteristic.uuid.uuid, TCHAR_TO_UTF8(*InUEGattCharacteristic.UUID), InUEGattCharacteristic.UUID.Len());
	}
	OutMLGattCharacteristic.instance_id = InUEGattCharacteristic.InstanceId;
	OutMLGattCharacteristic.permissions = UEGattCharacteristicPermissionsToBitField(InUEGattCharacteristic.Permissions);
	OutMLGattCharacteristic.properties = UEGattCharacteristicPropertiesToBitField(InUEGattCharacteristic.Properties);
	OutMLGattCharacteristic.write_type = UEGattCharacteristicWriteTypesToBitField(InUEGattCharacteristic.WriteTypes);
	if (InUEGattCharacteristic.Value.Num())
	{
		FMemory::Memcpy(OutMLGattCharacteristic.value, InUEGattCharacteristic.Value.GetData(), InUEGattCharacteristic.Value.Num());
	}
	OutMLGattCharacteristic.size = InUEGattCharacteristic.Value.Num();

	MLDescriptorNodePool.Empty(MLDescriptorNodePool.Max());
	if (MLDescriptorNodePool.Max() < InUEGattCharacteristic.Descriptors.Num())
	{
		MLDescriptorNodePool.Reserve(MLDescriptorNodePool.Max() * 2);
	}
	MLDescriptorNodePool.AddZeroed(InUEGattCharacteristic.Descriptors.Num());
	for (int32 DescriptorIndex = 0; DescriptorIndex < InUEGattCharacteristic.Descriptors.Num(); ++DescriptorIndex)
	{
		MLBluetoothGattDescriptorNode* DescriptorNode = &MLDescriptorNodePool[DescriptorIndex];
		DescriptorNode->next = nullptr;
		if (DescriptorIndex > 0)
		{
			MLDescriptorNodePool[DescriptorIndex - 1].next = DescriptorNode;
		}
		UEToMLGattDescriptor(InUEGattCharacteristic.Descriptors[DescriptorIndex], DescriptorNode->descriptor);
	}
	OutMLGattCharacteristic.descriptors = MLDescriptorNodePool.Num() ? &MLDescriptorNodePool[0] : nullptr;

	return true;
}

void FBLERunnable::MLToUEGattDescriptor(const MLBluetoothGattDescriptor& InMLGattDescriptor, FMagicLeapBluetoothGattDescriptor& OutUEGattDescriptor)
{
	OutUEGattDescriptor.Version = InMLGattDescriptor.version;
	OutUEGattDescriptor.UUID = UTF8_TO_TCHAR(InMLGattDescriptor.uuid.uuid);
	OutUEGattDescriptor.InstanceId = InMLGattDescriptor.instance_id;
	BitFieldToUEGattCharacteristicPermissions(InMLGattDescriptor.permissions, OutUEGattDescriptor.Permissions);
	OutUEGattDescriptor.Value.AddUninitialized(InMLGattDescriptor.size);
	FMemory::Memcpy(OutUEGattDescriptor.Value.GetData(), InMLGattDescriptor.value, InMLGattDescriptor.size);
}

bool FBLERunnable::UEToMLGattDescriptor(const FMagicLeapBluetoothGattDescriptor& InUEGattDescriptor, MLBluetoothGattDescriptor& OutMLGattDescriptor)
{
	if (InUEGattDescriptor.UUID.Len() > LENGTH_UUID)
	{
		LogError(TEXT("UUID length is greater than LENGTH_UUID!"));
		return false;
	}

	if (InUEGattDescriptor.Value.Num() > MAX_VALUE_BUFFER)
	{
		LogError(TEXT("value array length is greater than MAX_VALUE_BUFFER!"));
		return false;
	}

	OutMLGattDescriptor.version = InUEGattDescriptor.Version;
	FMemory::Memcpy(OutMLGattDescriptor.uuid.uuid, TCHAR_TO_UTF8(*InUEGattDescriptor.UUID), InUEGattDescriptor.UUID.Len() + 1);
	OutMLGattDescriptor.instance_id = InUEGattDescriptor.InstanceId;
	OutMLGattDescriptor.permissions = UEGattCharacteristicPermissionsToBitField(InUEGattDescriptor.Permissions);
	FMemory::Memcpy(OutMLGattDescriptor.value, InUEGattDescriptor.Value.GetData(), InUEGattDescriptor.Value.Num());
	OutMLGattDescriptor.size = InUEGattDescriptor.Value.Num();
	return true;
}

MLBluetoothGattConnectionPriority FBLERunnable::UEToMLGattConnectionPriority(EMagicLeapBluetoothGattConnectionPriority InPriority)
{
	MLBluetoothGattConnectionPriority OutPriority = MLBluetoothGattConnectionPriority_Balanced;
	switch (InPriority)
	{
	case EMagicLeapBluetoothGattConnectionPriority::Balanced: OutPriority = MLBluetoothGattConnectionPriority_Balanced; break;
	case EMagicLeapBluetoothGattConnectionPriority::High: OutPriority = MLBluetoothGattConnectionPriority_High; break;
	case EMagicLeapBluetoothGattConnectionPriority::LowPower: OutPriority = MLBluetoothGattConnectionPriority_Low_Power; break;
	}

	return OutPriority;
}

FString FBLERunnable::GattStatusToString(EMagicLeapBluetoothGattStatus InStatus)
{
	FString StatusString = TEXT("Undefined");
	switch (InStatus)
	{
	case EMagicLeapBluetoothGattStatus::Success: StatusString = TEXT("Success"); break;
	case EMagicLeapBluetoothGattStatus::ReadNotPermitted: StatusString = TEXT("ReadNotPermitted"); break;
	case EMagicLeapBluetoothGattStatus::WriteNotPermitted: StatusString = TEXT("WriteNotPermitted"); break;
	case EMagicLeapBluetoothGattStatus::InsufficientAuthentication: StatusString = TEXT("InsufficientAuthentication"); break;
	case EMagicLeapBluetoothGattStatus::RequestNot_Supported: StatusString = TEXT("RequestNot_Supported"); break;
	case EMagicLeapBluetoothGattStatus::InvalidOffset: StatusString = TEXT("InvalidOffset"); break;
	case EMagicLeapBluetoothGattStatus::InvalidAttributeLength: StatusString = TEXT("InvalidAttributeLength"); break;
	case EMagicLeapBluetoothGattStatus::InsufficientEncryption: StatusString = TEXT("InsufficientEncryption"); break;
	case EMagicLeapBluetoothGattStatus::ConnectionCongested: StatusString = TEXT("ConnectionCongested"); break;
	case EMagicLeapBluetoothGattStatus::Error: StatusString = TEXT("Error"); break;
	case EMagicLeapBluetoothGattStatus::Failure: StatusString = TEXT("Failure"); break;
	}

	return StatusString;
}

uint32 FBLERunnable::UEGattCharacteristicPermissionsToBitField(const TArray<EMagicLeapBluetoothGattCharacteristicPermissions>& InPermissions)
{
	uint32 BitField = 0;
	for (EMagicLeapBluetoothGattCharacteristicPermissions Permission : InPermissions)
	{
		switch (Permission)
		{
		case EMagicLeapBluetoothGattCharacteristicPermissions::Read: BitField |= MLBluetoothGattAttrPerm_Read; break;
		case EMagicLeapBluetoothGattCharacteristicPermissions::ReadEncrypted: BitField |= MLBluetoothGattAttrPerm_ReadEncrypted; break;
		case EMagicLeapBluetoothGattCharacteristicPermissions::ReadEncryptedMITM: BitField |= MLBluetoothGattAttrPerm_ReadEncryptedMITM; break;
		case EMagicLeapBluetoothGattCharacteristicPermissions::Write: BitField |= MLBluetoothGattAttrPerm_Write; break;
		case EMagicLeapBluetoothGattCharacteristicPermissions::WriteEncrypted: BitField |= MLBluetoothGattAttrPerm_WriteEncrypted; break;
		case EMagicLeapBluetoothGattCharacteristicPermissions::WriteEncryptedMITM: BitField |= MLBluetoothGattAttrPerm_WriteEncryptedMITM; break;
		case EMagicLeapBluetoothGattCharacteristicPermissions::WriteSigned: BitField |= MLBluetoothGattAttrPerm_WriteSigned; break;
		case EMagicLeapBluetoothGattCharacteristicPermissions::WriteSignedMITM: BitField |= MLBluetoothGattAttrPerm_WriteSignedMITM; break;
		}
	}

	return BitField;
}

uint32 FBLERunnable::UEGattCharacteristicPropertiesToBitField(const TArray<EMagicLeapBluetoothGattCharacteristicProperties>& InProperties)
{
	uint32 BitField = 0;
	for (EMagicLeapBluetoothGattCharacteristicProperties Property : InProperties)
	{
		switch (Property)
		{
		case EMagicLeapBluetoothGattCharacteristicProperties::Broadcast: BitField |= MLBluetoothGattCharProp_Broadcast; break;
		case EMagicLeapBluetoothGattCharacteristicProperties::Read: BitField |= MLBluetoothGattCharProp_Read; break;
		case EMagicLeapBluetoothGattCharacteristicProperties::WriteNoRes: BitField |= MLBluetoothGattCharProp_WriteNoRes; break;
		case EMagicLeapBluetoothGattCharacteristicProperties::Write: BitField |= MLBluetoothGattCharProp_Write; break;
		case EMagicLeapBluetoothGattCharacteristicProperties::Notify: BitField |= MLBluetoothGattCharProp_Notify; break;
		case EMagicLeapBluetoothGattCharacteristicProperties::Indicate: BitField |= MLBluetoothGattCharProp_Indicate; break;
		case EMagicLeapBluetoothGattCharacteristicProperties::SignedWrite: BitField |= MLBluetoothGattCharProp_SignedWrite; break;
		case EMagicLeapBluetoothGattCharacteristicProperties::ExtProps: BitField |= MLBluetoothGattCharProp_ExtProps; break;
		}
	}

	return BitField;
}

uint32 FBLERunnable::UEGattCharacteristicWriteTypesToBitField(const TArray< EMagicLeapBluetoothGattCharacteristicWriteTypes>& InWriteTypes)
{
	uint32 BitField = 0;
	for (EMagicLeapBluetoothGattCharacteristicWriteTypes WriteType : InWriteTypes)
	{
		switch (WriteType)
		{
		case EMagicLeapBluetoothGattCharacteristicWriteTypes::NoResponse: BitField |= MLBluetoothGattCharWriteType_NoResponse; break;
		case EMagicLeapBluetoothGattCharacteristicWriteTypes::Default: BitField |= MLBluetoothGattCharWriteType_Default; break;
		case EMagicLeapBluetoothGattCharacteristicWriteTypes::Signed: BitField |= MLBluetoothGattCharWriteType_Signed; break;
		}
	}

	return BitField;
}

void FBLERunnable::BitFieldToUEGattCharacteristicPermissions(uint32 InBitField, TArray<EMagicLeapBluetoothGattCharacteristicPermissions>& OutPermissions)
{
	if (InBitField & MLBluetoothGattAttrPerm_Read)
	{
		OutPermissions.Add(EMagicLeapBluetoothGattCharacteristicPermissions::Read);
	}

	if (InBitField & MLBluetoothGattAttrPerm_ReadEncrypted)
	{
		OutPermissions.Add(EMagicLeapBluetoothGattCharacteristicPermissions::ReadEncrypted);
	}

	if (InBitField & MLBluetoothGattAttrPerm_ReadEncryptedMITM)
	{
		OutPermissions.Add(EMagicLeapBluetoothGattCharacteristicPermissions::ReadEncryptedMITM);
	}

	if (InBitField & MLBluetoothGattAttrPerm_Write)
	{
		OutPermissions.Add(EMagicLeapBluetoothGattCharacteristicPermissions::Write);
	}

	if (InBitField & MLBluetoothGattAttrPerm_WriteEncrypted)
	{
		OutPermissions.Add(EMagicLeapBluetoothGattCharacteristicPermissions::WriteEncrypted);
	}

	if (InBitField & MLBluetoothGattAttrPerm_WriteEncryptedMITM)
	{
		OutPermissions.Add(EMagicLeapBluetoothGattCharacteristicPermissions::WriteEncryptedMITM);
	}

	if (InBitField & MLBluetoothGattAttrPerm_WriteSigned)
	{
		OutPermissions.Add(EMagicLeapBluetoothGattCharacteristicPermissions::WriteSigned);
	}

	if (InBitField & MLBluetoothGattAttrPerm_WriteSignedMITM)
	{
		OutPermissions.Add(EMagicLeapBluetoothGattCharacteristicPermissions::WriteSignedMITM);
	}
}

void FBLERunnable::BitFieldToUEGattCharacteristicProperties(uint32 InBitField, TArray<EMagicLeapBluetoothGattCharacteristicProperties>& OutProperties)
{
	if (InBitField & MLBluetoothGattCharProp_Broadcast)
	{
		OutProperties.Add(EMagicLeapBluetoothGattCharacteristicProperties::Broadcast);
	}

	if (InBitField & MLBluetoothGattCharProp_Read)
	{
		OutProperties.Add(EMagicLeapBluetoothGattCharacteristicProperties::Read);
	}

	if (InBitField & MLBluetoothGattCharProp_WriteNoRes)
	{
		OutProperties.Add(EMagicLeapBluetoothGattCharacteristicProperties::WriteNoRes);
	}

	if (InBitField & MLBluetoothGattCharProp_Write)
	{
		OutProperties.Add(EMagicLeapBluetoothGattCharacteristicProperties::Write);
	}

	if (InBitField & MLBluetoothGattCharProp_Notify)
	{
		OutProperties.Add(EMagicLeapBluetoothGattCharacteristicProperties::Notify);
	}

	if (InBitField & MLBluetoothGattCharProp_Indicate)
	{
		OutProperties.Add(EMagicLeapBluetoothGattCharacteristicProperties::Indicate);
	}

	if (InBitField & MLBluetoothGattCharProp_SignedWrite)
	{
		OutProperties.Add(EMagicLeapBluetoothGattCharacteristicProperties::SignedWrite);
	}

	if (InBitField & MLBluetoothGattCharProp_ExtProps)
	{
		OutProperties.Add(EMagicLeapBluetoothGattCharacteristicProperties::ExtProps);
	}
}

void FBLERunnable::BitFieldToUEGattCharacteristicWriteTypes(uint32 InBitField, TArray< EMagicLeapBluetoothGattCharacteristicWriteTypes>& OutWriteTypes)
{
	if (InBitField & MLBluetoothGattCharWriteType_NoResponse)
	{
		OutWriteTypes.Add(EMagicLeapBluetoothGattCharacteristicWriteTypes::NoResponse);
	}

	if (InBitField & MLBluetoothGattCharWriteType_Default)
	{
		OutWriteTypes.Add(EMagicLeapBluetoothGattCharacteristicWriteTypes::Default);
	}

	if (InBitField & MLBluetoothGattCharWriteType_Signed)
	{
		OutWriteTypes.Add(EMagicLeapBluetoothGattCharacteristicWriteTypes::Signed);
	}
}

const TCHAR* FBLERunnable::MLBluetoothGattStatusToString(MLBluetoothGattStatus InStatus)
{
	const TCHAR* StatusString = nullptr;
	switch (InStatus)
	{
		case MLBluetoothGattStatus_Success: StatusString = TEXT("Success"); break; 
		case MLBluetoothGattStatus_Read_Not_Permitted: StatusString = TEXT("ReadNotPermitted"); break;
		case MLBluetoothGattStatus_Write_Not_Permitted: StatusString = TEXT("WriteNotPermitted"); break;
		case MLBluetoothGattStatus_Insufficient_Authentication: StatusString = TEXT("InsufficientAuthentication"); break;
		case MLBluetoothGattStatus_Request_Not_Supported: StatusString = TEXT("RequestNotSupported"); break;
		case MLBluetoothGattStatus_Invalid_Offset: StatusString = TEXT("InvalidOffset"); break;
		case MLBluetoothGattStatus_Invalid_Attribute_Length: StatusString = TEXT("InvalidAttributeLength"); break;
		case MLBluetoothGattStatus_Insufficient_Encryption: StatusString = TEXT("InsufficientEncryption"); break;
		case MLBluetoothGattStatus_Connection_Congested: StatusString = TEXT("ConnectionCongested"); break;
		case MLBluetoothGattStatus_Error: StatusString = TEXT("Error"); break;
		case MLBluetoothGattStatus_Failure: StatusString = TEXT("Failure"); break;
	}

	return StatusString;
}
#endif // WITH_MLSDK
