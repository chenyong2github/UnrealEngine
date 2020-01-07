// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FZenaphore;

struct FZenaphoreWaiterNode
{
	FZenaphoreWaiterNode* Next = nullptr;
	bool bTriggered = false;
};

class FZenaphoreWaiter
{
public:
	FZenaphoreWaiter(FZenaphore& Outer, const TCHAR* WaitCpuScopeName)
		: Outer(Outer)
	{
	}

	void Wait();

private:
	friend class FZenaphore;

	FZenaphore& Outer;
	FZenaphoreWaiterNode WaiterNode;
	int32 SpinCount = 0;
};

class FZenaphore
{
public:
	FZenaphore();
	~FZenaphore();
	void NotifyOne();
	void NotifyAll();

private:
	friend class FZenaphoreWaiter;

	FEvent* Event;
	FCriticalSection Mutex;
	TAtomic<FZenaphoreWaiterNode*> HeadWaiter { nullptr };
};

