// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/TaskPrivate.h"
#include "Tasks/Pipe.h"

namespace UE { namespace Tasks { namespace Private
{
	bool FTaskBase::PushIntoPipe()
	{
		return GetPipe()->PushIntoPipe(*this);
	}

	void FTaskBase::StartPipeExecution()
	{
		if (GetPipe() != nullptr)
		{
			GetPipe()->ExecutionStarted();
		}
	}

	void FTaskBase::FinishPipeExecution()
	{
		if (GetPipe() != nullptr)
		{
			GetPipe()->ExecutionFinished();
			GetPipe()->ClearTask(*this);
		}
	}
}}}