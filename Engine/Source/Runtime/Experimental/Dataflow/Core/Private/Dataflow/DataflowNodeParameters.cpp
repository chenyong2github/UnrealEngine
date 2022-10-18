// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowNode.h"

namespace Dataflow
{
	uint64 FTimestamp::Invalid = 0;

	void FContextSingle::Evaluate(const FDataflowNode* Node, const FDataflowOutput* Output)
	{
		if (Node != nullptr)
		{
			Timestamp = FTimestamp(FPlatformTime::Cycles64());
			Node->Evaluate(*this, Output);
		}
	}
	
	bool FContextSingle::Evaluate(const FDataflowOutput& Connection)
	{
		return Connection.EvaluateImpl(*this);
	}



	void FContextThreaded::Evaluate(const FDataflowNode* Node, const FDataflowOutput* Output)
	{
		if (Node != nullptr)
		{
			Timestamp = FTimestamp(FPlatformTime::Cycles64());
			Node->Evaluate(*this, Output);
		}
	}

	bool FContextThreaded::Evaluate(const FDataflowOutput& Connection)
	{
		Connection.OutputLock->Lock(); ON_SCOPE_EXIT { Connection.OutputLock->Unlock(); };
		return Connection.EvaluateImpl(*this);
	}
}

