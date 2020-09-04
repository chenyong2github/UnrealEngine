// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapBLEPlugin.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY(LogMagicLeapBLE);

FMagicLeapBLEPlugin::FMagicLeapBLEPlugin()
: ConnectionState(EMagicLeapBluetoothGattConnectionState::NotConnected)
, CurrAtomicTaskType(FBLETask::EType::None)
, Runnable(nullptr)
{

}

void FMagicLeapBLEPlugin::StartupModule()
{
	IMagicLeapBLEPlugin::StartupModule();
	CurrAtomicTaskType = FBLETask::EType::None;
	Runnable = new FBLERunnable();
	ConnectionState = EMagicLeapBluetoothGattConnectionState::NotConnected;
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapBLEPlugin::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
}

void FMagicLeapBLEPlugin::ShutdownModule()
{
	FBLERunnable* InRunnable = Runnable;
	Runnable = nullptr;
	if (FTaskGraphInterface::IsRunning())
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [InRunnable]()
		{
			delete InRunnable;
		});
	}
	else
	{
		delete InRunnable;
	}
	
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	IModuleInterface::ShutdownModule();
}

bool FMagicLeapBLEPlugin::Tick(float DeltaTime)
{
#if WITH_MLSDK
	FBLETask CompletedTask;
	if (Runnable->TryGetCompletedTask(CompletedTask))
	{
		switch (CompletedTask.Type)
		{
		case FBLETask::EType::ScanForDevices:
		case FBLETask::EType::StopScan:
		case FBLETask::EType::ReportFoundDevice:
		{
			OnFoundDeviceDelegate.Call(CompletedTask.FoundDevice, CompletedTask.Result);
		}
		break;

		case FBLETask::EType::ConnectToDevice:
		case FBLETask::EType::DisconnectFromDevice:
		{
			if (!CompletedTask.Result.bSuccess)
			{
				OnConnStateChangedDelegate.Call(CompletedTask.ConnectionState, CompletedTask.Result);
			}
		}
		break;

		case FBLETask::EType::DeviceConnectionChanged:
		{
			ConnectionState = CompletedTask.ConnectionState;
			OnConnStateChangedDelegate.Call(CompletedTask.ConnectionState, CompletedTask.Result);
			// don't declare the task buffer free if this was an unsolicited disconnect
			if (CurrAtomicTaskType == FBLETask::EType::ConnectToDevice)
			{
				CurrAtomicTaskType = FBLETask::EType::None;
			}
			else if (CurrAtomicTaskType == FBLETask::EType::DisconnectFromDevice)
			{
				CurrAtomicTaskType = FBLETask::EType::None;
				CurrConnectionAddress.Empty();
			}
		}
		break;

		case FBLETask::EType::RequestRemoteRSSI:
		case FBLETask::EType::ReceiveRemoteRSSI:
		{
			OnReadRemoteRSSIDelegate.Call(CompletedTask.IntData, CompletedTask.GattStatus, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::RequestAvailableServices:
		case FBLETask::EType::ReceiveAvailableServices:
		{
			OnGotAvailableServicesDelegate.Call(CompletedTask.GattServices, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::ReadCharacteristic:
		case FBLETask::EType::CharacteristicRead:
		{
			OnReadCharacteristicDelegate.Call(CompletedTask.GattCharacteristic, CompletedTask.GattStatus, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::WriteCharacteristic:
		case FBLETask::EType::CharacteristicWritten:
		{
			OnWriteCharacteristicDelegate.Call(CompletedTask.GattCharacteristic, CompletedTask.GattStatus, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::ReadDescriptor:
		case FBLETask::EType::DescriptorRead:
		{
			OnReadDescriptorDelegate.Call(CompletedTask.GattDescriptor, CompletedTask.GattStatus, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::WriteDescriptor:
		case FBLETask::EType::DescriptorWritten:
		{
			OnWriteDescriptorDelegate.Call(CompletedTask.GattDescriptor, CompletedTask.GattStatus, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::RequestCharacteristicNotifications:
		case FBLETask::EType::ReceiveCharacteristicNotification:
		{
			OnCharacteristicChangedDelegate.Call(CompletedTask.GattCharacteristic, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::RequestConnectionPriority:
		case FBLETask::EType::ReceiveConnectionPriority:
		{
			OnConnPriorityChangedDelegate.Call(CompletedTask.IntData, CompletedTask.GattStatus, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::RequestMTU:
		case FBLETask::EType::ReceiveMTU:
		{
			OnMTUChangedDelegate.Call(CompletedTask.IntData, CompletedTask.GattStatus, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::RequestAdapterName:
		case FBLETask::EType::ReceiveAdapterName:
		{
			OnGotAdapterNameDelegate.Call(CompletedTask.StringData, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::RequestAdapterState:
		case FBLETask::EType::ReceiveAdapterState:
		{
			OnGotAdapterStateDelegate.Call(CompletedTask.AdapterState, CompletedTask.Result);
			CurrAtomicTaskType = FBLETask::EType::None;
		}
		break;

		case FBLETask::EType::Log:
		{
			OnLogDelegate.Call(CompletedTask.Log);
		}
		break;
		}
	}
#endif // WITH_MLSDK
	return true;
}

void FMagicLeapBLEPlugin::SetLogDelegate(const FMagicLeapBLEOnLogDelegate& InResultDelegate)
{
	OnLogDelegate = InResultDelegate;
}

void FMagicLeapBLEPlugin::StartScanAsync(const FMagicLeapBLEOnFoundDeviceDelegate& InResultDelegate)
{
	FMagicLeapResult Result = TryPushNewTask(FBLETask(FBLETask::EType::ScanForDevices));
	if (Result.bSuccess)
	{
		OnFoundDeviceDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(FMagicLeapBluetoothDevice(), Result);
	}
}

void FMagicLeapBLEPlugin::StopScan()
{
	TryPushNewTask(FBLETask(FBLETask::EType::StopScan));
}

void FMagicLeapBLEPlugin::GattConnectAsync(const FString& InAddress, const FMagicLeapBLEOnConnStateChangedDelegate& InResultDelegate)
{
	FBLETask Task(FBLETask::EType::ConnectToDevice);
	Task.StringData = InAddress;
	FMagicLeapResult Result = TryPushNewTask(Task);
	if (Result.bSuccess)
	{
		OnConnStateChangedDelegate = InResultDelegate;
		CurrConnectionAddress = InAddress;
	}
	else
	{
		InResultDelegate.Call(ConnectionState, Result);
	}
}

void FMagicLeapBLEPlugin::GattDisconnectAsync(const FMagicLeapBLEOnConnStateChangedDelegate& InResultDelegate)
{
	FMagicLeapResult Result = TryPushNewTask(FBLETask(FBLETask::EType::DisconnectFromDevice));
	if (Result.bSuccess)
	{
		OnConnStateChangedDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(ConnectionState, Result);
	}
}

void FMagicLeapBLEPlugin::GattReadRemoteRSSIAsync(const FMagicLeapBLEOnReadRemoteRSSIDelegate& InResultDelegate)
{
	FMagicLeapResult Result = TryPushNewTask(FBLETask(FBLETask::EType::RequestRemoteRSSI));
	if (Result.bSuccess)
	{
		OnReadRemoteRSSIDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(0, EMagicLeapBluetoothGattStatus::Failure, Result);
	}
}

void FMagicLeapBLEPlugin::GattGetAvailableServicesAsync(const FMagicLeapBLEOnGotAvailableServicesDelegate& InResultDelegate)
{
	FMagicLeapResult Result = TryPushNewTask(FBLETask(FBLETask::EType::RequestAvailableServices));
	if (Result.bSuccess)
	{
		OnGotAvailableServicesDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(TArray<FMagicLeapBluetoothGattService>(), Result);
	}
}

void FMagicLeapBLEPlugin::GattReadCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, const FMagicLeapBLEOnReadCharacteristicDelegate& InResultDelegate)
{
	FBLETask Task(FBLETask::EType::ReadCharacteristic);
	Task.GattCharacteristic = InCharacteristic;
	FMagicLeapResult Result = TryPushNewTask(Task);
	if (Result.bSuccess)
	{
		OnReadCharacteristicDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(InCharacteristic, EMagicLeapBluetoothGattStatus::Failure, Result);
	}
}

void FMagicLeapBLEPlugin::GattWriteCharacteristicAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, const FMagicLeapBLEOnWriteCharacteristicDelegate& InResultDelegate)
{
	FBLETask Task(FBLETask::EType::WriteCharacteristic);
	Task.GattCharacteristic = InCharacteristic;
	FMagicLeapResult Result = TryPushNewTask(Task);
	if (Result.bSuccess)
	{
		OnWriteCharacteristicDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(InCharacteristic, EMagicLeapBluetoothGattStatus::Failure, Result);
	}
}

void FMagicLeapBLEPlugin::GattReadDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor, const FMagicLeapBLEOnReadDescriptorDelegate& InResultDelegate)
{
	FBLETask Task(FBLETask::EType::ReadDescriptor);
	Task.GattDescriptor = InDescriptor;
	FMagicLeapResult Result = TryPushNewTask(Task);
	if (Result.bSuccess)
	{
		OnReadDescriptorDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(InDescriptor, EMagicLeapBluetoothGattStatus::Failure, Result);
	}
}

void FMagicLeapBLEPlugin::GattWriteDescriptorAsync(const FMagicLeapBluetoothGattDescriptor& InDescriptor, const FMagicLeapBLEOnWriteDescriptorDelegate& InResultDelegate)
{
	FBLETask Task(FBLETask::EType::WriteDescriptor);
	Task.GattDescriptor = InDescriptor;
	FMagicLeapResult Result = TryPushNewTask(Task);
	if (Result.bSuccess)
	{
		OnWriteDescriptorDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(InDescriptor, EMagicLeapBluetoothGattStatus::Failure, Result);
	}
}

void FMagicLeapBLEPlugin::GattSetCharacteristicNotificationAsync(const FMagicLeapBluetoothGattCharacteristic& InCharacteristic, bool bEnable, const FMagicLeapBLEOnCharacteristicChangedDelegate& InResultDelegate)
{
	FBLETask Task(FBLETask::EType::RequestCharacteristicNotifications);
	Task.GattCharacteristic = InCharacteristic;
	Task.bEnable = bEnable;
	FMagicLeapResult Result = TryPushNewTask(Task);
	if (Result.bSuccess)
	{
		OnCharacteristicChangedDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(InCharacteristic, Result);
	}
}

void FMagicLeapBLEPlugin::GattRequestConnectionPriorityAsync(EMagicLeapBluetoothGattConnectionPriority InPriority, const FMagicLeapBLEOnConnPriorityChangedDelegate& InResultDelegate)
{
	FBLETask Task(FBLETask::EType::RequestConnectionPriority);
	Task.ConnectionPriority = InPriority;
	FMagicLeapResult Result = TryPushNewTask(Task);
	if (Result.bSuccess)
	{
		OnConnPriorityChangedDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(0, EMagicLeapBluetoothGattStatus::Failure, Result);
	}
}

void FMagicLeapBLEPlugin::GattRequestMTUAsync(int32 MTU, const FMagicLeapBLEOnMTUChangedDelegate& InResultDelegate)
{
	FBLETask Task(FBLETask::EType::RequestMTU);
	Task.IntData = MTU;
	FMagicLeapResult Result = TryPushNewTask(Task);
	if (Result.bSuccess)
	{
		OnMTUChangedDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(0, EMagicLeapBluetoothGattStatus::Failure, Result);
	}
}

void FMagicLeapBLEPlugin::AdapterGetNameAsync(const FMagicLeapBLEOnGotAdapterNameDelegate& InResultDelegate)
{
	FMagicLeapResult Result = TryPushNewTask(FBLETask(FBLETask::EType::RequestAdapterName));
	if (Result.bSuccess)
	{
		OnGotAdapterNameDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(FString(), Result);
	}
}

void FMagicLeapBLEPlugin::AdapterGetStateAsync(const FMagicLeapBLEOnGotAdapterStateDelegate& InResultDelegate)
{
	FMagicLeapResult Result = TryPushNewTask(FBLETask(FBLETask::EType::RequestAdapterState));
	if (Result.bSuccess)
	{
		OnGotAdapterStateDelegate = InResultDelegate;
	}
	else
	{
		InResultDelegate.Call(EMagicLeapBluetoothAdapterState::Off, Result);
	}
}

void FMagicLeapBLEPlugin::RegisterForAdapterStateChangeNotifications(const FMagicLeapBLEOnAdapterStateChangedDelegate& InResultDelegate)
{
	OnAdapterStateChangedDelegate = InResultDelegate;
}

FMagicLeapResult FMagicLeapBLEPlugin::TryPushNewTask(const FBLETask& InTask)
{
	FMagicLeapResult Result;
	if (CurrAtomicTaskType != FBLETask::EType::None)
	{
		Result.AdditionalInfo = FString::Printf(TEXT("The %s request was ignored as the system is currently processing %s."), FBLETask::TypeToString(InTask.Type), FBLETask::TypeToString(CurrAtomicTaskType));
		return Result;
	}

	switch (InTask.Type)
	{
		case FBLETask::EType::ScanForDevices:
		case FBLETask::EType::StopScan:
		case FBLETask::EType::RequestAdapterName:
		case FBLETask::EType::RequestAdapterState:
		{
			Result.bSuccess = true;
		}
		break;
		case FBLETask::EType::ConnectToDevice:
		{
			switch (ConnectionState)
			{
			case EMagicLeapBluetoothGattConnectionState::NotConnected:
			{
				ConnectionState = EMagicLeapBluetoothGattConnectionState::Connecting;
				CurrAtomicTaskType = InTask.Type;
				Result.bSuccess = true;
			}
			break;
			case EMagicLeapBluetoothGattConnectionState::Connecting:
			{
				Result.AdditionalInfo = FString::Printf(TEXT("The connection request to %s was ignored as the system is already connecting to %s."), *InTask.StringData, *CurrConnectionAddress);
			}
			break;
			case EMagicLeapBluetoothGattConnectionState::Connected:
			{
				Result.AdditionalInfo = FString::Printf(TEXT("The connection request to %s was ignored as the system is already connected to %s."), *InTask.StringData, *CurrConnectionAddress);
			}
			break;
			case EMagicLeapBluetoothGattConnectionState::Disconnecting:
			{
				Result.AdditionalInfo = FString::Printf(TEXT("The connection request to %s was ignored as the system is currently disconnecting from %s."), *InTask.StringData, *CurrConnectionAddress);
			}
			break;
			}
		}
		break;
		case FBLETask::EType::DisconnectFromDevice:
		{
			switch (ConnectionState)
			{
			case EMagicLeapBluetoothGattConnectionState::NotConnected:
			{
				Result.AdditionalInfo = TEXT("The disconnect request was ignored by the system as it is not currently connected.");
			}
			break;
			case EMagicLeapBluetoothGattConnectionState::Connecting:
			{
				Result.AdditionalInfo = FString::Printf(TEXT("The disconnect request from %s was ignored as the system has not finished connecting to that address."), *CurrConnectionAddress);
			}
			break;
			case EMagicLeapBluetoothGattConnectionState::Connected:
			{
				ConnectionState = EMagicLeapBluetoothGattConnectionState::Disconnecting;
				CurrAtomicTaskType = InTask.Type;
				Result.bSuccess = true;
			}
			break;
			case EMagicLeapBluetoothGattConnectionState::Disconnecting:
			{
				Result.AdditionalInfo = FString::Printf(TEXT("The disconnect request from %s was ignored as the system is already disconnecting from that address."), *CurrConnectionAddress);
			}
			break;
			}
		}
		break;
		case FBLETask::EType::RequestRemoteRSSI:
		case FBLETask::EType::RequestAvailableServices:
		case FBLETask::EType::ReadCharacteristic:
		case FBLETask::EType::WriteCharacteristic:
		case FBLETask::EType::ReadDescriptor:
		case FBLETask::EType::WriteDescriptor:
		case FBLETask::EType::RequestConnectionPriority:
		case FBLETask::EType::RequestMTU:
		{
			if (ConnectionState != EMagicLeapBluetoothGattConnectionState::Connected)
			{
				Result.AdditionalInfo = FString::Printf(TEXT("The %s request was ignored by the system as it is not currently connected."), FBLETask::TypeToString(InTask.Type));
			}
			else
			{
				CurrAtomicTaskType = InTask.Type;
				Result.bSuccess = true;
			}
		}
		break;
		case FBLETask::EType::RequestCharacteristicNotifications:
		{
			if (ConnectionState != EMagicLeapBluetoothGattConnectionState::Connected)
			{
				Result.AdditionalInfo = FString::Printf(TEXT("The %s request was ignored by the system as it is not currently connected."), FBLETask::TypeToString(InTask.Type));
			}
			else
			{
				// not atomic
				Result.bSuccess = true;
			}
		}
		break;
		default:
		{
			Result.bSuccess = false;
		}
	}

	if (Result.bSuccess)
	{
#if WITH_MLSDK
		Runnable->PushNewTask(InTask);
#endif // WITH_MLSDK
	}
	return Result;
}

IMPLEMENT_MODULE(FMagicLeapBLEPlugin, MagicLeapBLE);
