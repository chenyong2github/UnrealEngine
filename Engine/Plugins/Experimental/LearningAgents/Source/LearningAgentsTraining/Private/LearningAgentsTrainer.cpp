// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsTrainer.h"

#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsRewards.h"
#include "LearningAgentsCompletions.h"
#include "LearningAgentsObservations.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsCritic.h"
#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningExperience.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "LearningNeuralNetwork.h"
#include "LearningNeuralNetworkObject.h"
#include "LearningPPOTrainer.h"
#include "LearningRewardObject.h"
#include "LearningCompletion.h"
#include "LearningCompletionObject.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "GameFramework/GameUserSettings.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Engine/GameViewportClient.h"
#include "EngineDefines.h"

namespace UE::Learning::Agents
{
	ELearningAgentsCompletion GetLearningAgentsCompletion(const ECompletionMode CompletionMode)
	{
		switch (CompletionMode)
		{
		case ECompletionMode::Running: UE_LOG(LogLearning, Error, TEXT("Cannot convert from ECompletionMode::Running to ELearningAgentsCompletion")); return ELearningAgentsCompletion::Termination;
		case ECompletionMode::Terminated: return ELearningAgentsCompletion::Termination;
		case ECompletionMode::Truncated: return ELearningAgentsCompletion::Truncation;
		default:UE_LOG(LogLearning, Error, TEXT("Unknown Completion Mode.")); return ELearningAgentsCompletion::Termination;
		}
	}

	ECompletionMode GetCompletionMode(const ELearningAgentsCompletion Completion)
	{
		switch (Completion)
		{
		case ELearningAgentsCompletion::Termination: return ECompletionMode::Terminated;
		case ELearningAgentsCompletion::Truncation: return ECompletionMode::Truncated;
		default:UE_LOG(LogLearning, Error, TEXT("Unknown Completion.")); return ECompletionMode::Running;
		}
	}
}

FLearningAgentsTrainerPathSettings::FLearningAgentsTrainerPathSettings()
{
	EditorEngineRelativePath.Path = FPaths::EngineDir();
	IntermediateRelativePath.Path = FPaths::ProjectIntermediateDir();
}

FString FLearningAgentsTrainerPathSettings::GetEditorEnginePath() const
{
#if WITH_EDITOR
	return EditorEngineRelativePath.Path;
#else
	if (NonEditorEngineRelativePath.IsEmpty())
	{
		UE_LOG(LogLearning, Warning, TEXT("GetEditorEnginePath: NonEditorEngineRelativePath not set"));
	}

	return NonEditorEngineRelativePath;
#endif
}

FString FLearningAgentsTrainerPathSettings::GetIntermediatePath() const
{
	return IntermediateRelativePath.Path;
}

ULearningAgentsTrainer::ULearningAgentsTrainer() : ULearningAgentsManagerComponent() {}
ULearningAgentsTrainer::ULearningAgentsTrainer(FVTableHelper& Helper) : ULearningAgentsTrainer() {}
ULearningAgentsTrainer::~ULearningAgentsTrainer() {}

void ULearningAgentsTrainer::SetupTrainer(
	ALearningAgentsManager* InAgentManager,
	ULearningAgentsInteractor* InInteractor,
	ULearningAgentsPolicy* InPolicy,
	ULearningAgentsCritic* InCritic,
	const FLearningAgentsTrainerSettings& TrainerSettings)
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

	if (!InPolicy)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InPolicy is nullptr."), *GetName());
		return;
	}

	if (!InPolicy->IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's Setup must be run before it can be used."), *GetName(), *InPolicy->GetName());
		return;
	}

	Policy = InPolicy;

	// The critic is optional unlike the other components
	if (InCritic)
	{
		if (!InCritic->IsSetup())
		{
			UE_LOG(LogLearning, Error, TEXT("%s: %s's Setup must be run before it can be used."), *GetName(), *InCritic->GetName());
			return;
		}

		Critic = InCritic;
	}

	// Setup Rewards
	RewardObjects.Empty();
	RewardFeatures.Empty();
	SetupRewards(this);
	Rewards = MakeShared<UE::Learning::FSumReward>(TEXT("Rewards"),
		TLearningArrayView<1, const TSharedRef<UE::Learning::FRewardObject>>(RewardFeatures),
		AgentManager->GetInstanceData().ToSharedRef(),
		AgentManager->GetMaxInstanceNum());

	if (RewardObjects.Num() == 0)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: No rewards added to Trainer during SetupRewards."), *GetName());
		return;
	}

	// Setup Completions
	CompletionObjects.Empty();
	CompletionFeatures.Empty();
	SetupCompletions(this);
	Completions = MakeShared<UE::Learning::FAnyCompletion>(TEXT("Completions"),
		TLearningArrayView<1, const TSharedRef<UE::Learning::FCompletionObject>>(CompletionFeatures),
		AgentManager->GetInstanceData().ToSharedRef(),
		AgentManager->GetMaxInstanceNum());

	if (RewardObjects.Num() == 0)
	{
		// Not an error or warning because it's fine to run training without any completions.
		UE_LOG(LogLearning, Display, TEXT("%s: No completions added to Trainer during SetupCompletions."), *GetName());
	}

	// Create Episode Buffer
	EpisodeBuffer = MakeUnique<UE::Learning::FEpisodeBuffer>();
	EpisodeBuffer->Resize(
		AgentManager->GetMaxInstanceNum(),
		TrainerSettings.MaxStepNum,
		Interactor->GetObservationFeature().DimNum(),
		Interactor->GetActionFeature().DimNum());

	MaxStepsCompletion = TrainerSettings.MaxStepsCompletion;

	// Create Replay Buffer
	ReplayBuffer = MakeUnique<UE::Learning::FReplayBuffer>();
	ReplayBuffer->Resize(
		Interactor->GetObservationFeature().DimNum(),
		Interactor->GetActionFeature().DimNum(),
		TrainerSettings.MaximumRecordedEpisodesPerIteration,
		TrainerSettings.MaximumRecordedStepsPerIteration);

	// Create Reset Buffer
	ResetBuffer = MakeUnique<UE::Learning::FResetInstanceBuffer>();
	ResetBuffer->Resize(AgentManager->GetMaxInstanceNum());

	// Record Timeout Setting
	TrainerTimeout = TrainerSettings.TrainerCommunicationTimeout;

	bIsSetup = true;
}

bool ULearningAgentsTrainer::AddAgent(const int32 AgentId)
{
	bool bSuccess = Super::AddAgent(AgentId);

	if (bSuccess && IsTraining())
	{
		// Reset the instance and the buffer in case we have stale 
		// data from another agent that was using this id previously
		ResetEpisodes({ AgentId });
		EpisodeBuffer->Reset(AgentId);
	}

	return bSuccess;
}

void ULearningAgentsTrainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsTraining)
	{
		EndTraining();
	}

	Super::EndPlay(EndPlayReason);
}

void ULearningAgentsTrainer::SetupRewards_Implementation(ULearningAgentsTrainer* AgentTrainer)
{
	// Can be overridden to setup rewards without blueprints
}

void ULearningAgentsTrainer::SetRewards_Implementation(const TArray<int32>& AgentIds)
{
	// Can be overridden to set rewards without blueprints
}

void ULearningAgentsTrainer::AddReward(TObjectPtr<ULearningAgentsReward> Object, const TSharedRef<UE::Learning::FRewardObject>& Reward)
{
	UE_LEARNING_CHECK(!IsSetup());
	RewardObjects.Add(Object);
	RewardFeatures.Add(Reward);
}

void ULearningAgentsTrainer::SetupCompletions_Implementation(ULearningAgentsTrainer* AgentTrainer)
{
	// Can be overridden to setup completions without blueprints
}

void ULearningAgentsTrainer::SetCompletions_Implementation(const TArray<int32>& AgentIds)
{
	// Can be overridden to evaluate completions without blueprints
}

void ULearningAgentsTrainer::AddCompletion(TObjectPtr<ULearningAgentsCompletion> Object, const TSharedRef<UE::Learning::FCompletionObject>& Completion)
{
	UE_LEARNING_CHECK(!IsSetup());
	CompletionObjects.Add(Object);
	CompletionFeatures.Add(Completion);
}

const bool ULearningAgentsTrainer::IsTraining() const
{
	return bIsTraining;
}

void ULearningAgentsTrainer::BeginTraining(
	const FLearningAgentsTrainerTrainingSettings& TrainerTrainingSettings,
	const FLearningAgentsTrainerGameSettings& TrainerGameSettings,
	const FLearningAgentsTrainerPathSettings& TrainerPathSettings,
	const FLearningAgentsCriticSettings& CriticSettings,
	const bool bReinitializePolicyNetwork,
	const bool bReinitializeCriticNetwork)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (IsTraining())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Already Training!"), *GetName());
		return;
	}

	// Check Paths

	const FString PythonExecutablePath = UE::Learning::Trainer::GetPythonExecutablePath(TrainerPathSettings.GetEditorEnginePath());

	if (!FPaths::FileExists(PythonExecutablePath))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Can't find Python executable \"%s\"."), *GetName(), *PythonExecutablePath);
		return;
	}

	const FString PythonContentPath = UE::Learning::Trainer::GetPythonContentPath(TrainerPathSettings.GetEditorEnginePath());

	if (!FPaths::DirectoryExists(PythonContentPath))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Can't find LearningAgents plugin Content \"%s\"."), *GetName(), *PythonContentPath);
		return;
	}

	const FString SitePackagesPath = UE::Learning::Trainer::GetSitePackagesPath(TrainerPathSettings.GetEditorEnginePath());

	if (!FPaths::DirectoryExists(SitePackagesPath))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Can't find Python site-packages \"%s\"."), *GetName(), *SitePackagesPath);
		return;
	}

	const FString IntermediatePath = UE::Learning::Trainer::GetIntermediatePath(TrainerPathSettings.GetIntermediatePath());

	// Record GameState Settings

	bFixedTimestepUsed = FApp::UseFixedTimeStep();
	FixedTimeStepDeltaTime = FApp::GetFixedDeltaTime();

	UGameUserSettings* GameSettings = UGameUserSettings::GetGameUserSettings();
	if (GameSettings)
	{
		bVSyncEnabled = GameSettings->IsVSyncEnabled();
	}

	UPhysicsSettings* PhysicsSettings = UPhysicsSettings::Get();
	if (PhysicsSettings)
	{
		MaxPhysicsStep = PhysicsSettings->MaxPhysicsDeltaTime;
	}

	UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (ViewportClient)
	{
		ViewModeIndex = ViewportClient->ViewModeIndex;
	}

	// Apply Training GameState Settings

	FApp::SetUseFixedTimeStep(TrainerGameSettings.bUseFixedTimeStep);

	if (TrainerGameSettings.FixedTimeStepFrequency > UE_SMALL_NUMBER)
	{
		FApp::SetFixedDeltaTime(1.0f / TrainerGameSettings.FixedTimeStepFrequency);
		if (TrainerGameSettings.bSetMaxPhysicsStepToFixedTimeStep && PhysicsSettings)
		{
			PhysicsSettings->MaxPhysicsDeltaTime = 1.0f / TrainerGameSettings.FixedTimeStepFrequency;
		}
	}
	else
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: Provided invalid FixedTimeStepFrequency: %0.5f"), *GetName(), TrainerGameSettings.FixedTimeStepFrequency);
	}

	if (TrainerGameSettings.bDisableVSync && GameSettings)
	{
		GameSettings->SetVSyncEnabled(false);
		GameSettings->ApplySettings(false);
	}

	if (TrainerGameSettings.bUseUnlitViewportRendering && ViewportClient)
	{
		ViewportClient->ViewModeIndex = EViewModeIndex::VMI_Unlit;
	}

	// Start Trainer

	UE::Learning::FPPOTrainerTrainingSettings PPOTrainingSettings;
	PPOTrainingSettings.IterationNum = TrainerTrainingSettings.NumberOfIterations;
	PPOTrainingSettings.bUseTensorboard = TrainerTrainingSettings.bUseTensorboard;
	PPOTrainingSettings.InitialActionScale = TrainerTrainingSettings.InitialActionScale;
	PPOTrainingSettings.DiscountFactor = TrainerTrainingSettings.DiscountFactor;
	PPOTrainingSettings.Seed = TrainerTrainingSettings.RandomSeed;
	PPOTrainingSettings.TrimEpisodeStartStepNum = TrainerTrainingSettings.NumberOfStepsToTrimAtStartOfEpisode;
	PPOTrainingSettings.TrimEpisodeEndStepNum = TrainerTrainingSettings.NumberOfStepsToTrimAtEndOfEpisode;
	PPOTrainingSettings.Device = TrainerTrainingSettings.Device == ELearningAgentsTrainerDevice::CPU ? UE::Learning::ETrainerDevice::CPU : UE::Learning::ETrainerDevice::GPU;

	UE::Learning::FPPOTrainerNetworkSettings PPONetworkSettings;
	PPONetworkSettings.PolicyActionNoiseMin = Policy->GetPolicyObject().Settings.ActionNoiseMin;
	PPONetworkSettings.PolicyActionNoiseMax = Policy->GetPolicyObject().Settings.ActionNoiseMax;
	PPONetworkSettings.PolicyActivationFunction = Policy->GetPolicyNetwork().ActivationFunction;
	PPONetworkSettings.PolicyHiddenLayerSize = Policy->GetPolicyNetwork().GetHiddenNum();
	PPONetworkSettings.PolicyLayerNum = Policy->GetPolicyNetwork().GetLayerNum();

	if (Critic)
	{
		if (CriticSettings.HiddenLayerSize != Critic->GetCriticNetwork().GetHiddenNum() ||
			CriticSettings.LayerNum != Critic->GetCriticNetwork().GetLayerNum() ||
			UE::Learning::Agents::GetActivationFunction(CriticSettings.ActivationFunction) != Critic->GetCriticNetwork().ActivationFunction)
		{
			UE_LOG(LogLearning, Warning, TEXT("%s: BeginTraining got different Critic Network Settings to those provided to SetupCritic."), *GetName());
		}

		PPONetworkSettings.CriticHiddenLayerSize = Critic->GetCriticNetwork().GetHiddenNum();
		PPONetworkSettings.CriticLayerNum = Critic->GetCriticNetwork().GetLayerNum();
		PPONetworkSettings.CriticActivationFunction = Critic->GetCriticNetwork().ActivationFunction;
	}
	else
	{
		PPONetworkSettings.CriticHiddenLayerSize = CriticSettings.HiddenLayerSize;
		PPONetworkSettings.CriticLayerNum = CriticSettings.LayerNum;
		PPONetworkSettings.CriticActivationFunction = UE::Learning::Agents::GetActivationFunction(CriticSettings.ActivationFunction);
	}

	// We assume that if the critic has been setup on the agent interactor, then
	// the user wants the critic network to be synced during training.
	UE::Learning::EPPOTrainerFlags TrainerFlags = 
		Critic ?
		UE::Learning::EPPOTrainerFlags::SynchronizeCriticNetwork :
		UE::Learning::EPPOTrainerFlags::None;

	if (!bReinitializePolicyNetwork) { TrainerFlags |= UE::Learning::EPPOTrainerFlags::UseInitialPolicyNetwork; }
	if (!bReinitializeCriticNetwork && Critic) { TrainerFlags |= UE::Learning::EPPOTrainerFlags::UseInitialCriticNetwork; }

	// Start Python Training Process (this must be done on game thread)
	Trainer = MakeUnique<UE::Learning::FSharedMemoryPPOTrainer>(
		GetName(),
		PythonExecutablePath,
		SitePackagesPath,
		PythonContentPath,
		IntermediatePath,
		*ReplayBuffer,
		PPOTrainingSettings,
		PPONetworkSettings,
		TrainerFlags);

	UE_LOG(LogLearning, Display, TEXT("%s: Receiving initial policy..."), *GetName());

	UE::Learning::ETrainerResponse Response = UE::Learning::ETrainerResponse::Success;

	if ((bool)(TrainerFlags & UE::Learning::EPPOTrainerFlags::UseInitialPolicyNetwork))
	{
		Response = Trainer->SendPolicy(Policy->GetPolicyNetwork(), TrainerTimeout);
	}
	else
	{
		Response = Trainer->RecvPolicy(Policy->GetPolicyNetwork(), TrainerTimeout);
	}

	if (Response != UE::Learning::ETrainerResponse::Success)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Error sending or receiving policy from trainer: %s. Check log for errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
		Trainer->Terminate();
		return;
	}

	if (Critic)
	{
		if ((bool)(TrainerFlags & UE::Learning::EPPOTrainerFlags::UseInitialCriticNetwork))
		{
			Response = Trainer->SendCritic(Critic->GetCriticNetwork(), TrainerTimeout);
		}
		else if ((bool)(TrainerFlags & UE::Learning::EPPOTrainerFlags::SynchronizeCriticNetwork))
		{
			Response = Trainer->RecvCritic(Critic->GetCriticNetwork(), TrainerTimeout);
		}
		else
		{
			Response = UE::Learning::ETrainerResponse::Success;
		}

		if (Response != UE::Learning::ETrainerResponse::Success)
		{
			UE_LOG(LogLearning, Error, TEXT("%s: Error sending or receiving critic from trainer: %s. Check log for errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
			Trainer->Terminate();
			return;
		}
	}

	// Reset Agents, Episode Buffer, and Replay Buffer
	ResetEpisodes(AddedAgentIds);
	EpisodeBuffer->Reset(AddedAgentSet);
	ReplayBuffer->Reset();

	bIsTraining = true;
}

void ULearningAgentsTrainer::DoneTraining()
{
	if (IsTraining())
	{
		// Wait for Trainer to finish
		Trainer->Wait(1.0f);

		// If not finished in time, terminate
		Trainer->Terminate();

		// Apply back previous game settings
		FApp::SetUseFixedTimeStep(bFixedTimestepUsed);
		FApp::SetFixedDeltaTime(FixedTimeStepDeltaTime);
		UGameUserSettings* GameSettings = UGameUserSettings::GetGameUserSettings();
		if (GameSettings)
		{
			GameSettings->SetVSyncEnabled(bVSyncEnabled);
			GameSettings->ApplySettings(true);
		}

		UPhysicsSettings* PhysicsSettings = UPhysicsSettings::Get();
		if (PhysicsSettings)
		{
			PhysicsSettings->MaxPhysicsDeltaTime = MaxPhysicsStep;
		}

		UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
		if (ViewportClient)
		{
			ViewportClient->ViewModeIndex = ViewModeIndex;
		}

		bIsTraining = false;
	}
}

void ULearningAgentsTrainer::EndTraining()
{
	if (IsTraining())
	{
		UE_LOG(LogLearning, Display, TEXT("%s: Stopping training..."), *GetName());
		Trainer->SendStop();
		DoneTraining();
	}
}

void ULearningAgentsTrainer::EvaluateRewards()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsTrainer::EvaluateRewards);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	SetRewards(AddedAgentIds);

	Rewards->Evaluate(AddedAgentSet);

#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsReward* RewardObject : RewardObjects)
	{
		if (RewardObject)
		{
			RewardObject->VisualLog(AddedAgentSet);
		}
	}
#endif
}

void ULearningAgentsTrainer::EvaluateCompletions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsTrainer::EvaluateCompletions);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	SetCompletions(AddedAgentIds);

	Completions->Evaluate(AddedAgentSet);
	
#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsCompletion* CompletionObject : CompletionObjects)
	{
		if (CompletionObject)
		{
			CompletionObject->VisualLog(AddedAgentSet);
		}
	}
#endif
}

void ULearningAgentsTrainer::ProcessExperience()
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (!IsTraining())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Training not running."), *GetName());
		return;
	}

	UE::Learning::FFeatureObject& Observations = Interactor->GetObservationFeature();
	UE::Learning::FFeatureObject& Actions = Interactor->GetActionFeature();

	// Add Experience to Episode Buffer
	EpisodeBuffer->Push(
		Observations.FeatureBuffer(),
		Actions.FeatureBuffer(),
		Rewards->RewardBuffer(),
		AddedAgentSet);

	// Check for completion based on reaching the maximum episode length
	UE::Learning::Completion::EvaluateEndOfEpisodeCompletions(
		Completions->CompletionBuffer(),
		EpisodeBuffer->GetEpisodeStepNums(),
		EpisodeBuffer->GetMaxStepNum(),
		MaxStepsCompletion == ELearningAgentsCompletion::Truncation 
			? UE::Learning::ECompletionMode::Truncated 
			: UE::Learning::ECompletionMode::Terminated,
		AddedAgentSet);

	// Find the set of Instances that need to be reset
	ResetBuffer->SetResetInstancesFromCompletions(Completions->CompletionBuffer(), AddedAgentSet);

	if (ResetBuffer->GetResetInstanceNum() > 0)
	{
		// Encode Observations for completed Instances
		Interactor->SetObservations(ResetBuffer->GetResetInstances().ToArray());
		Observations.Encode(ResetBuffer->GetResetInstances());
		
#if ENABLE_VISUAL_LOG
		for (const ULearningAgentsObservation* ObservationObject : Interactor->GetObservationObjects())
		{
			if (ObservationObject)
			{
				ObservationObject->VisualLog(ResetBuffer->GetResetInstances());
			}
		}
#endif

		const bool bReplayBufferFull = ReplayBuffer->AddEpisodes(
			Completions->CompletionBuffer(),
			Observations.FeatureBuffer(),
			*EpisodeBuffer,
			ResetBuffer->GetResetInstances());

		if (bReplayBufferFull)
		{
			UE::Learning::ETrainerResponse Response = Trainer->SendExperience(*ReplayBuffer, TrainerTimeout);

			if (Response != UE::Learning::ETrainerResponse::Success)
			{
				UE_LOG(LogLearning, Error, TEXT("%s: Error waiting to push experience to trainer. Check log for errors."), *GetName());
				EndTraining();
				return;
			}

			ReplayBuffer->Reset();

			// Get Updated Policy
			Response = Trainer->RecvPolicy(Policy->GetPolicyNetwork(), TrainerTimeout);

			if (Response == UE::Learning::ETrainerResponse::Completed)
			{
				UE_LOG(LogLearning, Display, TEXT("%s: Trainer completed training."), *GetName());
				DoneTraining();
				return;
			}
			else if (Response != UE::Learning::ETrainerResponse::Success)
			{
				UE_LOG(LogLearning, Error, TEXT("%s: Error waiting for policy from trainer. Check log for errors."), *GetName());
				EndTraining();
				return;
			}

			// Get Updated Critic
			if (Critic)
			{
				Response = Trainer->RecvCritic(Critic->GetCriticNetwork(), TrainerTimeout);

				if (Response != UE::Learning::ETrainerResponse::Success)
				{
					UE_LOG(LogLearning, Error, TEXT("%s: Error waiting for critic from trainer. Check log for errors."), *GetName());
					EndTraining();
					return;
				}
			}

			// Mark all instances for reset since we have a new policy
			ResetBuffer->SetResetInstances(AddedAgentSet);
		}

		ResetEpisodes(ResetBuffer->GetResetInstances().ToArray());

		EpisodeBuffer->Reset(ResetBuffer->GetResetInstances());
	}
}

void ULearningAgentsTrainer::ResetAllEpisodes()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsTrainer::ResetAllEpisodes);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	ResetEpisodes(AddedAgentIds);

	EpisodeBuffer->Reset(AddedAgentSet);
}

void ULearningAgentsTrainer::RunTraining(
	const FLearningAgentsTrainerTrainingSettings& TrainerTrainingSettings,
	const FLearningAgentsTrainerGameSettings& TrainerGameSettings,
	const FLearningAgentsTrainerPathSettings& TrainerPathSettings,
	const FLearningAgentsCriticSettings& CriticSettings,
	const bool bReinitializePolicyNetwork,
	const bool bReinitializeCriticNetwork)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	// If we aren't training yet, then start training and do the first inference step.
	if (!IsTraining())
	{
		BeginTraining(
			TrainerTrainingSettings,
			TrainerGameSettings, 
			TrainerPathSettings, 
			CriticSettings, 
			bReinitializePolicyNetwork, 
			bReinitializeCriticNetwork);

		if (!IsTraining())
		{
			// If IsTraining is false, then BeginTraining must have failed and we can't continue.
			return;
		}

		Policy->RunInference();
	}

	// Otherwise, do the regular training process.
	EvaluateCompletions();
	EvaluateRewards();
	ProcessExperience();
	Policy->RunInference();
}

float ULearningAgentsTrainer::GetReward(const int32 AgentId) const
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

	return Rewards->RewardBuffer()[AgentId];
}

bool ULearningAgentsTrainer::IsCompleted(const int32 AgentId, ELearningAgentsCompletion& OutCompletion) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		OutCompletion = ELearningAgentsCompletion::Termination;
		return false;
	}

	if (!HasAgent(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: AgentId %d not found in the agents set."), *GetName(), AgentId);
		OutCompletion = ELearningAgentsCompletion::Termination;
		return false;
	}

	const UE::Learning::ECompletionMode CompletionMode = Completions->CompletionBuffer()[AgentId];

	if (CompletionMode == UE::Learning::ECompletionMode::Running)
	{
		OutCompletion = ELearningAgentsCompletion::Termination;
		return false;
	}
	else
	{
		OutCompletion = UE::Learning::Agents::GetLearningAgentsCompletion(CompletionMode);
		return true;
	}
}
void ULearningAgentsTrainer::ResetEpisodes_Implementation(const TArray<int32>& AgentId)
{
	// Can be overridden to reset agent without blueprints
}
