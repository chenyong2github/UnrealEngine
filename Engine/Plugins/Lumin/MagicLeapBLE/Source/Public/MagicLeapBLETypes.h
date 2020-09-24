// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapTypes.h"
#include "MagicLeapBLETypes.generated.h"

/**
	Status values of GATT operations.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothGattStatus : uint8
{
	/** An operation is completed successfully. */
	Success,
	/** GATT read operation is not permitted. */
	ReadNotPermitted,
	/** GATT write operation is not permitted. */
	WriteNotPermitted,
	/** Insufficient authentication for a given operation. */
	InsufficientAuthentication,
	/** The given request is not supported. */
	RequestNot_Supported,
	/** A read or write operation was requested with an invalid offset. */
	InvalidOffset,
	/** A write operation exceeds the maximum length of the attribute. */
	InvalidAttributeLength,
	/** Insufficient encryption for a given operation. */
	InsufficientEncryption,
	/** A remote device connection is congested. */
	ConnectionCongested,
	/** Generic error. */
	Error,
	/** An operation failed. */
	Failure,
};

/**
	Definition of characteristic properties.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothGattCharacteristicPermissions : uint8
{
	Read,
	ReadEncrypted,
	ReadEncryptedMITM,
	Write,
	WriteEncrypted,
	WriteEncryptedMITM,
	WriteSigned,
	WriteSignedMITM,
};

/**
	Definition of characteristic permissions.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothGattCharacteristicProperties : uint8
{
	Broadcast,
	Read,
	WriteNoRes,
	Write,
	Notify,
	Indicate,
	SignedWrite,
	ExtProps,
};

/**
	Definition of characteristic write types.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothGattCharacteristicWriteTypes : uint8
{
	NoResponse,
	Default,
	Signed,
};

/**
	GATT Connection states.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothGattConnectionState : uint8
{
	NotConnected,
	Connecting,
	Connected,
	Disconnecting
};

/**
	Connection interval.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothGattConnectionPriority : uint8
{
	Balanced,
	High,
	LowPower,
};

/**
	Bluetooth adapter states.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothAdapterState : uint8
{
	Off,
	On,
};

/**
	Bond states.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothBondState : uint8
{
	None,
	Bonding,
	Bonded,
};

/**
	Bond types.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothBondType : uint8
{
	ClassicPin,
	SspJustWorks,
	SspNumericalComparison,
	SspPasskeyEntry,
	SspPasskeyNotification,
};

/**
	ACL states.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothACLState : uint8
{
	Connected,
	Disconnected,
};

/** 
	Device types.
*/
UENUM(BlueprintType)
enum class EMagicLeapBluetoothDeviceType : uint8
{
	Unknown = 0,
	LE = 2,
};

/** 
	A structure containing the result of BLE scanning.
*/
USTRUCT(BlueprintType)
struct MAGICLEAPBLE_API FMagicLeapBluetoothDevice
{
	GENERATED_BODY()

	/** Version of this structure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bluetooth Device | MagicLeap")
	int32 Version;
	/** Bluetooth device name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bluetooth Device | MagicLeap")
	FString Name;
	/** Bluetooth device address. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bluetooth Device | MagicLeap")
	FString Address;
	/** The RSSI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bluetooth Device | MagicLeap")
	uint8 RSSI;
	/** Bluetooth device type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bluetooth Device | MagicLeap")
	EMagicLeapBluetoothDeviceType Type;
};

/**
	A structure containing the contents of a GATT descriptor.
*/
USTRUCT(BlueprintType)
struct MAGICLEAPBLE_API FMagicLeapBluetoothGattDescriptor
{
	GENERATED_BODY()

	/** Version of this structure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	int32 Version;

	/** UUID for the descriptor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	FString UUID;

	/** Instance ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	int32 InstanceId;

	/** Permissions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	TArray<EMagicLeapBluetoothGattCharacteristicPermissions> Permissions;

	/** Value for the descriptor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	TArray<uint8> Value;
};

/**
	A structure containing the contents of a GATT characteristic.
*/
USTRUCT(BlueprintType)
struct MAGICLEAPBLE_API FMagicLeapBluetoothGattCharacteristic
{
	GENERATED_BODY()

	/** Version of this structure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	int32 Version;

	/** UUID for the characteristic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	FString UUID;

	/** Instance ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	int32 InstanceId;

	/** Permission. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	TArray<EMagicLeapBluetoothGattCharacteristicPermissions> Permissions;

	/** Properties. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	TArray<EMagicLeapBluetoothGattCharacteristicProperties> Properties;

	/** Write type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	TArray<EMagicLeapBluetoothGattCharacteristicWriteTypes> WriteTypes;

	/** Value for the characteristic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	TArray<uint8> Value;

	/** A list of desrcriptors for this characteristic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	TArray<FMagicLeapBluetoothGattDescriptor> Descriptors;
};

/**
	A structure containing the contents of a GATT include.
*/
USTRUCT(BlueprintType)
struct MAGICLEAPBLE_API FMagicLeapBluetoothGattIncludedService
{
	GENERATED_BODY()

	/** UUID for the service. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	FString UUID;

	/** Instance ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	int32 InstanceId;

	/** Service type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	int32 ServiceType;
};

/**
	A structure containing the contents of a GATT service.
*/
USTRUCT(BlueprintType)
struct MAGICLEAPBLE_API FMagicLeapBluetoothGattService
{
	GENERATED_BODY()

	/** Instance ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	int32 InstanceId;

	/** Service type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	int32 ServiceType;

	/** UUID for the service. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	FString UUID;

	/** A list of included services provided by this service. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	TArray<FMagicLeapBluetoothGattIncludedService> IncludedServices;

	/** A list of characteristics contained in this service. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BLE|MagicLeap")
	TArray<FMagicLeapBluetoothGattCharacteristic> Characteristics;
};

DECLARE_DELEGATE_OneParam(FMagicLeapBLEOnLogDelegateStatic, const FString&);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapBLEOnLogDelegateDynamic, const FString&, LogMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapBLEOnLogDelegateMulti, const FString&, LogMessage);

DECLARE_DELEGATE_TwoParams(FMagicLeapBLEOnFoundDeviceDelegateStatic, const FMagicLeapBluetoothDevice&, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapBLEOnFoundDeviceDelegateDynamic, const FMagicLeapBluetoothDevice&, BLEDevice, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapBLEOnFoundDeviceDelegateMulti, const FMagicLeapBluetoothDevice&, BLEDevice, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_TwoParams(FMagicLeapBLEOnConnStateChangedDelegateStatic, EMagicLeapBluetoothGattConnectionState, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapBLEOnConnStateChangedDelegateDynamic, EMagicLeapBluetoothGattConnectionState, ConnectionState, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapBLEOnConnStateChangedDelegateMulti, EMagicLeapBluetoothGattConnectionState, ConnectionState, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_ThreeParams(FMagicLeapBLEOnReadRemoteRSSIDelegateStatic, int32, EMagicLeapBluetoothGattStatus, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapBLEOnReadRemoteRSSIDelegateDynamic, int32, RSSI, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapBLEOnReadRemoteRSSIDelegateMulti, int32, RSSI, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_TwoParams(FMagicLeapBLEOnGotAvailableServicesDelegateStatic, const TArray<FMagicLeapBluetoothGattService>&, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapBLEOnGotAvailableServicesDelegateDynamic, const TArray<FMagicLeapBluetoothGattService>&, AvailableServices, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapBLEOnGotAvailableServicesDelegateMulti, const TArray<FMagicLeapBluetoothGattService>&, AvailableServices, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_ThreeParams(FMagicLeapBLEOnReadCharacteristicDelegateStatic, const FMagicLeapBluetoothGattCharacteristic&, EMagicLeapBluetoothGattStatus, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapBLEOnReadCharacteristicDelegateDynamic, const FMagicLeapBluetoothGattCharacteristic&, Characteristic, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapBLEOnReadCharacteristicDelegateMulti, const FMagicLeapBluetoothGattCharacteristic&, Characteristic, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_ThreeParams(FMagicLeapBLEOnWriteCharacteristicDelegateStatic, const FMagicLeapBluetoothGattCharacteristic&, EMagicLeapBluetoothGattStatus, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapBLEOnWriteCharacteristicDelegateDynamic, const FMagicLeapBluetoothGattCharacteristic&, Characteristic, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapBLEOnWriteCharacteristicDelegateMulti, const FMagicLeapBluetoothGattCharacteristic&, Characteristic, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_ThreeParams(FMagicLeapBLEOnReadDescriptorDelegateStatic, const FMagicLeapBluetoothGattDescriptor&, EMagicLeapBluetoothGattStatus, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapBLEOnReadDescriptorDelegateDynamic, const FMagicLeapBluetoothGattDescriptor&, Descriptor, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapBLEOnReadDescriptorDelegateMulti, const FMagicLeapBluetoothGattDescriptor&, Descriptor, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_ThreeParams(FMagicLeapBLEOnWriteDescriptorDelegateStatic, const FMagicLeapBluetoothGattDescriptor&, EMagicLeapBluetoothGattStatus, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapBLEOnWriteDescriptorDelegateDynamic, const FMagicLeapBluetoothGattDescriptor&, Descriptor, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapBLEOnWriteDescriptorDelegateMulti, const FMagicLeapBluetoothGattDescriptor&, Descriptor, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_TwoParams(FMagicLeapBLEOnCharacteristicChangedDelegateStatic, const FMagicLeapBluetoothGattCharacteristic&, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapBLEOnCharacteristicChangedDelegateDynamic, const FMagicLeapBluetoothGattCharacteristic&, Characteristic, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapBLEOnCharacteristicChangedDelegateMulti, const FMagicLeapBluetoothGattCharacteristic&, Characteristic, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_ThreeParams(FMagicLeapBLEOnConnPriorityChangedDelegateStatic, int32, EMagicLeapBluetoothGattStatus, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapBLEOnConnPriorityChangedDelegateDynamic, int32, Interval, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapBLEOnConnPriorityChangedDelegateMulti, int32, Interval, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_ThreeParams(FMagicLeapBLEOnMTUChangedDelegateStatic, int32, EMagicLeapBluetoothGattStatus, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapBLEOnMTUChangedDelegateDynamic, int32, NewMTU, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapBLEOnMTUChangedDelegateMulti, int32, NewMTU, EMagicLeapBluetoothGattStatus, Status, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_TwoParams(FMagicLeapBLEOnGotAdapterNameDelegateStatic, const FString&, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapBLEOnGotAdapterNameDelegateDynamic, const FString&, Name, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapBLEOnGotAdapterNameDelegateMulti, const FString&, Name, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_TwoParams(FMagicLeapBLEOnGotAdapterStateDelegateStatic, EMagicLeapBluetoothAdapterState, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapBLEOnGotAdapterStateDelegateDynamic, EMagicLeapBluetoothAdapterState, NewState, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapBLEOnGotAdapterStateDelegateMulti, EMagicLeapBluetoothAdapterState, NewState, const FMagicLeapResult&, Result);

DECLARE_DELEGATE_TwoParams(FMagicLeapBLEOnAdapterStateChangedDelegateStatic, EMagicLeapBluetoothAdapterState, const FMagicLeapResult&);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapBLEOnAdapterStateChangedDelegateDynamic, EMagicLeapBluetoothAdapterState, NewState, const FMagicLeapResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapBLEOnAdapterStateChangedDelegateMulti, EMagicLeapBluetoothAdapterState, NewState, const FMagicLeapResult&, Result);

typedef TMLMultiDelegateOneParam<FMagicLeapBLEOnLogDelegateStatic, FMagicLeapBLEOnLogDelegateDynamic, FMagicLeapBLEOnLogDelegateMulti, FString> FMagicLeapBLEOnLogDelegate;
typedef TMLMultiDelegateTwoParams<FMagicLeapBLEOnFoundDeviceDelegateStatic, FMagicLeapBLEOnFoundDeviceDelegateDynamic, FMagicLeapBLEOnFoundDeviceDelegateMulti, FMagicLeapBluetoothDevice, FMagicLeapResult> FMagicLeapBLEOnFoundDeviceDelegate;
typedef TMLMultiDelegateTwoParams<FMagicLeapBLEOnConnStateChangedDelegateStatic, FMagicLeapBLEOnConnStateChangedDelegateDynamic, FMagicLeapBLEOnConnStateChangedDelegateMulti, EMagicLeapBluetoothGattConnectionState, FMagicLeapResult> FMagicLeapBLEOnConnStateChangedDelegate;
typedef TMLMultiDelegateThreeParams<FMagicLeapBLEOnReadRemoteRSSIDelegateStatic, FMagicLeapBLEOnReadRemoteRSSIDelegateDynamic, FMagicLeapBLEOnReadRemoteRSSIDelegateMulti, int32, EMagicLeapBluetoothGattStatus, FMagicLeapResult> FMagicLeapBLEOnReadRemoteRSSIDelegate;
typedef TMLMultiDelegateTwoParams<FMagicLeapBLEOnGotAvailableServicesDelegateStatic, FMagicLeapBLEOnGotAvailableServicesDelegateDynamic, FMagicLeapBLEOnGotAvailableServicesDelegateMulti, TArray<FMagicLeapBluetoothGattService>, FMagicLeapResult> FMagicLeapBLEOnGotAvailableServicesDelegate;
typedef TMLMultiDelegateThreeParams<FMagicLeapBLEOnReadCharacteristicDelegateStatic, FMagicLeapBLEOnReadCharacteristicDelegateDynamic, FMagicLeapBLEOnReadCharacteristicDelegateMulti, FMagicLeapBluetoothGattCharacteristic, EMagicLeapBluetoothGattStatus, FMagicLeapResult> FMagicLeapBLEOnReadCharacteristicDelegate;
typedef TMLMultiDelegateThreeParams<FMagicLeapBLEOnWriteCharacteristicDelegateStatic, FMagicLeapBLEOnWriteCharacteristicDelegateDynamic, FMagicLeapBLEOnWriteCharacteristicDelegateMulti, FMagicLeapBluetoothGattCharacteristic, EMagicLeapBluetoothGattStatus, FMagicLeapResult> FMagicLeapBLEOnWriteCharacteristicDelegate;
typedef TMLMultiDelegateThreeParams<FMagicLeapBLEOnReadDescriptorDelegateStatic, FMagicLeapBLEOnReadDescriptorDelegateDynamic, FMagicLeapBLEOnReadDescriptorDelegateMulti, FMagicLeapBluetoothGattDescriptor, EMagicLeapBluetoothGattStatus, FMagicLeapResult> FMagicLeapBLEOnReadDescriptorDelegate;
typedef TMLMultiDelegateThreeParams<FMagicLeapBLEOnWriteDescriptorDelegateStatic, FMagicLeapBLEOnWriteDescriptorDelegateDynamic, FMagicLeapBLEOnWriteDescriptorDelegateMulti, FMagicLeapBluetoothGattDescriptor, EMagicLeapBluetoothGattStatus, FMagicLeapResult> FMagicLeapBLEOnWriteDescriptorDelegate;
typedef TMLMultiDelegateTwoParams<FMagicLeapBLEOnCharacteristicChangedDelegateStatic, FMagicLeapBLEOnCharacteristicChangedDelegateDynamic, FMagicLeapBLEOnCharacteristicChangedDelegateMulti, FMagicLeapBluetoothGattCharacteristic, FMagicLeapResult> FMagicLeapBLEOnCharacteristicChangedDelegate;
typedef TMLMultiDelegateThreeParams<FMagicLeapBLEOnConnPriorityChangedDelegateStatic, FMagicLeapBLEOnConnPriorityChangedDelegateDynamic, FMagicLeapBLEOnConnPriorityChangedDelegateMulti, int32, EMagicLeapBluetoothGattStatus, FMagicLeapResult> FMagicLeapBLEOnConnPriorityChangedDelegate;
typedef TMLMultiDelegateThreeParams<FMagicLeapBLEOnMTUChangedDelegateStatic, FMagicLeapBLEOnMTUChangedDelegateDynamic, FMagicLeapBLEOnMTUChangedDelegateMulti, int32, EMagicLeapBluetoothGattStatus, FMagicLeapResult> FMagicLeapBLEOnMTUChangedDelegate;
typedef TMLMultiDelegateTwoParams<FMagicLeapBLEOnGotAdapterNameDelegateStatic, FMagicLeapBLEOnGotAdapterNameDelegateDynamic, FMagicLeapBLEOnGotAdapterNameDelegateMulti, FString, FMagicLeapResult> FMagicLeapBLEOnGotAdapterNameDelegate;
typedef TMLMultiDelegateTwoParams<FMagicLeapBLEOnGotAdapterStateDelegateStatic, FMagicLeapBLEOnGotAdapterStateDelegateDynamic, FMagicLeapBLEOnGotAdapterStateDelegateMulti, EMagicLeapBluetoothAdapterState, FMagicLeapResult> FMagicLeapBLEOnGotAdapterStateDelegate;
typedef TMLMultiDelegateTwoParams<FMagicLeapBLEOnAdapterStateChangedDelegateStatic, FMagicLeapBLEOnAdapterStateChangedDelegateDynamic, FMagicLeapBLEOnAdapterStateChangedDelegateMulti, EMagicLeapBluetoothAdapterState, FMagicLeapResult> FMagicLeapBLEOnAdapterStateChangedDelegate;
