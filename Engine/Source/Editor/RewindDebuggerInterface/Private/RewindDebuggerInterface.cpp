// Copyright Epic Games, Inc. All Rights Reserved.

#include "IRewindDebuggerExtension.h"
#include "IRewindDebuggerViewCreator.h"
#include "IRewindDebuggerDoubleClickHandler.h"
#include "IRewindDebugger.h"

const FName IRewindDebuggerExtension::ModularFeatureName = "RewindDebuggerExtension";
const FName IRewindDebuggerViewCreator::ModularFeatureName = "RewindDebuggerViewCreator";
const FName IRewindDebuggerDoubleClickHandler::ModularFeatureName = "RewindDebuggerDoubleClickHandler";

IRewindDebugger::~IRewindDebugger()
{
}

IRewindDebugger::IRewindDebugger()
{
}