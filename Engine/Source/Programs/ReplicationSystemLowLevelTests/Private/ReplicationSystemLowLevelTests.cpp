// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/IrisConfig.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"
#include "Net/Core/Trace/Private/NetTraceInternal.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

#include "TestCommon/Initialization.h"

#include <catch2/catch_test_macros.hpp>

GROUP_BEFORE_GLOBAL(Catch::DefaultGroup)
{
	// Initialize trace
	FString Parameter;
	if (FParse::Value(FCommandLine::Get(), TEXT("-trace="), Parameter, false))
	{
		FTraceAuxiliary::Initialize(FCommandLine::Get());
		FTraceAuxiliary::TryAutoConnect();
	}

#if UE_NET_TRACE_ENABLED
	uint32 NetTraceVerbosity;
	if(FParse::Value(FCommandLine::Get(), TEXT("-nettrace="), NetTraceVerbosity))
	{
		FNetTrace::SetTraceVerbosity(NetTraceVerbosity);
	}
#endif

	UE::Net::SetUseIrisReplication(true);

	InitAll(true, true);

	FModuleManager::Get().LoadModule(TEXT("IrisCore"));
}

GROUP_AFTER_GLOBAL(Catch::DefaultGroup)
{
	CleanupAll();
}
