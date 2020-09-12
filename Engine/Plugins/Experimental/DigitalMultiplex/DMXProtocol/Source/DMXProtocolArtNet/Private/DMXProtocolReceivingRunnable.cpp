// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolReceivingRunnable.h"
#include "DMXProtocolArtNet.h"
#include "Interfaces/IDMXProtocolUniverse.h"

FDMXProtocolReceivingRunnable::FDMXProtocolReceivingRunnable(FDMXProtocolArtNet* InProtocol, uint32 InReceivingRefreshRate)
	: Stopping(false)
	, Protocol(InProtocol)
	, ReceivingRefreshRate(InReceivingRefreshRate)
{
	Thread = FRunnableThread::Create(this, TEXT("FDMXProtocolSenderArtNet"), 128 * 1024, TPri_BelowNormal, FPlatformAffinity::GetPoolThreadMask());
}

FDMXProtocolReceivingRunnable::~FDMXProtocolReceivingRunnable()
{
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}
}

bool FDMXProtocolReceivingRunnable::Init()
{
	return true;
}

uint32 FDMXProtocolReceivingRunnable::Run()
{
	while (!Stopping)
	{
		Update();
	}

	return 0;
}

void FDMXProtocolReceivingRunnable::Stop()
{
	Stopping = true;
}

void FDMXProtocolReceivingRunnable::Exit()
{
}

void FDMXProtocolReceivingRunnable::Tick()
{
	Update();
}

FSingleThreadRunnable* FDMXProtocolReceivingRunnable::GetSingleThreadInterface()
{
	return this;
}

void FDMXProtocolReceivingRunnable::PushNewTask(uint16 InUniverse, const FArrayReaderPtr& InBufferPtr)
{
	FScopeLock Lock(&IncomingTasksLock);

	IncomingMap.Add(InUniverse, InBufferPtr);
}

void FDMXProtocolReceivingRunnable::SetRefreshRate(uint32 InReceivingRefreshRate)
{
	ReceivingRefreshRate = InReceivingRefreshRate;
}

void FDMXProtocolReceivingRunnable::Update()
{
	{
		// Copy incmping map
		FScopeLock Lock(&IncomingTasksLock);
		CompletedMap.Append(IncomingMap);
		IncomingMap.Empty();
	}

	for (const TPair<uint16, FArrayReaderPtr> CompletedPair : CompletedMap)
	{
		if (TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = Protocol->GetUniverseByIdCreateDefault(CompletedPair.Key))
		{
			Universe->HandleReplyPacket(CompletedPair.Value);
		}
	}

	CompletedMap.Empty();

	if (ReceivingRefreshRate > 0)
	{
		FPlatformProcess::SleepNoStats(1.f / ReceivingRefreshRate);
	}
	else
	{
		FPlatformProcess::SleepNoStats(0.f);
	}
}
