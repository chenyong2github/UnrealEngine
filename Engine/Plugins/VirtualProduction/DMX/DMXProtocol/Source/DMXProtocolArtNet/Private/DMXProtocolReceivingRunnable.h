// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DMXProtocolCommon.h"

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/CriticalSection.h"
#include "Containers/Queue.h"
#include "Misc/SingleThreadRunnable.h"
#include "Misc/ScopeLock.h"

struct FDMXProtocolArtNetDMXPacket;
class FDMXProtocolArtNet;
class FDMXSignal;

class FScopeLock;

class FDMXProtocolArtNetReceivingRunnable
	: public FRunnable
	, public FSingleThreadRunnable
	, public TSharedFromThis<FDMXProtocolArtNetReceivingRunnable, ESPMode::ThreadSafe>
{
public:
	FDMXProtocolArtNetReceivingRunnable(uint32 InReceivingRefreshRate, const TSharedRef<FDMXProtocolArtNet, ESPMode::ThreadSafe>& InProtocolArtNet);
	virtual ~FDMXProtocolArtNetReceivingRunnable();

	static TSharedPtr<FDMXProtocolArtNetReceivingRunnable, ESPMode::ThreadSafe> CreateNew(uint32 InReceivingRefreshRate, const TSharedRef<FDMXProtocolArtNet, ESPMode::ThreadSafe>& InProtocolArtNet);

	FORCEINLINE const IDMXUniverseSignalMap& GameThread_GetInputBuffer()
	{
		FScopeLock Lock(&(CopyToGameThreadLock));
		check(IsInGameThread());
		return GameThreadOnlyBuffer;
	}

	/** Clears all buffers */
	void ClearBuffers();

	/** Pushes a packet received from the network into the receiving thread */
	void PushDMXPacket(uint16 InUniverse, const FDMXProtocolArtNetDMXPacket& ArtNetDMXPacket);

	/** Pushes a universe of channels into the receiver. Note: This can only be called from and read by the game thread. */
	void GameThread_InputDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment);

	/** Sets the rate at which the thread forwards received data */
	void SetRefreshRate(uint32 NewReceivingRefreshRate);

protected:
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

	void Update();

private:
	IDMXUniverseSignalMap ReceivingThreadOnlyBuffer;

	IDMXUniverseSignalMap GameThreadOnlyBuffer;

	TQueue<TSharedPtr<FDMXSignal>, EQueueMode::Spsc> Queue;

	FCriticalSection SetReceivingRateLock;

	FCriticalSection ClearBufferLock;

	FCriticalSection CopyToGameThreadLock;

	/** The thread object. */
	FRunnableThread* Thread;

	/** Flag indicating that the thread is stopping. */
	bool bStopping;

	uint32 ReceivingRefreshRate;

	TWeakPtr<FDMXProtocolArtNet, ESPMode::ThreadSafe> ProtocolArtNetPtr;
};
