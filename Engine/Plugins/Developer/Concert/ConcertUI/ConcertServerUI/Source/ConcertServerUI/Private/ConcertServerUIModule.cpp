// Copyright Epic Games, Inc. All Rights Reserved.

#include "IConcertServerUIModule.h"

#include "ConcertConsoleCommandExecutor.h"
#include "ConcertServerStyle.h"
#include "ConcertSyncServerLoopInitArgs.h"
#include "Widgets/ConcertServerWindowController.h"

#include "Features/IModularFeatures.h"
#include "Framework/Application/SlateApplication.h"
#include "StandaloneRenderer.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

/**
 * 
 */
class FConcertServerUIModule : public IConcertServerUIModule
{
public:
	virtual void StartupModule() override 
	{
		MultiUserServerLayoutIni = GConfig->GetConfigFilename(TEXT("MultiUserServerLayout"));
	}
	
	virtual void ShutdownModule() override
	{
		FConcertServerStyle::Shutdown();
		
		WindowController.Reset();
		FSlateApplication::Shutdown();
	}
	
	virtual void InitSlateForServer(FConcertSyncServerLoopInitArgs& InitArgs) override
	{
		if (ensureMsgf(!WindowController.IsValid(), TEXT("InitSlateForServer is designed to be called at most once to create UI to run alongside the Multi User server.")))
		{
			PreInitializeMultiUser();
			InitArgs.PostInitServerLoop.AddRaw(this, &FConcertServerUIModule::InitializeSlateApplication);
			InitArgs.TickPostGameThread.AddRaw(this, &FConcertServerUIModule::TickSlate);
		}
	}
	
private:

	/** Config path storing layout config. */
	FString MultiUserServerLayoutIni;

	/** Handles execution of commands */
	TUniquePtr<FConcertConsoleCommandExecutor> CommandExecutor;
	
	/** Creates and manages window. Only one instance per application. */
	TSharedPtr<FConcertServerWindowController> WindowController;

	void PreInitializeMultiUser()
	{
		FModuleManager::Get().LoadModuleChecked("EditorStyle");
		FConcertServerStyle::Initialize();

		// Log history must be initialized before MU server loop init prints any messages	
		FModuleManager::Get().LoadModuleChecked("OutputLog");
	}
	
	void InitializeSlateApplication(TSharedRef<IConcertSyncServer> SyncServer)
	{
		FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());
		const FText ApplicationTitle = LOCTEXT("AppTitle", "Unreal Multi User Server");
		FGlobalTabmanager::Get()->SetApplicationTitle(ApplicationTitle);

		CommandExecutor = MakeUnique<FConcertConsoleCommandExecutor>();
		IModularFeatures::Get().RegisterModularFeature(IConsoleCommandExecutor::ModularFeatureName(), CommandExecutor.Get());
        
        WindowController = MakeShared<FConcertServerWindowController>(FConcertServerWindowInitParams{ SyncServer, MultiUserServerLayoutIni });
	}
	
	void TickSlate(double Tick)
	{
		FSlateApplication::Get().PumpMessages();
		FSlateApplication::Get().Tick();
	}
};

IMPLEMENT_MODULE(FConcertServerUIModule, ConcertSyncServerUI);

#undef LOCTEXT_NAMESPACE