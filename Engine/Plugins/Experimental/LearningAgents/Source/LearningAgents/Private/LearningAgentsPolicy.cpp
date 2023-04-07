// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsPolicy.h"

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

namespace UE::Learning::Agents::Policy::Private
{
	static inline FString FloatArrayToString(const TLearningArrayView<1, const float> Array)
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
			else if (Idx == MaxItemNum - 1)
			{
				Output += TEXT("...");
			}
		}

		Output += TEXT("]");

		return Output;
	}

	static inline FString FloatArrayToStatsString(const TLearningArrayView<1, const float> Array)
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

ULearningAgentsPolicy::ULearningAgentsPolicy() : UActorComponent() {}
ULearningAgentsPolicy::ULearningAgentsPolicy(FVTableHelper& Helper) : ULearningAgentsPolicy() {}
ULearningAgentsPolicy::~ULearningAgentsPolicy() {}

void ULearningAgentsPolicy::SetupPolicy(ULearningAgentsType* InAgentType, const FLearningAgentsPolicySettings& PolicySettings)
{
	if (IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup already performed!"));
		return;
	}

	// Setup Agent Type

	if (!InAgentType)
	{
		UE_LOG(LogLearning, Error, TEXT("SetupPolicy called but AgentType is nullptr."));
		return;
	}

	if (!InAgentType->IsSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentType Setup must be run before policy can be setup."));
		return;
	}

	AgentType = InAgentType;

	// Setup Neural Network

	Network = NewObject<ULearningAgentsNeuralNetwork>(this, TEXT("PolicyNetwork"));
	Network->NeuralNetwork = MakeShared<UE::Learning::FNeuralNetwork>();
	Network->NeuralNetwork->Resize(
		AgentType->GetObservationFeature().DimNum(),
		2 * AgentType->GetActionFeature().DimNum(),
		PolicySettings.HiddenLayerSize,
		PolicySettings.LayerNum);
	Network->NeuralNetwork->ActivationFunction = UE::Learning::Agents::GetActivationFunction(PolicySettings.ActivationFunction);

	// Create Policy Object

	UE::Learning::FNeuralNetworkPolicyFunctionSettings PolicyFunctionSettings;
	PolicyFunctionSettings.ActionNoiseMin = PolicySettings.ActionNoiseMin;
	PolicyFunctionSettings.ActionNoiseMax = PolicySettings.ActionNoiseMax;
	PolicyFunctionSettings.ActionNoiseScale = PolicySettings.InitialActionNoiseScale;

	PolicyObject = MakeShared<UE::Learning::FNeuralNetworkPolicyFunction>(
		TEXT("PolicyObject"),
		AgentType->GetInstanceData().ToSharedRef(),
		AgentType->GetMaxInstanceNum(),
		Network->NeuralNetwork.ToSharedRef(),
		PolicySettings.ActionNoiseSeed,
		PolicyFunctionSettings);

	AgentType->GetInstanceData()->Link(AgentType->GetObservationFeature().FeatureHandle, PolicyObject->InputHandle);

	// Done!
	bPolicySetupPerformed = true;
}

bool ULearningAgentsPolicy::IsPolicySetupPerformed() const
{
	return bPolicySetupPerformed;
}

void ULearningAgentsPolicy::AddAgent(const int32 AgentId)
{
	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Policy setup must be run before agents can be added!"));
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

void ULearningAgentsPolicy::RemoveAgent(const int32 AgentId)
{
	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Policy setup must be run before agents can be removed!"));
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

bool ULearningAgentsPolicy::HasAgent(const int32 AgentId) const
{
	return SelectedAgentsSet.Contains(AgentId);
}

ULearningAgentsType* ULearningAgentsPolicy::GetAgentType(const TSubclassOf<ULearningAgentsType> AgentClass)
{
	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Policy setup must be run before getting the agent type!"));
		return nullptr;
	}

	return AgentType;
}

UE::Learning::FNeuralNetwork& ULearningAgentsPolicy::GetPolicyNetwork()
{
	return *Network->NeuralNetwork;
}

UE::Learning::FNeuralNetworkPolicyFunction& ULearningAgentsPolicy::GetPolicyObject()
{
	return *PolicyObject;
}

void ULearningAgentsPolicy::LoadPolicyFromSnapshot(const FDirectoryPath& Directory, const FString Filename)
{
	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Policy setup must be run before network can be loaded."));
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
			UE_LOG(LogLearning, Error, TEXT("Failed to load network from file %s. File size incorrect."), *FilePath);
			return;
		}

		Network->NeuralNetwork->DeserializeFromBytes(NetworkData);
	}
	else
	{
		UE_LOG(LogLearning, Warning, TEXT("Failed to load network. File not found: %s"), *FilePath);
	}
}

void ULearningAgentsPolicy::SavePolicyToSnapshot(const FDirectoryPath& Directory, const FString Filename) const
{
	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Policy setup must be run before network can be saved."));
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
		UE_LOG(LogLearning, Error, TEXT("Failed to save network to file: %s"), *FilePath);
	}
}

void ULearningAgentsPolicy::LoadPolicyFromAsset(const ULearningAgentsNeuralNetwork* NeuralNetworkAsset)
{
	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Policy setup must be run before network can be loaded."));
		return;
	}

	if (!NeuralNetworkAsset || !NeuralNetworkAsset->NeuralNetwork)
	{
		UE_LOG(LogLearning, Error, TEXT("Cannot load policy from invalid asset."));
		return;
	}

	if (NeuralNetworkAsset->NeuralNetwork->GetInputNum() != Network->NeuralNetwork->GetInputNum() ||
		NeuralNetworkAsset->NeuralNetwork->GetOutputNum() != Network->NeuralNetwork->GetOutputNum())
	{
		UE_LOG(LogLearning, Error, TEXT("Failed to load policy from asset. Network Asset inputs and outputs don't match."));
		return;
	}

	*Network->NeuralNetwork = *NeuralNetworkAsset->NeuralNetwork;
}

void ULearningAgentsPolicy::SavePolicyToAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset) const
{
	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Policy setup must be run before network can be saved."));
		return;
	}

	if (!NeuralNetworkAsset)
	{
		UE_LOG(LogLearning, Error, TEXT("Cannot save policy to invalid asset."));
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

void ULearningAgentsPolicy::EvaluatePolicy()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsPolicy::EvaluatePolicy);

	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before the policy can be evaluated."));
		return;
	}

	PolicyObject->Evaluate(SelectedAgentIds);

	// Copy the actions computed by the policy into the agent-type's feature buffer.
	// 
	// Normally we would just link these two handles, but in this case we want to allow
	// for multiple difference policies to be used for different agents, so that means
	// there may be multiple writers to the action feature vector handle and so therefore
	// the handles cannot be linked and we need to do the copy manually
	UE::Learning::Array::Copy(
		PolicyObject->InstanceData->View(AgentType->GetActionFeature().FeatureHandle),
		PolicyObject->InstanceData->ConstView(PolicyObject->OutputHandle),
		SelectedAgentIds);

#if ENABLE_VISUAL_LOG
	VisualLog(SelectedAgentIds);
#endif
}

float ULearningAgentsPolicy::GetAgentActionNoiseScale(const int32 AgentId) const
{
	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before getting the action noise."));
		return 0.0f;
	}

	if (!SelectedAgentsSet.Contains(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("Unable to get action noise for agent - AgentId %d not found in the added agents set."), AgentId);
		return 0.0f;
	}

	const TLearningArrayView<1, const float> ActionNoiseScaleView = PolicyObject->InstanceData->ConstView(PolicyObject->ActionNoiseScaleHandle);
	return ActionNoiseScaleView[AgentId];
}

void ULearningAgentsPolicy::SetAgentActionNoiseScale(const int32 AgentId, const float ActionNoiseScale)
{
	if (!IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before setting the action noise."));
		return;
	}

	if (!SelectedAgentsSet.Contains(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("Unable to set action noise for agent - AgentId %d not found in the added agents set."), AgentId);
		return;
	}

	const TLearningArrayView<1, float> ActionNoiseScaleView = PolicyObject->InstanceData->View(PolicyObject->ActionNoiseScaleHandle);
	ActionNoiseScaleView[AgentId] = ActionNoiseScale;
}

#if ENABLE_VISUAL_LOG
void ULearningAgentsPolicy::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsPolicy::VisualLog);

	const TLearningArrayView<2, const float> InputView = PolicyObject->InstanceData->ConstView(PolicyObject->InputHandle);
	const TLearningArrayView<2, const float> OutputView = PolicyObject->InstanceData->ConstView(PolicyObject->OutputHandle);
	const TLearningArrayView<2, const float> OutputMeanView = PolicyObject->InstanceData->ConstView(PolicyObject->OutputMeanHandle);
	const TLearningArrayView<2, const float> OutputStdView = PolicyObject->InstanceData->ConstView(PolicyObject->OutputStdHandle);
	const TLearningArrayView<1, const float> ActionNoiseScaleView = PolicyObject->InstanceData->ConstView(PolicyObject->ActionNoiseScaleHandle);

	for (const int32 Instance : Instances)
	{
		if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
		{
			const FString InputArrayString = UE::Learning::Agents::Policy::Private::FloatArrayToString(InputView[Instance]);
			const FString OutputArrayString = UE::Learning::Agents::Policy::Private::FloatArrayToString(OutputView[Instance]);
			const FString OutputMeanArrayString = UE::Learning::Agents::Policy::Private::FloatArrayToString(OutputMeanView[Instance]);
			const FString OutputStdArrayString = UE::Learning::Agents::Policy::Private::FloatArrayToString(OutputStdView[Instance]);
			const FString InputStatsString = UE::Learning::Agents::Policy::Private::FloatArrayToStatsString(InputView[Instance]);
			const FString OutputStatsString = UE::Learning::Agents::Policy::Private::FloatArrayToStatsString(OutputView[Instance]);

			UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
				Actor->GetActorLocation(),
				VisualLogColor.ToFColor(true),
				TEXT("Agent %i\nAction Noise Scale: [% 6.3f]\nInput: %s\nInput Stats (Min/Max/Mean/Std): %s\nOutput Mean: %s\nOutput Std: %s\nOutput Sample: %s\nOutput Stats (Min/Max/Mean/Std): %s"),
				Instance,
				ActionNoiseScaleView[Instance],
				*InputArrayString,
				*InputStatsString,
				*OutputMeanArrayString,
				*OutputStdArrayString,
				*OutputArrayString,
				*OutputStatsString);
		}
	}
}
#endif

#undef UE_LEARNING_AGENTS_VLOG_STRING