// Copyright Epic Games, Inc. All Rights Reserved.

#include "Quartz/QuartzSubsystem.h"

#include "Quartz/QuartzMetronome.h"
#include "Quartz/AudioMixerClockManager.h"
#include "Sound/QuartzQuantizationUtilities.h"

#include "AudioDevice.h"
#include "AudioMixerDevice.h"

static int32 MaxQuartzSubscribersToUpdatePerTickCvar = -1;
FAutoConsoleVariableRef CVarMaxQuartzSubscribersToUpdatePerTick(
	TEXT("au.Quartz.MaxSubscribersToUpdatePerTick"),
	MaxQuartzSubscribersToUpdatePerTickCvar,
	TEXT("Limits the number of Quartz subscribers to update per Tick.\n")
	TEXT("<= 0: No Limit, >= 1: Limit"),
	ECVF_Default);

static int32 DisableQuartzCvar = 0;
FAutoConsoleVariableRef CVarDisableQuartz(
	TEXT("au.Quartz.DisableQuartz"),
	DisableQuartzCvar,
	TEXT("Disables Quartz.\n")
	TEXT("0 (default): Enabled, 1: Disabled"),
	ECVF_Default);


static FAudioDevice* GetAudioDeviceUsingWorldContext(const UObject* WorldContextObject)
{
	UWorld* ThisWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!ThisWorld || !ThisWorld->bAllowAudioPlayback || ThisWorld->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	return ThisWorld->GetAudioDeviceRaw();
}


static Audio::FMixerDevice* GetAudioMixerDeviceUsingWorldContext(const UObject* WorldContextObject)
{
	if (FAudioDevice* AudioDevice = GetAudioDeviceUsingWorldContext(WorldContextObject))
	{
		if (!AudioDevice->IsAudioMixerEnabled())
		{
			return nullptr;
		}
		else
		{
			return static_cast<Audio::FMixerDevice*>(AudioDevice);
		}
	}
	return nullptr;
}


UQuartzSubsystem::UQuartzSubsystem()
{
}

UQuartzSubsystem::~UQuartzSubsystem()
{
}


void UQuartzSubsystem::Tick(float DeltaTime)
{
	if (DisableQuartzCvar)
	{
		return;
	}

	const int32 NumSubscribers = QuartzTickSubscribers.Num();

	if (MaxQuartzSubscribersToUpdatePerTickCvar <= 0 || NumSubscribers <= MaxQuartzSubscribersToUpdatePerTickCvar)
	{
		// we can afford to update ALL subscribers
		for (UQuartzClockHandle* Entry : QuartzTickSubscribers)
		{
			if (Entry->QuartzIsTickable())
			{
				Entry->QuartzTick(DeltaTime);
			}
		}

		UpdateIndex = 0;
	}
	else
	{
		// only update up to our limit
		for (int i = 0; i < MaxQuartzSubscribersToUpdatePerTickCvar; ++i)
		{
			if (QuartzTickSubscribers[UpdateIndex]->QuartzIsTickable())
			{
				QuartzTickSubscribers[UpdateIndex]->QuartzTick(DeltaTime);
			}

			if (++UpdateIndex == NumSubscribers)
			{
				UpdateIndex = 0;
			}
		}
	}
}

bool UQuartzSubsystem::IsTickable() const
{
	if (DisableQuartzCvar)
	{
		return false;
	}

	if (!QuartzTickSubscribers.Num())
	{
		return false;
	}

	for (const UQuartzClockHandle* Entry : QuartzTickSubscribers)
	{
		if (Entry->QuartzIsTickable())
		{
			return true;
		}
	}

	return false;
}

TStatId UQuartzSubsystem::GetStatId() const
{
	return Super::GetStatID();
}


void UQuartzSubsystem::SubscribeToQuartzTick(UQuartzClockHandle* InObjectToTick)
{
	if (DisableQuartzCvar)
	{
		return;
	}

	QuartzTickSubscribers.AddUnique(InObjectToTick);
}


void UQuartzSubsystem::UnsubscribeFromQuartzTick(UQuartzClockHandle* InObjectToTick)
{
	if (DisableQuartzCvar)
	{
		return;
	}

	QuartzTickSubscribers.RemoveSingleSwap(InObjectToTick);
}


UQuartzSubsystem* UQuartzSubsystem::Get(UWorld* World)
{
	if (DisableQuartzCvar)
	{
		return nullptr;
	}

	return World->GetSubsystem<UQuartzSubsystem>();
}


TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe> UQuartzSubsystem::CreateQuartzCommandQueue()
{
	return MakeShared<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe>();
}



Audio::FQuartzQuantizedRequestData UQuartzSubsystem::CreateDataDataForSchedulePlaySound(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate, const FQuartzQuantizationBoundary& InQuantizationBoundary)
{
	Audio::FQuartzQuantizedRequestData CommandInitInfo;

	CommandInitInfo.ClockName = InClockHandle->GetClockName();
	CommandInitInfo.ClockHandleName = InClockHandle->GetHandleName();
	CommandInitInfo.QuantizationBoundary = InQuantizationBoundary;
	CommandInitInfo.QuantizedCommandPtr = MakeShared<Audio::FQuantizedPlayCommand>();

	if (InDelegate.IsBound())
	{
		CommandInitInfo.GameThreadDelegateID = InClockHandle->AddCommandDelegate(InDelegate, CommandInitInfo.GameThreadCommandQueue);
	}

	return CommandInitInfo;
}

bool UQuartzSubsystem::IsQuartzEnabled()
{
	return DisableQuartzCvar == 0;
}


Audio::FQuartzQuantizedRequestData UQuartzSubsystem::CreateDataForTickRateChange(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate, const Audio::FQuartzClockTickRate& InNewTickRate, const FQuartzQuantizationBoundary& InQuantizationBoundary)
{
	TSharedPtr<Audio::FQuantizedTickRateChange> TickRateChangeCommandPtr = MakeShared<Audio::FQuantizedTickRateChange>();
	TickRateChangeCommandPtr->SetTickRate(InNewTickRate);

	Audio::FQuartzQuantizedRequestData CommandInitInfo;

	CommandInitInfo.ClockName = InClockHandle->GetClockName();
	CommandInitInfo.ClockHandleName = InClockHandle->GetHandleName();
	CommandInitInfo.QuantizationBoundary = InQuantizationBoundary;
	CommandInitInfo.QuantizedCommandPtr = TickRateChangeCommandPtr;

	if (InDelegate.IsBound())
	{
		CommandInitInfo.GameThreadDelegateID = InClockHandle->AddCommandDelegate(InDelegate, CommandInitInfo.GameThreadCommandQueue);
	}

	return CommandInitInfo;
}

Audio::FQuartzQuantizedRequestData UQuartzSubsystem::CreateDataForTransportReset(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate)
{
	TSharedPtr<Audio::FQuantizedTransportReset> TransportResetCommandPtr = MakeShared<Audio::FQuantizedTransportReset>();

	Audio::FQuartzQuantizedRequestData CommandInitInfo;

	CommandInitInfo.ClockName = InClockHandle->GetClockName();
	CommandInitInfo.ClockHandleName = InClockHandle->GetHandleName();
	CommandInitInfo.QuantizationBoundary = EQuartzCommandQuantization::Bar;
	CommandInitInfo.QuantizedCommandPtr = TransportResetCommandPtr;

	if (InDelegate.IsBound())
	{
		CommandInitInfo.GameThreadDelegateID = InClockHandle->AddCommandDelegate(InDelegate, CommandInitInfo.GameThreadCommandQueue);
	}

	return CommandInitInfo;
}

UQuartzClockHandle* UQuartzSubsystem::CreateNewClock(const UObject* WorldContextObject, FName ClockName, FQuartzClockSettings InSettings, bool bOverrideSettingsIfClockExists)
{
	if (DisableQuartzCvar)
	{
		return nullptr;
	}

	if (ClockName.IsNone())
	{
		return nullptr; // TODO: Create a unique name
	}

	// add or create clock
	Audio::FQuartzClockManager* ClockManager = GetClockManager(WorldContextObject);
	if (!ClockManager)
	{
		return nullptr;
	}

	ClockManager->GetOrCreateClock(ClockName, InSettings, bOverrideSettingsIfClockExists);

	UQuartzClockHandle* ClockHandlePtr = NewObject<UQuartzClockHandle>()->Init(WorldContextObject->GetWorld())->SubscribeToClock(WorldContextObject, ClockName);
	return ClockHandlePtr;
}


UQuartzClockHandle* UQuartzSubsystem::GetHandleForClock(const UObject* WorldContextObject, FName ClockName)
{
	if (DisableQuartzCvar)
	{
		return nullptr;
	}

	Audio::FQuartzClockManager* ClockManager = GetClockManager(WorldContextObject);
	if (!ClockManager)
	{
		return nullptr;
	}

	if (ClockManager->DoesClockExist(ClockName))
	{
		return NewObject<UQuartzClockHandle>()->Init(WorldContextObject->GetWorld())->SubscribeToClock(WorldContextObject, ClockName);
	}

	return nullptr;
}


bool UQuartzSubsystem::DoesClockExist(const UObject* WorldContextObject, FName ClockName)
{
	if (DisableQuartzCvar)
	{
		return false;
	}

	Audio::FQuartzClockManager* ClockManager = GetClockManager(WorldContextObject);
	if (!ClockManager)
	{
		return false;
	}

	return ClockManager->DoesClockExist(ClockName);
}


float UQuartzSubsystem::GetGameThreadToAudioRenderThreadAverageLatency(const UObject* WorldContextObject)
{
	Audio::FQuartzClockManager* ClockManager = GetClockManager(WorldContextObject);
	if (!ClockManager)
	{
		return { };
	}
	return ClockManager->GetLifetimeAverageLatency();
}


float UQuartzSubsystem::GetGameThreadToAudioRenderThreadMinLatency(const UObject* WorldContextObject)
{
	Audio::FQuartzClockManager* ClockManager = GetClockManager(WorldContextObject);
	if (!ClockManager)
	{
		return { };
	}
	return ClockManager->GetMinLatency();
}


float UQuartzSubsystem::GetGameThreadToAudioRenderThreadMaxLatency(const UObject* WorldContextObject)
{
	Audio::FQuartzClockManager* ClockManager = GetClockManager(WorldContextObject);
	if (!ClockManager)
	{
		return { };
	}
	return ClockManager->GetMinLatency();
}


float UQuartzSubsystem::GetAudioRenderThreadToGameThreadAverageLatency()
{
	return GetLifetimeAverageLatency();
}


float UQuartzSubsystem::GetAudioRenderThreadToGameThreadMinLatency()
{
	return GetMinLatency();
}


float UQuartzSubsystem::GetAudioRenderThreadToGameThreadMaxLatency()
{
	return GetMaxLatency();
}


float UQuartzSubsystem::GetRoundTripAverageLatency(const UObject* WorldContextObject)
{
	// very much an estimate
	return GetAudioRenderThreadToGameThreadAverageLatency() + GetGameThreadToAudioRenderThreadAverageLatency(WorldContextObject);
}


float UQuartzSubsystem::GetRoundTripMinLatency(const UObject* WorldContextObject)
{
	return GetAudioRenderThreadToGameThreadMaxLatency() + GetGameThreadToAudioRenderThreadMaxLatency(WorldContextObject);
}


float UQuartzSubsystem::GetRoundTripMaxLatency(const UObject* WorldContextObject)
{
	return GetAudioRenderThreadToGameThreadMinLatency() + GetGameThreadToAudioRenderThreadMinLatency(WorldContextObject);
}


void UQuartzSubsystem::AddCommandToClock(const UObject* WorldContextObject, Audio::FQuartzQuantizedCommandInitInfo& InQuantizationCommandInitInfo)
{
	if (DisableQuartzCvar)
	{
		return;
	}

	Audio::FQuartzClockManager* ClockManager = GetClockManager(WorldContextObject);
	if (!ClockManager)
	{
		return;
	}

	ClockManager->AddCommandToClock(InQuantizationCommandInitInfo);
}


Audio::FQuartzClockManager* UQuartzSubsystem::GetClockManager(const UObject* WorldContextObject) const
{
	if (DisableQuartzCvar)
	{
		return nullptr;
	}

	Audio::FMixerDevice* MixerDevice = GetAudioMixerDeviceUsingWorldContext(WorldContextObject);

	if (MixerDevice)
	{
		return &MixerDevice->QuantizedEventClockManager;
	}

	return nullptr;
}
