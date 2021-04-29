// Copyright Epic Games, Inc. All Rights Reserved.

#include "IdleTask.h"

BEGIN_NAMESPACE_UE_AC

std::vector< FIdleTask* > Idlers;

static void DoIdle()
{
	for (std::vector< FIdleTask* >::iterator Iter = Idlers.begin(); Iter != Idlers.end(); ++Iter)
	{
		(**Iter).Idle();
	}
}

static void RegisterIdleCallback()
{
	static bool IsRehistered = false;
	if (!IsRehistered)
	{
		DGRegisterIdleCallBack(DoIdle);
		IsRehistered = true;
	}
}

// Contructor
FIdleTask::FIdleTask()
{
	Idlers.push_back(this);
	RegisterIdleCallback();
}

// Destructor
FIdleTask::~FIdleTask()
{
	for (std::vector< FIdleTask* >::iterator Iter = Idlers.begin(); Iter != Idlers.end(); ++Iter)
	{
		if (*Iter == this)
		{
			Idlers.erase(Iter);
			break;
		}
	}
}

// Time between calls to Idle
void FIdleTask::SetDelay(double DelayInSeconds)
{
	Delay = DelayInSeconds;
}

void FIdleTask::Start()
{
	NextIdle = 0;
}

void FIdleTask::Stop()
{
	NextIdle = INFINITY;
}

END_NAMESPACE_UE_AC
