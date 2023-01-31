// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Misc/Optional.h"

class FControlFlow;

// In contrast to, for example, iterator loops
class CONTROLFLOWS_API FConditionalLoop : public TSharedFromThis<FConditionalLoop>
{
public:
	FConditionalLoop();
	
	FControlFlow& CheckConditionFirst();
	FControlFlow& RunLoopFirst();
	FControlFlow& ExecuteAtLeastOnce();
	FControlFlow& SetCheckConditionFirst(bool bInValue);

private:
	friend class FControlFlowTask_ConditionalLoop;

	TOptional<bool> CheckConditionalFirst;
	TSharedRef<FControlFlow> FlowLoop;
};