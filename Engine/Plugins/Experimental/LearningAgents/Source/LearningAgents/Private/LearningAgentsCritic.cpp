// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsCritic.h"

#include "LearningAgentsType.h"
#include "LearningFeatureObject.h"
#include "LearningNeuralNetwork.h"
#include "LearningNeuralNetworkObject.h"
#include "LearningLog.h"

#include "UObject/Package.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/Actor.h"
#include "VisualLogger/VisualLogger.h"

#define UE_LEARNING_AGENTS_VLOG_STRING(Owner, Category, Verbosity, Location, Color, Format, ...) \
	UE_VLOG_LOCATION(Owner, Category, Verbosity, Location, 0.0f, Color, Format, ##__VA_ARGS__)

namespace UE::Learning::Agents::Critic::Private
{
	static inline FString ArrayToString(const TLearningArrayView<1, const float> Array)
	{
		const int32 ItemNum = Array.Num();
		const int32 MaxItemNum = 32;
		const int32 OutputItemNum = FMath::Min(ItemNum, MaxItemNum);

		FString Output = TEXT("[");

		for (int32 Idx = 0; Idx < OutputItemNum; Idx++)
		{
			Output.Appendf(TEXT("% 6.3f"), Array[Idx]);

			if (Idx < OutputItemNum - 1)
			{
				Output += TEXT(" ");
			}
			else if (Idx == ItemNum - 1)
			{
				Output += TEXT("]");
			}
			else if (Idx == MaxItemNum - 1)
			{
				Output += TEXT("...]");
			}
		}


		return Output;
	}

	static inline FString ArrayToStatsString(const TLearningArrayView<1, const float> Array)
	{
		const int32 ItemNum = Array.Num();

		float Min = +FLT_MAX, Max = -FLT_MAX, Mean = 0.0f;
		for (int32 Idx = 0; Idx < ItemNum; Idx++)
		{
			Min = FMath::Min(Min, Array[Idx]);
			Max = FMath::Max(Max, Array[Idx]);
			Mean += Array[Idx] / ItemNum;
		}

		float Var = 0.0f;
		for (int32 Idx = 0; Idx < ItemNum; Idx++)
		{
			Var += FMath::Square(Array[Idx] - Mean) / ItemNum;
		}

		return FString::Printf(TEXT("[% 6.3f/% 6.3f/% 6.3f/% 6.3f]"),
			Min, Max, Mean, FMath::Sqrt(Var));
	}
}

ULearningAgentsCritic::ULearningAgentsCritic() : UActorComponent() {}
ULearningAgentsCritic::ULearningAgentsCritic(FVTableHelper& Helper) : ULearningAgentsCritic() {}
ULearningAgentsCritic::~ULearningAgentsCritic() {}

void ULearningAgentsCritic::SetupCritic(ULearningAgentsType* InAgentType, const FLearningAgentsCriticSettings& CriticSettings)
{
	if (IsCriticSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup already performed!"));
		return;
	}

	// Setup Agent Type

	if (!InAgentType)
	{
		UE_LOG(LogLearning, Error, TEXT("SetupCritic called but AgentType is nullptr."));
		return;
	}

	if (!InAgentType->IsSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentType Setup must be run before critic can be setup."));
		return;
	}

	AgentType = InAgentType;

	// Setup Neural Network

	Network = NewObject<ULearningAgentsNeuralNetwork>(this, TEXT("CriticNetwork"));
	Network->NeuralNetwork = MakeShared<UE::Learning::FNeuralNetwork>();
	Network->NeuralNetwork->Resize(
		AgentType->GetObservationFeature().DimNum(),
		1,
		CriticSettings.HiddenLayerSize,
		CriticSettings.LayerNum);
	Network->NeuralNetwork->ActivationFunction = UE::Learning::Agents::GetActivationFunction(CriticSettings.ActivationFunction);

	// Create Critic Object

	CriticObject = MakeShared<UE::Learning::FNeuralNetworkCriticFunction>(
		TEXT("CriticObject"),
		AgentType->GetInstanceData().ToSharedRef(),
		AgentType->GetMaxInstanceNum(),
		Network->NeuralNetwork.ToSharedRef());

	AgentType->GetInstanceData()->Link(AgentType->GetObservationFeature().FeatureHandle, CriticObject->InputHandle);

	// Done!
	bCriticSetupPerformed = true;
}

bool ULearningAgentsCritic::IsCriticSetupPerformed() const
{
	return bCriticSetupPerformed;
}

void ULearningAgentsCritic::AddAgent(int32 AgentId)
{
	if (!IsCriticSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Critic setup must be run before agents can be added!"));
		return;
	}

	if (!AgentType->GetOccupiedAgentSet().Contains(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("Unable to add: AgentId %d not found on AgentType. Make sure to add agents to the agent type before adding."), AgentId);
		return;
	}

	if (SelectedAgentIds.Contains(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %i is already included in agents set"), AgentId);
		return;
	}

	SelectedAgentIds.Add(AgentId);
	SelectedAgentsSet = SelectedAgentIds;
	SelectedAgentsSet.TryMakeSlice();
}

void ULearningAgentsCritic::RemoveAgent(int32 AgentId)
{
	if (!IsCriticSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Critic setup must be run before agents can be removed!"));
		return;
	}

	if (SelectedAgentIds.RemoveSingleSwap(AgentId, false) == 0)
	{
		UE_LOG(LogLearning, Error, TEXT("Unable to remove: AgentId %d not found in the added agents set."), AgentId);
		return;
	}

	SelectedAgentsSet = SelectedAgentIds;
	SelectedAgentsSet.TryMakeSlice();
}

bool ULearningAgentsCritic::HasAgent(int32 AgentId) const
{
	return SelectedAgentsSet.Contains(AgentId);
}

ULearningAgentsType* ULearningAgentsCritic::GetAgentType(TSubclassOf<ULearningAgentsType> AgentClass)
{
	if (!IsCriticSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Critic setup must be run before getting the agent type!"));
		return nullptr;
	}

	return AgentType;
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
	if (!IsCriticSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Critic setup must be run before network can be loaded."));
		return;
	}

	TArray64<uint8> NetworkData;
	FString FilePath = Directory.Path + FGenericPlatformMisc::GetDefaultPathSeparator() + Filename;
	if (FFileHelper::LoadFileToArray(NetworkData, *FilePath))
	{
		const int32 TotalByteNum = UE::Learning::FNeuralNetwork::GetSerializationByteNum(
			Network->NeuralNetwork->GetInputNum(),
			Network->NeuralNetwork->GetOutputNum(),
			Network->NeuralNetwork->GetHiddenNum(),
			Network->NeuralNetwork->GetLayerNum());

		if (NetworkData.Num() != TotalByteNum)
		{
			UE_LOG(LogLearning, Error, TEXT("Failed to load network from file %s. File size incorrect."), *FilePath);
			return;
		}

		Network->NeuralNetwork->DeserializeFromBytes(NetworkData);
	}
	else
	{
		UE_LOG(LogLearning, Error, TEXT("Failed to load network. File not found: %s"), *FilePath);
	}
}

void ULearningAgentsCritic::SaveCriticToSnapshot(const FDirectoryPath& Directory, const FString Filename) const
{
	if (!IsCriticSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Critic setup must be run before network can be saved."));
		return;
	}

	TArray64<uint8> NetworkData;
	NetworkData.SetNumUninitialized(UE::Learning::FNeuralNetwork::GetSerializationByteNum(
		Network->NeuralNetwork->GetInputNum(),
		Network->NeuralNetwork->GetOutputNum(),
		Network->NeuralNetwork->GetHiddenNum(),
		Network->NeuralNetwork->GetLayerNum()));

	Network->NeuralNetwork->SerializeToBytes(NetworkData);

	FString FilePath = Directory.Path + FGenericPlatformMisc::GetDefaultPathSeparator() + Filename;
	if (!FFileHelper::SaveArrayToFile(NetworkData, *FilePath))
	{
		UE_LOG(LogLearning, Error, TEXT("Failed to save network to file: %s"), *FilePath);
	}
}

void ULearningAgentsCritic::LoadCriticFromAsset(const ULearningAgentsNeuralNetwork* NeuralNetworkAsset)
{
	if (!IsCriticSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Critic setup must be run before network can be loaded."));
		return;
	}

	if (!NeuralNetworkAsset || !NeuralNetworkAsset->NeuralNetwork)
	{
		UE_LOG(LogLearning, Error, TEXT("Cannot load critic from invalid asset."));
		return;
	}

	if (NeuralNetworkAsset->NeuralNetwork->GetInputNum() != Network->NeuralNetwork->GetInputNum() ||
		NeuralNetworkAsset->NeuralNetwork->GetOutputNum() != Network->NeuralNetwork->GetOutputNum())
	{
		UE_LOG(LogLearning, Error, TEXT("Failed to load critic from asset. Network Asset inputs and outputs don't match."));
		return;
	}

	*Network->NeuralNetwork = *NeuralNetworkAsset->NeuralNetwork;
}

void ULearningAgentsCritic::SaveCriticToAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset) const
{
	if (!IsCriticSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Critic setup must be run before network can be saved."));
		return;
	}

	if (!NeuralNetworkAsset)
	{
		UE_LOG(LogLearning, Error, TEXT("Cannot save critic to invalid asset."));
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

	if (!IsCriticSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before the critic can be evaluated."));
		return;
	}

	CriticObject->Evaluate(SelectedAgentIds);

#if ENABLE_VISUAL_LOG
	VisualLog(SelectedAgentIds);
#endif
}

#if ENABLE_VISUAL_LOG
void ULearningAgentsCritic::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsCritic::VisualLog);

	const TLearningArrayView<2, const float> InputView = CriticObject->InstanceData->ConstView(CriticObject->InputHandle);
	const TLearningArrayView<1, const float> OutputView = CriticObject->InstanceData->ConstView(CriticObject->OutputHandle);

	for (const int32 Instance : Instances)
	{
		if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
		{
			const FString InputArrayString = UE::Learning::Agents::Critic::Private::ArrayToString(InputView[Instance]);
			const FString InputStatsString = UE::Learning::Agents::Critic::Private::ArrayToStatsString(InputView[Instance]);

			UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
				Actor->GetActorLocation(),
				VisualLogColor.ToFColor(true),
				TEXT("Agent %i\nInput: %s\nInput Stats (Min/Max/Mean/Std): %s\nOutput: [% 6.3f]"),
				Instance,
				*InputArrayString,
				*InputStatsString,
				OutputView[Instance]);
		}
	}
}
#endif

#undef UE_LEARNING_AGENTS_VLOG_STRING