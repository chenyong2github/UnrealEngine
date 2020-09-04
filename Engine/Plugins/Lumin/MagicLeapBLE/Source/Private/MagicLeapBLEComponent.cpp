// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapBLEComponent.h"
#include "MagicLeapBLEPlugin.h"

void UMagicLeapBLEComponent::BeginPlay()
{
	Super::BeginPlay();
	GetMagicLeapBLEPlugin().SetLogDelegate(FMagicLeapBLEOnLogDelegate(OnLogDelegate));
}

void UMagicLeapBLEComponent::StartScanAsync()
{
	GetMagicLeapBLEPlugin().StartScanAsync(FMagicLeapBLEOnFoundDeviceDelegate(OnFoundDeviceDelegate));
}

void UMagicLeapBLEComponent::StopScan()
{
	GetMagicLeapBLEPlugin().StopScan();
}

void UMagicLeapBLEComponent::GattConnectAsync(const FString& InAddress)
{
	GetMagicLeapBLEPlugin().GattConnectAsync(InAddress, FMagicLeapBLEOnConnStateChangedDelegate(OnConnStateChangedDelegate));
}

void UMagicLeapBLEComponent::GattDisconnectAsync()
{
	GetMagicLeapBLEPlugin().GattDisconnectAsync(FMagicLeapBLEOnConnStateChangedDelegate(OnConnStateChangedDelegate));
}

void UMagicLeapBLEComponent::GattReadRemoteRSSIAsync()
{
	GetMagicLeapBLEPlugin().GattReadRemoteRSSIAsync(FMagicLeapBLEOnReadRemoteRSSIDelegate(OnReadRemoteRSSIDelegate));
}

void UMagicLeapBLEComponent::GattGetAvailableServicesAsync()
{
	GetMagicLeapBLEPlugin().GattGetAvailableServicesAsync(FMagicLeapBLEOnGotAvailableServicesDelegate(OnGotAvailableServicesDelegate));
}

void UMagicLeapBLEComponent::GattReadCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic)
{
	GetMagicLeapBLEPlugin().GattReadCharacteristicAsync(InCharacteristic, FMagicLeapBLEOnReadCharacteristicDelegate(OnReadCharacteristicDelegate));
}

void UMagicLeapBLEComponent::GattWriteCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic)
{
	GetMagicLeapBLEPlugin().GattWriteCharacteristicAsync(InCharacteristic, FMagicLeapBLEOnWriteCharacteristicDelegate(OnWriteCharacteristicDelegate));
}

void UMagicLeapBLEComponent::GattReadDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor)
{
	GetMagicLeapBLEPlugin().GattReadDescriptorAsync(InDescriptor, FMagicLeapBLEOnReadDescriptorDelegate(OnReadDescriptorDelegate));
}

void UMagicLeapBLEComponent::GattWriteDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor)
{
	GetMagicLeapBLEPlugin().GattWriteDescriptorAsync(InDescriptor, FMagicLeapBLEOnWriteDescriptorDelegate(OnWriteDescriptorDelegate));
}

void UMagicLeapBLEComponent::GattSetCharacteristicNotificationAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, bool bEnable)
{
	GetMagicLeapBLEPlugin().GattSetCharacteristicNotificationAsync(InCharacteristic, bEnable, FMagicLeapBLEOnCharacteristicChangedDelegate(OnCharacteristicChangedDelegate));
}

void UMagicLeapBLEComponent::GattRequestConnectionPriorityAsync(EMagicLeapBluetoothGattConnectionPriority InPriority)
{
	GetMagicLeapBLEPlugin().GattRequestConnectionPriorityAsync(InPriority, FMagicLeapBLEOnConnPriorityChangedDelegate(OnConnPriorityChangedDelegate));
}

void UMagicLeapBLEComponent::GattRequestMTUAsync(int32 MTU)
{
	GetMagicLeapBLEPlugin().GattRequestMTUAsync(MTU, FMagicLeapBLEOnMTUChangedDelegate(OnMTUChangedDelegate));
}

void UMagicLeapBLEComponent::AdapterGetNameAsync()
{
	GetMagicLeapBLEPlugin().AdapterGetNameAsync(FMagicLeapBLEOnGotAdapterNameDelegate(OnGotAdapterNameDelegate));
}

void UMagicLeapBLEComponent::AdapterGetStateAsync()
{
	GetMagicLeapBLEPlugin().AdapterGetStateAsync(FMagicLeapBLEOnGotAdapterStateDelegate(OnGotAdapterStateDelegate));
}

void UMagicLeapBLEComponent::RegisterForAdapterStateChangeNotifications()
{
	GetMagicLeapBLEPlugin().RegisterForAdapterStateChangeNotifications(FMagicLeapBLEOnAdapterStateChangedDelegate(OnAdapterStateChangedDelegate));
}
