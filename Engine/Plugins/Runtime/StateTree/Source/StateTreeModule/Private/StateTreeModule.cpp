// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeModule.h"
#include "StateTreeTypes.h"

#if WITH_STATETREE_DEBUGGER
#include "Debugger/StateTreeTraceModule.h"
#include "Features/IModularFeatures.h"
#include "Misc/CoreDelegates.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "StateTreeSettings.h"
#include "Trace/StoreClient.h"
#include "Trace/StoreService.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"

#if WITH_TRACE_STORE
#include "Misc/Paths.h"
#endif // WITH_TRACE_STORE

#endif // WITH_STATETREE_DEBUGGER

#define LOCTEXT_NAMESPACE "StateTree"

class FStateTreeModule : public IStateTreeModule
{
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void StartTraces();
	void StopTraces();

#if WITH_STATETREE_DEBUGGER
	/**
	 * Gets the store client.
	 */
	virtual UE::Trace::FStoreClient* GetStoreClient() override  { return StoreClient.Get(); }
	
	TSharedPtr<TraceServices::IAnalysisService> TraceAnalysisService;
	TSharedPtr<TraceServices::IModuleService> TraceModuleService;

	/** The location of the trace files managed by the trace store. */
	FString StoreDir;

	/** The client used to connect to the trace store. */
	TUniquePtr<UE::Trace::FStoreClient> StoreClient;

#if WITH_TRACE_STORE
	TUniquePtr<UE::Trace::FStoreService> StoreService;
#endif // WITH_TRACE_STORE
	
	FStateTreeTraceModule StateTreeTraceModule;
	FDelegateHandle StoreServiceHandle;
#endif // WITH_STATETREE_DEBUGGER
};

IMPLEMENT_MODULE(FStateTreeModule, StateTreeModule)

void FStateTreeModule::StartupModule()
{
	StartTraces();
}

void FStateTreeModule::ShutdownModule()
{
	StopTraces();
}

void FStateTreeModule::StartTraces()
{
#if WITH_STATETREE_DEBUGGER
	if (!UStateTreeSettings::Get().bUseDebugger || IsRunningCommandlet())
	{
		return;
	}

	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	TraceAnalysisService = TraceServicesModule.GetAnalysisService();
	TraceModuleService = TraceServicesModule.GetModuleService();

	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, &StateTreeTraceModule);

	if (!FTraceAuxiliary::IsConnected())
	{
		// cpu tracing is enabled by default, but not connected.
		// when not connected, disable all channels initially to avoid a whole bunch of extra cpu, memory, and disk overhead from processing all the extra default trace channels
		////////////////////////////////////////////////////////////////////////////////
		UE::Trace::EnumerateChannels([](const ANSICHAR* ChannelName, const bool bEnabled, void*)
			{
				if (bEnabled)
				{
					FString ChannelNameFString(ChannelName);
					UE::Trace::ToggleChannel(ChannelNameFString.GetCharArray().GetData(), false);
				}
			}
			, nullptr);
	}

	UE::Trace::ToggleChannel(TEXT("StateTreeDebugChannel"), true);
	UE::Trace::ToggleChannel(TEXT("FrameChannel"), true);

	// FTraceAuxiliary::FOptions Options;
	// Options.bExcludeTail = true;
	// FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::Network, TEXT("127.0.0.1"), TEXT(""), &Options, LogStateTree);
	FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::Network, TEXT("127.0.0.1"), TEXT(""), nullptr, LogStateTree);

	// Conditionally create local store service after engine init (if someone doesn't beat us to it).
	// This is temp until a more formal local server is done by the insights system.
	StoreServiceHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]
	{
		LLM_SCOPE_BYNAME(TEXT("StateTree"));

		if (!GetStoreClient())
		{
#if WITH_TRACE_STORE
			UE_LOG(LogCore, Display, TEXT("StateTree module auto-connecting to internal trace server..."));

			// Create the Store Service.
			StoreDir = FPaths::ProjectSavedDir() / TEXT("TraceSessions");
			UE::Trace::FStoreService::FDesc StoreServiceDesc;
			StoreServiceDesc.StoreDir = *StoreDir;
			StoreServiceDesc.RecorderPort = 0; // Let system decide port
			StoreServiceDesc.ThreadCount = 2;
			StoreService = TUniquePtr<UE::Trace::FStoreService>(UE::Trace::FStoreService::Create(StoreServiceDesc));
			if (StoreService.IsValid())
			{
				// Connect to our newly created store
				StoreClient = TUniquePtr<UE::Trace::FStoreClient>(UE::Trace::FStoreClient::Connect(TEXT("localhost")));
				UE::Trace::SendTo(TEXT("localhost"), StoreService->GetRecorderPort());
				
				FCoreDelegates::OnPreExit.AddLambda([this]() { StoreService.Reset(); });
			}
#else
			UE_LOG(LogCore, Display, TEXT("StateTree module auto-connecting to local trace server..."));

			StoreClient = TUniquePtr<UE::Trace::FStoreClient>(UE::Trace::FStoreClient::Connect(TEXT("localhost")));
			if (StoreClient.IsValid())
			{
				const UE::Trace::FStoreClient::FStatus* Status = StoreClient->GetStatus();
				StoreDir = FString(Status->GetStoreDir());
			}
#endif // WITH_TRACE_STORE
		}
	});
#endif // WITH_STATETREE_DEBUGGER
}

void FStateTreeModule::StopTraces()
{
#if WITH_STATETREE_DEBUGGER
	if (StoreServiceHandle.IsValid())
	{
		if (FTraceAuxiliary::IsConnected())
		{
			FTraceAuxiliary::Stop();
		}
		
		IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &StateTreeTraceModule);
		
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(StoreServiceHandle);
		StoreServiceHandle.Reset();
	}
#endif // WITH_STATETREE_DEBUGGER
}

#undef LOCTEXT_NAMESPACE
