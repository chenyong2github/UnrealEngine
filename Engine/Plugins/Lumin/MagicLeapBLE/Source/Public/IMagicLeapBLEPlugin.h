// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "MagicLeapBLETypes.h"

/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules
 * within this plugin.
 */
class IMagicLeapBLEPlugin : public IModuleInterface
{
public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMagicLeapBLEPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked<IMagicLeapBLEPlugin>("MagicLeapBLE");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MagicLeapBLE");
	}

	virtual void SetLogDelegate(const FMagicLeapBLEOnLogDelegate& ResultDelegate) = 0;
	virtual void StartScanAsync(const FMagicLeapBLEOnFoundDeviceDelegate& ResultDelegate) = 0;
	virtual void StopScan() = 0;
	virtual void GattConnectAsync(const FString& InAddress, const FMagicLeapBLEOnConnStateChangedDelegate& ResultDelegate) = 0;
	virtual void GattDisconnectAsync(const FMagicLeapBLEOnConnStateChangedDelegate& ResultDelegate) = 0;
	virtual void GattReadRemoteRSSIAsync(const FMagicLeapBLEOnReadRemoteRSSIDelegate& ResultDelegate) = 0;
	virtual void GattGetAvailableServicesAsync(const FMagicLeapBLEOnGotAvailableServicesDelegate& ResultDelegate) = 0;
	virtual void GattReadCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, const FMagicLeapBLEOnReadCharacteristicDelegate& ResultDelegate) = 0;
	virtual void GattWriteCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, const FMagicLeapBLEOnWriteCharacteristicDelegate& ResultDelegate) = 0;
	virtual void GattReadDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor, const FMagicLeapBLEOnReadDescriptorDelegate& ResultDelegate) = 0;
	virtual void GattWriteDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor, const FMagicLeapBLEOnWriteDescriptorDelegate& ResultDelegate) = 0;
	virtual void GattSetCharacteristicNotificationAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, bool bEnable, const FMagicLeapBLEOnCharacteristicChangedDelegate& ResultDelegate) = 0;
	virtual void GattRequestConnectionPriorityAsync(EMagicLeapBluetoothGattConnectionPriority InPriority, const FMagicLeapBLEOnConnPriorityChangedDelegate& ResultDelegate) = 0;
	virtual void GattRequestMTUAsync(int32 MTU, const FMagicLeapBLEOnMTUChangedDelegate& ResultDelegate) = 0;
	virtual void AdapterGetNameAsync(const FMagicLeapBLEOnGotAdapterNameDelegate& ResultDelegate) = 0;
	virtual void AdapterGetStateAsync(const FMagicLeapBLEOnGotAdapterStateDelegate& ResultDelegate) = 0;
};
