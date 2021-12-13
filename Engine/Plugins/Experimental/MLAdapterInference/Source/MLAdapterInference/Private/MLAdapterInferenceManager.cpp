// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLAdapterInferenceManager.h"
#include "MLAdapterInferenceAgent.h"
#include "MLAdapterInferenceSettings.h"
#include "MLAdapterInferenceTypes.h"
#include "NeuralNetwork.h"
#include "Sessions/MLAdapterSession.h"


#if WITH_EDITORONLY_DATA
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#endif // WITH_EDITORONLY_DATA

void UMLAdapterInferenceManager::OnPostWorldInit(UWorld* World, const UWorld::InitializationValues)
{
	if (World && ShouldInitForWorld(*World))
	{
		LastActiveWorld = World;

		if (HasSession())
		{
			GetSession().OnPostWorldInit(*World);
		}

		// Create agents from config
		const TArrayView<const FInferenceAgentConfig>& AgentConfigs = UMLAdapterInferenceSettings::GetAgentConfigs();

		if (AgentConfigs.IsEmpty())
		{
			UE_LOG(LogMLAdapterInference, Warning, TEXT("AgentConfigs is empty so no agents will be constructed. Consider setting Edit->ProjectSettings->Engine->MLAdapter->AgentConfigs"));
		}

		for (const FInferenceAgentConfig& InferenceAgentConfig : AgentConfigs)
		{
			FMLAdapterAgentConfig AdapterAgentConfig = InferenceAgentConfig.AsMLAdapterAgentConfig();

			FMLAdapter::FAgentID AgentID = GetSession().AddAgent(AdapterAgentConfig);
			UE_LOG(LogMLAdapterInference, Log, TEXT("Created new agent of class %s with AgentID %d"), *AdapterAgentConfig.AgentClassName.ToString(), AgentID);

			UNeuralNetwork* Brain = (UNeuralNetwork*)InferenceAgentConfig.NeuralNetworkPath.TryLoad();
			checkf(Brain != nullptr, TEXT("Unable to load UNeuralNetwork from NeuralNetworkPath"));

			UMLAdapterInferenceAgent* Agent = Cast<UMLAdapterInferenceAgent>(GetSession().GetAgent(AgentID));
			Agent->Brain = Brain;
		}
	}
}
