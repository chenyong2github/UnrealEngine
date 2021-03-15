// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/TaskPrivate.h"
#include "Tasks/Pipe.h"

namespace UE { namespace Tasks { namespace Private
{
	bool FTaskBase::PushIntoPipe()
	{
		return GetPipe()->PushIntoPipe(*this);
	}

	void FTaskBase::UnblockPipe()
	{
		if (GetPipe() != nullptr)
		{
			check(bPushedIntoPipe);
			GetPipe()->ClearTask(*this);
		}
	}
}}}