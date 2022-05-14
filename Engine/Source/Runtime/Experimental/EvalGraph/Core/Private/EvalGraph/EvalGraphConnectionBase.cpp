// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphConnectionBase.h"

#include "EvalGraph/EvalGraphNodeParameters.h"
#include "EvalGraph/EvalGraphNode.h"

namespace Eg
{
	FConnectionBase::FConnectionBase(FName InType, FName InName, FNode* InOwningNode, FGuid InGuid)
		: Type(InType)
		, Name(InName)
		, Guid(InGuid)
		, OwningNode(InOwningNode)
	{}

	void FConnectionBase::AddBaseInput(FNode* InNode, FConnectionBase* That) 
	{ InNode->AddBaseInput(That); }
	void FConnectionBase::AddBaseOutput(FNode* InNode, FConnectionBase* That) 
	{ InNode->AddBaseOutput(That); }

}

