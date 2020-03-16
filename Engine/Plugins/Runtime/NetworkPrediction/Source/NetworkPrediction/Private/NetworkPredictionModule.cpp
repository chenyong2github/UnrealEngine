// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"
#include "Engine/World.h"
#include "NetworkedSimulationModelCues.h"
#include "Misc/CoreDelegates.h"
#include "Trace/NetworkPredictionTrace.h"
#include "Trace/Trace.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "String/ParseTokens.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "FNetworkPredictionModule"

class FNetworkPredictionModule : public INetworkPredictionModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	FDelegateHandle PieHandle;
};

void FNetworkPredictionModule::StartupModule()
{
	// Disable by default unless in the command line args. This is temp as the existing insights -trace parsing happen before the plugin is loaded
	Trace::ToggleChannel(TEXT("NetworkPredictionChannel"), false);

	FString EnabledChannels;
	FParse::Value(FCommandLine::Get(), TEXT("-trace="), EnabledChannels, false);
	UE::String::ParseTokens(EnabledChannels, TEXT(","), [](FStringView Token) {
		if (Token.Compare(TEXT("NetworkPrediction"), ESearchCase::IgnoreCase)==0 || Token.Compare(TEXT("NP"), ESearchCase::IgnoreCase)==0)
		{
			Trace::ToggleChannel(TEXT("NetworkPredictionChannel"), true);
		}
	});

	FCoreDelegates::OnPostEngineInit.AddLambda([]()
	{
		FGlobalCueTypeTable::Get().FinalizeTypes();
	});

#if WITH_EDITOR
	PieHandle = FEditorDelegates::PreBeginPIE.AddLambda([this](const bool bBegan)
	{
		UE_NP_TRACE_PIE_START();
	});
#endif
}


void FNetworkPredictionModule::ShutdownModule()
{
#if WITH_EDITOR
	FEditorDelegates::PreBeginPIE.Remove(PieHandle);
#endif
}

IMPLEMENT_MODULE( FNetworkPredictionModule, NetworkPrediction )
#undef LOCTEXT_NAMESPACE

