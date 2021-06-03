// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "CoreFwd.h"
#include "Logging/LogMacros.h"
#include "Common/TcpSocketBuilder.h"
#include "string.h"

class FIOSDeviceOutputReaderRunnable;
class FIOSTargetDevice;
class FIOSTargetDeviceOutput;




inline FIOSDeviceOutputReaderRunnable::FIOSDeviceOutputReaderRunnable(const FTargetDeviceId InDeviceId, FOutputDevice* InOutput)
	: StopTaskCounter(0)
	, DeviceId(InDeviceId)
	, Output(InOutput)
{
}


inline bool FIOSDeviceOutputReaderRunnable::Init(void) 
{ 
	return true;
}

inline void FIOSDeviceOutputReaderRunnable::Exit(void) 
{
	StopTaskCounter.Increment();
}

inline void FIOSDeviceOutputReaderRunnable::Stop(void)
{
	StopTaskCounter.Increment();
}

inline uint32 FIOSDeviceOutputReaderRunnable::Run(void)
{
	Output->Serialize(TEXT("Starting Output"), ELogVerbosity::Log, NAME_None);
	while (StopTaskCounter.GetValue() == 0)
	{
		FString Text;
		if (OutputQueue.Dequeue(Text))
		{
			if (Text.Contains(TEXT("[UE]"), ESearchCase::CaseSensitive))
			{
				Output->Serialize(*Text, ELogVerbosity::Log, NAME_None);
			}
		}
		else
		{
			FPlatformProcess::Sleep(0.1f);
		}
	}

	return 0;
}

inline bool FIOSTargetDeviceOutput::Init(const FIOSTargetDevice& TargetDevice, FOutputDevice* Output)
{
	check(Output);
	// Output will be produced by background thread
	check(Output->CanBeUsedOnAnyThread());
	
	DeviceId = TargetDevice.GetId();
	DeviceName = TargetDevice.GetName();

	Output->Serialize(TEXT("Creating FIOSTargetDeviceOutput ....."), ELogVerbosity::Log, NAME_None);
		
	auto* Runnable = new FIOSDeviceOutputReaderRunnable(DeviceId, Output);
	DeviceOutputThread = TUniquePtr<FRunnableThread>(FRunnableThread::Create(Runnable, TEXT("FIOSDeviceOutputReaderRunnable")));
	return true;
}
