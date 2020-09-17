// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "MagicLeapBLETypes.h"
#include "MagicLeapBLEComponent.generated.h"

/**
	Component that provides access to the BLE API functionality.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPBLE_API UMagicLeapBLEComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
		Register's this components log delegate with the BLE plugin.
	*/
	void BeginPlay() override;

	/**
		Starts Bluetooth LE scan.  The results will be delivered through scanner callback.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void StartScanAsync();

	/**
		Stops Bluetooth LE scan in progress.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void StopScan();

	/** 
		Initiates a connection to a Bluetooth GATT capable device. 
		@param InAddress The address to connect to.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattConnectAsync(const FString& InAddress);

	/**
		Disconnects an established connection, or cancels a connection attempt.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattDisconnectAsync();

	/**
		Reads the RSSI for a connected remote device.  The on_gatt_read_remote_rssi callback will be invoked when the RSSI value
		has been read.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattReadRemoteRSSIAsync();

	/** 
		Gets a list of GATT services offered by the remote devices.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattGetAvailableServicesAsync();

	/** 
		Reads the requested characteristic from the connected remote device.
		@param InCharacteristic The characteristic to read from the connected device.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattReadCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic);

	/**
		Writes a given characteristic and its value to the connected remote device.
		@param InCharacteristic The characteristic to be written to the connected device.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattWriteCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic);

	/**
		Reads the requested descriptor from the connected remote device.
		@param InDescriptor The descriptor to be read from the connected device.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattReadDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor);

	/**
		Writes the value of a given descriptor to the connected device.
		@param InDescriptor The descriptor to be written to the connected device.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattWriteDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor);

	/**
		Enables or disables notifications/indications for a given chracteristic.
		@param InCharacteristic The characteristic to receive change notifications from on the remote device.
		@param bEnable Enables/Disables the notifications for InCharacteristic.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattSetCharacteristicNotificationAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, bool bEnable);

	/**
		Requests a connection parameter update.
		@param InPriority The new connection priority for the connected device.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattRequestConnectionPriorityAsync(EMagicLeapBluetoothGattConnectionPriority InPriority);

	/**
		Requests to change MTU size.
		@param MTU The new MTU for the connected device.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void GattRequestMTUAsync(int32 MTU);

	/**
		Requests the name of the local Bluetooth adapter.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void AdapterGetNameAsync();

	/**
		Requests the state of the local Bluetooth adpater.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void AdapterGetStateAsync();

	/**
		Requests adapter state changes to be relayed to the calling app.
	*/
	UFUNCTION(BlueprintCallable, Category = "BLE | MagicLeap")
	void RegisterForAdapterStateChangeNotifications();

private:
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnLogDelegateMulti OnLogDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnFoundDeviceDelegateMulti OnFoundDeviceDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnConnStateChangedDelegateMulti OnConnStateChangedDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnReadRemoteRSSIDelegateMulti OnReadRemoteRSSIDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnGotAvailableServicesDelegateMulti OnGotAvailableServicesDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnReadCharacteristicDelegateMulti OnReadCharacteristicDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnWriteCharacteristicDelegateMulti OnWriteCharacteristicDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnReadDescriptorDelegateMulti OnReadDescriptorDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnWriteDescriptorDelegateMulti OnWriteDescriptorDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnCharacteristicChangedDelegateMulti OnCharacteristicChangedDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnConnPriorityChangedDelegateMulti OnConnPriorityChangedDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnMTUChangedDelegateMulti OnMTUChangedDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnGotAdapterNameDelegateMulti OnGotAdapterNameDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnGotAdapterStateDelegateMulti OnGotAdapterStateDelegate;
	UPROPERTY(BlueprintAssignable, Category = "BLE | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapBLEOnAdapterStateChangedDelegateMulti OnAdapterStateChangedDelegate;
};
