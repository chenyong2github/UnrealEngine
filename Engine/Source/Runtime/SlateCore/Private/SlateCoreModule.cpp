// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Debugging/ConsoleSlateDebugger.h"
#include "Debugging/ConsoleSlateDebuggerInvalidate.h"
#include "Debugging/ConsoleSlateDebuggerPaint.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "SlateGlobals.h"
#include "Types/SlateStructs.h"


DEFINE_LOG_CATEGORY(LogSlate);
DEFINE_LOG_CATEGORY(LogSlateStyles);

const float FOptionalSize::Unspecified = -1.0f;


/**
 * Implements the SlateCore module.
 */
class FSlateCoreModule
	: public IModuleInterface
{
public:
	FSlateCoreModule()
	{
#if WITH_SLATE_DEBUGGING
		SlateDebuggerEvent = MakeUnique<FConsoleSlateDebugger>();
		SlateDebuggerInvalidate = MakeUnique<FConsoleSlateDebuggerInvalidate>();
		SlateDebuggerPaint = MakeUnique<FConsoleSlateDebuggerPaint>();
#endif
	}

#if WITH_SLATE_DEBUGGING
private:
	TUniquePtr<FConsoleSlateDebugger> SlateDebuggerEvent;
	TUniquePtr<FConsoleSlateDebuggerInvalidate> SlateDebuggerInvalidate;
	TUniquePtr<FConsoleSlateDebuggerPaint> SlateDebuggerPaint;
#endif
};


IMPLEMENT_MODULE(FSlateCoreModule, SlateCore);
