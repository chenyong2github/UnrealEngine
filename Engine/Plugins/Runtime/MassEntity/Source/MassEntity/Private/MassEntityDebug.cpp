// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityDebug.h"
#include "Misc/OutputDevice.h"
#include "MassProcessor.h"


DEFINE_ENUM_TO_STRING(EPipeProcessingPhase);

namespace UE { namespace Pipe { namespace Debug {

void DebugOutputDescription(TConstArrayView<UPipeProcessor*> Processors, FOutputDevice& Ar)
{
#if WITH_PIPE_DEBUG
	const bool bAutoLineEnd = Ar.GetAutoEmitLineTerminator();
	Ar.SetAutoEmitLineTerminator(false);
	for (const UPipeProcessor* Proc : Processors)
	{
		if (Proc)
		{
			Proc->DebugOutputDescription(Ar);
			Ar.Logf(TEXT("\n"));
		}
		else
		{
			Ar.Logf(TEXT("NULL\n"));
		}
	}
	Ar.SetAutoEmitLineTerminator(bAutoLineEnd);
#endif // WITH_PIPE_DEBUG
}

}}} // namespace UE::Pipe::Debug

