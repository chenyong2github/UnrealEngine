// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

class ISocketSubsystem;
class FSocket;

class FSessionMonitorConnection : public FRunnable
{
private:
	FSessionMonitorConnection(const FSessionMonitorConnection&) = delete;
	FSessionMonitorConnection& operator=(const FSessionMonitorConnection&) = delete;
	uint32 Run() override;
	void ProcessMessage(const FString& Msg);

	void SendMsg(const TCHAR* Type);

	uint16 Port;
	ISocketSubsystem* SocketSubsystem;
	FSocket* Socket;

	FRunnableThread* ReadThread;
	FEvent* ReadThreadReady;

public:
	FSessionMonitorConnection(uint16_t Port);
	~FSessionMonitorConnection();
	void Heartbeat();
};

