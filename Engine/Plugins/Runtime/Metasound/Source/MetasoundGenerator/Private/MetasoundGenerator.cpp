// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundGenerator.h"

#include "AudioParameter.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundGeneratorBuilder.h"
#include "MetasoundGeneratorModuleImpl.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundOutputNode.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundRouter.h"
#include "MetasoundTrace.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertexData.h"
#include "Analysis/MetasoundFrontendAnalyzerFactory.h"
#include "Analysis/MetasoundFrontendAnalyzerRegistry.h"
#include "Analysis/MetasoundFrontendVertexAnalyzer.h"
#include "DSP/FloatArrayMath.h"
#include "Interfaces/MetasoundFrontendSourceInterface.h"
#include "Modules/ModuleManager.h"

#ifndef ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING 
#define ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING !UE_BUILD_SHIPPING
#endif

namespace Metasound
{
	namespace ConsoleVariables
	{
		static bool bEnableAsyncMetaSoundGeneratorBuilder = true;
		static bool bEnableExperimentalOneShotOperatorCache = false;
		static bool bEnableExperimentalOperatorCache = false;
#if ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING
		static bool bEnableMetaSoundGeneratorNonFiniteLogging = false;
		static bool bEnableMetaSoundGeneratorInvalidSampleValueLogging = false;
		static float MetasoundGeneratorSampleValueThreshold = 2.f;
#endif // if ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING
	}

	namespace MetasoundGeneratorPrivate
	{
#if ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING
		void LogInvalidAudioSampleValues(const FString& InMetaSoundName, const TArray<FAudioBufferReadRef>& InAudioBuffers)
		{
			if (ConsoleVariables::bEnableMetaSoundGeneratorNonFiniteLogging || ConsoleVariables::bEnableMetaSoundGeneratorInvalidSampleValueLogging)
			{
				const int32 NumChannels = InAudioBuffers.Num();

				// Check outputs for non finite values if any sample value logging is enabled.
				for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++)
				{
					const FAudioBuffer& Buffer = *InAudioBuffers[ChannelIndex];
					const float* Data = Buffer.GetData();
					const int32 Num = Buffer.Num();

					for (int32 i = 0; i < Num; i++)
					{
						if (!FMath::IsFinite(Data[i]))
						{
							UE_LOG(LogMetaSound, Error, TEXT("Found non-finite sample (%f) in channel %d of MetaSound %s"), Data[i], ChannelIndex, *InMetaSoundName);
							break;
						}
					}

					// Only check threshold if explicitly enabled
					if (ConsoleVariables::bEnableMetaSoundGeneratorInvalidSampleValueLogging)
					{
						const float Threshold = FMath::Abs(ConsoleVariables::MetasoundGeneratorSampleValueThreshold);
						const float MaxAbsValue = Audio::ArrayMaxAbsValue(Buffer);
						if (MaxAbsValue > Threshold)
						{
							UE_LOG(LogMetaSound, Warning, TEXT("Found sample (absolute value: %f) exceeding threshold (%f) in channel %d of MetaSound %s"), MaxAbsValue, Threshold, ChannelIndex, *InMetaSoundName);

						}
					}
				}
			}
		}
#endif // if ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING

#if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
		struct FRenderTimer
		{
			FRenderTimer(const FOperatorSettings& InSettings, double InAnalysisDuration)
			{
				// Use single pole IIR filter to smooth data.
				
				const double AnalysisAudioFrameCount = FMath::Max(1.0, InAnalysisDuration * InSettings.GetSampleRate());
				const double AnalysisRenderBlockCount = FMath::Max(1.0, AnalysisAudioFrameCount / FMath::Max(1, InSettings.GetNumFramesPerBlock()));
				const double DigitalCutoff = 1. / AnalysisRenderBlockCount;

				SmoothingAlpha = 1. - FMath::Exp(-UE_PI * DigitalCutoff);
				SmoothingAlpha = FMath::Clamp(SmoothingAlpha, 0.0, 1.0 - UE_DOUBLE_SMALL_NUMBER);
				SecondsOfAudioProducedPerBlock = static_cast<double>(InSettings.GetNumFramesPerBlock()) / FMath::Max(1., static_cast<double>(InSettings.GetSampleRate()));
			}

			double GetCPUCoreUtilization() const
			{
				return CPUCoreUtilization;
			}

			void UpdateCPUCoreUtilization(double InCPUSecondsToRenderBlock)
			{
				if (InCPUSecondsToRenderBlock > 0.0)
				{
					double NewCPUUtil = InCPUSecondsToRenderBlock / SecondsOfAudioProducedPerBlock;
					if (CPUCoreUtilization >= 0.0)
					{
						CPUCoreUtilization = SmoothingAlpha * NewCPUUtil + (1. - SmoothingAlpha) * CPUCoreUtilization;
					}
					else
					{
						CPUCoreUtilization = NewCPUUtil;
					}
				}
			}

		private:
			double CPUCoreUtilization = -1.0;
			double SmoothingAlpha = 1.0;
			double SecondsOfAudioProducedPerBlock = 0.0;
		};

		struct FBlockRenderScope
		{
			FBlockRenderScope(FRenderTimer& InTimer)
			: Timer(&InTimer)
			{
				StartCycle = FPlatformTime::Cycles64();
			}

			~FBlockRenderScope()
			{
				uint64 EndCycle = FPlatformTime::Cycles64();
				if (EndCycle > StartCycle)
				{
					Timer->UpdateCPUCoreUtilization(FPlatformTime::ToSeconds64(EndCycle - StartCycle));
				}
			}
		private:
			uint64 StartCycle = 0;
			FRenderTimer* Timer = nullptr;
		};

#endif // if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
	}
}

FAutoConsoleVariableRef CVarMetaSoundEnableAsyncGeneratorBuilder(
	TEXT("au.MetaSound.EnableAsyncGeneratorBuilder"),
	Metasound::ConsoleVariables::bEnableAsyncMetaSoundGeneratorBuilder,
	TEXT("Enables async building of FMetaSoundGenerators\n")
	TEXT("Default: true"),
	ECVF_Default);

FAutoConsoleVariableRef CVarMetaSoundEnableExperimentalOneShotOperatorCache(
	TEXT("au.MetaSound.Experimental.EnableOneShotOperatorCache"),
	Metasound::ConsoleVariables::bEnableExperimentalOneShotOperatorCache,
	TEXT("Enables caching of MetaSound operators using the OneShot source interface.\n")
	TEXT("Default: false"),
	ECVF_Default);

FAutoConsoleVariableRef CVarMetaSoundEnableExperimentalOperatorCache(
	TEXT("au.MetaSound.Experimental.EnableOperatorCache"),
	Metasound::ConsoleVariables::bEnableExperimentalOperatorCache,
	TEXT("Enables caching of all MetaSound operators.\n")
	TEXT("Default: false"),
	ECVF_Default);

#if ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING

FAutoConsoleVariableRef CVarMetaSoundEnableGeneratorNonFiniteLogging(
	TEXT("au.MetaSound.EnableGeneratorNonFiniteLogging"),
	Metasound::ConsoleVariables::bEnableMetaSoundGeneratorNonFiniteLogging,
	TEXT("Enables logging of non-finite (NaN/inf) audio samples values produced from a FMetaSoundGenerator\n")
	TEXT("Default: false"),
	ECVF_Default);

FAutoConsoleVariableRef CVarMetaSoundEnableGeneratorInvalidSampleValueLogging(
	TEXT("au.MetaSound.EnableGeneratorInvalidSampleValueLogging"),
	Metasound::ConsoleVariables::bEnableMetaSoundGeneratorInvalidSampleValueLogging,
	TEXT("Enables logging of audio samples values produced from a FMetaSoundGenerator which exceed the absolute sample value threshold\n")
	TEXT("Default: false"),
	ECVF_Default);

FAutoConsoleVariableRef CVarMetaSoundGeneratorSampleValueThrehshold(
	TEXT("au.MetaSound.GeneratorSampleValueThreshold"),
	Metasound::ConsoleVariables::MetasoundGeneratorSampleValueThreshold,
	TEXT("If invalid sample value logging is enabled, this sets the maximum abs value threshold for logging samples\n")
	TEXT("Default: 2.0"),
	ECVF_Default);

#endif // if !UE_BUILD_SHIPPING

namespace Metasound
{
	namespace MetasoundGeneratorPrivate
	{
		bool HasOneShotInterface(const FVertexInterface& Interface)
		{
			return Interface.GetOutputInterface().Contains(Frontend::SourceOneShotInterface::Outputs::OnFinished);
		}
	}

	void FMetasoundGeneratorInitParams::Release()
	{
		Graph.Reset();
		Environment = {};
		MetaSoundName = {};
		AudioOutputNames = {};
		DefaultParameters = {};
		DataChannel.Reset();
		DynamicOperatorTransactor.Reset();
	}

	FMetasoundGenerator::FMetasoundGenerator(FMetasoundGeneratorInitParams&& InParams)
		: MetasoundName(InParams.MetaSoundName)
		, OperatorSettings(InParams.OperatorSettings)
		, bIsFinishTriggered(false)
		, bIsFinished(false)
		, NumChannels(0)
		, NumFramesPerExecute(0)
		, NumSamplesPerExecute(0)
		, OnPlayTriggerRef(FTriggerWriteRef::CreateNew(InParams.OperatorSettings))
		, OnFinishedTriggerRef(FTriggerWriteRef::CreateNew(InParams.OperatorSettings))
		, bPendingGraphTrigger(true)
		, bIsNewGraphPending(false)
		, bIsWaitingForFirstGraph(true)
		, ParameterQueue(MoveTemp(InParams.DataChannel))
#if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
		, RenderTimer(MakeUnique<MetasoundGeneratorPrivate::FRenderTimer>(InParams.OperatorSettings, 1. /* AnalysisPeriod */))
#endif // if ENABLE_METASOUND_GENERATOR_RENDER_TIMING

	{
		NumChannels = InParams.AudioOutputNames.Num();
		NumFramesPerExecute = InParams.OperatorSettings.GetNumFramesPerBlock();
		NumSamplesPerExecute = NumChannels * NumFramesPerExecute;

		const bool bIsDynamic = InParams.DynamicOperatorTransactor.IsValid();

		// Create the routing for parameter packs
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		ParameterPackSendAddress = UMetasoundParameterPack::CreateSendAddressFromEnvironment(InParams.Environment);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		ParameterPackReceiver = FDataTransmissionCenter::Get().RegisterNewReceiver<FMetasoundParameterStorageWrapper>(ParameterPackSendAddress, FReceiverInitParams{ InParams.OperatorSettings });

		// attempt to use operator cache instead of building a new operator.
		bool bDidUseCachedOperator = false;
		const bool bIsOperatorCacheEnabled = ConsoleVariables::bEnableExperimentalOneShotOperatorCache || ConsoleVariables::bEnableExperimentalOperatorCache;
		// Dynamic operators cannot use the operator cache because they can change their internal structure. 
		// The operator cache assumes that the operator is unchanged from it's original structure. 
		if (bIsOperatorCacheEnabled && !bIsDynamic)
		{
			bUseOperatorCache = ConsoleVariables::bEnableExperimentalOperatorCache || MetasoundGeneratorPrivate::HasOneShotInterface(InParams.Graph->GetVertexInterface());

			if (bUseOperatorCache)
			{
				bDidUseCachedOperator = TryUseCachedOperator(InParams, true /* bTriggerGenerator */);
			}
		}

		if (!bDidUseCachedOperator)
		{
			BuilderTask = MakeUnique<FAsyncTask<FAsyncMetaSoundBuilder>>(this, MoveTemp(InParams), true /* bTriggerGenerator */);
			
			if (!InParams.bBuildSynchronous && Metasound::ConsoleVariables::bEnableAsyncMetaSoundGeneratorBuilder)
			{
				// Build operator asynchronously
				BuilderTask->StartBackgroundTask(GBackgroundPriorityThreadPool);
			}
			else
			{
				// Build operator synchronously
				BuilderTask->StartSynchronousTask();
				BuilderTask = nullptr;
				UpdateGraphIfPending();
			}
		}
	}

	FMetasoundGenerator::~FMetasoundGenerator()
	{
		if (BuilderTask.IsValid())
		{
			BuilderTask->EnsureCompletion();
			BuilderTask = nullptr;
		}

		if (bUseOperatorCache)
		{
			ReleaseOperatorToCache();
		}

		// Remove routing for parameter packs
		ParameterPackReceiver.Reset();
		FDataTransmissionCenter::Get().UnregisterDataChannelIfUnconnected(ParameterPackSendAddress);
	}

	bool FMetasoundGenerator::TryUseCachedOperator(FMetasoundGeneratorInitParams& InInitParams, bool bInTriggerGraph)
	{
		using namespace MetasoundGeneratorPrivate;
		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetasoundGenerator::TryUseCachedOperator);

		FMetasoundGeneratorModule& Module = FModuleManager::GetModuleChecked<FMetasoundGeneratorModule>("MetasoundGenerator");
		TSharedPtr<FOperatorCache> OperatorCache = Module.GetOperatorCache();
		if (OperatorCache.IsValid())
		{
			// See if the cache has an operator with matching OperatorID
			OperatorID = InInitParams.Graph->GetInstanceID();
			FOperatorAndInputs GraphOperatorAndInputs = OperatorCache->ClaimCachedOperator(OperatorID);
			if (GraphOperatorAndInputs.Operator.IsValid())
			{
				UE_LOG(LogMetasoundGenerator, VeryVerbose, TEXT("Using cached operator %s for MetaSound %s"), *LexToString(OperatorID), *InInitParams.MetaSoundName); 

				// Apply and default inputs to the operator.
				GeneratorBuilder::ApplyAudioParameters(OperatorSettings, MoveTemp(InInitParams.DefaultParameters), GraphOperatorAndInputs.Inputs);

				// Reset operator internal state before playing it.
				if (IOperator::FResetFunction Reset = GraphOperatorAndInputs.Operator->GetResetFunction())
				{
					IOperator::FResetParams ResetParams {OperatorSettings, InInitParams.Environment};
					Reset(GraphOperatorAndInputs.Operator.Get(), ResetParams);
				}
				
				// Create data needed for MetaSound Generator
				FMetasoundGeneratorData Data = GeneratorBuilder::BuildGeneratorData(InInitParams, MoveTemp(GraphOperatorAndInputs), nullptr);

				SetGraph(MakeUnique<FMetasoundGeneratorData>(MoveTemp(Data)), bInTriggerGraph);
				
				return true;
			}
		}

		return false;
	}

	void FMetasoundGenerator::ReleaseOperatorToCache()
	{
		using namespace MetasoundGeneratorPrivate;
		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FMetasoundGenerator::ReleaseOperatorToCache);

		if (OperatorID.IsValid())
		{
			FMetasoundGeneratorModule& Module = FModuleManager::GetModuleChecked<FMetasoundGeneratorModule>("MetasoundGenerator");
			TSharedPtr<FOperatorCache> OperatorCache = Module.GetOperatorCache();
			if (OperatorCache.IsValid())
			{
				TUniquePtr<IOperator> GraphOperator = RootExecuter.ReleaseOperator();

				if (GraphOperator.IsValid())
				{
					// Release graph operator and input data to the cache
					UE_LOG(LogMetasoundGenerator, VeryVerbose, TEXT("Caching operator %s"), *LexToString(OperatorID));
					OperatorCache->AddOperatorToCache(OperatorID, MoveTemp(GraphOperator), MoveTemp(VertexInterfaceData.GetInputs()));
				}
			}
		}

		// Clear out any internal references to graph data
		ClearGraph();
	}

	FDelegateHandle FMetasoundGenerator::AddGraphSetCallback(FOnSetGraph::FDelegate&& Delegate)
	{
		FScopeLock SetPendingGraphLock(&PendingGraphMutex);
		// If we already have a graph give the delegate an initial call
		if (!bIsNewGraphPending && !bIsWaitingForFirstGraph)
		{
			Delegate.ExecuteIfBound();
		}
		return OnSetGraph.Add(Delegate);
	}

	bool FMetasoundGenerator::RemoveGraphSetCallback(const FDelegateHandle& Handle)
	{
		return OnSetGraph.Remove(Handle);
	}

	void FMetasoundGenerator::SetPendingGraph(MetasoundGeneratorPrivate::FMetasoundGeneratorData&& InData, bool bTriggerGraph)
	{
		using namespace MetasoundGeneratorPrivate;

		FScopeLock SetPendingGraphLock(&PendingGraphMutex);
		{
			PendingGraphData = MakeUnique<FMetasoundGeneratorData>(MoveTemp(InData));
			bPendingGraphTrigger = bTriggerGraph;
			bIsNewGraphPending = true;
		}
	}

	void FMetasoundGenerator::SetPendingGraphBuildFailed()
	{
		FScopeLock SetPendingGraphLock(&PendingGraphMutex);
		{
			PendingGraphData.Reset();
			bPendingGraphTrigger = false;
			bIsNewGraphPending = true;
		}
	}

	bool FMetasoundGenerator::UpdateGraphIfPending()
	{
		FScopeLock GraphLock(&PendingGraphMutex);
		if (bIsNewGraphPending)
		{
			SetGraph(MoveTemp(PendingGraphData), bPendingGraphTrigger);
			bIsNewGraphPending = false;
			return true;
		}

		return false;
	}

	void FMetasoundGenerator::SetGraph(TUniquePtr<MetasoundGeneratorPrivate::FMetasoundGeneratorData>&& InData, bool bTriggerGraph)
	{
		if (!InData.IsValid())
		{
			return;
		}

		InterleavedAudioBuffer.Reset();

		// Copy off all vertex interface data
		VertexInterfaceData = MoveTemp(InData->VertexInterfaceData);

		GraphOutputAudio.Reset();
		if (InData->OutputBuffers.Num() == NumChannels)
		{
			if (InData->OutputBuffers.Num() > 0)
			{
				GraphOutputAudio.Append(InData->OutputBuffers.GetData(), InData->OutputBuffers.Num());
			}
		}
		else
		{
			int32 FoundNumChannels = InData->OutputBuffers.Num();

			UE_LOG(LogMetaSound, Warning, TEXT("Metasound generator expected %d number of channels, found %d"), NumChannels, FoundNumChannels);

			int32 NumChannelsToCopy = FMath::Min(FoundNumChannels, NumChannels);
			int32 NumChannelsToCreate = NumChannels - NumChannelsToCopy;

			if (NumChannelsToCopy > 0)
			{
				GraphOutputAudio.Append(InData->OutputBuffers.GetData(), NumChannelsToCopy);
			}
			for (int32 i = 0; i < NumChannelsToCreate; i++)
			{
				GraphOutputAudio.Add(TDataReadReference<FAudioBuffer>::CreateNew(InData->OperatorSettings));
			}
		}

		OnPlayTriggerRef = InData->TriggerOnPlayRef;
		OnFinishedTriggerRef = InData->TriggerOnFinishRef;

		// The graph operator and graph audio output contain all the values needed
		// by the sound generator.
		RootExecuter.SetOperator(MoveTemp(InData->GraphOperator));


		// Query the graph output to get the number of output audio channels.
		// Multichannel version:
		check(NumChannels == GraphOutputAudio.Num());

		if (NumSamplesPerExecute > 0)
		{
			// Preallocate interleaved buffer as it is necessary for any audio generation calls.
			InterleavedAudioBuffer.AddUninitialized(NumSamplesPerExecute);
		}

		GraphAnalyzer = MoveTemp(InData->GraphAnalyzer);

		ParameterSetters = MoveTemp(InData->ParameterSetters);
		ParameterPackSetters = MoveTemp(InData->ParameterPackSetters);

		if (bTriggerGraph)
		{
			OnPlayTriggerRef->TriggerFrame(0);
		}

		bIsWaitingForFirstGraph = false;
		OnSetGraph.Broadcast();
	}

	void FMetasoundGenerator::ClearGraph()
	{
		RootExecuter.ReleaseOperator();
		VertexInterfaceData = FVertexInterfaceData();
		GraphOutputAudio.Reset();
		OnFinishedTriggerRef = OnPlayTriggerRef = TDataWriteReference<FTrigger>::CreateNew(OperatorSettings);
		GraphAnalyzer.Reset();
		ParameterSetters.Reset();
		ParameterPackSetters.Reset();
	}

	void FMetasoundGenerator::QueueParameterPack(TSharedPtr<FMetasoundParameterPackStorage> ParameterPack)
	{
		ParameterPackQueue.Enqueue(ParameterPack);
	}

	int32 FMetasoundGenerator::GetNumChannels() const
	{
		return NumChannels;
	}

	bool AnalyzerAddressesReferToSameGeneratorOutput(const Frontend::FAnalyzerAddress& Lhs, const Frontend::FAnalyzerAddress& Rhs)
	{
		return Lhs.OutputName == Rhs.OutputName && Lhs.AnalyzerName == Rhs.AnalyzerName;
	}
	
	void FMetasoundGenerator::AddOutputVertexAnalyzer(const Frontend::FAnalyzerAddress& AnalyzerAddress)
	{
		OutputAnalyzerModificationQueue.Enqueue([this, AnalyzerAddress]
		{
			if (OutputAnalyzers.ContainsByPredicate([AnalyzerAddress](const TUniquePtr<Frontend::IVertexAnalyzer>& Analyzer)
			{
				return AnalyzerAddressesReferToSameGeneratorOutput(AnalyzerAddress, Analyzer->GetAnalyzerAddress());
			}))
			{
				return;
			}
			
			const FAnyDataReference* OutputReference = VertexInterfaceData.GetOutputs().FindDataReference(AnalyzerAddress.OutputName);
			
			if (nullptr == OutputReference)
			{
				return;
			}
			
			const Frontend::IVertexAnalyzerFactory* Factory = Frontend::IVertexAnalyzerRegistry::Get().FindAnalyzerFactory(AnalyzerAddress.AnalyzerName);
			
			if (nullptr == Factory)
			{
				return;
			}
			
			TUniquePtr<Frontend::IVertexAnalyzer> Analyzer = Factory->CreateAnalyzer({ AnalyzerAddress, OperatorSettings, *OutputReference});
			
			if (nullptr == Analyzer)
			{
				return;
			}

			if (!Analyzer->OnOutputDataChanged.IsBound())
			{
				Analyzer->OnOutputDataChanged.BindLambda(
					[this, AnalyzerAddress](const FName AnalyzerOutputName, TSharedPtr<IOutputStorage> OutputData)
					{
						OnOutputChanged.Broadcast(
							AnalyzerAddress.AnalyzerName,
							AnalyzerAddress.OutputName,
							AnalyzerOutputName,
							OutputData);
					});
			}
			
			OutputAnalyzers.Emplace(MoveTemp(Analyzer));
		});
	}

	void FMetasoundGenerator::RemoveOutputVertexAnalyzer(const Frontend::FAnalyzerAddress& AnalyzerAddress)
	{
		OutputAnalyzerModificationQueue.Enqueue([this, AnalyzerAddress]
		{
			OutputAnalyzers.RemoveAll([AnalyzerAddress](const TUniquePtr<Frontend::IVertexAnalyzer>& Analyzer)
			{
				return AnalyzerAddressesReferToSameGeneratorOutput(AnalyzerAddress, Analyzer->GetAnalyzerAddress());
			});
		});
	}

	int32 FMetasoundGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamplesRemaining)
	{
		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("MetasoundGenerator::OnGenerateAudio %s"), *MetasoundName));

		// Defer finishing the metasound generator one block
		if (bIsFinishTriggered)
		{
			bIsFinished = true;
		}

		if (NumSamplesRemaining <= 0)
		{
			return 0;
		}

		UpdateGraphIfPending();

		// Output silent audio if we're still building a graph
		if (bIsWaitingForFirstGraph)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("MetasoundGenerator::OnGenerateAudio::MissedRenderDeadline %s"), *MetasoundName));
			FMemory::Memset(OutAudio, 0, sizeof(float)* NumSamplesRemaining);
			return NumSamplesRemaining;
		}

		// If no longer pending and executer is no-op, kill the MetaSound.
		// Covers case where there was an error when building, resulting in
		// Executer operator being assigned to NoOp.
		else if (RootExecuter.IsNoOp() || NumSamplesPerExecute < 1)
		{
			bIsFinished = true;
			FMemory::Memset(OutAudio, 0, sizeof(float) * NumSamplesRemaining);
			return NumSamplesRemaining;
		}

		// Modify the output analyzers as needed
		{
			while (TOptional<TUniqueFunction<void()>> ModFn = OutputAnalyzerModificationQueue.Dequeue())
			{
				(*ModFn)();
			}
		}

		// If we have any audio left in the internal overflow buffer from 
		// previous calls, write that to the output before generating more audio.
		int32 NumSamplesWritten = FillWithBuffer(OverflowBuffer, OutAudio, NumSamplesRemaining);

		if (NumSamplesWritten > 0)
		{
			NumSamplesRemaining -= NumSamplesWritten;
			OverflowBuffer.RemoveAtSwap(0 /* Index */, NumSamplesWritten /* Count */, false /* bAllowShrinking */);
		}

		while (NumSamplesRemaining > 0)
		{
			ApplyPendingUpdatesToInputs();

			{
#if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
				// Time how long the root executer takes.
				MetasoundGeneratorPrivate::FBlockRenderScope BlockRenderScope(*RenderTimer);
#endif // if ENABLE_METASOUND_GENERATOR_RENDER_TIMING

				// Call metasound graph operator.
				RootExecuter.Execute();
			}

			if (GraphAnalyzer.IsValid())
			{
				GraphAnalyzer->Execute();
			}

			// Execute the output analyzers
			for (const TUniquePtr<Frontend::IVertexAnalyzer>& Analyzer : OutputAnalyzers)
			{
				Analyzer->Execute();
			}

			// Check if generated finished during this execute call
			if (*OnFinishedTriggerRef)
			{
				FinishSample = ((*OnFinishedTriggerRef)[0] * NumChannels);
			}

			// Interleave audio because ISoundGenerator interface expects interleaved audio.
			InterleaveGeneratedAudio();


			// Add audio generated during graph execution to the output buffer.
			int32 ThisLoopNumSamplesWritten = FillWithBuffer(InterleavedAudioBuffer, &OutAudio[NumSamplesWritten], NumSamplesRemaining);

			NumSamplesRemaining -= ThisLoopNumSamplesWritten;
			NumSamplesWritten += ThisLoopNumSamplesWritten;

			// If not all the samples were written, then we have to save the 
			// additional samples to the overflow buffer.
			if (ThisLoopNumSamplesWritten < InterleavedAudioBuffer.Num())
			{
				int32 OverflowCount = InterleavedAudioBuffer.Num() - ThisLoopNumSamplesWritten;

				OverflowBuffer.Reset();
				OverflowBuffer.AddUninitialized(OverflowCount);

				FMemory::Memcpy(OverflowBuffer.GetData(), &InterleavedAudioBuffer.GetData()[ThisLoopNumSamplesWritten], OverflowCount * sizeof(float));
			}

			RootExecuter.PostExecute();
		}

		return NumSamplesWritten;
	}

	int32 FMetasoundGenerator::GetDesiredNumSamplesToRenderPerCallback() const
	{
		return NumFramesPerExecute * NumChannels;
	}

	bool FMetasoundGenerator::IsFinished() const
	{
		return bIsFinished;
	}

#if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
	double FMetasoundGenerator::GetCPUCoreUtilization() const
	{
		return RenderTimer->GetCPUCoreUtilization();
	}
#endif // if ENABLE_METASOUND_GENERATOR_RENDER_TIMING

	int32 FMetasoundGenerator::FillWithBuffer(const Audio::FAlignedFloatBuffer& InBuffer, float* OutAudio, int32 MaxNumOutputSamples)
	{
		int32 InNum = InBuffer.Num();

		if (InNum > 0)
		{
			int32 NumSamplesToCopy = FMath::Min(InNum, MaxNumOutputSamples);
			FMemory::Memcpy(OutAudio, InBuffer.GetData(), NumSamplesToCopy * sizeof(float));

			if (FinishSample != INDEX_NONE)
			{
				FinishSample -= NumSamplesToCopy;
				if (FinishSample <= 0)
				{
					bIsFinishTriggered = true;
					FinishSample = INDEX_NONE;
				}
			}

			return NumSamplesToCopy;
		}

		return 0;
	}

	void FMetasoundGenerator::InterleaveGeneratedAudio()
	{
		// Prepare output buffer
		InterleavedAudioBuffer.Reset();

		if (NumSamplesPerExecute > 0)
		{
			InterleavedAudioBuffer.AddUninitialized(NumSamplesPerExecute);
		}

#if ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING
		MetasoundGeneratorPrivate::LogInvalidAudioSampleValues(MetasoundName, GraphOutputAudio);
#endif // if ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING

		// Iterate over channels
		for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++)
		{
			const FAudioBuffer& InputBuffer = *GraphOutputAudio[ChannelIndex];

			const float* InputPtr = InputBuffer.GetData();
			float* OutputPtr = &InterleavedAudioBuffer.GetData()[ChannelIndex];

			// Assign values to output for single channel.
			for (int32 FrameIndex = 0; FrameIndex < NumFramesPerExecute; FrameIndex++)
			{
				*OutputPtr = InputPtr[FrameIndex];
				OutputPtr += NumChannels;
			}
		}
		// TODO: memcpy for single channel. 
	}

	void FMetasoundGenerator::ApplyPendingUpdatesToInputs()
	{
		using namespace Frontend;
		using namespace MetasoundGeneratorPrivate;

		auto ProcessPack = [&](FMetasoundParameterPackStorage* Pack)
			{
				for (auto Walker = Pack->begin(); Walker != Pack->end(); ++Walker)
				{
					if (const FParameterPackSetter* ParameterPackSetter = ParameterPackSetters.Find(Walker->Name))
					{
						if (ParameterPackSetter->DataType == Walker->TypeName)
						{
							ParameterPackSetter->SetParameterWithPayload(Walker->GetPayload());
						}
					}
				}
			};

		// Handle parameters from the FMetasoundParameterTransmitter
		if (ParameterQueue.IsValid())
		{
			TOptional<FMetaSoundParameterTransmitter::FParameter> Parameter;
			while ((Parameter = ParameterQueue->Dequeue()))
			{
				if (FParameterSetter* Setter = ParameterSetters.Find(Parameter->Name))
				{
					Setter->Assign(OperatorSettings, Parameter->Value, Setter->DataReference);
				}
			}
		}

		// Handle parameter packs that have come from the IAudioParameterInterface system...
		if (ParameterPackReceiver.IsValid())
		{
			FMetasoundParameterStorageWrapper Pack;
			while (ParameterPackReceiver->CanPop())
			{
				ParameterPackReceiver->Pop(Pack);
				ProcessPack(Pack.Get().Get());
			}
		}

		// Handle parameter packs that came from QueueParameterPack...
		TSharedPtr<FMetasoundParameterPackStorage> QueuedParameterPack;
		while (ParameterPackQueue.Dequeue(QueuedParameterPack))
		{
			ProcessPack(QueuedParameterPack.Get());
		}
	}
} 
