// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXOutputPort.h"

#include "DMXProtocolSettings.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXSender.h"
#include "IO/DMXOutputPortConfig.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXRawListener.h"

#include "HAL/RunnableThread.h"


#define LOCTEXT_NAMESPACE "DMXOutputPort"

FDMXOutputPortSharedRef FDMXOutputPort::CreateFromConfig(FDMXOutputPortConfig& OutputPortConfig)
{
	// Port Configs are expected to have a valid guid always
	check(OutputPortConfig.GetPortGuid().IsValid());

	FDMXOutputPortSharedRef NewOutputPort = MakeShared<FDMXOutputPort, ESPMode::ThreadSafe>();

	NewOutputPort->PortGuid = OutputPortConfig.GetPortGuid();

	UDMXProtocolSettings* Settings = GetMutableDefault<UDMXProtocolSettings>();
	check(Settings);

	NewOutputPort->CommunicationDeterminator.SetSendEnabled(Settings->IsSendDMXEnabled());
	NewOutputPort->CommunicationDeterminator.SetReceiveEnabled(Settings->IsReceiveDMXEnabled());

	Settings->OnSetSendDMXEnabled.AddThreadSafeSP(NewOutputPort, &FDMXOutputPort::OnSetSendDMXEnabled);
	Settings->OnSetReceiveDMXEnabled.AddThreadSafeSP(NewOutputPort, &FDMXOutputPort::OnSetReceiveDMXEnabled);

	NewOutputPort->UpdateFromConfig(OutputPortConfig);

	const FString SenderThreadName = FString(TEXT("DMXOutputPort_")) + OutputPortConfig.GetPortName();
	NewOutputPort->Thread = FRunnableThread::Create(&NewOutputPort.Get(), *SenderThreadName, 0U, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Created output port %s"), *NewOutputPort->PortName);

	return NewOutputPort;
}

FDMXOutputPort::~FDMXOutputPort()
{	
	// All Inputs need to be explicitly removed before destruction 
	check(RawListeners.Num() == 0);
	
	// Port needs be unregistered before destruction
	check(DMXSenderArray.Num() == 0);

	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;
	}

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Destroyed output port %s"), *PortName);
}

void FDMXOutputPort::UpdateFromConfig(FDMXOutputPortConfig& OutputPortConfig)
{	
	// Need a valid config for the port
	OutputPortConfig.MakeValid();

	// Can only use configs that are in project settings
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	const bool bConfigIsInProjectSettings = ProtocolSettings->OutputPortConfigs.ContainsByPredicate([&OutputPortConfig](const FDMXOutputPortConfig& Other) {
		return OutputPortConfig.GetPortGuid() == Other.GetPortGuid();
	});
	checkf(bConfigIsInProjectSettings, TEXT("Can only use configs that are in project settings"));

	// Find if the port needs update its registration with the protocol
	const bool bNeedsUpdateRegistration = [this, &OutputPortConfig]()
	{
		if (IsRegistered() != CommunicationDeterminator.IsSendDMXEnabled())
		{
			return true;
		}

		FName ProtocolName = Protocol.IsValid() ? Protocol->GetProtocolName() : NAME_None;

		if (ProtocolName == OutputPortConfig.GetProtocolName() &&
			DeviceAddress == OutputPortConfig.GetDeviceAddress() &&
			DestinationAddresses == OutputPortConfig.GetDestinationAddresses() &&
			CommunicationType == OutputPortConfig.GetCommunicationType() &&
			Priority == OutputPortConfig.GetPriority() &&
			DelaySeconds == OutputPortConfig.GetDelaySeconds())
		{
			return false;
		}	

		return true;
	}();

	// Unregister the port if required before the new protocol is set
	if (bNeedsUpdateRegistration)
	{
		if (IsRegistered())
		{
			Unregister();
		}
	}

	Protocol = IDMXProtocol::Get(OutputPortConfig.GetProtocolName());

	// Copy properties from the config
	const FGuid& ConfigPortGuid = OutputPortConfig.GetPortGuid();
	check(PortGuid.IsValid());
	PortGuid = ConfigPortGuid;

	CommunicationType = OutputPortConfig.GetCommunicationType();
	ExternUniverseStart = OutputPortConfig.GetExternUniverseStart();
	DeviceAddress = OutputPortConfig.GetDeviceAddress();
	DestinationAddresses = OutputPortConfig.GetDestinationAddresses();
	LocalUniverseStart = OutputPortConfig.GetLocalUniverseStart();
	NumUniverses = OutputPortConfig.GetNumUniverses();
	PortName = OutputPortConfig.GetPortName();
	Priority = OutputPortConfig.GetPriority();
	DelaySeconds = OutputPortConfig.GetDelaySeconds();

	CommunicationDeterminator.SetLoopbackToEngine(OutputPortConfig.NeedsLoopbackToEngine());

	// Re-register the port if required
	if (bNeedsUpdateRegistration)
	{
		Register();
	}

	OnPortUpdated.Broadcast();
}

const FGuid& FDMXOutputPort::GetPortGuid() const
{
	check(PortGuid.IsValid());
	return PortGuid;
}

void FDMXOutputPort::ClearBuffers()
{
	for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
	{
		RawListener->ClearBuffer();
	}

	FScopeLock LockClearBuffers(&ClearBuffersCriticalSection);

	SignalFragments.Empty();
	ExternUniverseToLatestSignalMap.Reset();
}

bool FDMXOutputPort::IsLoopbackToEngine() const
{
	return CommunicationDeterminator.NeedsLoopbackToEngine();
}

TArray<FString> FDMXOutputPort::GetDestinationAddresses() const
{
	TArray<FString> Result;
	Result.Reserve(DestinationAddresses.Num());
	for (const FDMXOutputPortDestinationAddress& DestinationAddress : DestinationAddresses)
	{
		Result.Add(DestinationAddress.DestinationAddressString);
	}

	return Result;
}

bool FDMXOutputPort::GameThreadGetDMXSignal(int32 LocalUniverseID, FDMXSignalSharedPtr& OutDMXSignal, bool bEvenIfNotLoopbackToEngine)
{
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();
	if (bNeedsLoopbackToEngine || bEvenIfNotLoopbackToEngine)
	{
		int32 ExternUniverseID = ConvertLocalToExternUniverseID(LocalUniverseID);

		FScopeLock LockExternUniverseToLatestSignalMap(&AccessExternUniverseToLatestSignalMapCriticalSection);
		const FDMXSignalSharedPtr* SignalPtr = ExternUniverseToLatestSignalMap.Find(ExternUniverseID);
		if (SignalPtr)
		{
			OutDMXSignal = *SignalPtr;
			return true;
		}
	}

	return false;
}

bool FDMXOutputPort::GameThreadGetDMXSignalFromRemoteUniverse(FDMXSignalSharedPtr& OutDMXSignal, int32 RemoteUniverseID, bool bEvenIfNotLoopbackToEngine)
{
	// DEPRECATED 4.27
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();
	if (bNeedsLoopbackToEngine || bEvenIfNotLoopbackToEngine)
	{
		FScopeLock LockExternUniverseToLatestSignalMap(&AccessExternUniverseToLatestSignalMapCriticalSection);
		const FDMXSignalSharedPtr* SignalPtr = ExternUniverseToLatestSignalMap.Find(RemoteUniverseID);
		if (SignalPtr)
		{
			OutDMXSignal = *SignalPtr;
			return true;
		}
	}

	return false;
}

FString FDMXOutputPort::GetDestinationAddress() const
{
	// DEPRECATED 5.0
	return DestinationAddresses.Num() > 0 ? DestinationAddresses[0].DestinationAddressString : TEXT("");
}

bool FDMXOutputPort::IsRegistered() const
{
	if (DMXSenderArray.Num() > 0)
	{
		return true;
	}

	return false;
}

void FDMXOutputPort::AddRawListener(TSharedRef<FDMXRawListener> InRawListener)
{
	check(!RawListeners.Contains(InRawListener));

	// Inputs need to run in the game thread
	check(IsInGameThread());

	RawListeners.Add(InRawListener);
}

void FDMXOutputPort::RemoveRawListener(TSharedRef<FDMXRawListener> InRawListenerToRemove)
{
	RawListeners.Remove(InRawListenerToRemove);
}

void FDMXOutputPort::SendDMX(int32 LocalUniverseID, const TMap<int32, uint8>& ChannelToValueMap)
{
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	if (IsLocalUniverseInPortRange(LocalUniverseID))
	{
		const bool bNeedsSendDMX = CommunicationDeterminator.NeedsSendDMX();
		const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();

		// Update the buffer for loopback if dmx needs be sent and/or looped back
		if (bNeedsSendDMX || bNeedsLoopbackToEngine)
		{
			const int32 ExternUniverseID = ConvertLocalToExternUniverseID(LocalUniverseID);

			const double SendTime = FPlatformTime::Seconds() + DelaySeconds;
			const TSharedPtr<FDMXSignalFragment> Fragment = MakeShared<FDMXSignalFragment>(ExternUniverseID, ChannelToValueMap, SendTime);

			SignalFragments.Enqueue(Fragment);
		}
	}
}

void FDMXOutputPort::SendDMXToRemoteUniverse(const TMap<int32, uint8>& ChannelToValueMap, int32 RemoteUniverse)
{
	// DEPRECATED 4.27
#if UE_BUILD_DEBUG
	check(IsInGameThread());
#endif // UE_BUILD_DEBUG

	if (IsExternUniverseInPortRange(RemoteUniverse))
	{
		const bool bNeedsSendDMX = CommunicationDeterminator.NeedsSendDMX();
		const bool bNeedsLoopbackToEngine = CommunicationDeterminator.NeedsLoopbackToEngine();

		// Update the buffer for loopback if dmx needs be sent and/or looped back
		if (bNeedsSendDMX || bNeedsLoopbackToEngine)
		{
			const double SendTime = FPlatformTime::Seconds() + DelaySeconds;
			const TSharedPtr<FDMXSignalFragment> Fragment = MakeShared<FDMXSignalFragment>(RemoteUniverse, ChannelToValueMap, SendTime);

			SignalFragments.Enqueue(Fragment);
		}
	}
}

bool FDMXOutputPort::Register()
{
	if (Protocol.IsValid() && IsValidPortSlow() && CommunicationDeterminator.IsSendDMXEnabled() && !FDMXPortManager::Get().AreProtocolsSuspended())
	{
		DMXSenderArray = Protocol->RegisterOutputPort(SharedThis(this));

		if (DMXSenderArray.Num() > 0)
		{
			CommunicationDeterminator.SetHasValidSender(true);
			return true;
		}
	}

	CommunicationDeterminator.SetHasValidSender(false);
	return false;
}

void FDMXOutputPort::Unregister()
{
	if (IsRegistered())
	{
		if (Protocol.IsValid())
		{
			Protocol->UnregisterOutputPort(SharedThis(this));
		}

		DMXSenderArray.Reset();
	}

	CommunicationDeterminator.SetHasValidSender(false);
}

void FDMXOutputPort::OnSetSendDMXEnabled(bool bEnabled)
{
	CommunicationDeterminator.SetSendEnabled(bEnabled);

	UpdateFromConfig(*FindOutputPortConfigChecked());
}

void FDMXOutputPort::OnSetReceiveDMXEnabled(bool bEnabled)
{
	CommunicationDeterminator.SetReceiveEnabled(bEnabled);

	UpdateFromConfig(*FindOutputPortConfigChecked());
}

FDMXOutputPortConfig* FDMXOutputPort::FindOutputPortConfigChecked() const
{
	UDMXProtocolSettings* ProjectSettings = GetMutableDefault<UDMXProtocolSettings>();

	for (FDMXOutputPortConfig& OutputPortConfig : ProjectSettings->OutputPortConfigs)
	{
		if (OutputPortConfig.GetPortGuid() == PortGuid)
		{
			return &OutputPortConfig;
		}
	}

	// Check failed, no config found
	checkNoEntry();

	return nullptr;
}

bool FDMXOutputPort::Init()
{
	return true;
}

uint32 FDMXOutputPort::Run()
{
	const UDMXProtocolSettings* DMXSettings = GetDefault<UDMXProtocolSettings>();
	check(DMXSettings);
	
	// Fixed rate delta time
	const double SendDeltaTime = 1.f / DMXSettings->SendingRefreshRate;

	while (!bStopping)
	{
		const double StartTime = FPlatformTime::Seconds();

		ProcessSendDMX();

		const double EndTime = FPlatformTime::Seconds();
		const double WaitTime = SendDeltaTime - (EndTime - StartTime);

		if (WaitTime > 0.f)
		{
			// Sleep by the amount which is set in refresh rate
			FPlatformProcess::SleepNoStats(WaitTime);
		}

		// In the unlikely case we took to long to send, we instantly continue, but do not take 
		// further measures to compensate - We would have to run faster than DMX send rate to catch up.
	}

	return 0;
}

void FDMXOutputPort::Stop()
{
	bStopping = true;
}

void FDMXOutputPort::Exit()
{
}

void FDMXOutputPort::Tick()
{
	ProcessSendDMX();
}

FSingleThreadRunnable* FDMXOutputPort::GetSingleThreadInterface()
{
	return this;
}

void FDMXOutputPort::ProcessSendDMX()
{
	// Delay signals
	const double Now = FPlatformTime::Seconds();

	FScopeLock LockClearBuffers(&ClearBuffersCriticalSection);
	
	// Write dmx fragments
	{
		for (;;)
		{
			TSharedPtr<FDMXSignalFragment, ESPMode::ThreadSafe> OldestFragment;
			if (SignalFragments.Peek(OldestFragment))
			{
				if (OldestFragment->SendTime <= Now)
				{
					FDMXSignalSharedPtr* SignalPtr = ExternUniverseToLatestSignalMap.Find(OldestFragment->ExternUniverseID);
					if (!SignalPtr)
					{
						FScopeLock LockExternUniverseToLatestSignalMap(&AccessExternUniverseToLatestSignalMapCriticalSection);
						SignalPtr = &ExternUniverseToLatestSignalMap.Add(OldestFragment->ExternUniverseID, MakeShared<FDMXSignal, ESPMode::ThreadSafe>());
					}
					checkf(SignalPtr, TEXT("Cannot find or add a DMX signal to the DMX Output Port buffer"));

					// Write the fragment & meta data 
					FDMXSignalSharedPtr& Signal = *SignalPtr;
					for (const TTuple<int32, uint8>& ChannelValueKvp : OldestFragment->ChannelToValueMap)
					{
						int32 ChannelIndex = ChannelValueKvp.Key - 1;
						// Filter invalid indicies so we can send bp calls here without testing them first.
						if (Signal->ChannelData.IsValidIndex(ChannelIndex))
						{
							Signal->ChannelData[ChannelIndex] = ChannelValueKvp.Value;
						}
					}

					Signal->ExternUniverseID = OldestFragment->ExternUniverseID;
					Signal->Timestamp = Now;
					Signal->Priority = Priority;

					// Drop the written fragment
					SignalFragments.Pop();

					continue;
				}

				break;
			}

			break;
		}
	}

	// Send new and alive DMX Signals
	const bool bNeedsSendDMX = CommunicationDeterminator.NeedsSendDMX();
	for (const TTuple<int32, FDMXSignalSharedPtr>& UniverseToSignalPair : ExternUniverseToLatestSignalMap)
	{
		if (UniverseToSignalPair.Value->Timestamp <= Now)
		{
			// Keeping the signal alive here:
			// Increment the timestamp by one second so the signal will be sent anew in one second.
			UniverseToSignalPair.Value->Timestamp = Now + 1.0;

			// Send via the protocol's sender
			if (bNeedsSendDMX)
			{
				for (const TSharedPtr<IDMXSender>& DMXSender : DMXSenderArray)
				{
					DMXSender->SendDMXSignal(UniverseToSignalPair.Value.ToSharedRef());
				}
			}

			// Loopback to Listeners
			for (const TSharedRef<FDMXRawListener>& RawListener : RawListeners)
			{
				RawListener->EnqueueSignal(this, UniverseToSignalPair.Value.ToSharedRef());
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
