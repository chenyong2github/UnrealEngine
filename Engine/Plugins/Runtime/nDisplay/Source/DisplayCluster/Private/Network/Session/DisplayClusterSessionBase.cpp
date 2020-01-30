// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Session/DisplayClusterSessionBase.h"
#include "Network/DisplayClusterServer.h"
#include "Network/DisplayClusterMessage.h"

#include "HAL/RunnableThread.h"

#include "DisplayClusterLog.h"


FDisplayClusterSessionBase::FDisplayClusterSessionBase(FSocket* InSocket, IDisplayClusterSessionListener* InListener, const FString& InName) :
	FDisplayClusterSocketOps(InSocket),
	Name(InName),
	Listener(InListener)
{
	check(InSocket);
	check(InListener);
}

FDisplayClusterSessionBase::~FDisplayClusterSessionBase()
{
	UE_LOG(LogDisplayClusterNetwork, VeryVerbose, TEXT("Session %s .dtor"), *Name);
	Stop();
}

void FDisplayClusterSessionBase::StartSession()
{
	Listener->NotifySessionOpen(this);

	ThreadObj.Reset(FRunnableThread::Create(this, *(Name + FString("_thread")), 1024 * 1024, TPri_Normal, FPlatformAffinity::GetPoolThreadMask()));
	ensure(ThreadObj);

	UE_LOG(LogDisplayClusterNetwork, Log, TEXT("Session %s started"), *Name);
}

void FDisplayClusterSessionBase::Stop()
{
	GetSocket()->Close();
	ThreadObj->WaitForCompletion();
}
