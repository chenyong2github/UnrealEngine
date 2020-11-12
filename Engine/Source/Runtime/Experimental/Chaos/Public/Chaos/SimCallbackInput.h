// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Queue.h"
#include "Chaos/Defines.h"
#include "Chaos/Core.h"

namespace Chaos
{

class ISimCallbackObject;

struct FSimCallbackOutput
{
	FSimCallbackOutput()
		: InternalTime(-1)
	{
	}

	/** The internal time of the sim when this output was generated */
	FReal InternalTime;

protected:

	// Do not delete directly, use FreeOutputData_External
	~FSimCallbackOutput() = default;
};

struct FSimCallbackInput
{
	FSimCallbackInput()
	: ExternalTime(-1)
	, NumSteps(0)
	{
	}

	FReal GetExternalTime() const { return ExternalTime; }

	//Called by substep code so we can reuse input for multiple steps
	void SetNumSteps_External(int32 InNumSteps)
	{
		NumSteps = InNumSteps;
	}

protected:
	// Do not delete directly, use FreeInputData_Internal
	~FSimCallbackInput() = default;

private:
	/** The external time associated with this input */
	FReal ExternalTime;
	int32 NumSteps;	//the number of steps this input belongs to

	void Release_Internal(ISimCallbackObject& CallbackObj);

	friend class ISimCallbackObject;
};

struct FSimCallbackNoInput : public FSimCallbackInput
{
	void Reset(){}
};

struct FSimCallbackNoOutput : public FSimCallbackOutput
{
	void Reset() {}
};


}; // namespace Chaos
