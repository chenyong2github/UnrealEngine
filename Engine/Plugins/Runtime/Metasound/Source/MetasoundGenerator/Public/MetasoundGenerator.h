// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundExecutableOperator.h"
#include "MetasoundGraphOperator.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundParameterPack.h"
#include "MetasoundRouter.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"
#include "MetasoundVertexData.h"
#include "Analysis/MetasoundFrontendGraphAnalyzer.h"
#include "Async/AsyncWork.h"
#include "Containers/MpscQueue.h"
#include "Sound/SoundGenerator.h"
#include "Delegates/Delegate.h"

#ifndef ENABLE_METASOUND_GENERATOR_RENDER_TIMING
#define ENABLE_METASOUND_GENERATOR_RENDER_TIMING WITH_EDITOR
#endif // ifndef ENABLE_METASOUND_GENERATOR_RENDER_TIMING

namespace Metasound
{
	namespace MetasoundGeneratorPrivate
	{
#if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
		struct FRenderTimer;
#endif // if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
	}

	// Struct needed for building the metasound graph
	struct METASOUNDGENERATOR_API FMetasoundGeneratorInitParams
	{
		FOperatorSettings OperatorSettings;
		FOperatorBuilderSettings BuilderSettings;
		TSharedPtr<const IGraph, ESPMode::ThreadSafe> Graph;
		FMetasoundEnvironment Environment;
		FString MetaSoundName;
		TArray<FVertexName> AudioOutputNames;
		TArray<FAudioParameter> DefaultParameters;
		bool bBuildSynchronous = false;

		void Release();
	};

	// A struct that provides a method of pushing "raw" data from a parameter pack into a specific metasound input node.
	struct FParameterSetter
	{
		FName DataType;
		void* Destination;
		const Frontend::IParameterAssignmentFunction& Setter;
		FParameterSetter(FName InDataType, void* InDestination, const Frontend::IParameterAssignmentFunction& InSetter)
			: DataType(InDataType)
			, Destination(InDestination)
			, Setter(InSetter)
		{}
		void SetParameterWithPayload(const void* ParameterPayload) const
		{
			Setter(ParameterPayload, Destination);
		}
	};

	struct FMetasoundGeneratorData
	{
		FOperatorSettings OperatorSettings;
		TUniquePtr<IOperator> GraphOperator;
		FVertexInterfaceData VertexInterfaceData;
		TMap<FName, FParameterSetter> ParameterSetters;
		TUniquePtr<Frontend::FGraphAnalyzer> GraphAnalyzer;
		TArray<TDataReadReference<FAudioBuffer>> OutputBuffers;
		FTriggerWriteRef TriggerOnPlayRef;
		FTriggerReadRef TriggerOnFinishRef;
	};

	class FMetasoundGenerator;

	class FAsyncMetaSoundBuilder : public FNonAbandonableTask
	{
	public:
		FAsyncMetaSoundBuilder(FMetasoundGenerator* InGenerator, FMetasoundGeneratorInitParams&& InInitParams, bool bInTriggerGenerator);

		~FAsyncMetaSoundBuilder() = default;

		void DoWork();

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncMetaSoundBuilder, STATGROUP_ThreadPoolAsyncTasks);
		}

	private:
		FMetasoundGeneratorData BuildGeneratorData(const FMetasoundGeneratorInitParams& InInitParams, TUniquePtr<IOperator> InOperator, TUniquePtr<Frontend::FGraphAnalyzer> InAnalyzer) const;
		TUniquePtr<IOperator> BuildGraphOperator(TArray<FAudioParameter>&& InParameters, FBuildResults& OutBuildResults) const;
		TUniquePtr<Frontend::FGraphAnalyzer> BuildGraphAnalyzer(TMap<FGuid, FDataReferenceCollection>&& InInternalDataReferences) const;
		void LogBuildErrors(const FBuildResults& InBuildResults) const;
		TArray<FAudioBufferReadRef> FindOutputAudioBuffers(const FVertexInterfaceData& InVertexData) const;

		FMetasoundGenerator* Generator;
		FMetasoundGeneratorInitParams InitParams;
		bool bTriggerGenerator;
	};

	DECLARE_TS_MULTICAST_DELEGATE(FOnSetGraph);

	/** FMetasoundGenerator generates audio from a given metasound IOperator
	 * which produces a multichannel audio output.
	 */
	class METASOUNDGENERATOR_API FMetasoundGenerator : public ISoundGenerator
	{
	public:
		using FOperatorUniquePtr = TUniquePtr<Metasound::IOperator>;
		using FAudioBufferReadRef = Metasound::FAudioBufferReadRef;

		FString MetasoundName;

		const FOperatorSettings OperatorSettings;

		/** Create the generator with a graph operator and an output audio reference.
		 *
		 * @param InParams - The generator initialization parameters
		 */
		explicit FMetasoundGenerator(FMetasoundGeneratorInitParams&& InParams);

		virtual ~FMetasoundGenerator() override;

		/** Set the value of a graph's input data using the assignment operator.
		 *
		 * @param InName - The name of the graph's input data reference.
		 * @param InData - The value to assign.
		 */
		template<typename DataType>
		void SetInputValue(const FVertexName& InName, DataType InData)
		{
			if (const FAnyDataReference* Ref = VertexInterfaceData.GetInputs().FindDataReference(InName))
			{
				*(Ref->GetDataWriteReference<typename TDecay<DataType>::Type>()) = InData;
			}
		}

		/** Apply a function to the graph's input data.
		 *
		 * @param InName - The name of the graph's input data reference.
		 * @param InFunc - A function which takes the DataType as an input.
		 */ 
		template<typename DataType>
		void ApplyToInputValue(const FVertexName& InName, TFunctionRef<void(DataType&)> InFunc)
		{
			if (const FAnyDataReference* Ref = VertexInterfaceData.GetInputs().FindDataReference(InName))
			{
				InFunc(*(Ref->GetDataWriteReference<typename TDecay<DataType>::Type>()));
			}
		}

		void QueueParameterPack(TSharedPtr<FMetasoundParameterPackStorage> ParameterPack);

		/**
		 * Get a write reference to one of the generator's inputs, if it exists.
		 * NOTE: This reference is only safe to use immediately on the same thread that this generator's
		 * OnGenerateAudio() is called.
		 *
		 * @tparam DataType - The expected data type of the input
		 * @param InputName - The user-defined name of the input
		 */
		template<typename DataType>
		TOptional<TDataWriteReference<DataType>> GetInputWriteReference(const FVertexName InputName)
		{
			TOptional<TDataWriteReference<DataType>> WriteRef;
			
			if (const FAnyDataReference* Ref = VertexInterfaceData.GetInputs().FindDataReference(InputName))
			{
				WriteRef = Ref->GetDataWriteReference<typename TDecay<DataType>::Type>();
			}
			
			return WriteRef;
		}
		
		/**
		 * Get a read reference to one of the generator's outputs, if it exists.
		 * NOTE: This reference is only safe to use immediately on the same thread that this generator's
		 * OnGenerateAudio() is called.
		 *
		 * @tparam DataType - The expected data type of the output
		 * @param OutputName - The user-defined name of the output
		 */
		template<typename DataType>
		TOptional<TDataReadReference<DataType>> GetOutputReadReference(const FVertexName OutputName)
		{
			TOptional<TDataReadReference<DataType>> ReadRef;

			if (const FAnyDataReference* Ref = VertexInterfaceData.GetOutputs().FindDataReference(OutputName))
			{
				ReadRef = Ref->GetDataReadReference<typename TDecay<DataType>::Type>();
			}
			
			return ReadRef;
		}

		/**
		 * Add a vertex analyzer for a named output with the given address info.
		 *
		 * @param AnalyzerAddress - Address information for the analyzer to use when making views
		 */
		void AddOutputVertexAnalyzer(const Frontend::FAnalyzerAddress& AnalyzerAddress);
		
		/**
		 * Remove a vertex analyzer for a named output
		 *
		 * @param OutputName - The name of the output in the MetaSound graph
		 */
		void RemoveOutputVertexAnalyzer(const FName& OutputName);
		
		/** Return the number of audio channels. */
		int32 GetNumChannels() const;

		//~ Begin FSoundGenerator
		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
		int32 GetDesiredNumSamplesToRenderPerCallback() const override;
		bool IsFinished() const override;
		//~ End FSoundGenerator

#if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
		/** Fraction of a single CPU core used to render audio on a scale of 0.0 to 1.0 */
		double GetCPUCoreUtilization() const;

#endif // if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
		
		/** Update the current graph operator with a new graph operator. The number of channels
		 * of InGraphOutputAudioRef must match the existing number of channels reported by
		 * GetNumChannels() in order for this function to successfully replace the graph operator.
		 *
		 * @param InData - Metasound data of built graph.
		 * @param bTriggerGraph - If true, "OnPlay" will be triggered on the new graph.
		 */
		void SetPendingGraph(FMetasoundGeneratorData&& InData, bool bTriggerGraph);
		void SetPendingGraphBuildFailed();

		// Called when a new graph has been "compiled" and set up as this generator's graph.
		FOnSetGraph OnSetGraph;

	private:

		bool UpdateGraphIfPending();

		// Internal set graph after checking compatibility.
		void SetGraph(TUniquePtr<FMetasoundGeneratorData>&& InData, bool bTriggerGraph);

		// Fill OutAudio with data in InBuffer, up to maximum number of samples.
		// Returns the number of samples used.
		int32 FillWithBuffer(const Audio::FAlignedFloatBuffer& InBuffer, float* OutAudio, int32 MaxNumOutputSamples);

		// Metasound creates deinterleaved audio while sound generator requires interleaved audio.
		void InterleaveGeneratedAudio();
		
		void UnpackAndTransmitUpdatedParameters();

		FExecuter RootExecuter;
		FVertexInterfaceData VertexInterfaceData;

		bool bIsGraphBuilding;
		bool bIsFinishTriggered;
		bool bIsFinished;

		int32 FinishSample = INDEX_NONE;
		int32 NumChannels;
		int32 NumFramesPerExecute;
		int32 NumSamplesPerExecute;

		TArray<FAudioBufferReadRef> GraphOutputAudio;

		// Triggered when metasound is played
		FTriggerWriteRef OnPlayTriggerRef;

		// Triggered when metasound is finished
		FTriggerReadRef OnFinishedTriggerRef;

		Audio::FAlignedFloatBuffer InterleavedAudioBuffer;

		Audio::FAlignedFloatBuffer OverflowBuffer;

		typedef FAsyncTask<FAsyncMetaSoundBuilder> FBuilderTask;
		TUniquePtr<FBuilderTask> BuilderTask;

		FCriticalSection PendingGraphMutex;
		TUniquePtr<FMetasoundGeneratorData> PendingGraphData;
		bool bPendingGraphTrigger;
		bool bIsNewGraphPending;
		bool bIsWaitingForFirstGraph;

		TUniquePtr<Frontend::FGraphAnalyzer> GraphAnalyzer;

		// These next items are needed to provide a destination for the FAudioDevice, etc. to
		// send parameter packs to. Every playing metasound will have a parameter destination
		// that can accept parameter packs.
		FSendAddress ParameterPackSendAddress;
		TReceiverPtr<FMetasoundParameterStorageWrapper> ParameterPackReceiver;
		
		// This map provides setters for all of the input nodes in the metasound graph. 
		// It is used when processing named parameters in a parameter pack.
		TMap<FName, FParameterSetter> ParameterSetters;

		// While parameter packs may arrive via the IAudioParameterInterface system,
		// a faster method of sending parameters is via the QueueParameterPack function 
		// and this queue.
		TMpscQueue<TSharedPtr<FMetasoundParameterPackStorage>> ParameterPackQueue;

		TMpscQueue<TUniqueFunction<void()>> OutputAnalyzerModificationQueue;
		TMap<FName, TUniquePtr<Frontend::IVertexAnalyzer>> OutputAnalyzers;
#if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
		TUniquePtr<MetasoundGeneratorPrivate::FRenderTimer> RenderTimer;
#endif // if ENABLE_METASOUND_GENERATOR_RENDER_TIMING
	};
}
