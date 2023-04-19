// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsPolicy.h"

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

ULearningAgentsPolicy::ULearningAgentsPolicy() : ULearningAgentsManagerComponent() {}
ULearningAgentsPolicy::ULearningAgentsPolicy(FVTableHelper& Helper) : ULearningAgentsPolicy() {}
ULearningAgentsPolicy::~ULearningAgentsPolicy() {}

void ULearningAgentsPolicy::SetupPolicy(ALearningAgentsManager* InAgentManager, ULearningAgentsInteractor* InInteractor, const FLearningAgentsPolicySettings& PolicySettings)
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
	Network = NewObject<ULearningAgentsNeuralNetwork>(this, TEXT("PolicyNetwork"));
	Network->NeuralNetwork = MakeShared<UE::Learning::FNeuralNetwork>();
	Network->NeuralNetwork->Resize(
		Interactor->GetObservationFeature().DimNum(),
		2 * Interactor->GetActionFeature().DimNum(),
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
		AgentManager->GetInstanceData().ToSharedRef(),
		AgentManager->GetMaxInstanceNum(),
		Network->NeuralNetwork.ToSharedRef(),
		PolicySettings.ActionNoiseSeed,
		PolicyFunctionSettings);

	AgentManager->GetInstanceData()->Link(Interactor->GetObservationFeature().FeatureHandle, PolicyObject->InputHandle);

	InitialActionNoiseScale = PolicySettings.InitialActionNoiseScale;
	
	bIsSetup = true;
}

bool ULearningAgentsPolicy::AddAgent(const int32 AgentId)
{
	bool bSuccess = Super::AddAgent(AgentId);

	if (bSuccess)
	{
		// Reset the noise scale for this agent back to the initial value.
		SetAgentActionNoiseScale(AgentId, InitialActionNoiseScale);
	}

	return bSuccess;
}

UE::Learning::FNeuralNetwork& ULearningAgentsPolicy::GetPolicyNetwork()
{
	return *Network->NeuralNetwork;
}

UE::Learning::FNeuralNetworkPolicyFunction& ULearningAgentsPolicy::GetPolicyObject()
{
	return *PolicyObject;
}

void ULearningAgentsPolicy::LoadPolicyFromSnapshot(const FFilePath& File)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	Network->LoadNetworkFromSnapshot(File);
}

void ULearningAgentsPolicy::SavePolicyToSnapshot(const FFilePath& File) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	Network->SaveNetworkToSnapshot(File);
}

void ULearningAgentsPolicy::LoadPolicyFromAsset(const ULearningAgentsNeuralNetwork* NeuralNetworkAsset)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
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

void ULearningAgentsPolicy::SavePolicyToAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
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

void ULearningAgentsPolicy::EvaluatePolicy()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsPolicy::EvaluatePolicy);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	PolicyObject->Evaluate(AddedAgentIds);

	// Copy the actions computed by the policy into the agent-type's feature buffer.
	// 
	// Normally we would just link these two handles, but in this case we want to allow
	// for multiple difference policies to be used for different agents, so that means
	// there may be multiple writers to the action feature vector handle and so therefore
	// the handles cannot be linked and we need to do the copy manually
	UE::Learning::Array::Copy(
		PolicyObject->InstanceData->View(Interactor->GetActionFeature().FeatureHandle),
		PolicyObject->InstanceData->ConstView(PolicyObject->OutputHandle),
		AddedAgentIds);

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	VisualLog(AddedAgentIds);
#endif
}

void ULearningAgentsPolicy::RunInference()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsPolicy::RunInference);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	Interactor->EncodeObservations();
	EvaluatePolicy();
	Interactor->DecodeActions();
}
float ULearningAgentsPolicy::GetAgentActionNoiseScale(const int32 AgentId) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return 0.0f;
	}

	if (!HasAgent(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: AgentId %d not found in the agents set."), *GetName(), AgentId);
		return 0.0f;
	}

	const TLearningArrayView<1, const float> ActionNoiseScaleView = PolicyObject->InstanceData->ConstView(PolicyObject->ActionNoiseScaleHandle);
	return ActionNoiseScaleView[AgentId];
}

void ULearningAgentsPolicy::SetAgentActionNoiseScale(const int32 AgentId, const float ActionNoiseScale)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (!HasAgent(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: AgentId %d not found in the agents set."), *GetName(), AgentId);
		return;
	}

	const TLearningArrayView<1, float> ActionNoiseScaleView = PolicyObject->InstanceData->View(PolicyObject->ActionNoiseScaleHandle);
	ActionNoiseScaleView[AgentId] = ActionNoiseScale;
}

void ULearningAgentsPolicy::SetAllAgentsActionNoiseScale(const float ActionNoiseScale)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	const TLearningArrayView<1, float> ActionNoiseScaleView = PolicyObject->InstanceData->View(PolicyObject->ActionNoiseScaleHandle);
	UE::Learning::Array::Set(ActionNoiseScaleView, ActionNoiseScale, AddedAgentSet);
}

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
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
		if (const AActor* Actor = Cast<AActor>(Interactor->GetAgent(Instance)))
		{

			UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
				Actor->GetActorLocation(),
				VisualLogColor.ToFColor(true),
				TEXT("Agent %i\nAction Noise Scale: [% 6.3f]\nInput: %s\nInput Stats (Min/Max/Mean/Std): %s\nOutput Mean: %s\nOutput Std: %s\nOutput Sample: %s\nOutput Stats (Min/Max/Mean/Std): %s"),
				Instance,
				ActionNoiseScaleView[Instance],
				*UE::Learning::Array::FormatFloat(InputView[Instance]),
				*UE::Learning::Array::FormatFloat(OutputView[Instance]),
				*UE::Learning::Array::FormatFloat(OutputMeanView[Instance]),
				*UE::Learning::Array::FormatFloat(OutputStdView[Instance]),
				*UE::Learning::Agents::Debug::FloatArrayToStatsString(InputView[Instance]),
				*UE::Learning::Agents::Debug::FloatArrayToStatsString(OutputView[Instance]));

		}
	}
}
#endif
