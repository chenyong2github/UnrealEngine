// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSACNReceivingRunnable.h"

#include "DMXProtocolTypes.h"
#include "DMXProtocolSACN.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Packets/DMXProtocolE131PDUPacket.h"

#include "Async/TaskGraphInterfaces.h"
#include "Misc/ScopeLock.h"
#include "Templates/SharedPointer.h"


FDMXProtocolSACNReceivingRunnable::FDMXProtocolSACNReceivingRunnable(uint32 InReceivingRefreshRate)
	: Thread(nullptr)
	, bStopping(false)
	, ReceivingRefreshRate(InReceivingRefreshRate)
{
}

FDMXProtocolSACNReceivingRunnable::~FDMXProtocolSACNReceivingRunnable()
{
	Stop();

	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;

		Thread = nullptr;
	}
}

TSharedPtr<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe> FDMXProtocolSACNReceivingRunnable::CreateNew(uint32 InReceivingRefreshRate)
{
	TSharedPtr<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe> NewReceivingRunnable = MakeShared<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe>(InReceivingRefreshRate);

	NewReceivingRunnable->Thread = FRunnableThread::Create(static_cast<FRunnable*>(NewReceivingRunnable.Get()), TEXT("DMXProtocolSACNReceivingRunnable"), 0U, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());

	return NewReceivingRunnable;
}

void FDMXProtocolSACNReceivingRunnable::ClearBuffers()
{
	Queue.Empty();

	TSharedPtr<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe> ThisSP = SharedThis(this);

	constexpr ENamedThreads::Type GameThread = ENamedThreads::GameThread;
	AsyncTask(GameThread, [ThisSP]() {
		ThisSP->GameThreadOnlyBuffer.Reset();
	});
}

void FDMXProtocolSACNReceivingRunnable::PushDMXPacket(uint16 InUniverse, const FDMXProtocolE131DMPLayerPacket& E131DMPLayerPacket)
{
	TSharedPtr<FDMXSignal> DMXSignal = MakeShared<FDMXSignal>(FApp::GetTimecode(), InUniverse, TArray<uint8>(E131DMPLayerPacket.DMX, DMX_UNIVERSE_SIZE));

	Queue.Enqueue(DMXSignal);
}

void FDMXProtocolSACNReceivingRunnable::GameThread_InputDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment)
{
	check(IsInGameThread());

	TArray<uint8> Channels;
	Channels.AddZeroed(DMX_UNIVERSE_SIZE);

	for (const TPair<uint32, uint8>& ChannelValue : DMXFragment)
	{
		Channels[ChannelValue.Key] = ChannelValue.Value;
	}

	double TimeStamp = FPlatformTime::Seconds();
	GameThreadOnlyBuffer.FindOrAdd(UniverseID) = MakeShared<FDMXSignal>(FApp::GetTimecode(), UniverseID, Channels);
}

void FDMXProtocolSACNReceivingRunnable::SetRefreshRate(uint32 NewReceivingRefreshRate)
{
	FScopeLock Lock(&SetReceivingRateLock);

	ReceivingRefreshRate = NewReceivingRefreshRate;
}

bool FDMXProtocolSACNReceivingRunnable::Init()
{
	return true;
}

uint32 FDMXProtocolSACNReceivingRunnable::Run()
{
	while (!bStopping)
	{
		Update();

		FPlatformProcess::SleepNoStats(1.f / ReceivingRefreshRate);
	}

	return 0;
}

void FDMXProtocolSACNReceivingRunnable::Stop()
{
	bStopping = true;
}

void FDMXProtocolSACNReceivingRunnable::Exit()
{

}

FSingleThreadRunnable* FDMXProtocolSACNReceivingRunnable::GetSingleThreadInterface()
{
	return this;
}

void FDMXProtocolSACNReceivingRunnable::Tick()
{
	// Only called when platform is single-threaded
	Update();
}

void FDMXProtocolSACNReceivingRunnable::Update()
{
	if (bStopping || IsEngineExitRequested())
	{
		return;
	}

	// Let the game thread capture This
	TSharedPtr<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe> ThisSP = SharedThis(this);

	constexpr ENamedThreads::Type GameThread = ENamedThreads::GameThread;
	AsyncTask(GameThread, [ThisSP]() {
		// Drop frames if they're more than one frame behind the current frame (2 frames)
		FFrameRate FrameRate = FFrameRate(1.0f, ThisSP->ReceivingRefreshRate);
		double TolerableFrameTimeSeconds = FApp::GetTimecode().ToTimespan(FrameRate).GetTotalSeconds() + FrameRate.AsDecimal() * 2.0f;

		TSharedPtr<FDMXSignal> Signal;
		while(ThisSP->Queue.Dequeue(Signal))
		{
			double SignalFrameTimeSeconds = Signal->Timestamp.ToTimespan(FrameRate).GetTotalSeconds();
			if (SignalFrameTimeSeconds > TolerableFrameTimeSeconds)
			{
				ThisSP->Queue.Empty();

				UE_LOG(LogDMXProtocol, Warning, TEXT("DMX sACN Network Buffer overflow. Dropping DMX signal."));
				break;
			}

			ThisSP->GameThreadOnlyBuffer.FindOrAdd(Signal->UniverseID) = Signal;
		}
	});
}
