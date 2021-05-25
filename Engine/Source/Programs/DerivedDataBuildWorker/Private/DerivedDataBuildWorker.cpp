// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "Containers/Array.h"
#include "CoreTypes.h"
#include "DerivedDataBuildFunction.h"
#include "DerivedDataBuildFunctionFactory.h"
#include "DerivedDataBuildLoop.h"
#include "Features/IModularFeatures.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeExit.h"
#include "RequiredProgramMainCPPInclude.h"

DEFINE_LOG_CATEGORY_STATIC(LogDerivedDataBuildWorker, Log, All);

//////////////////////////////////////////////////////////////////////////

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	FTaskTagScope Scope(ETaskTag::EGameThread);
	FString CmdLine = FCommandLine::BuildFromArgV(nullptr, ArgC, ArgV, TEXT("-ddc=None"));
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

	TMap<FName, const UE::DerivedData::IBuildFunction*> BuildFunctions;
	for (UE::DerivedData::IBuildFunctionFactory* FunctionFactory : IModularFeatures::Get().GetModularFeatureImplementations<UE::DerivedData::IBuildFunctionFactory>(UE::DerivedData::IBuildFunctionFactory::GetFeatureName()))
	{
		const UE::DerivedData::IBuildFunction& BuildFunction = FunctionFactory->GetFunction();
		BuildFunctions.FindOrAdd(FName(BuildFunction.GetName())) = &BuildFunction;
	}

	BuildLoop.PerformBuilds([&] (FName FunctionName, UE::DerivedData::FBuildContext& BuildContext)
		{
			if (const UE::DerivedData::IBuildFunction** FoundFunc = BuildFunctions.Find(FunctionName))
			{
				const UE::DerivedData::IBuildFunction* BuildFunction = *FoundFunc;
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
