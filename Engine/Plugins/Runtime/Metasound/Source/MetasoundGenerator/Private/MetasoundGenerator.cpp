// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundGenerator.h"

#include "DSP/Dsp.h"
#include "MetasoundGraph.h"
#include "MetasoundInputNode.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundOutputNode.h"
#include "MetasoundTrigger.h"


namespace Metasound
{
	FMetasoundGenerator::FMetasoundGenerator(FMetasoundGeneratorInitParams&& InParams)
		: bIsPlaying(false)
		, bIsFinished(false)
		, NumChannels(0)
		, NumFramesPerExecute(0)
		, NumSamplesPerExecute(0)
		, OnPlayTriggerRef(InParams.TriggerOnPlayRef)
		, OnFinishedTriggerRef(InParams.TriggerOnFinishRef)
	{
		SetGraph(MoveTemp(InParams));
	}

	FMetasoundGenerator::~FMetasoundGenerator()
	{
	}

	bool FMetasoundGenerator::UpdateGraphOperator(FMetasoundGeneratorInitParams&& InParams)
	{
		// multichannel version:
		const int32 GraphNumChannels = InParams.OutputBuffers.Num();

		if (GraphNumChannels == NumChannels)
		{
			SetGraph(MoveTemp(InParams));

			return true;
		}

		return false;
	}

	void FMetasoundGenerator::SetGraph(FMetasoundGeneratorInitParams&& InParams)
	{
		InterleavedAudioBuffer.Reset();
		NumFramesPerExecute = 0;
		NumSamplesPerExecute = 0;

		GraphOutputAudio.Reset();
		if (InParams.OutputBuffers.Num() > 0)
		{
			GraphOutputAudio.Append(InParams.OutputBuffers.GetData(), InParams.OutputBuffers.Num());
		}

		OnPlayTriggerRef = InParams.TriggerOnPlayRef;
		OnFinishedTriggerRef = InParams.TriggerOnFinishRef;

		// The graph operator and graph audio output contain all the values needed
		// by the sound generator.
		RootExecuter.SetOperator(MoveTemp(InParams.GraphOperator));


		// Query the graph output to get the number of output audio channels.
		// Multichannel version:
		NumChannels = GraphOutputAudio.Num();


		// All buffers have same number of frames, so only need to query
		// first buffer to know number of frames.
		if (NumChannels > 0)
		{
			NumFramesPerExecute = GraphOutputAudio[0]->Num();
		}
		else
		{
			NumFramesPerExecute = 0;
		}

		NumSamplesPerExecute = NumFramesPerExecute * NumChannels;

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

		if (!bIsPlaying)
		{
			OnPlayTriggerRef->TriggerFrame(0);
			bIsPlaying = true;
		}

		if (NumSamplesPerExecute < 1)
		{
			FMemory::Memset(OutAudio, 0, sizeof(float) * NumSamplesRemaining);
			return NumSamplesRemaining;
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

		OnPlayTriggerRef->AdvanceBlock();

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
