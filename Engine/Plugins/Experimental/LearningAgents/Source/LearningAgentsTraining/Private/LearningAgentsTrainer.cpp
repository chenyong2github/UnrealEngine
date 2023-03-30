// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsTrainer.h"

#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "LearningAgentsType.h"
#include "LearningAgentsRewards.h"
#include "LearningAgentsCompletions.h"
#include "LearningAgentsObservations.h"
#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningExperience.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "LearningNeuralNetwork.h"
#include "LearningPolicyObject.h"
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

ULearningAgentsTrainer::ULearningAgentsTrainer() : ULearningAgentsTypeComponent() {}
ULearningAgentsTrainer::ULearningAgentsTrainer(FVTableHelper& Helper) : ULearningAgentsTrainer() {}
ULearningAgentsTrainer::~ULearningAgentsTrainer() {}

void ULearningAgentsTrainer::OnAgentAdded_Implementation(int32 AgentId, UObject* Agent)
{
	Super::OnAgentAdded_Implementation(AgentId, Agent);

	if (bSetupPerformed)
	{
		// Reset the instance and the buffer in case we have stale data from another agent that was using this id previously
		ResetInstance({AgentId});
		EpisodeBuffer->Reset(AgentId);
	}
}

void ULearningAgentsTrainer::SetupTrainer(ULearningAgentsType* AgentTypeOverride, const FLearningAgentsTrainerSettings& Settings)
{
	if (AgentTypeOverride)
	{
		AgentType = AgentTypeOverride;
	}
	else if (!AgentType)
	{
		UE_LOG(LogLearning, Error, TEXT("LearningAgentsTrainer setup called but AgentType is nullptr."));
		return;
	}

	if (!AgentType->IsSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("ULearningAgentsTrainer: AgentType not setup. "
			"Consider using ULearningAgentsTrainer::OnAgentTypeSetupComplete when calling SetupTrainer."));
		return;
	}

	// Reset Setup Flag
	bSetupPerformed = false;

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

	// Done!
	bSetupPerformed = true;
}

const bool ULearningAgentsTrainer::IsSetupPerformed() const
{
	return bSetupPerformed;
}

void ULearningAgentsTrainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsTraining)
	{
		StopTraining();
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
	check(!bSetupPerformed);
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
	check(!bSetupPerformed);
	CompletionObjects.Add(Object);
	CompletionFeatures.Add(Completion);
}

const bool ULearningAgentsTrainer::IsTraining() const
{
	return bIsTraining;
}

void ULearningAgentsTrainer::StartTraining(
	const FLearningAgentsTrainerTrainingSettings& TrainingSettings, 
	const FLearningAgentsTrainerGameSettings& Settings)
{
	if (!bSetupPerformed)
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

	FApp::SetUseFixedTimeStep(Settings.bUseFixedTimeStep);

	if (Settings.FixedTimeStepFrequency > UE_SMALL_NUMBER)
	{
		FApp::SetFixedDeltaTime(1.0f / Settings.FixedTimeStepFrequency);
		if (Settings.bSetMaxPhysicsStepToFixedTimeStep && PhysicsSettings)
		{
			PhysicsSettings->MaxPhysicsDeltaTime = 1.0f / Settings.FixedTimeStepFrequency;
		}
	}
	else
	{
		UE_LOG(LogLearning, Warning, TEXT("Provided invalid FixedTimeStepFrequency: %0.5f"), Settings.FixedTimeStepFrequency);
	}

	if (Settings.bDisableVSync && GameSettings)
	{
		GameSettings->SetVSyncEnabled(false);
		GameSettings->ApplySettings(false);
	}

	if (Settings.bUseUnlitViewportRendering && ViewportClient)
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

	// todo bmulcahy all these paths should be configurable by the dev
	UE_LEARNING_CHECKF(PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_LINUX, TEXT("Python only supported on Windows, Mac, and Linux."));
	const FString PythonExe = PLATFORM_WINDOWS ? TEXT("python.exe") : TEXT("bin/python");
	const FString EnginePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*(FPaths::RootDir() / TEXT("../../../../../../Engine")));
	const FString PythonExecutablePath = EnginePath / TEXT("Binaries/ThirdParty/Python3") / FPlatformMisc::GetUBTPlatform() / PythonExe;
	const FString SitePackagesPath = EnginePath / TEXT("Plugins/Experimental/PythonFoundationPackages/Content/Python/Lib/") / FPlatformMisc::GetUBTPlatform() / TEXT("site-packages");
	const FString PythonContentPath = EnginePath / TEXT("Plugins/Experimental/LearningAgents/Content/Python/");
	const FString IntermediatePath = EnginePath / TEXT("Plugins/Experimental/LearningAgents/Intermediate");
#endif

	UE::Learning::FPPOTrainerSettings PPOSettings;
	PPOSettings.IterationNum = TrainingSettings.NumberOfIterations;
	PPOSettings.bReinitializeNetwork = TrainingSettings.bReinitializeNetwork;
	PPOSettings.bUseTensorboard = TrainingSettings.bUseTensorboard;
	PPOSettings.InitialActionScale = TrainingSettings.InitialActionScale;
	PPOSettings.DiscountFactor = TrainingSettings.DiscountFactor;
	PPOSettings.Seed = TrainingSettings.RandomSeed;
	PPOSettings.TrimEpisodeStartStepNum = TrainingSettings.NumberOfStepsToTrimAtStartOfEpisode;
	PPOSettings.TrimEpisodeEndStepNum = TrainingSettings.NumberOfStepsToTrimAtEndOfEpisode;
	PPOSettings.Device = TrainingSettings.Device == ELearningAgentsTrainerDevice::CPU ? UE::Learning::ETrainerDevice::CPU : UE::Learning::ETrainerDevice::GPU;

	UE::Learning::FNeuralNetwork& NeuralNetwork = AgentType->GetNeuralNetwork();
	const UE::Learning::FNeuralNetworkPolicyFunction& Policy = AgentType->GetPolicy();

	// Start Python Training Process (this must be done on main thread)
	Trainer = MakeUnique<UE::Learning::FSharedMemoryPPOTrainer>(
		TEXT("Training"),
		PythonExecutablePath,
		SitePackagesPath,
		PythonContentPath,
		IntermediatePath,
		NeuralNetwork,
		Policy.Settings.ActionNoiseMin,
		Policy.Settings.ActionNoiseMax,
		*ReplayBuffer,
		PPOSettings);

	TrainerTimeout = TrainingSettings.TrainerCommunicationTimeout;

	UE::Learning::ETrainerResponse Response;

	if (TrainingSettings.bReinitializeNetwork)
	{
		UE_LOG(LogLearning, Display, TEXT("Receiving initial policy..."));
		Response = Trainer->RecvPolicy(NeuralNetwork, TrainerTimeout);
	}
	else
	{
		UE_LOG(LogLearning, Display, TEXT("Sending initial policy..."));
		Response = Trainer->SendPolicy(NeuralNetwork, TrainerTimeout);
	}

	if (Response != UE::Learning::ETrainerResponse::Success)
	{
		UE_LOG(LogLearning, Error, TEXT("Error sending or receiving policy from trainer. Check log for errors."));
		Trainer->Terminate();
		UE_DEBUG_BREAK();
		return;
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
	if (bIsTraining)
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

void ULearningAgentsTrainer::StopTraining()
{
	if (bIsTraining)
	{
		UE_LOG(LogLearning, Display, TEXT("Stopping training..."));
		Trainer->SendStop();
		DoneTraining();
	}
}

void ULearningAgentsTrainer::EvaluateRewards()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsTrainer::EvaluateRewards);

	if (!bSetupPerformed)
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

	if (!bSetupPerformed)
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
	if (!bSetupPerformed)
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before training can be performed."));
		return;
	}

	if (!bIsTraining)
	{
		if (!bPreviouslyLogged)
		{
			UE_LOG(LogLearning, Error, TEXT("Attempted to iterate training but the trainer is not running. Ensure StartTraining() is called before IterateTraining() and no errors occurred."));
			UE_DEBUG_BREAK();
			bPreviouslyLogged = true;
		}

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
				StopTraining();
				UE_DEBUG_BREAK();
				return;
			}

			// Reset Replay Buffer

			ReplayBuffer->Reset();

			// Get Updated Policy

			Response = Trainer->RecvPolicy(AgentType->GetNeuralNetwork(), TrainerTimeout);

			if (Response == UE::Learning::ETrainerResponse::Completed)
			{
				UE_LOG(LogLearning, Display, TEXT("Trainer completed training."));
				DoneTraining();
				return;
			}
			else if (Response != UE::Learning::ETrainerResponse::Success)
			{
				UE_LOG(LogLearning, Error, TEXT("Error waiting for policy from trainer. Check log for errors."));
				StopTraining();
				UE_DEBUG_BREAK();
				return;
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

	if (!bSetupPerformed)
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
