// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

BEGIN_NAMESPACE_UE_AC

// Schedule an idle task
class FIdleTask
{
  public:
	// Contructor
	FIdleTask();

	// Destructor
	virtual ~FIdleTask();

	// Time between calls to Idle
	void SetDelay(double DelayInSeconds);

	void Start();

	void Stop();

	// Print time differences
	virtual void Idle() = 0;

  private:
	double NextIdle = 0.0;
	double Delay = 1.0;
};

class FTestIdleTask : public FIdleTask
{
	virtual void Idle() override
	{
		if (++count == 100)
		{
			UE_AC_TraceF("FTestIdleTask::Idle\n");
			count = 0;
		}
	}

	int count = 0;
};

END_NAMESPACE_UE_AC
