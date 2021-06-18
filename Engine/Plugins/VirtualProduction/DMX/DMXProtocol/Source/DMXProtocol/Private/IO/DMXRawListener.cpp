// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXRawListener.h"

#include "IO/DMXPort.h"


FDMXRawListener::FDMXRawListener(TSharedRef<FDMXPort, ESPMode::ThreadSafe> InOwnerPort)
	: OwnerPort(InOwnerPort)
	, ExternUniverseOffset(InOwnerPort->GetExternUniverseOffset())
	, bStopped(true)
#if UE_BUILD_DEBUG
	, ProducerObj(nullptr)
	, ConsumerObj(nullptr)
#endif // UE_BUILD_DEBUG
{}

FDMXRawListener::~FDMXRawListener()
{
	// Needs be stopped before releasing
	check(bStopped);

	if (TSharedPtr<FDMXPort, ESPMode::ThreadSafe> PinnedOwnerPort = OwnerPort.Pin())
	{
		PinnedOwnerPort->OnPortUpdated.RemoveAll(this);
	}
}

void FDMXRawListener::Start()
{
	if (TSharedPtr<FDMXPort, ESPMode::ThreadSafe> PinnedOwnerPort = OwnerPort.Pin())
	{
		PinnedOwnerPort->OnPortUpdated.AddSP(this, &FDMXRawListener::OnPortUpdated);
		PinnedOwnerPort->AddRawInput(AsShared());

		bStopped = false;
	}
}

void FDMXRawListener::Stop()
{
	// Without the Stop method, the port would not know it had to free its shared pointer to this.
	// It may seem benefitial to hold a weak ref in the port, however assuming we may well have many
	// listeners, this would cause significant overhead when copying of the weak ptr. 
	// This is less relevant for raw listeners, but kept for consistency with ticked universe 
	// listener and minimal performance benefits. 
	if (FDMXPortSharedPtr PinnedOwnerPort = OwnerPort.Pin())
	{
		PinnedOwnerPort->RemoveRawInput(AsShared());
	}

	bStopped = true;
}

void FDMXRawListener::EnqueueSignal(void* Producer, const FDMXSignalSharedRef& Signal)
{
#if UE_BUILD_DEBUG
	// Test single producer
	if (!ProducerObj)
	{
		ProducerObj = Producer;
	}
	FDMXPortSharedPtr PinnedOwnerPort = OwnerPort.Pin();
	checkf(PinnedOwnerPort.IsValid() && ProducerObj == PinnedOwnerPort.Get(), TEXT("More than one producer detected in FDMXRawListener::DequeueSignal."));
#endif // UE_BUILD_DEBUG

	RawBuffer.Enqueue(Signal);
}

bool FDMXRawListener::DequeueSignal(void* Consumer, FDMXSignalSharedPtr& OutSignal, int32& OutLocalUniverseID)
{
#if UE_BUILD_DEBUG
	// Test single consumer
	if (!ConsumerObj)
	{
		ConsumerObj = Consumer;
	}
	checkf(ConsumerObj == Consumer, TEXT("More than one consumer detected in FDMXRawListener::DequeueSignal."));
#endif // UE_BUILD_DEBUG

	if (RawBuffer.Dequeue(OutSignal))
	{
		OutLocalUniverseID = OutSignal->ExternUniverseID - ExternUniverseOffset;

		// Function comment states the OutSignal is always valid if true is returned
		// This is the case as only shared references are enqueued
		return true;
	}

	OutSignal = nullptr;
	OutLocalUniverseID = -1;
	return false;
}

void FDMXRawListener::ClearBuffer()
{
	RawBuffer.Empty();
}

void FDMXRawListener::OnPortUpdated()
{
	if (TSharedPtr<FDMXPort, ESPMode::ThreadSafe> PinnedOwnerPort = OwnerPort.Pin())
	{
		ExternUniverseOffset = PinnedOwnerPort->GetExternUniverseOffset();
	}
}
