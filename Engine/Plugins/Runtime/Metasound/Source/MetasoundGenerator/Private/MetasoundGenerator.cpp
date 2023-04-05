// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundGenerator.h"

#include "AudioParameter.h"
#include "MetasoundFrontendDataTypeRegistry.h"
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

#ifndef ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING 
#define ENABLE_METASOUNDGENERATOR_INVALID_SAMPLE_VALUE_LOGGING !UE_BUILD_SHIPPING
#endif

namespace Metasound
{
	namespace ConsoleVariables
	{
		static bool bEnableAsyncMetaSoundGeneratorBuilder = true;
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
	void FMetasoundGeneratorInitParams::Release()
	{
		Graph.Reset();
		Environment = {};
		MetaSoundName = {};
		AudioOutputNames = {};
		DefaultParameters = {};
	}

	FAsyncMetaSoundBuilder::FAsyncMetaSoundBuilder(FMetasoundGenerator* InGenerator, FMetasoundGeneratorInitParams&& InInitParams, bool bInTriggerGenerator)
		: Generator(InGenerator)
		, InitParams(MoveTemp(InInitParams))
		, bTriggerGenerator(bInTriggerGenerator)
	{
	}

	void FAsyncMetaSoundBuilder::DoWork()
	{
		using namespace Audio;
		using namespace Frontend;
		using namespace MetasoundGeneratorPrivate;

		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("AsyncMetaSoundBuilder::DoWork %s"), *InitParams.MetaSoundName));

		FBuildResults BuildResults;

		// Create an instance of the new graph
		FGraphOperatorAndInputs GraphOperatorAndInputs = BuildGraphOperator(MoveTemp(InitParams.DefaultParameters), BuildResults);
		LogBuildErrors(BuildResults);

		if (GraphOperatorAndInputs.Operator.IsValid())
		{
			// Create graph analyzer
			TUniquePtr<FGraphAnalyzer> GraphAnalyzer = BuildGraphAnalyzer(MoveTemp(BuildResults.InternalDataReferences));
	
			// Collect data for generator
			FMetasoundGeneratorData GeneratorData = BuildGeneratorData(InitParams, MoveTemp(GraphOperatorAndInputs), MoveTemp(GraphAnalyzer));

			Generator->SetPendingGraph(MoveTemp(GeneratorData), bTriggerGenerator);
		}
		else 
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to build Metasound operator from graph in MetasoundSource [%s]"), *InitParams.MetaSoundName);
			// Set null generator data to inform that generator failed to build. 
			// Otherwise, generator will continually wait for a new generator.
			Generator->SetPendingGraphBuildFailed();
		}

		InitParams.Release();
	}

	MetasoundGeneratorPrivate::FMetasoundGeneratorData FAsyncMetaSoundBuilder::BuildGeneratorData(const FMetasoundGeneratorInitParams& InInitParams, FAsyncMetaSoundBuilder::FGraphOperatorAndInputs&& InGraphOperatorAndInputs, TUniquePtr<Frontend::FGraphAnalyzer> InAnalyzer) const
	{
		using namespace Audio;
		using namespace Frontend;
		using namespace MetasoundGeneratorPrivate;

		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("AsyncMetaSoundBuilder::BuildGeneratorData"));

		checkf(InGraphOperatorAndInputs.Operator.IsValid(), TEXT("Graph operator must be a valid object"));

		// Gather relevant input and output references
		FVertexInterfaceData VertexData(InInitParams.Graph->GetVertexInterface());
		InGraphOperatorAndInputs.Operator->Bind(VertexData);

		// Replace input data with writable inputs
		VertexData.GetInputs() = InGraphOperatorAndInputs.Inputs;

		// Get inputs
		FTriggerWriteRef PlayTrigger = VertexData.GetInputs().GetOrConstructDataWriteReference<FTrigger>(SourceInterface::Inputs::OnPlay, InInitParams.OperatorSettings, false);

		// Get outputs
		TArray<FAudioBufferReadRef> OutputBuffers = FindOutputAudioBuffers(VertexData);
		FTriggerReadRef FinishTrigger = TDataReadReferenceFactory<FTrigger>::CreateExplicitArgs(InInitParams.OperatorSettings, false);

		if (InInitParams.Graph->GetVertexInterface().GetOutputInterface().Contains(SourceOneShotInterface::Outputs::OnFinished))
		{
			// Only attempt to retrieve the on finished trigger if it exists.
			// Attempting to retrieve a data reference from a non-existent vertex 
			// will log an error. 
			FinishTrigger = VertexData.GetOutputs().GetOrConstructDataReadReference<FTrigger>(SourceOneShotInterface::Outputs::OnFinished, InitParams.OperatorSettings, false);
		}

		// Create the parameter setter map so parameter packs can be cracked
		// open and distributed as appropriate...
		const IDataTypeRegistry& DataTypeRegistry = IDataTypeRegistry::Get();
		FParameterSetterSortedMap ParameterSetters;
		TMap<FName, FParameterPackSetter> ParameterPackSetters;
		FInputVertexInterfaceData& GraphInputs = VertexData.GetInputs();
		for (const MetasoundVertexDataPrivate::TBinding<FInputDataVertex>& Binding : GraphInputs)
		{
			// Only assign inputs that are writable. 
			if (EDataReferenceAccessType::Write == Binding.GetAccessType())
			{
				if (const FAnyDataReference* DataRef = Binding.GetDataReference())
				{
					const FInputDataVertex& InputVertex = Binding.GetVertex();
					const Frontend::IParameterAssignmentFunction& PackSetter = DataTypeRegistry.GetRawAssignmentFunction(InputVertex.DataTypeName);
					if (PackSetter)
					{
						FParameterPackSetter ParameterPackSetter(InputVertex.DataTypeName, DataRef->GetRaw(), PackSetter);
						ParameterPackSetters.Add(InputVertex.VertexName, ParameterPackSetter);
					}

					Frontend::FLiteralAssignmentFunction LiteralSetter = DataTypeRegistry.GetLiteralAssignmentFunction(InputVertex.DataTypeName);
					if (LiteralSetter)
					{
						ParameterSetters.Add(InputVertex.VertexName, FParameterSetter{LiteralSetter, *DataRef});
					}
				}
			}
		}

		// Set data needed for graph
		return FMetasoundGeneratorData 
		{
			InInitParams.OperatorSettings,
			MoveTemp(InGraphOperatorAndInputs.Operator),
			MoveTemp(VertexData),
			MoveTemp(ParameterSetters),
			MoveTemp(ParameterPackSetters),
			MoveTemp(InAnalyzer),
			MoveTemp(OutputBuffers),
			MoveTemp(PlayTrigger),
			MoveTemp(FinishTrigger),
		};
	}

	FAsyncMetaSoundBuilder::FGraphOperatorAndInputs FAsyncMetaSoundBuilder::BuildGraphOperator(TArray<FAudioParameter>&& InParameters, FBuildResults& OutBuildResults) const
	{
		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("AsyncMetaSoundBuilder::BuildGraphOperator"));
		using namespace Frontend;

		// Choose which type of data reference access to create depending upon the access of the vertex.
		auto VertexAccessTypeToDataReferenceAccessType = [](EVertexAccessType InVertexAccessType) -> EDataReferenceAccessType
		{
			switch(InVertexAccessType)
			{
				case EVertexAccessType::Value:
					return EDataReferenceAccessType::Value;

				case EVertexAccessType::Reference:
				default:
					return EDataReferenceAccessType::Write;
			}
		};

		const FInputVertexInterface& InputInterface = InitParams.Graph->GetVertexInterface().GetInputInterface();
		FInputVertexInterfaceData InputData(InputInterface);

		// Set input data based on the input parameters and the input interface
		IDataTypeRegistry& DataRegistry = IDataTypeRegistry::Get();
		for (FAudioParameter& Parameter : InParameters)
		{
			const FName ParamName = Parameter.ParamName;
			if (const FInputDataVertex* InputVertex = InputInterface.Find(ParamName))
			{
				FLiteral Literal = Frontend::ConvertParameterToLiteral(MoveTemp(Parameter));

				TOptional<FAnyDataReference> DataReference = DataRegistry.CreateDataReference(InputVertex->DataTypeName, VertexAccessTypeToDataReferenceAccessType(InputVertex->AccessType), Literal, InitParams.OperatorSettings);

				if (DataReference)
				{
					InputData.BindVertex(ParamName, *DataReference);
				}
				else if(MetaSoundParameterEnableWarningOnIgnoredParameterCVar)
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Failed to create initial input data reference from parameter %s of type %s on graph in MetaSoundSource [%s]"), *ParamName.ToString(), *InputVertex->DataTypeName.ToString(), *InitParams.MetaSoundName);
				}
			}
			else if(MetaSoundParameterEnableWarningOnIgnoredParameterCVar)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to set initial input parameter %s on graph in MetaSoundSource [%s]"), *ParamName.ToString(), *InitParams.MetaSoundName);
			}
		}

		// Set any remaining inputs to their default values.
		for (const MetasoundVertexDataPrivate::TBinding<FInputDataVertex>& Binding : InputData)
		{
			// Only create data reference if something does not already exist. 
			if (!Binding.IsBound())
			{
				const FInputDataVertex& InputVertex = Binding.GetVertex();
				EDataReferenceAccessType AccessType = VertexAccessTypeToDataReferenceAccessType(InputVertex.AccessType);
				TOptional<FAnyDataReference> DataReference = DataRegistry.CreateDataReference(InputVertex.DataTypeName, VertexAccessTypeToDataReferenceAccessType(InputVertex.AccessType), InputVertex.GetDefaultLiteral(), InitParams.OperatorSettings);

				if (DataReference)
				{
					InputData.BindVertex(InputVertex.VertexName, *DataReference);
				}
			}
		}

		// Reset as elements in array have been moved.
		InParameters.Reset();

		// Create an instance of the new graph
		FBuildGraphOperatorParams BuildParams { *InitParams.Graph, InitParams.OperatorSettings, InputData, InitParams.Environment };
		FGraphOperatorAndInputs OpAndInputs;
		OpAndInputs.Operator = FOperatorBuilder(InitParams.BuilderSettings).BuildGraphOperator(BuildParams, OutBuildResults);
		OpAndInputs.Inputs = InputData;

		return OpAndInputs;
	}

	TUniquePtr<Frontend::FGraphAnalyzer> FAsyncMetaSoundBuilder::BuildGraphAnalyzer(TMap<FGuid, FDataReferenceCollection>&& InInternalDataReferences) const
	{
		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("AsyncMetaSoundBuilder::BuildGraphAnalyzer"));
		using namespace Frontend;

		if (InitParams.BuilderSettings.bPopulateInternalDataReferences)
		{
			const uint64 InstanceID = InitParams.Environment.GetValue<uint64>(SourceInterface::Environment::TransmitterID);
			return MakeUnique<FGraphAnalyzer>(InitParams.OperatorSettings, InstanceID, MoveTemp(InInternalDataReferences));
		}
		return nullptr;
	}

	void FAsyncMetaSoundBuilder::LogBuildErrors(const FBuildResults& InBuildResults) const
	{
		// Log build errors
		for (const IOperatorBuilder::FBuildErrorPtr& Error : InBuildResults.Errors)
		{
			if (Error.IsValid())
			{
				UE_LOG(LogMetaSound, Warning, TEXT("MetasoundSource [%s] build error [%s] \"%s\""), *InitParams.MetaSoundName, *(Error->GetErrorType().ToString()), *(Error->GetErrorDescription().ToString()));
			}
		}
	}

	TArray<FAudioBufferReadRef> FAsyncMetaSoundBuilder::FindOutputAudioBuffers(const FVertexInterfaceData& InVertexData) const
	{
		TArray<FAudioBufferReadRef> OutputBuffers;

		const FOutputVertexInterfaceData& OutputVertexData = InVertexData.GetOutputs();

		// Get output audio buffers.
		for (const FVertexName& AudioOutputName : InitParams.AudioOutputNames)
		{
			if (!OutputVertexData.IsVertexBound(AudioOutputName))
			{
				UE_LOG(LogMetaSound, Warning, TEXT("MetasoundSource [%s] does not contain audio output [%s] in output"), *InitParams.MetaSoundName, *AudioOutputName.ToString());
			}
			OutputBuffers.Add(OutputVertexData.GetOrConstructDataReadReference<FAudioBuffer>(AudioOutputName, InitParams.OperatorSettings));
		}

		return OutputBuffers;
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

		// Create the routing for parameter packs
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		ParameterPackSendAddress = UMetasoundParameterPack::CreateSendAddressFromEnvironment(InParams.Environment);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		ParameterPackReceiver = FDataTransmissionCenter::Get().RegisterNewReceiver<FMetasoundParameterStorageWrapper>(ParameterPackSendAddress, FReceiverInitParams{ InParams.OperatorSettings });

		BuilderTask = MakeUnique<FBuilderTask>(this, MoveTemp(InParams), true /* bTriggerGenerator */);
		
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
			bIsWaitingForFirstGraph = false;
		}
	}

	FMetasoundGenerator::~FMetasoundGenerator()
	{
		if (BuilderTask.IsValid())
		{
			BuilderTask->EnsureCompletion();
			BuilderTask = nullptr;
		}

		// Remove routing for parameter packs
		ParameterPackReceiver.Reset();
		FDataTransmissionCenter::Get().UnregisterDataChannelIfUnconnected(ParameterPackSendAddress);
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

		OnSetGraph.Broadcast();
	}

	void FMetasoundGenerator::QueueParameterPack(TSharedPtr<FMetasoundParameterPackStorage> ParameterPack)
	{
		ParameterPackQueue.Enqueue(ParameterPack);
	}

	int32 FMetasoundGenerator::GetNumChannels() const
	{
		return NumChannels;
	}

	void FMetasoundGenerator::AddOutputVertexAnalyzer(const Frontend::FAnalyzerAddress& AnalyzerAddress)
	{
		OutputAnalyzerModificationQueue.Enqueue([this, AnalyzerAddress]
		{
			if (OutputAnalyzers.Contains(AnalyzerAddress.OutputName))
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
			
			OutputAnalyzers.Emplace(AnalyzerAddress.OutputName, MoveTemp(Analyzer));
		});
	}
	
	void FMetasoundGenerator::RemoveOutputVertexAnalyzer(const FName& OutputName)
	{
		OutputAnalyzerModificationQueue.Enqueue([this, OutputName]
		{
			OutputAnalyzers.Remove(OutputName);
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

		const bool bDidUpdateGraph = UpdateGraphIfPending();
		bIsWaitingForFirstGraph = bIsWaitingForFirstGraph && !bDidUpdateGraph;

		// Output silent audio if we're still building a graph
		if (bIsWaitingForFirstGraph)
		{
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
			for (const auto& Pair : OutputAnalyzers)
			{
				Pair.Value->Execute();
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
