// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundGenerator.h"

#include "DSP/Dsp.h"
#include "MetasoundGraph.h"
#include "MetasoundInputNode.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundOutputNode.h"
#include "MetasoundTrigger.h"
//#include "MetasoundSource.h"

namespace Metasound
{
	FAsyncMetaSoundBuilder::FAsyncMetaSoundBuilder(FMetasoundGeneratorInitParams&& InInitParams)
		: InitParams(MoveTemp(InInitParams))
		, PlayTrigger(FTriggerWriteRef::CreateNew(InInitParams.OperatorSettings))
		, FinishTrigger(FTriggerReadRef::CreateNew(InInitParams.OperatorSettings))
		, bSuccess(false)
	{
	}

	void FAsyncMetaSoundBuilder::DoWork()
	{
		// Create handles for new root graph
		Frontend::FConstDocumentHandle NewDocumentHandle = Frontend::IDocumentController::CreateDocumentHandle(Frontend::MakeAccessPtr<const FMetasoundFrontendDocument>(InitParams.DocumentCopy.AccessPoint, InitParams.DocumentCopy));
		Frontend::FConstGraphHandle RootGraph = NewDocumentHandle->GetRootGraph();
		ensureAlways(RootGraph->IsValid());

		TArray<IOperatorBuilder::FBuildErrorPtr> BuildErrors;
		GraphOperator = RootGraph->BuildOperator(InitParams.OperatorSettings, InitParams.Environment, BuildErrors);

		// Log build errors
		for (const IOperatorBuilder::FBuildErrorPtr& Error : BuildErrors)
		{
			if (Error.IsValid())
			{
				UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] build error [%s] \"%s\""), *InitParams.MetaSoundName, *(Error->GetErrorType().ToString()), *(Error->GetErrorDescription().ToString()));
			}
		}

		if (!GraphOperator.IsValid())
		{
			UE_LOG(LogMetasound, Error, TEXT("Failed to build Metasound operator from graph in MetasoundSource [%s]"), *InitParams.MetaSoundName);
			bSuccess = false;
		}
		else
		{
			FDataReferenceCollection Outputs = GraphOperator->GetOutputs();

			// Get output audio buffers.
			if (InitParams.NumOutputChannels == 2)
			{
				if (!Outputs.ContainsDataReadReference<FStereoAudioFormat>(InitParams.OutputName))
				{
					UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] does not contain stereo output [%s] in output"), *InitParams.MetaSoundName, *InitParams.OutputName);
				}
				OutputBuffers = Outputs.GetDataReadReferenceOrConstruct<FStereoAudioFormat>(InitParams.OutputName, InitParams.OperatorSettings)->GetBuffers();
			}
			else if (InitParams.NumOutputChannels == 1)
			{
				if (!Outputs.ContainsDataReadReference<FMonoAudioFormat>(InitParams.OutputName))
				{
					UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] does not contain mono output [%s] in output"), *InitParams.MetaSoundName, *InitParams.OutputName);
				}
				OutputBuffers = Outputs.GetDataReadReferenceOrConstruct<FMonoAudioFormat>(InitParams.OutputName, InitParams.OperatorSettings)->GetBuffers();
			}

			// References must be cached before moving the operator to the InitParams
			FDataReferenceCollection Inputs = GraphOperator->GetInputs();
			PlayTrigger = Inputs.GetDataWriteReferenceOrConstruct<FTrigger>(InitParams.InputName, InitParams.OperatorSettings, false);
			FinishTrigger = Outputs.GetDataReadReferenceOrConstruct<FTrigger>(InitParams.IsFinishedOutputName, InitParams.OperatorSettings, false);
			bSuccess = true;
		}
	}

	bool FAsyncMetaSoundBuilder::SetDataOnGenerator(FMetasoundGenerator& InGenerator)
	{
		if (bSuccess)
		{
			FMetasoundGeneratorData GeneratorData =
			{
				MoveTemp(GraphOperator),
				OutputBuffers,
				MoveTemp(PlayTrigger),
				MoveTemp(FinishTrigger)
			};

			InGenerator.SetGraph(MoveTemp(GeneratorData));
		}
		return bSuccess;
	}

	FMetasoundGenerator::FMetasoundGenerator(FMetasoundGeneratorInitParams&& InParams)
		: bIsGraphBuilding(true)
		, bIsPlaying(false)
		, bIsFinished(false)
		, NumChannels(0)
		, NumFramesPerExecute(0)
		, NumSamplesPerExecute(0)
		, OnPlayTriggerRef(FTriggerWriteRef::CreateNew(InParams.OperatorSettings))
		, OnFinishedTriggerRef(FTriggerWriteRef::CreateNew(InParams.OperatorSettings))
	{
		NumChannels = InParams.NumOutputChannels;
		NumFramesPerExecute = InParams.OperatorSettings.GetNumFramesPerBlock();
		NumSamplesPerExecute = NumChannels * NumFramesPerExecute;
		BuilderTask = MakeUnique<FBuilderTask>(MoveTemp(InParams));
		BuilderTask->StartBackgroundTask(GBackgroundPriorityThreadPool);
	}

	FMetasoundGenerator::~FMetasoundGenerator()
	{
		if (BuilderTask.IsValid())
		{
			BuilderTask->EnsureCompletion();
			BuilderTask = nullptr;
		}
	}

	bool FMetasoundGenerator::IsTickable() const
	{
		return bIsGraphBuilding;
	}

	void FMetasoundGenerator::Tick(float DeltaTime)
	{
		if (BuilderTask->IsDone())
		{	
			FAsyncMetaSoundBuilder& Builder = BuilderTask->GetTask();
			if (Builder.SetDataOnGenerator(*this))
			{
				// We're done building our graph now
				bIsGraphBuilding = false;
			}
			else
			{
				// Failed to load/generate the graph, kill the metasound
				bIsFinished = true;
			}

			BuilderTask = nullptr;
		}
	}

	void FMetasoundGenerator::SetGraph(FMetasoundGeneratorData&& InData)
	{
		InterleavedAudioBuffer.Reset();

		GraphOutputAudio.Reset();
		if (InData.OutputBuffers.Num() > 0)
		{
			GraphOutputAudio.Append(InData.OutputBuffers.GetData(), InData.OutputBuffers.Num());
		}

		OnPlayTriggerRef = InData.TriggerOnPlayRef;
		OnFinishedTriggerRef = InData.TriggerOnFinishRef;

		// The graph operator and graph audio output contain all the values needed
		// by the sound generator.
		RootExecuter.SetOperator(MoveTemp(InData.GraphOperator));


		// Query the graph output to get the number of output audio channels.
		// Multichannel version:
		check(NumChannels == GraphOutputAudio.Num());

		if (NumSamplesPerExecute > 0)
		{
			// Preallocate interleaved buffer as it is necessary for any audio generation calls.
			InterleavedAudioBuffer.AddUninitialized(NumSamplesPerExecute);
		}
	}

	int32 FMetasoundGenerator::GetNumChannels() const
	{
		return GraphOutputAudio.Num();
	}

	int32 FMetasoundGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamplesRemaining)
	{
		if (NumSamplesRemaining <= 0)
		{
			return 0;
		}

		// Output silent audio if we're still building a graph
		if (bIsGraphBuilding || NumSamplesPerExecute < 1)
		{
			FMemory::Memset(OutAudio, 0, sizeof(float) * NumSamplesRemaining);
			return NumSamplesRemaining;
		}

		if (!bIsPlaying)
		{
			OnPlayTriggerRef->TriggerFrame(0);
			bIsPlaying = true;
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
			// Call metasound graph operator.
			RootExecuter.Execute();

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
		}

		if (*OnFinishedTriggerRef)
		{
			bIsFinished = true;
		}

		return NumSamplesWritten;
	}

	int32 FMetasoundGenerator::GetDesiredNumSamplesToRenderPerCallback() const
	{
		// TODO: may improve performance if this number is increased. 
		return NumFramesPerExecute;
	}

	bool FMetasoundGenerator::IsFinished() const
	{
		return bIsFinished;
	}

	int32 FMetasoundGenerator::FillWithBuffer(const Audio::AlignedFloatBuffer& InBuffer, float* OutAudio, int32 MaxNumOutputSamples)
	{
		int32 InNum = InBuffer.Num();

		if (InNum > 0)
		{
			if (InNum < MaxNumOutputSamples)
			{
				FMemory::Memcpy(OutAudio, InBuffer.GetData(), InNum * sizeof(float));
				return InNum;
			}
			else
			{
				FMemory::Memcpy(OutAudio, InBuffer.GetData(), MaxNumOutputSamples * sizeof(float));
				return MaxNumOutputSamples;
			}
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
} 
