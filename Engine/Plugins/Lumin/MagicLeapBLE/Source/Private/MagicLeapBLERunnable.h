// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MagicLeapRunnable.h"
#include "MagicLeapBLETypes.h"
#include "Lumin/CAPIShims/LuminAPIBluetooth.h"
#include "HAL/ThreadSafeBool.h"

struct FBLETask : public FMagicLeapTask
{
	enum class EType : uint32
	{
		None,
		ScanForDevices,
		StopScan,
		ReportFoundDevice,
		ConnectToDevice,
		DisconnectFromDevice,
		DeviceConnectionChanged,
		RequestRemoteRSSI,
		ReceiveRemoteRSSI,
		RequestAvailableServices,
		ReceiveAvailableServices,
		ReadCharacteristic,
		CharacteristicRead,
		WriteCharacteristic,
		CharacteristicWritten,
		ReadDescriptor,
		DescriptorRead,
		WriteDescriptor,
		DescriptorWritten,
		RequestCharacteristicNotifications,
		ReceiveCharacteristicNotification,
		RequestConnectionPriority,
		ReceiveConnectionPriority,
		RequestMTU,
		ReceiveMTU,
		RequestAdapterName,
		ReceiveAdapterName,
		RequestAdapterState,
		ReceiveAdapterState,
		AdapterStateChanged,
		Log,
	};

	EType Type;
	FString Log;
	int32 IntData;
	bool bEnable;
	FString StringData;
	FMagicLeapBluetoothDevice FoundDevice;
	EMagicLeapBluetoothGattStatus GattStatus;
	EMagicLeapBluetoothGattConnectionState ConnectionState;
	FMagicLeapBluetoothGattCharacteristic GattCharacteristic;
	FMagicLeapBluetoothGattDescriptor GattDescriptor;
	EMagicLeapBluetoothAdapterState AdapterState;
	EMagicLeapBluetoothBondState BondState;
	EMagicLeapBluetoothACLState ACLState;
	EMagicLeapBluetoothGattConnectionPriority ConnectionPriority;
	TArray<FMagicLeapBluetoothGattService> GattServices;

	FBLETask()
	: Type(EType::None)
	{
	}

	FBLETask(EType InType)
	: Type(InType)
	{
	}

	FBLETask(const FBLETask& Other)
	{
		*this = Other;
	}

	FBLETask& operator=(const FBLETask& Other)
	{
		bSuccess = Other.bSuccess;
		Result = Other.Result;
		Type = Other.Type;
		Log = Other.Log;
		IntData = Other.IntData;
		bEnable = Other.bEnable;
		StringData = Other.StringData;
		FoundDevice = Other.FoundDevice;
		GattStatus = Other.GattStatus;
		ConnectionState = Other.ConnectionState;
		GattCharacteristic = Other.GattCharacteristic;
		GattDescriptor = Other.GattDescriptor;
		AdapterState = Other.AdapterState;
		BondState = Other.BondState;
		ACLState = Other.ACLState;
		ConnectionPriority = Other.ConnectionPriority;
		GattServices = Other.GattServices;
		return *this;
	}

	const static TCHAR* TypeToString(EType InType)
	{
		const TCHAR* TypeString = nullptr;
		switch (InType)
		{
			case EType::None: TypeString = TEXT("None"); break;
			case EType::ScanForDevices: TypeString = TEXT("ScanForDevices"); break;
			case EType::StopScan: TypeString = TEXT("StopScan"); break;
			case EType::ReportFoundDevice: TypeString = TEXT("ReportFoundDevice"); break;
			case EType::ConnectToDevice: TypeString = TEXT("ConnectToDevice"); break;
			case EType::DisconnectFromDevice: TypeString = TEXT("DisconnectFromDevice"); break;
			case EType::DeviceConnectionChanged: TypeString = TEXT("DeviceConnectionChanged"); break;
			case EType::RequestRemoteRSSI: TypeString = TEXT("RequestRemoteRSSI"); break;
			case EType::ReceiveRemoteRSSI: TypeString = TEXT("ReceiveRemoteRSSI"); break;
			case EType::RequestAvailableServices: TypeString = TEXT("RequestAvailableServices"); break;
			case EType::ReceiveAvailableServices: TypeString = TEXT("ReceiveAvailableServices"); break;
			case EType::ReadCharacteristic: TypeString = TEXT("ReadCharacteristic"); break;
			case EType::CharacteristicRead: TypeString = TEXT("CharacteristicRead"); break;
			case EType::WriteCharacteristic: TypeString = TEXT("WriteCharacteristic"); break;
			case EType::CharacteristicWritten: TypeString = TEXT("CharacteristicWritten"); break;
			case EType::ReadDescriptor: TypeString = TEXT("ReadDescriptor"); break;
			case EType::DescriptorRead: TypeString = TEXT("DescriptorRead"); break;
			case EType::WriteDescriptor: TypeString = TEXT("WriteDescriptor"); break;
			case EType::DescriptorWritten: TypeString = TEXT("DescriptorWritten"); break;
			case EType::RequestCharacteristicNotifications: TypeString = TEXT("RequestCharacteristicNotifications"); break;
			case EType::ReceiveCharacteristicNotification: TypeString = TEXT("ReceiveCharacteristicNotification"); break;
			case EType::RequestConnectionPriority: TypeString = TEXT("RequestConnectionPriority"); break;
			case EType::ReceiveConnectionPriority: TypeString = TEXT("ReceiveConnectionPriority"); break;
			case EType::RequestMTU: TypeString = TEXT("RequestMTU"); break;
			case EType::ReceiveMTU: TypeString = TEXT("ReceiveMTU"); break;
			case EType::RequestAdapterName: TypeString = TEXT("RequestAdapterName"); break;
			case EType::ReceiveAdapterName: TypeString = TEXT("ReceiveAdapterName"); break;
			case EType::RequestAdapterState: TypeString = TEXT("RequestAdapterState"); break;
			case EType::ReceiveAdapterState: TypeString = TEXT("ReceiveAdapterState"); break;
			case EType::AdapterStateChanged: TypeString = TEXT("AdapterStateChanged"); break;
			case EType::Log: TypeString = TEXT("Log"); break;
		}

		return TypeString;
	}
};

#if WITH_MLSDK
class FBLERunnable : public FMagicLeapRunnable<FBLETask>
{
public:
	FBLERunnable();

private:
	bool TryInit();
	bool ProcessCurrentTask() override;
	void LogInfo(const FString& InLogMsg);
	void LogError(const FString& InLogMsg);

	bool StartScan_WorkerThread();
	bool StopScan_WorkerThread();
	bool GattConnect_WorkerThread();
	bool GattDisconnect_WorkerThread();
	bool GattReadRemoteRSSI_WorkerThread();
	bool GattGetAvailableServices_WorkerThread();
	bool GattReadCharacteristic_WorkerThread();
	bool GattWriteCharacteristic_WorkerThread();
	bool GattReadDescriptor_WorkerThread();
	bool GattWriteDescriptor_WorkerThread();
	bool GattSetCharacteristicNotification_WorkerThread();
	bool GattRequestConnectionPriority_WorkerThread();
	bool GattRequestMTU_WorkerThread();
	bool AdapterGetName_WorkerThread();
	bool AdapterGetState_WorkerThread();

	static void OnScanResult(MLBluetoothLeScanResult* InResult, void* InContext);
	static void OnGattConnectionStateChanged(MLBluetoothGattStatus Status, MLBluetoothGattConnectionState NewState, void* InContext);
	static void OnGattReadRemoteRSSI(int32_t RSSI, MLBluetoothGattStatus Status, void* InContext);
	static void OnGattServicesDiscovered(MLBluetoothGattStatus Status, void* InContext);
	static void OnGattCharacteristicRead(const MLBluetoothGattCharacteristic* Characteristic, MLBluetoothGattStatus Status, void* InContext);
	static void OnGattCharacteristicWrite(const MLBluetoothGattCharacteristic* Characteristic, MLBluetoothGattStatus Status, void* InContext);
	static void OnGattDescriptorRead(const MLBluetoothGattDescriptor* Descriptor, MLBluetoothGattStatus Status, void* InContext);
	static void OnGattDescriptorWrite(const MLBluetoothGattDescriptor* Descriptor, MLBluetoothGattStatus Status, void* InContext);
	static void OnGattNotify(const MLBluetoothGattCharacteristic* Characteristic, void* InContext);
	static void OnGattConnectionParametersUpdated(int32_t Interval, MLBluetoothGattStatus Status, void* InContext);
	static void OnGattMTUChanged(int32_t MTU, MLBluetoothGattStatus Status, void* InContext);
	static void OnAdapterStateChanged(MLBluetoothAdapterState State, void* InContext);
	static void OnAdapterBondStateChanged(const MLBluetoothDevice* Device, MLBluetoothBondState State, void* InContext);

	// converters
	static void MLToUEDevice(const MLBluetoothDevice& InMLDevice, FMagicLeapBluetoothDevice& OutUEDevice);
	static EMagicLeapBluetoothDeviceType MLToUEDeviceType(MLBluetoothDeviceType InMLDeviceType);
	static EMagicLeapBluetoothGattStatus MLToUEGattStatus(MLBluetoothGattStatus InMLGattStatus);
	static EMagicLeapBluetoothGattConnectionState MLToUEGattConnectionState(MLBluetoothGattConnectionState InMLGattConnectionState);
	static EMagicLeapBluetoothAdapterState MLToUEAdapterState(MLBluetoothAdapterState InMLBluetoothAdapterState);
	static EMagicLeapBluetoothBondState MLToUEBondState(MLBluetoothBondState InMLBluetoothBondState);
	static EMagicLeapBluetoothACLState MLToUEACLState(MLBluetoothAclState InMLBluetoothACLState);
	static MLBluetoothGattConnectionPriority UEToMLGattConnectionPriority(EMagicLeapBluetoothGattConnectionPriority InUEConnectionPriority);
	static void MLToUEGattIncludedService(const MLBluetoothGattIncludedService& InMLGattIncludedService, FMagicLeapBluetoothGattIncludedService& OutUEGattIncludedService);
	static void MLToUEGattCharacteristic(const MLBluetoothGattCharacteristic& InMLGattCharacteristic, FMagicLeapBluetoothGattCharacteristic& OutUEGattCharacteristic);
	bool UEToMLGattCharacteristic(const FMagicLeapBluetoothGattCharacteristic& InUEGattCharacteristic, MLBluetoothGattCharacteristic& OutMLGattCharacteristic);
	static void MLToUEGattDescriptor(const MLBluetoothGattDescriptor& InMLGattDescriptor, FMagicLeapBluetoothGattDescriptor& OutUEGattDescriptor);
	bool UEToMLGattDescriptor(const FMagicLeapBluetoothGattDescriptor& InUEGattDescriptor, MLBluetoothGattDescriptor& OutMLGattDescriptor);
	static FString GattStatusToString(EMagicLeapBluetoothGattStatus InStatus);
	static uint32 UEGattCharacteristicPermissionsToBitField(const TArray<EMagicLeapBluetoothGattCharacteristicPermissions>& InPermissions);
	static uint32 UEGattCharacteristicPropertiesToBitField(const TArray<EMagicLeapBluetoothGattCharacteristicProperties>& InProperties);
	static uint32 UEGattCharacteristicWriteTypesToBitField(const TArray< EMagicLeapBluetoothGattCharacteristicWriteTypes>& InWriteTypes);
	static void BitFieldToUEGattCharacteristicPermissions(uint32 InBitField, TArray<EMagicLeapBluetoothGattCharacteristicPermissions>& OutPermissions);
	static void BitFieldToUEGattCharacteristicProperties(uint32 InBitField, TArray<EMagicLeapBluetoothGattCharacteristicProperties>& OutProperties);
	static void BitFieldToUEGattCharacteristicWriteTypes(uint32 InBitField, TArray< EMagicLeapBluetoothGattCharacteristicWriteTypes>& OutWriteTypes);
	static const TCHAR* MLBluetoothGattStatusToString(MLBluetoothGattStatus InStatus);

	bool bInitialized;
	FThreadSafeBool bRequestedConnection;
	MLBluetoothLeScannerCallbacks ScannerCallbacks;
	MLBluetoothLeGattClientCallbacks GattCallbacks;
	MLBluetoothAdapterCallbacks AdapterCallbacks;
	static TArray<MLBluetoothGattDescriptorNode> MLDescriptorNodePool;
};
#else
class FBLERunnable {};
#endif // WITH_MLSDK
