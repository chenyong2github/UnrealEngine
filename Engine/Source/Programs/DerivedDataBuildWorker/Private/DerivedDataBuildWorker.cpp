// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "Containers/Array.h"
#include "CoreTypes.h"
#include "DerivedDataBuildLoop.h"
#include "DerivedDataBuildWorkerInterface.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeExit.h"
#include "RequiredProgramMainCPPInclude.h"

DEFINE_LOG_CATEGORY_STATIC(LogDerivedDataBuildWorker, Log, All);

//////////////////////////////////////////////////////////////////////////

extern void DerivedDataBuildWorkerInit();

namespace UE::DerivedData
{

TMap<FName, IBuildFunction*> GBuildFunctions;

void RegisterWorkerBuildFunction(IBuildFunction* BuildFunction)
{
	GBuildFunctions.FindOrAdd(FName(BuildFunction->GetName())) = BuildFunction;
}

}

//////////////////////////////////////////////////////////////////////////

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	FTaskTagScope Scope(ETaskTag::EGameThread);
	FString CmdLine = FCommandLine::BuildFromArgV(nullptr, ArgC, ArgV, nullptr);
	GEngineLoop.PreInit(*CmdLine);

	// Make sure the engine is properly cleaned up whenever we exit this function
	ON_SCOPE_EXIT
	{
		FEngineLoop::AppPreExit();
		FEngineLoop::AppExit();
	};

	UE::DerivedData::FBuildLoop BuildLoop;
	if (!BuildLoop.Init())
	{
		return 1;
	}

	DerivedDataBuildWorkerInit();

	BuildLoop.PerformBuilds([&] (FName FunctionName, UE::DerivedData::FBuildContext& BuildContext)
		{
			if (UE::DerivedData::IBuildFunction** FoundFunc = UE::DerivedData::GBuildFunctions.Find(FunctionName))
			{
				UE::DerivedData::IBuildFunction* BuildFunction = *FoundFunc;
				UE_LOG(LogDerivedDataBuildWorker, Display, TEXT("Starting build function '%s'"), *FunctionName.ToString());
				uint64 BuildStartTime = FPlatformTime::Cycles64();
				BuildFunction->Build(BuildContext);
				UE_LOG(LogDerivedDataBuildWorker, Display, TEXT("Completed in %fms"), FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64()-BuildStartTime));

				return true;
			}
			UE_LOG(LogDerivedDataBuildWorker, Error, TEXT("Unknown build function: %s"), *FunctionName.ToString());
			return false;
		});

	BuildLoop.Teardown();

	return 0;
}
