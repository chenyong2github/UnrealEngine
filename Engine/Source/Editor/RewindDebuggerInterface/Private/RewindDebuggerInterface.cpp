// Copyright Epic Games, Inc. All Rights Reserved.

#include "IRewindDebuggerExtension.h"
#include "IRewindDebuggerViewCreator.h"
#include "IRewindDebugger.h"

const FName IRewindDebuggerExtension::ModularFeatureName = "RewindDebuggerExtension";
const FName IRewindDebuggerViewCreator::ModularFeatureName = "RewindDebuggerViewCreator";

IRewindDebugger::~IRewindDebugger()
{
}

IRewindDebugger::IRewindDebugger()
{
}