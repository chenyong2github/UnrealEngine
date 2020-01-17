// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/SingleThreadRunnable.h"
#include "Serialization/ArrayWriter.h"

class FRunnableThread;

/**
 * Delegate type for received data.
 */
DECLARE_DELEGATE_OneParam(FOnDMXDataReceived, const FArrayReaderPtr&);

class IDMXProtocolSender 
	: public FRunnable
	, public FSingleThreadRunnable 
{
public:
	virtual bool EnqueueOutboundPackage(FDMXPacketPtr Packet) = 0;
};

class IDMXProtocolReceiver
	: public FRunnable
	, public FSingleThreadRunnable
{
public:
	virtual FOnDMXDataReceived& OnDataReceived() = 0;

	virtual FRunnableThread* GetThread() const = 0;
};

