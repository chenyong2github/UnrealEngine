// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsCritic.h"

#include "LearningAgentsManager.h"
#include "LearningAgentsInteractor.h"
#include "LearningFeatureObject.h"
#include "LearningNeuralNetwork.h"
#include "LearningNeuralNetworkObject.h"
#include "LearningLog.h"

#include "UObject/Package.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/Actor.h"

ULearningAgentsCritic::ULearningAgentsCritic() : ULearningAgentsManagerComponent() {}
ULearningAgentsCritic::ULearningAgentsCritic(FVTableHelper& Helper) : ULearningAgentsCritic() {}
ULearningAgentsCritic::~ULearningAgentsCritic() {}

void ULearningAgentsCritic::SetupCritic(ALearningAgentsManager* InAgentManager, ULearningAgentsInteractor* InInteractor, const FLearningAgentsCriticSettings& CriticSettings)
{
	if (IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup already run!"), *GetName());
		return;
	}

	if (!InAgentManager)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InAgentManager is nullptr."), *GetName());
		return;
	}

	if (!InAgentManager->IsManagerSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's SetupManager must be run before it can be used."), *GetName(), *InAgentManager->GetName());
		return;
	}

	AgentManager = InAgentManager;

	if (!InInteractor)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InInteractor is nullptr."), *GetName());
		return;
	}

	if (!InInteractor->IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's Setup must be run before it can be used."), *GetName(), *InInteractor->GetName());
		return;
	}

	Interactor = InInteractor;

	// Setup Neural Network
	Network = NewObject<ULearningAgentsNeuralNetwork>(this, TEXT("CriticNetwork"));
	Network->NeuralNetwork = MakeShared<UE::Learning::FNeuralNetwork>();
	Network->NeuralNetwork->Resize(
		Interactor->GetObservationFeature().DimNum(),
		1,
		CriticSettings.HiddenLayerSize,
		CriticSettings.LayerNum);
	Network->NeuralNetwork->ActivationFunction = UE::Learning::Agents::GetActivationFunction(CriticSettings.ActivationFunction);

	// Create Critic Object
	CriticObject = MakeShared<UE::Learning::FNeuralNetworkCriticFunction>(
		TEXT("CriticObject"),
		AgentManager->GetInstanceData().ToSharedRef(),
		AgentManager->GetMaxInstanceNum(),
		Network->NeuralNetwork.ToSharedRef());

	AgentManager->GetInstanceData()->Link(Interactor->GetObservationFeature().FeatureHandle, CriticObject->InputHandle);

	bIsSetup = true;
}

UE::Learning::FNeuralNetwork& ULearningAgentsCritic::GetCriticNetwork()
{
	return *Network->NeuralNetwork;
}

UE::Learning::FNeuralNetworkCriticFunction& ULearningAgentsCritic::GetCriticObject()
{
	return *CriticObject;
}

void ULearningAgentsCritic::LoadCriticFromSnapshot(const FDirectoryPath& Directory, const FString Filename)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return;
	}

	TArray64<uint8> NetworkData;
	const FString FilePath = Directory.Path + FGenericPlatformMisc::GetDefaultPathSeparator() + Filename;
	if (FFileHelper::LoadFileToArray(NetworkData, *FilePath))
	{
		const int32 TotalByteNum = UE::Learning::FNeuralNetwork::GetSerializationByteNum(
			Network->NeuralNetwork->GetInputNum(),
			Network->NeuralNetwork->GetOutputNum(),
			Network->NeuralNetwork->GetHiddenNum(),
			Network->NeuralNetwork->GetLayerNum());

		if (NetworkData.Num() != TotalByteNum)
		{
			UE_LOG(LogLearning, Error, TEXT("%s: Failed to load network from file %s. File size incorrect."), *GetName(), *FilePath);
			return;
		}

		Network->NeuralNetwork->DeserializeFromBytes(NetworkData);
	}
	else
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Failed to load network. File not found: %s"), *GetName(), *FilePath);
	}
}

void ULearningAgentsCritic::SaveCriticToSnapshot(const FDirectoryPath& Directory, const FString Filename) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return;
	}

	TArray64<uint8> NetworkData;
	NetworkData.SetNumUninitialized(UE::Learning::FNeuralNetwork::GetSerializationByteNum(
		Network->NeuralNetwork->GetInputNum(),
		Network->NeuralNetwork->GetOutputNum(),
		Network->NeuralNetwork->GetHiddenNum(),
		Network->NeuralNetwork->GetLayerNum()));

	Network->NeuralNetwork->SerializeToBytes(NetworkData);

	const FString FilePath = Directory.Path + FGenericPlatformMisc::GetDefaultPathSeparator() + Filename;
	if (!FFileHelper::SaveArrayToFile(NetworkData, *FilePath))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Failed to save network to file: %s"), *GetName(), *FilePath);
	}
}

void ULearningAgentsCritic::LoadCriticFromAsset(const ULearningAgentsNeuralNetwork* NeuralNetworkAsset)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return;
	}

	if (!NeuralNetworkAsset || !NeuralNetworkAsset->NeuralNetwork)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Asset is invalid."), *GetName());
		return;
	}

	if (NeuralNetworkAsset->NeuralNetwork->GetInputNum() != Network->NeuralNetwork->GetInputNum() ||
		NeuralNetworkAsset->NeuralNetwork->GetOutputNum() != Network->NeuralNetwork->GetOutputNum())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Failed to load network from asset. Inputs and outputs don't match."), *GetName());
		return;
	}

	*Network->NeuralNetwork = *NeuralNetworkAsset->NeuralNetwork;
}

void ULearningAgentsCritic::SaveCriticToAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return;
	}

	if (!NeuralNetworkAsset)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Asset is invalid."), *GetName());
		return;
	}

	if (!NeuralNetworkAsset->NeuralNetwork)
	{
		NeuralNetworkAsset->NeuralNetwork = MakeShared<UE::Learning::FNeuralNetwork>();
	}

	*NeuralNetworkAsset->NeuralNetwork = *Network->NeuralNetwork;

	// Manually mark the package as dirty since just using `Modify` prevents 
	// marking packages as dirty during PIE which is most likely when this
	// is being used.
	if (UPackage* Package = NeuralNetworkAsset->GetPackage())
	{
		const bool bIsDirty = Package->IsDirty();

		if (!bIsDirty)
		{
			Package->SetDirtyFlag(true);
		}

		Package->PackageMarkedDirtyEvent.Broadcast(Package, bIsDirty);
	}
}

void ULearningAgentsCritic::EvaluateCritic()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsCritic::EvaluateCritic);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return;
	}

	CriticObject->Evaluate(AddedAgentSet);

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	VisualLog(AddedAgentSet);
#endif
}

float ULearningAgentsCritic::GetEstimatedDiscountedReturn(const int32 AgentId) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not run."), *GetName());
		return 0.0f;
	}

	if (!HasAgent(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: AgentId %d not found in the agents set."), *GetName(), AgentId);
		return 0.0f;
	}

	const TLearningArrayView<1, const float> CriticOutputView = CriticObject->InstanceData->ConstView(CriticObject->OutputHandle);

	return CriticOutputView[AgentId];
}

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
void ULearningAgentsCritic::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsCritic::VisualLog);

	const TLearningArrayView<2, const float> InputView = CriticObject->InstanceData->ConstView(CriticObject->InputHandle);
	const TLearningArrayView<1, const float> OutputView = CriticObject->InstanceData->ConstView(CriticObject->OutputHandle);

	for (const int32 Instance : Instances)
	{
		if (const AActor* Actor = Cast<AActor>(Interactor->GetAgent(Instance)))
		{
			UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
				Actor->GetActorLocation(),
				VisualLogColor.ToFColor(true),
				TEXT("Agent %i\nInput: %s\nInput Stats (Min/Max/Mean/Std): %s\nOutput: [% 6.3f]"),
				Instance,
				*UE::Learning::Array::FormatFloat(InputView[Instance]),
				*UE::Learning::Agents::Debug::FloatArrayToStatsString(InputView[Instance]),
				OutputView[Instance]);
		}
	}
}
#endif
