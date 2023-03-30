// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningPPOTrainer.h"

#include "LearningArray.h"
#include "LearningLog.h"
#include "LearningNeuralNetwork.h"
#include "LearningExperience.h"
#include "LearningProgress.h"
#include "LearningSharedMemory.h"
#include "LearningSharedMemoryTraining.h"
#include "LearningSocketTraining.h"

#include "Misc/MonitoredProcess.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Sockets.h"
#include "Common/TcpSocketBuilder.h"
#include "SocketSubsystem.h"

ULearningSocketPPOTrainerServerCommandlet::ULearningSocketPPOTrainerServerCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

int32 ULearningSocketPPOTrainerServerCommandlet::Main(const FString& Commandline)
{
	UE_LOG(LogLearning, Display, TEXT("Running PPO Training Server Commandlet..."));

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Params;

	UCommandlet::ParseCommandLine(*Commandline, Tokens, Switches, Params);

	const FString* PythonExecutiblePathParam = Params.Find(TEXT("PythonExecutiblePath"));
	const FString* SitePackagesPathParam = Params.Find(TEXT("SitePackagesPath"));
	const FString* PythonContentPathParam = Params.Find(TEXT("PythonContentPath"));
	const FString* IntermediatePathParam = Params.Find(TEXT("IntermediatePath"));
	const FString* IpAddressParam = Params.Find(TEXT("IpAddress"));
	const FString* PortParam = Params.Find(TEXT("Port"));
	const FString* LogSettingsParam = Params.Find(TEXT("LogSettings"));

#if WITH_EDITOR
	const FString PythonExecutiblePath = PythonExecutiblePathParam ? *PythonExecutiblePathParam : UE::Learning::Trainer::DefaultEditorPythonExecutablePath();
	const FString SitePackagesPath = SitePackagesPathParam ? *SitePackagesPathParam : UE::Learning::Trainer::DefaultEditorSitePackagesPath();
	const FString PythonContentPath = PythonContentPathParam ? *PythonContentPathParam : UE::Learning::Trainer::DefaultEditorPythonContentPath();
	const FString IntermediatePath = IntermediatePathParam ? *IntermediatePathParam : UE::Learning::Trainer::DefaultEditorIntermediatePath();
#else
	UE_LEARNING_NOT_IMPLEMENTED();
	const FString PythonExecutiblePath = TEXT("");
	const FString SitePackagesPath = TEXT("");
	const FString PythonContentPath = TEXT("");
	const FString IntermediatePath = TEXT("");
	return 0;
#endif
	const TCHAR* IpAddress = IpAddressParam ? *(*IpAddressParam) : UE::Learning::Trainer::DefaultIp;
	const uint32 Port = PortParam ? FCString::Atoi(*(*PortParam)) : UE::Learning::Trainer::DefaultPort;
	
	UE::Learning::ELogSetting LogSettings = UE::Learning::ELogSetting::Normal;
	if (LogSettingsParam)
	{
		if (*LogSettingsParam == TEXT("Normal"))
		{
			LogSettings = UE::Learning::ELogSetting::Normal;
		}
		else if (*LogSettingsParam == TEXT("Silent"))
		{
			LogSettings = UE::Learning::ELogSetting::Silent;
		}
		else
		{
			UE_LEARNING_NOT_IMPLEMENTED();
			return 1;
		}
	}
	
	UE_LOG(LogLearning, Display, TEXT("---  PPO Training Server Arguments ---"));
	UE_LOG(LogLearning, Display, TEXT("PythonExecutiblePath: %s"), *PythonExecutiblePath);
	UE_LOG(LogLearning, Display, TEXT("SitePackagesPath: %s"), *SitePackagesPath);
	UE_LOG(LogLearning, Display, TEXT("PythonContentPath: %s"), *PythonContentPath);
	UE_LOG(LogLearning, Display, TEXT("IntermediatePath: %s"), *IntermediatePath);
	UE_LOG(LogLearning, Display, TEXT("IpAddress: %s"), IpAddress);
	UE_LOG(LogLearning, Display, TEXT("Port: %i"), Port);
	UE_LOG(LogLearning, Display, TEXT("LogSettings: %s"), LogSettings == UE::Learning::ELogSetting::Normal ? TEXT("Normal") : TEXT("Silent"));

	UE::Learning::FSocketPPOTrainerServerProcess ServerProcess(
		PythonExecutiblePath,
		SitePackagesPath,
		PythonContentPath,
		IntermediatePath,
		IpAddress,
		Port,
		LogSettings);

	while (ServerProcess.IsRunning())
	{
		FPlatformProcess::Sleep(0.01f);
	}

	return 0;
}

namespace UE::Learning
{
	FSharedMemoryPPOTrainer::FSharedMemoryPPOTrainer(
		const FString& TaskName,
		const FString& PythonExecutiblePath,
		const FString& SitePackagesPath,
		const FString& PythonContentPath,
		const FString& IntermediatePath,
		const FNeuralNetwork& Network,
		const float ActionNoiseMin,
		const float ActionNoiseMax,
		const FReplayBuffer& ReplayBuffer,
		const FPPOTrainerSettings& Settings,
		const ELogSetting LogSettings,
		const bool bHideTrainingWindow,
		const bool bRedirectTrainingOutput)
	{
		UE_LEARNING_CHECK(Network.GetOutputNum() % 2 == 0);

		const int32 ObservationVectorDimensionNum = Network.GetInputNum();
		const int32 ActionVectorDimensionNum = Network.GetOutputNum() / 2;

		if (!ensure(Policy.Region == nullptr))
		{
			UE_LOG(LogLearning, Warning, TEXT("Training already started!"));
			return;
		}

		// Allocate shared memory

		ProcessIdx = 0;
		FParse::Value(FCommandLine::Get(), TEXT("LearningProcessIdx"), ProcessIdx);

		if (ProcessIdx == 0)
		{
			// Allocate Shared Memory

			Policy = SharedMemory::Allocate<1, uint8>({ Network.GetTotalByteNum() });
			Controls = SharedMemory::Allocate<2, volatile int32>({ Settings.ProcessNum, SharedMemoryTraining::GetControlNum() });
			EpisodeStarts = SharedMemory::Allocate<2, int32>({ Settings.ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });
			EpisodeLengths = SharedMemory::Allocate<2, int32>({ Settings.ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });
			EpisodeCompletionModes = SharedMemory::Allocate<2, ECompletionMode>({ Settings.ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });
			EpisodeFinalObservations = SharedMemory::Allocate<3, float>({ Settings.ProcessNum, ReplayBuffer.GetMaxEpisodeNum(), ObservationVectorDimensionNum });
			Observations = SharedMemory::Allocate<3, float>({ Settings.ProcessNum, ReplayBuffer.GetMaxStepNum(), ObservationVectorDimensionNum });
			Actions = SharedMemory::Allocate<3, float>({ Settings.ProcessNum, ReplayBuffer.GetMaxStepNum(), ActionVectorDimensionNum });
			Rewards = SharedMemory::Allocate<2, float>({ Settings.ProcessNum, ReplayBuffer.GetMaxStepNum() });

			// We need to zero the control memory before we start
			// the training sub-process since it may contain uninitialized 
			// values or those left over from previous runs.

			Array::Zero(Controls.View);

			// Create Experience Gathering Sub-processes

			for (uint16 SubprocessIdx = 1; SubprocessIdx < Settings.ProcessNum; SubprocessIdx++)
			{
				ensureMsgf(!WITH_EDITOR || IsRunningCommandlet(), TEXT("Multi-processing generally does not work in-editor as it requires a standalone executable."));

				FString SubprocessCommandLine = FCommandLine::GetOriginal();

				SubprocessCommandLine += FString::Printf(TEXT(" -LearningProcessIdx %i"), SubprocessIdx);
				SubprocessCommandLine += FString(TEXT(" -LearningPolicyGuid ")) + Policy.Guid.ToString();
				SubprocessCommandLine += FString(TEXT(" -LearningControlsGuid ")) + Controls.Guid.ToString();
				SubprocessCommandLine += FString(TEXT(" -LearningEpisodeStartsGuid ")) + EpisodeStarts.Guid.ToString();
				SubprocessCommandLine += FString(TEXT(" -LearningEpisodeLengthsGuid ")) + EpisodeLengths.Guid.ToString();
				SubprocessCommandLine += FString(TEXT(" -LearningEpisodeCompletionModesGuid ")) + EpisodeCompletionModes.Guid.ToString();
				SubprocessCommandLine += FString(TEXT(" -LearningEpisodeFinalObservationsGuid ")) + EpisodeFinalObservations.Guid.ToString();
				SubprocessCommandLine += FString(TEXT(" -LearningObservationsGuid ")) + Observations.Guid.ToString();
				SubprocessCommandLine += FString(TEXT(" -LearningActionsGuid ")) + Actions.Guid.ToString();
				SubprocessCommandLine += FString(TEXT(" -LearningRewardsGuid ")) + Rewards.Guid.ToString();

				const TSharedPtr<FMonitoredProcess> Subprocess = MakeShared<FMonitoredProcess>(
					FPlatformProcess::ExecutablePath(), 
					SubprocessCommandLine, 
					Settings.bMultiProcessHideTrainingWindow, 
					Settings.bMultiProcessRedirectTrainingOutput);

				if (Settings.bMultiProcessRedirectTrainingOutput)
				{
					Subprocess->OnCanceled().BindRaw(this, &FSharedMemoryPPOTrainer::HandleSubprocessCanceled);
					Subprocess->OnCompleted().BindRaw(this, &FSharedMemoryPPOTrainer::HandleSubprocessCompleted);
					Subprocess->OnOutput().BindStatic(&FSharedMemoryPPOTrainer::HandleSubprocessOutput);
				}

				Subprocess->Launch();

				UE_LOG(LogLearning, Display, TEXT("Subprocess Command: %s %s"), FPlatformProcess::ExecutablePath(), *SubprocessCommandLine);

				ExperienceGatheringSubprocesses.Emplace(Subprocess);
			}

			// Write Config

			const FString TimeStamp = FDateTime::Now().ToFormattedString(TEXT("%Y-%m-%d_%H-%M-%S"));
			const FString TrainerMethod = TEXT("PPO");
			const FString TrainerType = TEXT("SharedMemory");
			const FString ConfigPath = IntermediatePath / TEXT("Configs") / FString::Printf(TEXT("%s_%s_%s_%s.json"), *TaskName, *TrainerMethod, *TrainerType, *TimeStamp);

			TSharedRef<FJsonObject> ConfigObject = MakeShared<FJsonObject>();
			ConfigObject->SetStringField(TEXT("TaskName"), *TaskName);
			ConfigObject->SetStringField(TEXT("TrainerMethod"), TrainerMethod);
			ConfigObject->SetStringField(TEXT("TrainerType"), TrainerType);
			ConfigObject->SetStringField(TEXT("TimeStamp"), *TimeStamp);

			ConfigObject->SetStringField(TEXT("SitePackagesPath"), *SitePackagesPath);
			ConfigObject->SetStringField(TEXT("IntermediatePath"), *IntermediatePath);

			ConfigObject->SetStringField(TEXT("PolicyGuid"), *Policy.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			ConfigObject->SetStringField(TEXT("ControlsGuid"), *Controls.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			ConfigObject->SetStringField(TEXT("EpisodeStartsGuid"), *EpisodeStarts.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			ConfigObject->SetStringField(TEXT("EpisodeLengthsGuid"), *EpisodeLengths.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			ConfigObject->SetStringField(TEXT("EpisodeCompletionModesGuid"), *EpisodeCompletionModes.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			ConfigObject->SetStringField(TEXT("EpisodeFinalObservationsGuid"), *EpisodeFinalObservations.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			ConfigObject->SetStringField(TEXT("ObservationsGuid"), *Observations.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			ConfigObject->SetStringField(TEXT("ActionsGuid"), *Actions.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			ConfigObject->SetStringField(TEXT("RewardsGuid"), *Rewards.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));

			ConfigObject->SetNumberField(TEXT("ObservationVectorDimensionNum"), ObservationVectorDimensionNum);
			ConfigObject->SetNumberField(TEXT("ActionVectorDimensionNum"), ActionVectorDimensionNum);
			ConfigObject->SetNumberField(TEXT("NetworkTotalByteNum"), Network.GetTotalByteNum());
			ConfigObject->SetNumberField(TEXT("MaxEpisodeNum"), ReplayBuffer.GetMaxEpisodeNum());
			ConfigObject->SetNumberField(TEXT("MaxStepNum"), ReplayBuffer.GetMaxStepNum());
			ConfigObject->SetNumberField(TEXT("HiddenUnitNum"), Network.GetHiddenNum());
			ConfigObject->SetNumberField(TEXT("LayerNum"), Network.GetLayerNum());
			ConfigObject->SetStringField(TEXT("ActivationFunction"), ActivationFunctionString(Network.ActivationFunction));

			ConfigObject->SetNumberField(TEXT("ActionNoiseMin"), ActionNoiseMin);
			ConfigObject->SetNumberField(TEXT("ActionNoiseMax"), ActionNoiseMax);

			ConfigObject->SetNumberField(TEXT("ProcessNum"), Settings.ProcessNum);
			ConfigObject->SetNumberField(TEXT("IterationNum"), Settings.IterationNum);
			ConfigObject->SetNumberField(TEXT("LearningRateActor"), Settings.LearningRateActor);
			ConfigObject->SetNumberField(TEXT("LearningRateCritic"), Settings.LearningRateCritic);
			ConfigObject->SetNumberField(TEXT("LearningRateDecay"), Settings.LearningRateDecay);
			ConfigObject->SetNumberField(TEXT("WeightDecay"), Settings.WeightDecay);
			ConfigObject->SetNumberField(TEXT("InitialActionScale"), Settings.InitialActionScale);
			ConfigObject->SetNumberField(TEXT("BatchSize"), Settings.BatchSize);
			ConfigObject->SetNumberField(TEXT("EpsilonClip"), Settings.EpsilonClip);
			ConfigObject->SetNumberField(TEXT("ActionRegularizationWeight"), Settings.ActionRegularizationWeight);
			ConfigObject->SetNumberField(TEXT("EntropyWeight"), Settings.EntropyWeight);
			ConfigObject->SetNumberField(TEXT("GaeLambda"), Settings.GaeLambda);
			ConfigObject->SetBoolField(TEXT("ClipAdvantages"), Settings.bClipAdvantages);
			ConfigObject->SetBoolField(TEXT("AdvantageNormalization"), Settings.bAdvantageNormalization);
			ConfigObject->SetNumberField(TEXT("TrimEpisodeStartStepNum"), Settings.TrimEpisodeStartStepNum);
			ConfigObject->SetNumberField(TEXT("TrimEpisodeEndStepNum"), Settings.TrimEpisodeEndStepNum);
			ConfigObject->SetNumberField(TEXT("Seed"), Settings.Seed);
			ConfigObject->SetNumberField(TEXT("DiscountFactor"), Settings.DiscountFactor);
			ConfigObject->SetBoolField(TEXT("ReinitializeNetwork"), Settings.bReinitializeNetwork);
			ConfigObject->SetStringField(TEXT("Device"), Trainer::GetDeviceString(Settings.Device));
			ConfigObject->SetBoolField(TEXT("UseTensorBoard"), Settings.bUseTensorboard);
			ConfigObject->SetBoolField(TEXT("LoggingEnabled"), LogSettings == ELogSetting::Silent ? false : true);

			FString JsonString;
			TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString, 0);
			FJsonSerializer::Serialize(ConfigObject, JsonWriter, true);

			FFileHelper::SaveStringToFile(JsonString, *ConfigPath);

			// Start Python Training Sub-process

			const FString CommandLineArguments = FString::Printf(TEXT("\"%s\" SharedMemory \"%s\""), *(PythonContentPath / TEXT("train_ppo.py")), *ConfigPath);

			TrainingProcess = MakeShared<FMonitoredProcess>(PythonExecutiblePath, CommandLineArguments, bHideTrainingWindow, bRedirectTrainingOutput);

			if (bRedirectTrainingOutput)
			{
				TrainingProcess->OnCanceled().BindRaw(this, &FSharedMemoryPPOTrainer::HandleTrainingProcessCanceled);
				TrainingProcess->OnCompleted().BindRaw(this, &FSharedMemoryPPOTrainer::HandleTrainingProcessCompleted);
				TrainingProcess->OnOutput().BindStatic(&FSharedMemoryPPOTrainer::HandleTrainingProcessOutput);
			}

			TrainingProcess->Launch();
		}
		else
		{
			// Parse Guids from command line args

			FGuid PolicyGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningPolicyGuid"), PolicyGuid));
			FGuid ControlsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningControlsGuid"), ControlsGuid));
			FGuid EpisodeStartsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningEpisodeStartsGuid"), EpisodeStartsGuid));
			FGuid EpisodeLengthsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningEpisodeLengthsGuid"), EpisodeLengthsGuid));
			FGuid EpisodeCompletionModesGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningEpisodeCompletionModesGuid"), EpisodeCompletionModesGuid));
			FGuid EpisodeFinalObservationsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningEpisodeFinalObservationsGuid"), EpisodeFinalObservationsGuid));
			FGuid ObservationsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningObservationsGuid"), ObservationsGuid));
			FGuid ActionsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningActionsGuid"), ActionsGuid));
			FGuid RewardsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningRewardsGuid"), RewardsGuid));

			// Map shared memory

			Policy = SharedMemory::Map<1, uint8>(PolicyGuid, { Network.GetTotalByteNum() });
			Controls = SharedMemory::Map<2, volatile int32>(ControlsGuid, { Settings.ProcessNum, SharedMemoryTraining::GetControlNum() });
			EpisodeStarts = SharedMemory::Map<2, int32>(EpisodeStartsGuid, { Settings.ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });
			EpisodeLengths = SharedMemory::Map<2, int32>(EpisodeLengthsGuid, { Settings.ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });
			EpisodeCompletionModes = SharedMemory::Map<2, ECompletionMode>(EpisodeCompletionModesGuid, { Settings.ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });
			EpisodeFinalObservations = SharedMemory::Map<3, float>(EpisodeFinalObservationsGuid, { Settings.ProcessNum, ReplayBuffer.GetMaxEpisodeNum(), ObservationVectorDimensionNum });
			Observations = SharedMemory::Map<3, float>(ObservationsGuid, { Settings.ProcessNum, ReplayBuffer.GetMaxStepNum(), ObservationVectorDimensionNum });
			Actions = SharedMemory::Map<3, float>(ActionsGuid, { Settings.ProcessNum, ReplayBuffer.GetMaxStepNum(), ActionVectorDimensionNum });
			Rewards = SharedMemory::Map<2, float>(RewardsGuid, { Settings.ProcessNum, ReplayBuffer.GetMaxStepNum() });
		}
	}

	FSharedMemoryPPOTrainer::~FSharedMemoryPPOTrainer()
	{
		Terminate();
	}

	ETrainerResponse FSharedMemoryPPOTrainer::Wait(float Timeout)
	{
		const float SleepTime = 0.001f;
		float WaitTime = 0.0f;

		while (TrainingProcess.IsValid())
		{
			FPlatformProcess::Sleep(SleepTime);
			WaitTime += SleepTime;

			if (WaitTime > Timeout)
			{
				return ETrainerResponse::Timeout;
			}
		}

		return ETrainerResponse::Success;
	}

	void FSharedMemoryPPOTrainer::Terminate()
	{
		if (TrainingProcess.IsValid())
		{
			TrainingProcess->Cancel(true);
		}

		TrainingProcess.Reset();

		if (Policy.Region)
		{
			Deallocate();
		}
	}

	ETrainerResponse FSharedMemoryPPOTrainer::SendStop(const float Timeout)
	{
		return SharedMemoryTraining::SendStop(Controls.View[ProcessIdx]);
	}

	ETrainerResponse FSharedMemoryPPOTrainer::RecvPolicy(
		FNeuralNetwork& OutNetwork,
		const float Timeout,
		FRWLock* NetworkLock,
		const ELogSetting LogSettings)
	{
		return SharedMemoryTraining::RecvPolicy(
			Controls.View[ProcessIdx],
			OutNetwork,
			Policy.View,
			Timeout,
			NetworkLock,
			LogSettings);
	}

	ETrainerResponse FSharedMemoryPPOTrainer::SendPolicy(
		const FNeuralNetwork& Network,
		const float Timeout,
		FRWLock* NetworkLock,
		const ELogSetting LogSettings)
	{
		return SharedMemoryTraining::SendPolicy(
			Controls.View[ProcessIdx],
			Policy.View,
			Network,
			Timeout,
			NetworkLock,
			LogSettings);
	}

	ETrainerResponse FSharedMemoryPPOTrainer::SendExperience(
		const FReplayBuffer& ReplayBuffer,
		const float Timeout,
		const ELogSetting LogSettings)
	{
		return SharedMemoryTraining::SendExperience(
			EpisodeStarts.View[ProcessIdx],
			EpisodeLengths.View[ProcessIdx],
			EpisodeCompletionModes.View[ProcessIdx],
			EpisodeFinalObservations.View[ProcessIdx],
			Observations.View[ProcessIdx],
			Actions.View[ProcessIdx],
			Rewards.View[ProcessIdx],
			Controls.View[ProcessIdx],
			ReplayBuffer,
			Timeout,
			LogSettings);
	}

	void FSharedMemoryPPOTrainer::Deallocate()
	{
		if (Policy.Region != nullptr)
		{
			SharedMemory::Deallocate(Policy);
			SharedMemory::Deallocate(Controls);
			SharedMemory::Deallocate(EpisodeStarts);
			SharedMemory::Deallocate(EpisodeLengths);
			SharedMemory::Deallocate(EpisodeCompletionModes);
			SharedMemory::Deallocate(EpisodeFinalObservations);
			SharedMemory::Deallocate(Observations);
			SharedMemory::Deallocate(Actions);
			SharedMemory::Deallocate(Rewards);
		}
	}

	void FSharedMemoryPPOTrainer::HandleSubprocessCanceled()
	{
		UE_LOG(LogLearning, Warning, TEXT("Subprocess canceled"));
	}

	void FSharedMemoryPPOTrainer::HandleSubprocessCompleted(int32 ReturnCode)
	{
		if (ReturnCode != 0)
		{
			UE_LOG(LogLearning, Warning, TEXT("Subprocess finished with warnings or errors"));
		}
	}

	void FSharedMemoryPPOTrainer::HandleSubprocessOutput(FString Output)
	{
		if (!Output.IsEmpty())
		{
			UE_LOG(LogLearning, Display, TEXT("Subprocess: %s"), *Output);
		}
	}


	void FSharedMemoryPPOTrainer::HandleTrainingProcessCanceled()
	{
		UE_LOG(LogLearning, Warning, TEXT("Training process canceled"));

		TrainingProcess.Reset();
	}

	void FSharedMemoryPPOTrainer::HandleTrainingProcessCompleted(int32 ReturnCode)
	{
		if (ReturnCode != 0)
		{
			UE_LOG(LogLearning, Warning, TEXT("Training Process finished with warnings or errors"));
		}

		TrainingProcess.Reset();
	}

	void FSharedMemoryPPOTrainer::HandleTrainingProcessOutput(FString Output)
	{
		if (!Output.IsEmpty())
		{
			UE_LOG(LogLearning, Display, TEXT("Training Process: %s"), *Output);
		}
	}

	FSocketPPOTrainerServerProcess::FSocketPPOTrainerServerProcess(
		const FString& PythonExecutiblePath,
		const FString& SitePackagesPath,
		const FString& PythonContentPath,
		const FString& IntermediatePath,
		const TCHAR* IpAddress,
		const uint32 Port,
		const ELogSetting LogSettings)
	{
		const FString CommandLineArguments = FString::Printf(TEXT("\"%s\" Socket \"%s:%i\" \"%s\" \"%s\" %i"), 
			*(PythonContentPath / TEXT("train_ppo.py")), IpAddress, Port, *SitePackagesPath, *IntermediatePath, LogSettings == ELogSetting::Normal ? 1 : 0);

		TrainingProcess = MakeShared<FMonitoredProcess>(PythonExecutiblePath, CommandLineArguments, true, true);
		TrainingProcess->OnCanceled().BindRaw(this, &FSocketPPOTrainerServerProcess::HandleTrainingProcessCanceled);
		TrainingProcess->OnCompleted().BindRaw(this, &FSocketPPOTrainerServerProcess::HandleTrainingProcessCompleted);
		TrainingProcess->OnOutput().BindStatic(&FSocketPPOTrainerServerProcess::HandleTrainingProcessOutput);
		TrainingProcess->Launch();
	}

	FSocketPPOTrainerServerProcess::~FSocketPPOTrainerServerProcess()
	{
		Terminate();
	}

	bool FSocketPPOTrainerServerProcess::IsRunning() const
	{
		return TrainingProcess.IsValid();
	}

	bool FSocketPPOTrainerServerProcess::Wait(float Timeout)
	{
		const float SleepTime = 0.001f;
		float WaitTime = 0.0f;

		while (TrainingProcess.IsValid())
		{
			FPlatformProcess::Sleep(SleepTime);
			WaitTime += SleepTime;

			if (WaitTime > Timeout)
			{
				return false;
			}
		}

		return true;
	}

	void FSocketPPOTrainerServerProcess::Terminate()
	{
		if (TrainingProcess.IsValid())
		{
			TrainingProcess->Cancel(true);
		}

		TrainingProcess.Reset();
	}

	void FSocketPPOTrainerServerProcess::HandleTrainingProcessCanceled()
	{
		UE_LOG(LogLearning, Warning, TEXT("Training process canceled"));

		TrainingProcess.Reset();
	}

	void FSocketPPOTrainerServerProcess::HandleTrainingProcessCompleted(int32 ReturnCode)
	{
		if (ReturnCode != 0)
		{
			UE_LOG(LogLearning, Warning, TEXT("Training Process finished with warnings or errors"));
		}

		TrainingProcess.Reset();
	}

	void FSocketPPOTrainerServerProcess::HandleTrainingProcessOutput(FString Output)
	{
		if (!Output.IsEmpty())
		{
			UE_LOG(LogLearning, Display, TEXT("Training Process: %s"), *Output);
		}
	}

	FSocketPPOTrainer::FSocketPPOTrainer(
		ETrainerResponse& OutResponse,
		const FString& TaskName,
		const FNeuralNetwork& Network,
		const float ActionNoiseMin,
		const float ActionNoiseMax,
		const FReplayBuffer& ReplayBuffer,
		const TCHAR* IpAddress,
		const uint32 Port,
		const float Timeout,
		const FPPOTrainerSettings& Settings)
	{
		UE_LEARNING_CHECK(Network.GetOutputNum() % 2 == 0);

		const int32 ObservationVectorDimensionNum = Network.GetInputNum();
		const int32 ActionVectorDimensionNum = Network.GetOutputNum() / 2;

		// Write Config

		const FString TimeStamp = FDateTime::Now().ToFormattedString(TEXT("%Y-%m-%d_%H-%M-%S"));
		const FString TrainerMethod = TEXT("PPO");
		const FString TrainerType = TEXT("Network");

		TSharedRef<FJsonObject> ConfigObject = MakeShared<FJsonObject>();
		ConfigObject->SetStringField(TEXT("TaskName"), *TaskName);
		ConfigObject->SetStringField(TEXT("TrainerMethod"), TrainerMethod);
		ConfigObject->SetStringField(TEXT("TrainerType"), TrainerType);
		ConfigObject->SetStringField(TEXT("TimeStamp"), *TimeStamp);

		ConfigObject->SetNumberField(TEXT("ObservationVectorDimensionNum"), ObservationVectorDimensionNum);
		ConfigObject->SetNumberField(TEXT("ActionVectorDimensionNum"), ActionVectorDimensionNum);
		ConfigObject->SetNumberField(TEXT("NetworkTotalByteNum"), Network.GetTotalByteNum());
		ConfigObject->SetNumberField(TEXT("MaxEpisodeNum"), ReplayBuffer.GetMaxEpisodeNum());
		ConfigObject->SetNumberField(TEXT("MaxStepNum"), ReplayBuffer.GetMaxStepNum());
		ConfigObject->SetNumberField(TEXT("HiddenUnitNum"), Network.GetHiddenNum());
		ConfigObject->SetNumberField(TEXT("LayerNum"), Network.GetLayerNum());
		ConfigObject->SetStringField(TEXT("ActivationFunction"), ActivationFunctionString(Network.ActivationFunction));

		ConfigObject->SetNumberField(TEXT("ActionNoiseMin"), ActionNoiseMin);
		ConfigObject->SetNumberField(TEXT("ActionNoiseMax"), ActionNoiseMax);

		ConfigObject->SetNumberField(TEXT("IterationNum"), Settings.IterationNum);
		ConfigObject->SetNumberField(TEXT("LearningRateActor"), Settings.LearningRateActor);
		ConfigObject->SetNumberField(TEXT("LearningRateCritic"), Settings.LearningRateCritic);
		ConfigObject->SetNumberField(TEXT("LearningRateDecay"), Settings.LearningRateDecay);
		ConfigObject->SetNumberField(TEXT("WeightDecay"), Settings.WeightDecay);
		ConfigObject->SetNumberField(TEXT("InitialActionScale"), Settings.InitialActionScale);
		ConfigObject->SetNumberField(TEXT("BatchSize"), Settings.BatchSize);
		ConfigObject->SetNumberField(TEXT("EpsilonClip"), Settings.EpsilonClip);
		ConfigObject->SetNumberField(TEXT("ActionRegularizationWeight"), Settings.ActionRegularizationWeight);
		ConfigObject->SetNumberField(TEXT("EntropyWeight"), Settings.EntropyWeight);
		ConfigObject->SetNumberField(TEXT("GaeLambda"), Settings.GaeLambda);
		ConfigObject->SetBoolField(TEXT("ClipAdvantages"), Settings.bClipAdvantages);
		ConfigObject->SetBoolField(TEXT("AdvantageNormalization"), Settings.bAdvantageNormalization);
		ConfigObject->SetNumberField(TEXT("TrimEpisodeStartStepNum"), Settings.TrimEpisodeStartStepNum);
		ConfigObject->SetNumberField(TEXT("TrimEpisodeEndStepNum"), Settings.TrimEpisodeEndStepNum);
		ConfigObject->SetNumberField(TEXT("Seed"), Settings.Seed);
		ConfigObject->SetNumberField(TEXT("DiscountFactor"), Settings.DiscountFactor);
		ConfigObject->SetBoolField(TEXT("ReinitializeNetwork"), Settings.bReinitializeNetwork);
		ConfigObject->SetStringField(TEXT("Device"), Trainer::GetDeviceString(Settings.Device));
		ConfigObject->SetBoolField(TEXT("UseTensorBoard"), Settings.bUseTensorboard);

		FString JsonString;
		TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString, 0);
		FJsonSerializer::Serialize(ConfigObject, JsonWriter, true);

		// Allocate buffer to receive network data in

		NetworkBuffer.SetNumUninitialized({ Network.GetTotalByteNum() });

		// Create Socket

		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SocketSubsystem)
		{
			UE_LOG(LogLearning, Error, TEXT("Could not get socket subsystem"));
			OutResponse = ETrainerResponse::Unexpected;
			return;
		}

		bool bIsValid = false;
		TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
		Address->SetIp(IpAddress, bIsValid);
		Address->SetPort(Port);

		if (!bIsValid)
		{
			UE_LOG(LogLearning, Error, TEXT("Invalid Ip Address \"%s\"..."), IpAddress);
			OutResponse = ETrainerResponse::Unexpected;
			return;
		}

		// Connect  to Server

		Socket = FTcpSocketBuilder(TEXT("LearningNetworkPPOTrainerSocket")).AsNonBlocking().Build();
		Socket->Connect(*Address);

		OutResponse = SocketTraining::WaitForConnection(*Socket, Timeout);
		if (OutResponse != ETrainerResponse::Success) { return; }

		// Send Config

		OutResponse = SocketTraining::SendConfig(*Socket, JsonString, Timeout);
		return;
	}

	FSocketPPOTrainer::~FSocketPPOTrainer()
	{
		Terminate();
	}

	ETrainerResponse FSocketPPOTrainer::Wait(const float Timeout)
	{
		return ETrainerResponse::Success;
	}

	void FSocketPPOTrainer::Terminate()
	{
		if (Socket)
		{
			Socket->Close();
			Socket = nullptr;
		}
	}

	ETrainerResponse FSocketPPOTrainer::SendStop(const float Timeout)
	{
		return SocketTraining::SendStop(*Socket, Timeout);
	}

	ETrainerResponse FSocketPPOTrainer::RecvPolicy(
		FNeuralNetwork& OutNetwork,
		const float Timeout,
		FRWLock* NetworkLock,
		const ELogSetting LogSettings)
	{
		return SocketTraining::RecvPolicy(*Socket, OutNetwork, NetworkBuffer, Timeout, NetworkLock, LogSettings);
	}

	ETrainerResponse FSocketPPOTrainer::SendPolicy(
		const FNeuralNetwork& Network,
		const float Timeout,
		FRWLock* NetworkLock,
		const ELogSetting LogSettings)
	{
		return SocketTraining::SendPolicy(*Socket, NetworkBuffer, Network, Timeout, NetworkLock, LogSettings);
	}

	ETrainerResponse FSocketPPOTrainer::SendExperience(
		const FReplayBuffer& ReplayBuffer,
		const float Timeout,
		const ELogSetting LogSettings)
	{
		return SocketTraining::SendExperience(*Socket, ReplayBuffer, Timeout, LogSettings);
	}

	namespace PPOTrainer
	{
		ETrainerResponse Train(
			IPPOTrainer& Trainer,
			FReplayBuffer& ReplayBuffer,
			FEpisodeBuffer& EpisodeBuffer,
			FResetInstanceBuffer& ResetBuffer,
			FNeuralNetwork& Network,
			TLearningArrayView<2, float> ObservationVectorBuffer,
			TLearningArrayView<2, float> ActionVectorBuffer,
			TLearningArrayView<1, float> RewardBuffer,
			TLearningArrayView<1, ECompletionMode> CompletionBuffer,
			const ECompletionMode EpisodeEndCompletionMode,
			const TFunctionRef<void(const FIndexSet Instances)> ResetFunction,
			const TFunctionRef<void(const FIndexSet Instances)> ObservationFunction,
			const TFunctionRef<void(const FIndexSet Instances)> PolicyFunction,
			const TFunctionRef<void(const FIndexSet Instances)> ActionFunction,
			const TFunctionRef<void(const FIndexSet Instances)> UpdateFunction,
			const TFunctionRef<void(const FIndexSet Instances)> RewardFunction,
			const TFunctionRef<void(const FIndexSet Instances)> CompletionFunction,
			const FIndexSet Instances,
			const bool bReinitializeNetwork,
			TAtomic<bool>* bRequestTrainingStopSignal,
			FRWLock* NetworkLock,
			TAtomic<bool>* bNetworkUpdatedSignal,
			const ELogSetting LogSettings)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Learning::PPOTrainer::Train);

			ETrainerResponse Response = ETrainerResponse::Success;

			if (bReinitializeNetwork)
			{
				// Receive initial Policy

				if (LogSettings != ELogSetting::Silent)
				{
					UE_LOG(LogLearning, Display, TEXT("Receiving initial Policy..."));
				}

				Response = Trainer.RecvPolicy(Network, 20.0f, NetworkLock);

				if (Response != ETrainerResponse::Success)
				{
					if (LogSettings != ELogSetting::Silent)
					{
						UE_LOG(LogLearning, Error, TEXT("Error receiving initial policy from trainer: %s. Check log for errors."), Trainer::ResponseString(Response));
					}

					Trainer.Terminate();
					return Response;
				}

				if (bNetworkUpdatedSignal)
				{
					*bNetworkUpdatedSignal = true;
				}
			}
			else
			{
				// Send initial Policy

				if (LogSettings != ELogSetting::Silent)
				{
					UE_LOG(LogLearning, Display, TEXT("Sending initial Policy..."));
				}

				Response = Trainer.SendPolicy(Network, 20.0f, NetworkLock);

				if (Response != ETrainerResponse::Success)
				{
					if (LogSettings != ELogSetting::Silent)
					{
						UE_LOG(LogLearning, Error, TEXT("Error sending initial policy to trainer: %s. Check log for errors."), Trainer::ResponseString(Response));
					}

					Trainer.Terminate();
					return Response;
				}
			}

			// Start Training Loop

			while (true)
			{
				if (bRequestTrainingStopSignal && (*bRequestTrainingStopSignal))
				{
					*bRequestTrainingStopSignal = false;

					if (LogSettings != ELogSetting::Silent)
					{
						UE_LOG(LogLearning, Display, TEXT("Stopping Training..."));
					}

					Response = Trainer.SendStop();

					if (Response != ETrainerResponse::Success)
					{
						if (LogSettings != ELogSetting::Silent)
						{
							UE_LOG(LogLearning, Error, TEXT("Error sending stop signal to trainer: %s. Check log for errors."), Trainer::ResponseString(Response));
						}

						Trainer.Terminate();
						return Response;
					}

					break;
				}
				else
				{
					Experience::GatherExperienceUntilReplayBufferFull(
						ReplayBuffer,
						EpisodeBuffer,
						ResetBuffer,
						ObservationVectorBuffer,
						ActionVectorBuffer,
						RewardBuffer,
						CompletionBuffer,
						EpisodeEndCompletionMode,
						ResetFunction,
						ObservationFunction,
						PolicyFunction,
						ActionFunction,
						UpdateFunction,
						RewardFunction,
						CompletionFunction,
						Instances);

					Response = Trainer.SendExperience(ReplayBuffer, 10.0f);

					if (Response != ETrainerResponse::Success)
					{
						if (LogSettings != ELogSetting::Silent)
						{
							UE_LOG(LogLearning, Error, TEXT("Error sending experience to trainer: %s. Check log for errors."), Trainer::ResponseString(Response));
						}

						Trainer.Terminate();
						return Response;
					}
				}

				Response = Trainer.RecvPolicy(Network, 10.0f, NetworkLock);

				if (Response == ETrainerResponse::Completed)
				{
					if (LogSettings != ELogSetting::Silent)
					{
						UE_LOG(LogLearning, Display, TEXT("Trainer completed training."));
					}
					break;
				}
				else if (Response != ETrainerResponse::Success)
				{
					if (LogSettings != ELogSetting::Silent)
					{
						UE_LOG(LogLearning, Error, TEXT("Error receiving policy from trainer: %s. Check log for errors."), Trainer::ResponseString(Response));
					}
					break;
				}

				if (bNetworkUpdatedSignal)
				{
					*bNetworkUpdatedSignal = true;
				}
			}

			// Allow some time for trainer to shut down gracefully before we kill it...
			
			Response = Trainer.Wait(5.0f);

			if (Response != ETrainerResponse::Success)
			{
				if (LogSettings != ELogSetting::Silent)
				{
					UE_LOG(LogLearning, Error, TEXT("Error waiting for trainer to exit: %s. Check log for errors."), Trainer::ResponseString(Response));
				}
			}

			Trainer.Terminate();

			if (LogSettings != ELogSetting::Silent)
			{
				UE_LOG(LogLearning, Display, TEXT("Training Task Done!"));
			}

			return ETrainerResponse::Success;
		}
	}
}
