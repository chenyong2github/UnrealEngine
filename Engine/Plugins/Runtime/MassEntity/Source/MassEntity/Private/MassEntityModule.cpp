// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "IMassEntityModule.h"
#if WITH_UNREAL_DEVELOPER_TOOLS
#include "MessageLogModule.h"
#include "Engine/World.h"
#include "Logging/MessageLog.h"
#endif // WITH_UNREAL_DEVELOPER_TOOLS

#define LOCTEXT_NAMESPACE "Pipe"

class FPipeModuleModule : public IPipeModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
#if WITH_UNREAL_DEVELOPER_TOOLS
	static void OnWorldCleanup(UWorld* /*World*/, bool /*bSessionEnded*/, bool /*bCleanupResources*/);
	FDelegateHandle OnWorldCleanupHandle;
#endif // WITH_UNREAL_DEVELOPER_TOOLS
};

IMPLEMENT_MODULE(FPipeModuleModule, Pipe)


void FPipeModuleModule::StartupModule()
{
	// This code will execute after your module is loaded into memory (but after global variables are initialized, of course.)

#if WITH_UNREAL_DEVELOPER_TOOLS
	{
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		FMessageLogInitializationOptions InitOptions;
		InitOptions.bShowPages = true;
		InitOptions.bShowFilters = true;
		MessageLogModule.RegisterLogListing("Pipe", LOCTEXT("Pipe", "Pipe"), InitOptions);

		OnWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&FPipeModuleModule::OnWorldCleanup);
	}
#endif // WITH_UNREAL_DEVELOPER_TOOLS
}


void FPipeModuleModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
#if WITH_UNREAL_DEVELOPER_TOOLS
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupHandle);
#endif // WITH_UNREAL_DEVELOPER_TOOLS
}

#if WITH_UNREAL_DEVELOPER_TOOLS
void FPipeModuleModule::OnWorldCleanup(UWorld* /*World*/, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
{
	// clearing out messages from the world being cleaned up
	FMessageLog("Pipe").NewPage(FText::FromString(TEXT("Pipe")));
}
#endif // WITH_UNREAL_DEVELOPER_TOOLS

#undef LOCTEXT_NAMESPACE 
