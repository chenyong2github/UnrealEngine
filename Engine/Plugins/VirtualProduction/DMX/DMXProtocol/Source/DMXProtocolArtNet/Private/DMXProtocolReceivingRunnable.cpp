// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolReceivingRunnable.h"

#include "DMXProtocolTypes.h"
#include "DMXProtocolArtNet.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Packets/DMXProtocolArtNetPackets.h"

#include "Async/AsyncWork.h"
#include "Misc/ScopeLock.h"
#include "Templates/SharedPointer.h"


FDMXProtocolArtNetReceivingRunnable::FDMXProtocolArtNetReceivingRunnable(uint32 InReceivingRefreshRate)
	: Thread(nullptr)
	, bStopping(false)
	, ReceivingRefreshRate(InReceivingRefreshRate)
{
}

FDMXProtocolArtNetReceivingRunnable::~FDMXProtocolArtNetReceivingRunnable()
{
	Stop();

	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;

		Thread = nullptr;
	}
}

TSharedPtr<FDMXProtocolArtNetReceivingRunnable, ESPMode::ThreadSafe> FDMXProtocolArtNetReceivingRunnable::CreateNew(uint32 InReceivingRefreshRate)
{
	TSharedPtr<FDMXProtocolArtNetReceivingRunnable, ESPMode::ThreadSafe> NewReceivingRunnable = MakeShared<FDMXProtocolArtNetReceivingRunnable, ESPMode::ThreadSafe>(InReceivingRefreshRate);

	NewReceivingRunnable->Thread = FRunnableThread::Create(static_cast<FRunnable*>(NewReceivingRunnable.Get()), TEXT("DMXProtocolArtNetReceivingRunnable"), 0U, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());

	return NewReceivingRunnable;
}

void FDMXProtocolArtNetReceivingRunnable::ClearBuffers()
{
	FScopeLock Lock(&ClearBufferLock);

	Queue.Empty();

	ReceivingThreadOnlyBuffer.Reset();

	TSharedPtr<FDMXProtocolArtNetReceivingRunnable, ESPMode::ThreadSafe> ThisSP = SharedThis(this);

	constexpr ENamedThreads::Type GameThread = ENamedThreads::GameThread;
	AsyncTask(GameThread, [ThisSP]() {
		ThisSP->GameThreadOnlyBuffer.Reset();
		});
}

void FDMXProtocolArtNetReceivingRunnable::PushDMXPacket(uint16 InUniverse, const FDMXProtocolArtNetDMXPacket& ArtNetDMXPacket)
{
	TSharedPtr<FDMXSignal> DMXSignal = MakeShared<FDMXSignal>(FApp::GetTimecode(), InUniverse, TArray<uint8>(ArtNetDMXPacket.Data, DMX_UNIVERSE_SIZE));

	Queue.Enqueue(DMXSignal);
}

void FDMXProtocolArtNetReceivingRunnable::GameThread_InputDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment)
{
	check(IsInGameThread());

	TArray<uint8> Channels;
	Channels.AddZeroed(DMX_UNIVERSE_SIZE);

	for (const TPair<uint32, uint8>& ChannelValue : DMXFragment)
	{
		Channels[ChannelValue.Key - 1] = ChannelValue.Value;
	}

	TSharedPtr<FDMXSignal>* ExistingSignalPtr = GameThreadOnlyBuffer.Find(UniverseID);
	if (ExistingSignalPtr)
	{
		// Copy fragments into existing
		for (const TPair<uint32, uint8>& ChannelValue : DMXFragment)
		{
			(*ExistingSignalPtr)->ChannelData[ChannelValue.Key - 1] = ChannelValue.Value;
		}
	}
	else
	{
		GameThreadOnlyBuffer.Add(UniverseID, MakeShared<FDMXSignal>(FApp::GetTimecode(), UniverseID, Channels));
	}
}

void FDMXProtocolArtNetReceivingRunnable::SetRefreshRate(uint32 NewReceivingRefreshRate)
{
	FScopeLock Lock(&SetReceivingRateLock);

	ReceivingRefreshRate = NewReceivingRefreshRate;
}

bool FDMXProtocolArtNetReceivingRunnable::Init()
{
	return true;
}

uint32 FDMXProtocolArtNetReceivingRunnable::Run()
{
	while (!bStopping)
	{
		Update();

		FPlatformProcess::SleepNoStats(1.f / (float)ReceivingRefreshRate);
	}

	return 0;
}

void FDMXProtocolArtNetReceivingRunnable::Stop()
{
	bStopping = true;
}

void FDMXProtocolArtNetReceivingRunnable::Exit()
{

}

FSingleThreadRunnable* FDMXProtocolArtNetReceivingRunnable::GetSingleThreadInterface()
{
	return this;
}

void FDMXProtocolArtNetReceivingRunnable::Tick()
{
	// Only called when platform is single-threaded
	Update();
}

void FDMXProtocolArtNetReceivingRunnable::Update()
{
	if (bStopping || IsEngineExitRequested())
	{
		return;
	}

	// Let the game thread capture This
	TSharedPtr<FDMXProtocolArtNetReceivingRunnable, ESPMode::ThreadSafe> ThisSP = SharedThis(this);

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
