// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Queue.h"
#include "Chaos/Defines.h"
#include "Chaos/Core.h"

namespace Chaos
{

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
	{
	}

	FReal GetExternalTime() const { return ExternalTime; }

protected:
	// Do not delete directly, use FreeInputData_Internal
	~FSimCallbackInput() = default;

private:
	/** The external time associated with this input */
	FReal ExternalTime;

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
