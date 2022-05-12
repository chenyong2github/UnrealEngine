// Copyright Epic Games, Inc. All Rights Reserved.

#include "IRewindDebuggerExtension.h"
#include "IRewindDebuggerViewCreator.h"
#include "IRewindDebuggerTrackCreator.h"
#include "IRewindDebuggerDoubleClickHandler.h"
#include "IRewindDebugger.h"

const FName IRewindDebuggerExtension::ModularFeatureName = "RewindDebuggerExtension";
const FName IRewindDebuggerViewCreator::ModularFeatureName = "RewindDebuggerViewCreator";
const FName IRewindDebuggerDoubleClickHandler::ModularFeatureName = "RewindDebuggerDoubleClickHandler";

namespace RewindDebugger
{
	const FName IRewindDebuggerTrackCreator::ModularFeatureName = "RewindDebuggerTrackCreator";
}

IRewindDebugger* IRewindDebugger::InternalInstance = nullptr;

IRewindDebugger::~IRewindDebugger()
{
}

IRewindDebugger::IRewindDebugger()
{
}

IRewindDebugger* IRewindDebugger::Instance()
{
	return InternalInstance;
}