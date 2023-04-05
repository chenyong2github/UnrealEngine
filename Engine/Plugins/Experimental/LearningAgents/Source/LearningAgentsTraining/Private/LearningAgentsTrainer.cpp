// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsTrainer.h"

#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "LearningAgentsType.h"
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

ULearningAgentsTrainer::ULearningAgentsTrainer() : UActorComponent() {}
ULearningAgentsTrainer::ULearningAgentsTrainer(FVTableHelper& Helper) : ULearningAgentsTrainer() {}
ULearningAgentsTrainer::~ULearningAgentsTrainer() {}

void ULearningAgentsTrainer::SetupTrainer(
	ULearningAgentsType* InAgentType,
	ULearningAgentsPolicy* InPolicy,
	ULearningAgentsCritic* InCritic,
	const FLearningAgentsTrainerSettings& Settings)
{
	if (IsTrainerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup already performed!"));
		return;
	}

	// Setup Agent Type, Policy, and Critic

	if (!InAgentType)
	{
		UE_LOG(LogLearning, Error, TEXT("SetupTrainer called with nullptr for AgentType."));
		return;
	}

	if (!InAgentType->IsSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentType Setup not performed."));
		return;
	}

	AgentType = InAgentType;

	if (!InPolicy)
	{
		UE_LOG(LogLearning, Error, TEXT("SetupTrainer called with nullptr for Policy."));
		return;
	}

	if (!InPolicy->IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Policy Setup not Performed"));
		return;
	}

	Policy = InPolicy;

	if (InCritic)
	{
		if (!InCritic->IsCriticSetupPerformed())
		{
			UE_LOG(LogLearning, Error, TEXT("Critic Setup not Performed"));
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
		AgentType->GetInstanceData().ToSharedRef(),
		AgentType->GetMaxInstanceNum());

	// Setup Completions

	CompletionObjects.Empty();
	CompletionFeatures.Empty();
	SetupCompletions(this);
	Completions = MakeShared<UE::Learning::FAnyCompletion>(TEXT("Completions"),
		TLearningArrayView<1, const TSharedRef<UE::Learning::FCompletionObject>>(CompletionFeatures),
		AgentType->GetInstanceData().ToSharedRef(),
		AgentType->GetMaxInstanceNum());

	// Create Episode Buffer

	EpisodeBuffer = MakeUnique<UE::Learning::FEpisodeBuffer>();
	EpisodeBuffer->Resize(
		AgentType->GetMaxInstanceNum(),
		Settings.MaxStepNum,
		AgentType->GetObservationFeature().DimNum(),
		AgentType->GetActionFeature().DimNum());

	MaxStepsCompletion = Settings.MaxStepsCompletion;

	// Create Replay Buffer

	ReplayBuffer = MakeUnique<UE::Learning::FReplayBuffer>();
	ReplayBuffer->Resize(
		AgentType->GetObservationFeature().DimNum(),
		AgentType->GetActionFeature().DimNum(),
		Settings.MaximumRecordedEpisodesPerIteration,
		Settings.MaximumRecordedStepsPerIteration);

	// Create Reset Buffer

	ResetBuffer = MakeUnique<UE::Learning::FResetInstanceBuffer>();
	ResetBuffer->Resize(AgentType->GetMaxInstanceNum());

	// Record Timeout Setting

	TrainerTimeout = Settings.TrainerCommunicationTimeout;

	// Done!

	bTrainerSetupPerformed = true;
}

bool ULearningAgentsTrainer::IsTrainerSetupPerformed() const
{
	return bTrainerSetupPerformed;
}

void ULearningAgentsTrainer::AddAgent(int32 AgentId)
{
	if (!IsTrainerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Trainer setup must be run before agents can be added!"));
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

	if (IsTraining())
	{
		// Reset the instance and the buffer in case we have stale 
		// data from another agent that was using this id previously
		ResetInstance({ AgentId });
		EpisodeBuffer->Reset(AgentId);
	}
}

void ULearningAgentsTrainer::RemoveAgent(int32 AgentId)
{
	if (!IsTrainerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Trainer setup must be run before agents can be removed!"));
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

bool ULearningAgentsTrainer::HasAgent(int32 AgentId) const
{
	return SelectedAgentsSet.Contains(AgentId);
}

ULearningAgentsType* ULearningAgentsTrainer::GetAgentType(TSubclassOf<ULearningAgentsType> AgentClass)
{
	if (!IsTrainerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Trainer setup must be run before getting the agent type!"));
		return nullptr;
	}

	return AgentType;
}

const UObject* ULearningAgentsTrainer::GetAgent(int32 AgentId) const
{
	UE_LEARNING_CHECK(AgentType);
	return AgentType->GetAgent(AgentId);
}

UObject* ULearningAgentsTrainer::GetAgent(int32 AgentId)
{
	UE_LEARNING_CHECK(AgentType);
	return AgentType->GetAgent(AgentId);
}

const ULearningAgentsType* ULearningAgentsTrainer::GetAgentType() const
{
	UE_LEARNING_CHECK(AgentType);
	return AgentType;
}

ULearningAgentsType* ULearningAgentsTrainer::GetAgentType()
{
	UE_LEARNING_CHECK(AgentType);
	return AgentType;
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
	UE_LEARNING_CHECK(!IsTrainerSetupPerformed());
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
	UE_LEARNING_CHECK(!IsTrainerSetupPerformed());
	CompletionObjects.Add(Object);
	CompletionFeatures.Add(Completion);
}

const bool ULearningAgentsTrainer::IsTraining() const
{
	return bIsTraining;
}

void ULearningAgentsTrainer::BeginTraining(
	const FLearningAgentsTrainerTrainingSettings& TrainingSettings, 
	const FLearningAgentsTrainerGameSettings& TrainerGameSettings,
	const FLearningAgentsCriticSettings& CriticSettings,
	const bool bReinitializePolicyNetwork,
	const bool bReinitializeCriticNetwork)
{
	if (!IsTrainerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before training can start."));
		return;
	}

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
		UE_LOG(LogLearning, Warning, TEXT("Provided invalid FixedTimeStepFrequency: %0.5f"), TrainerGameSettings.FixedTimeStepFrequency);
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

#if WITH_EDITOR
	const FString PythonExecutablePath = UE::Learning::Trainer::DefaultEditorPythonExecutablePath();
	const FString SitePackagesPath = UE::Learning::Trainer::DefaultEditorSitePackagesPath();
	const FString PythonContentPath = UE::Learning::Trainer::DefaultEditorPythonContentPath();
	const FString IntermediatePath = UE::Learning::Trainer::DefaultEditorIntermediatePath();
#else
	// If we want to run training in a cooked, non-editor build, then by default we wont have access to python or the 
	// learning training scripts - these are editor-only and will be stripped during the cooking process.
	//
	// However, running training in non-editor builds can be very important - we probably want to disable rendering 
	// and sound while we are training to make experience gathering as fast as possible - and for any non-trivial game 
	// is simply may not be realistic to run it for a long time in play-in-editor mode.
	//
	// For this reason even in non-editor builds we let you provide paths the `python.exe` provided by the editor, 
	// as well as PythonFoundationPackages site-packages, and the Learning training scripts. This allows you to 
	// run training when these things actually exist somewhere on your machine, which will usually be the case on 
	// a normal development machine.

	UE_LEARNING_CHECKF(PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_LINUX, TEXT("Python only supported on Windows, Mac, and Linux."));
	const FString PythonExe = PLATFORM_WINDOWS ? TEXT("python.exe") : TEXT("bin/python");
	const FString EnginePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*(FPaths::RootDir() / TEXT("../../../../../../Engine")));
	const FString PythonExecutablePath = EnginePath / TEXT("Binaries/ThirdParty/Python3") / FPlatformMisc::GetUBTPlatform() / PythonExe;
	const FString SitePackagesPath = EnginePath / TEXT("Plugins/Experimental/PythonFoundationPackages/Content/Python/Lib/") / FPlatformMisc::GetUBTPlatform() / TEXT("site-packages");
	const FString PythonContentPath = EnginePath / TEXT("Plugins/Experimental/LearningAgents/Content/Python/");
	const FString IntermediatePath = EnginePath / TEXT("Plugins/Experimental/LearningAgents/Intermediate");
#endif

	UE::Learning::FPPOTrainerTrainingSettings PPOTrainingSettings;
	PPOTrainingSettings.IterationNum = TrainingSettings.NumberOfIterations;
	PPOTrainingSettings.bUseTensorboard = TrainingSettings.bUseTensorboard;
	PPOTrainingSettings.InitialActionScale = TrainingSettings.InitialActionScale;
	PPOTrainingSettings.DiscountFactor = TrainingSettings.DiscountFactor;
	PPOTrainingSettings.Seed = TrainingSettings.RandomSeed;
	PPOTrainingSettings.TrimEpisodeStartStepNum = TrainingSettings.NumberOfStepsToTrimAtStartOfEpisode;
	PPOTrainingSettings.TrimEpisodeEndStepNum = TrainingSettings.NumberOfStepsToTrimAtEndOfEpisode;
	PPOTrainingSettings.Device = TrainingSettings.Device == ELearningAgentsTrainerDevice::CPU ? UE::Learning::ETrainerDevice::CPU : UE::Learning::ETrainerDevice::GPU;

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
			UE_LOG(LogLearning, Warning, TEXT("StartTraining got different Critic Network Settings to those provided to SetupCritic."));
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

	// We assume that if the critic has been setup on the agent type, then
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

	UE_LOG(LogLearning, Display, TEXT("Receiving initial policy..."));

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
		UE_LOG(LogLearning, Error, TEXT("Error sending or receiving policy from trainer: %s. Check log for errors."), UE::Learning::Trainer::GetResponseString(Response));
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
			UE_LOG(LogLearning, Error, TEXT("Error sending or receiving critic from trainer: %s. Check log for errors."), UE::Learning::Trainer::GetResponseString(Response));
			Trainer->Terminate();
			return;
		}
	}

	// Reset Agents, Episode Buffer, and Replay Buffer
	ResetInstance(SelectedAgentIds);

	EpisodeBuffer->Reset(SelectedAgentsSet);
	ReplayBuffer->Reset();

	// Done!
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

		// Done
		bIsTraining = false;
	}
}

void ULearningAgentsTrainer::EndTraining()
{
	if (IsTraining())
	{
		UE_LOG(LogLearning, Display, TEXT("Stopping training..."));
		Trainer->SendStop();
		DoneTraining();
	}
}

void ULearningAgentsTrainer::EvaluateRewards()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsTrainer::EvaluateRewards);

	if (!IsTrainerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before rewards can be evaluated."));
		return;
	}

	SetRewards(SelectedAgentIds);

	Rewards->Evaluate(SelectedAgentsSet);

#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsReward* RewardObject : RewardObjects)
	{
		if (RewardObject)
		{
			RewardObject->VisualLog(SelectedAgentsSet);
		}
	}
#endif
}

void ULearningAgentsTrainer::EvaluateCompletions()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsTrainer::EvaluateCompletions);

	if (!IsTrainerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before completions can be evaluated."));
		return;
	}

	SetCompletions(SelectedAgentIds);

	Completions->Evaluate(SelectedAgentsSet);
	
#if ENABLE_VISUAL_LOG
	for (const ULearningAgentsCompletion* CompletionObject : CompletionObjects)
	{
		if (CompletionObject)
		{
			CompletionObject->VisualLog(SelectedAgentsSet);
		}
	}
#endif
}

void ULearningAgentsTrainer::IterateTraining()
{
	if (!IsTrainerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before training can be performed."));
		return;
	}

	if (!IsTraining())
	{
		UE_LOG(LogLearning, Error, TEXT("Attempted to iterate training but the trainer is not running. Ensure StartTraining() is called before IterateTraining() and no errors occurred."));
		return;
	}

	UE::Learning::FFeatureObject& Observations = AgentType->GetObservationFeature();
	UE::Learning::FFeatureObject& Actions = AgentType->GetActionFeature();

	// Add Experience to Episode Buffer

	EpisodeBuffer->Push(
		Observations.FeatureBuffer(),
		Actions.FeatureBuffer(),
		Rewards->RewardBuffer(),
		SelectedAgentsSet);

	// Check for completion based on reaching the maximum episode length

	UE::Learning::Completion::EvaluateEndOfEpisodeCompletions(
		Completions->CompletionBuffer(),
		EpisodeBuffer->GetEpisodeStepNums(),
		EpisodeBuffer->GetMaxStepNum(),
		MaxStepsCompletion == ELearningAgentsCompletion::Truncation 
			? UE::Learning::ECompletionMode::Truncated 
			: UE::Learning::ECompletionMode::Terminated,
		SelectedAgentsSet);

	// Find the set of Instances that need to be reset

	ResetBuffer->SetResetInstancesFromCompletions(Completions->CompletionBuffer(), SelectedAgentsSet);

	if (ResetBuffer->GetResetInstanceNum() > 0)
	{
		// Encode Observations for completed Instances

		AgentType->SetObservations(ResetBuffer->GetResetInstances().ToArray());

		Observations.Encode(ResetBuffer->GetResetInstances());
		
#if ENABLE_VISUAL_LOG
		for (const ULearningAgentsObservation* ObservationObject : AgentType->GetObservationObjects())
		{
			if (ObservationObject)
			{
				ObservationObject->VisualLog(ResetBuffer->GetResetInstances());
			}
		}
#endif

		// Push Experience to Replay Buffer

		bool bReplayBufferFull = ReplayBuffer->AddEpisodes(
			Completions->CompletionBuffer(),
			Observations.FeatureBuffer(),
			*EpisodeBuffer,
			ResetBuffer->GetResetInstances());

		if (bReplayBufferFull)
		{
			// Send Experience

			UE::Learning::ETrainerResponse Response = UE::Learning::ETrainerResponse::Success;

			Response = Trainer->SendExperience(*ReplayBuffer, TrainerTimeout);

			if (Response != UE::Learning::ETrainerResponse::Success)
			{
				UE_LOG(LogLearning, Error, TEXT("Error waiting to push experience to trainer. Check log for errors."));
				EndTraining();
				return;
			}

			// Reset Replay Buffer

			ReplayBuffer->Reset();

			// Get Updated Policy

			Response = Trainer->RecvPolicy(Policy->GetPolicyNetwork(), TrainerTimeout);

			if (Response == UE::Learning::ETrainerResponse::Completed)
			{
				UE_LOG(LogLearning, Display, TEXT("Trainer completed training."));
				DoneTraining();
				return;
			}
			else if (Response != UE::Learning::ETrainerResponse::Success)
			{
				UE_LOG(LogLearning, Error, TEXT("Error waiting for policy from trainer. Check log for errors."));
				EndTraining();
				return;
			}

			// Get Updated Critic

			if (Critic)
			{
				Response = Trainer->RecvCritic(Critic->GetCriticNetwork(), TrainerTimeout);

				if (Response != UE::Learning::ETrainerResponse::Success)
				{
					UE_LOG(LogLearning, Error, TEXT("Error waiting for critic from trainer. Check log for errors."));
					EndTraining();
					return;
				}
			}

			// Mark all instances for reset since we have a new policy
			ResetBuffer->SetResetInstances(SelectedAgentsSet);
		}

		// Reset Instances
		ResetInstance(ResetBuffer->GetResetInstances().ToArray());

		EpisodeBuffer->Reset(ResetBuffer->GetResetInstances());
	}
}

void ULearningAgentsTrainer::ResetAllInstances()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsTrainer::ResetAllInstances);

	if (!IsTrainerSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before agents can be reset."));
		return;
	}

	ResetInstance(SelectedAgentIds);

	EpisodeBuffer->Reset(SelectedAgentsSet);
}

void ULearningAgentsTrainer::ResetInstance_Implementation(const TArray<int32>& AgentId)
{
	// Can be overridden to reset agent without blueprints
}
