// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_bluetooth.h>
#include <ml_bluetooth_adapter.h>
#include <ml_bluetooth_gatt.h>
#include <ml_bluetooth_le.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

	CREATE_FUNCTION_SHIM(ml_bluetooth_le, MLResult, MLBluetoothLeSetScannerCallbacks)
#define MLBluetoothLeSetScannerCallbacks ::LUMIN_MLSDK_API::MLBluetoothLeSetScannerCallbacksShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_le, MLResult, MLBluetoothLeStartScan)
#define MLBluetoothLeStartScan ::LUMIN_MLSDK_API::MLBluetoothLeStartScanShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_le, MLResult, MLBluetoothLeStopScan)
#define MLBluetoothLeStopScan ::LUMIN_MLSDK_API::MLBluetoothLeStopScanShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_le, const char*, MLBluetoothLeGetResultString)
#define MLBluetoothLeGetResultString ::LUMIN_MLSDK_API::MLBluetoothLeGetResultStringShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattConnect)
#define MLBluetoothGattConnect ::LUMIN_MLSDK_API::MLBluetoothGattConnectShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattDisconnect)
#define MLBluetoothGattDisconnect ::LUMIN_MLSDK_API::MLBluetoothGattDisconnectShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattDiscoverServices)
#define MLBluetoothGattDiscoverServices ::LUMIN_MLSDK_API::MLBluetoothGattDiscoverServicesShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattReadRemoteRssi)
#define MLBluetoothGattReadRemoteRssi ::LUMIN_MLSDK_API::MLBluetoothGattReadRemoteRssiShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattGetServiceRecord)
#define MLBluetoothGattGetServiceRecord ::LUMIN_MLSDK_API::MLBluetoothGattGetServiceRecordShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattReleaseServiceList)
#define MLBluetoothGattReleaseServiceList ::LUMIN_MLSDK_API::MLBluetoothGattReleaseServiceListShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattReadCharacteristic)
#define MLBluetoothGattReadCharacteristic ::LUMIN_MLSDK_API::MLBluetoothGattReadCharacteristicShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattWriteCharacteristic)
#define MLBluetoothGattWriteCharacteristic ::LUMIN_MLSDK_API::MLBluetoothGattWriteCharacteristicShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattReadDescriptor)
#define MLBluetoothGattReadDescriptor ::LUMIN_MLSDK_API::MLBluetoothGattReadDescriptorShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattWriteDescriptor)
#define MLBluetoothGattWriteDescriptor ::LUMIN_MLSDK_API::MLBluetoothGattWriteDescriptorShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattSetCharacteristicNotification)
#define MLBluetoothGattSetCharacteristicNotification ::LUMIN_MLSDK_API::MLBluetoothGattSetCharacteristicNotificationShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattRequestConnectionPriority)
#define MLBluetoothGattRequestConnectionPriority ::LUMIN_MLSDK_API::MLBluetoothGattRequestConnectionPriorityShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattRequestMtu)
#define MLBluetoothGattRequestMtu ::LUMIN_MLSDK_API::MLBluetoothGattRequestMtuShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, MLResult, MLBluetoothGattSetClientCallbacks)
#define MLBluetoothGattSetClientCallbacks ::LUMIN_MLSDK_API::MLBluetoothGattSetClientCallbacksShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_gatt, const char*, MLBluetoothGattGetResultString)
#define MLBluetoothGattGetResultString ::LUMIN_MLSDK_API::MLBluetoothGattGetResultStringShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_adapter, MLResult, MLBluetoothAdapterGetName)
#define MLBluetoothAdapterGetName ::LUMIN_MLSDK_API::MLBluetoothAdapterGetNameShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_adapter, MLResult, MLBluetoothAdapterGetState)
#define MLBluetoothAdapterGetState ::LUMIN_MLSDK_API::MLBluetoothAdapterGetStateShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_adapter, MLResult, MLBluetoothAdapterCreateBond)
#define MLBluetoothAdapterCreateBond ::LUMIN_MLSDK_API::MLBluetoothAdapterCreateBondShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_adapter, MLResult, MLBluetoothAdapterSetCallbacks)
#define MLBluetoothAdapterSetCallbacks ::LUMIN_MLSDK_API::MLBluetoothAdapterSetCallbacksShim
		CREATE_FUNCTION_SHIM(ml_bluetooth_adapter, const char*, MLBluetoothAdapterGetResultString)
#define MLBluetoothAdapterGetResultString ::LUMIN_MLSDK_API::MLBluetoothAdapterGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
