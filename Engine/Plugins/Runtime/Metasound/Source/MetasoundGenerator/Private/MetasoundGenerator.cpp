// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundGenerator.h"

#include "DSP/Dsp.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataReferenceTypes.h"
#include "MetasoundOscNode.h"
#include "MetasoundAudioMultiplyNode.h"
#include "MetasoundADSRNode.h"
#include "MetasoundPeriodicBopNode.h"
#include "MetasoundOutputNode.h"
#include "MetasoundInputNode.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundGraph.h"
#include "MetasoundFrequency.h"

namespace Metasound
{
	FMetasoundGenerator::FMetasoundGenerator(FOperatorUniquePtr InGraphOperator, const FMultichannelAudioFormatReadRef& InGraphOutputAudioRef)
	:	NumChannels(0)
	,	NumFramesPerExecute(0)
	,	NumSamplesPerExecute(0)
	,	GraphOutputAudioRef(InGraphOutputAudioRef) // Copy audio ref here to avoid unneeded construction of unused graph output audio object.
	{
		SetGraph(MoveTemp(InGraphOperator), InGraphOutputAudioRef);
	}

	FMetasoundGenerator::~FMetasoundGenerator()
	{
	}

	bool FMetasoundGenerator::UpdateGraphOperator(FOperatorUniquePtr InGraphOperator, const FMultichannelAudioFormatReadRef& InGraphOutputAudioRef)
	{
		if (InGraphOutputAudioRef->GetNumChannels() == NumChannels)
		{
			SetGraph(MoveTemp(InGraphOperator), InGraphOutputAudioRef);

			return true;
		}

		return false;
	}

	void FMetasoundGenerator::SetGraph(FOperatorUniquePtr InGraphOperator, const FMultichannelAudioFormatReadRef& InGraphOutputAudioRef)
	{
		InterleavedAudioBuffer.Reset();
		NumFramesPerExecute = 0;
		NumSamplesPerExecute = 0;

		// The graph operator and graph audio output contain all the values needed
		// by the sound generator.
		RootExecuter.SetOperator(MoveTemp(InGraphOperator));
		GraphOutputAudioRef = InGraphOutputAudioRef;

		// Query the graph output to get the number of output audio channels.
		NumChannels = GraphOutputAudioRef->GetNumChannels();

		if (NumChannels > 0)
		{
			// All buffers have same number of frames, so only need to query
			// first buffer to know number of frames.
			NumFramesPerExecute = GraphOutputAudioRef->GetBuffers()[0]->Num();

			NumSamplesPerExecute = NumFramesPerExecute * NumChannels;

			if (NumSamplesPerExecute > 0)
			{
				// Preallocate interleaved buffer as it is necessary for any audio generation calls.
				InterleavedAudioBuffer.AddUninitialized(NumSamplesPerExecute);
			}
		}
	}

	int32 FMetasoundGenerator::GetNumChannels() const
	{
		return GraphOutputAudioRef->GetNumChannels();
	}

	int32 FMetasoundGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamplesRemaining)
	{
		if (NumSamplesRemaining <= 0)
		{
			return 0;
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

				FMemory::Memcpy(OverflowBuffer.GetData(), &InterleavedAudioBuffer.GetData()[ThisLoopNumSamplesWritten], OverflowCount);
			}
		}

		return NumSamplesWritten;
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
		const FMultichannelAudioFormat& DeinterleavedAudio = *GraphOutputAudioRef;

		// Prepare output buffer
		InterleavedAudioBuffer.Reset();

		if (NumSamplesPerExecute > 0)
		{
			InterleavedAudioBuffer.AddUninitialized(NumSamplesPerExecute);
		}

		const TArrayView<const FAudioBufferReadRef>& Buffers = DeinterleavedAudio.GetBuffers();

		// Iterate over channels
		for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++)
		{
			const FAudioBuffer& InputBuffer = *Buffers[ChannelIndex];

			const float* InputPtr = InputBuffer.GetData();
			float* OutputPtr = &InterleavedAudioBuffer.GetData()[ChannelIndex];

			// Assign values to output for single channel.
			for (int32 FrameIndex = 0; FrameIndex < NumFramesPerExecute; FrameIndex++)
			{
				*OutputPtr = InputPtr[FrameIndex];
				OutputPtr += NumChannels;
			}
		}
	}
} 


