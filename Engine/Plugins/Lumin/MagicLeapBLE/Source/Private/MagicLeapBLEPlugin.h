// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IMagicLeapPlugin.h"
#include "IMagicLeapBLEPlugin.h"
#include "MagicLeapBLERunnable.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapBLE, Verbose, All);

class FMagicLeapBLEPlugin : public IMagicLeapBLEPlugin
{
public:
	FMagicLeapBLEPlugin();
	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime);

	void SetLogDelegate(const FMagicLeapBLEOnLogDelegate& ResultDelegate) override;
	void StartScanAsync(const FMagicLeapBLEOnFoundDeviceDelegate& ResultDelegate) override;
	void StopScan() override;
	void GattConnectAsync(const FString& InAddress, const FMagicLeapBLEOnConnStateChangedDelegate& ResultDelegate) override;
	void GattDisconnectAsync(const FMagicLeapBLEOnConnStateChangedDelegate& ResultDelegate) override;
	void GattReadRemoteRSSIAsync(const FMagicLeapBLEOnReadRemoteRSSIDelegate& ResultDelegate) override;
	void GattGetAvailableServicesAsync(const FMagicLeapBLEOnGotAvailableServicesDelegate& ResultDelegate) override;
	void GattReadCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, const FMagicLeapBLEOnReadCharacteristicDelegate& ResultDelegate) override;
	void GattWriteCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, const FMagicLeapBLEOnWriteCharacteristicDelegate& ResultDelegate) override;
	void GattReadDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor, const FMagicLeapBLEOnReadDescriptorDelegate& ResultDelegate) override;
	void GattWriteDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor, const FMagicLeapBLEOnWriteDescriptorDelegate& ResultDelegate) override;
	void GattSetCharacteristicNotificationAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, bool bEnable, const FMagicLeapBLEOnCharacteristicChangedDelegate& ResultDelegate) override;
	void GattRequestConnectionPriorityAsync(EMagicLeapBluetoothGattConnectionPriority InPriority, const FMagicLeapBLEOnConnPriorityChangedDelegate& ResultDelegate) override;
	void GattRequestMTUAsync(int32 MTU, const FMagicLeapBLEOnMTUChangedDelegate& ResultDelegate) override;
	void AdapterGetNameAsync(const FMagicLeapBLEOnGotAdapterNameDelegate& ResultDelegate) override;
	void AdapterGetStateAsync(const FMagicLeapBLEOnGotAdapterStateDelegate& ResultDelegate) override;
	void RegisterForAdapterStateChangeNotifications(const FMagicLeapBLEOnAdapterStateChangedDelegate& ResultDelegate);

private:
	EMagicLeapBluetoothGattConnectionState ConnectionState;
	FString CurrConnectionAddress;
	FBLETask::EType CurrAtomicTaskType;
	FBLERunnable* Runnable;
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	FMagicLeapBLEOnLogDelegate OnLogDelegate;
	FMagicLeapBLEOnFoundDeviceDelegate OnFoundDeviceDelegate;
	FMagicLeapBLEOnConnStateChangedDelegate OnConnStateChangedDelegate;
	FMagicLeapBLEOnReadRemoteRSSIDelegate OnReadRemoteRSSIDelegate;
	FMagicLeapBLEOnGotAvailableServicesDelegate OnGotAvailableServicesDelegate;
	FMagicLeapBLEOnReadCharacteristicDelegate OnReadCharacteristicDelegate;
	FMagicLeapBLEOnWriteCharacteristicDelegate OnWriteCharacteristicDelegate;
	FMagicLeapBLEOnReadDescriptorDelegate OnReadDescriptorDelegate;
	FMagicLeapBLEOnWriteDescriptorDelegate OnWriteDescriptorDelegate;
	FMagicLeapBLEOnCharacteristicChangedDelegate OnCharacteristicChangedDelegate;
	FMagicLeapBLEOnConnPriorityChangedDelegate OnConnPriorityChangedDelegate;
	FMagicLeapBLEOnMTUChangedDelegate OnMTUChangedDelegate;
	FMagicLeapBLEOnGotAdapterNameDelegate OnGotAdapterNameDelegate;
	FMagicLeapBLEOnGotAdapterStateDelegate OnGotAdapterStateDelegate;
	FMagicLeapBLEOnAdapterStateChangedDelegate OnAdapterStateChangedDelegate;

	FMagicLeapResult TryPushNewTask(const FBLETask& InTask);
};

inline FMagicLeapBLEPlugin& GetMagicLeapBLEPlugin()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapBLEPlugin>("MagicLeapBLE");
}
