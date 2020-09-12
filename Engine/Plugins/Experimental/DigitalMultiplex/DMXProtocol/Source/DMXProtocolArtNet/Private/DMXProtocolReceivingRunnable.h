// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DMXProtocolCommon.h"

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/CriticalSection.h"
#include "Containers/Queue.h"
#include "Misc/SingleThreadRunnable.h"

class FDMXProtocolArtNet;

class FDMXProtocolReceivingRunnable
	: public FRunnable
	, public FSingleThreadRunnable
{
public:
	FDMXProtocolReceivingRunnable(FDMXProtocolArtNet* InProtocol, uint32 InReceivingRefreshRate);
	virtual ~FDMXProtocolReceivingRunnable();

public:
	//~ Begin FRunnable implementation
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	//~ End FRunnable implementation

	//~ Begin FSingleThreadRunnable implementation
	virtual void Tick() override;
	virtual class FSingleThreadRunnable* GetSingleThreadInterface() override;
	//~ End FSingleThreadRunnable implementation

	void PushNewTask(uint16 InUniverse, const FArrayReaderPtr& InBufferPtr);

	void SetRefreshRate(uint32 InReceivingRefreshRate);

	FCriticalSection& GetIncomingTasksLock() const { return IncomingTasksLock; }

protected:
	void Update();

private:
	mutable FCriticalSection IncomingTasksLock;

	/** The thread object. */
	FRunnableThread* Thread;

	TMap<uint16, FArrayReaderPtr> IncomingMap;

	TMap<uint16, FArrayReaderPtr> CompletedMap;

	/** Flag indicating that the thread is stopping. */
	bool Stopping;

	FDMXProtocolArtNet* Protocol;

	TAtomic<uint32> ReceivingRefreshRate;

};