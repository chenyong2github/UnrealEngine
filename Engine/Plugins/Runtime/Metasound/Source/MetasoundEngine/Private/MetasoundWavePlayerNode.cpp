// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWavePlayerNode.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundWave.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundBuildError.h"
#include "MetasoundBop.h"
#include "AudioResampler.h"

#define LOCTEXT_NAMESPACE "MetasoundWaveNode"

namespace Metasound
{
	// WavePlayer custom error 
	class FWavePlayerError : public FBuildErrorBase
	{
	public:
		FWavePlayerError(const FWavePlayerNode& InNode, FText InErrorDescription)
			: FBuildErrorBase(ErrorType, InErrorDescription)
		{
			AddNode(InNode);
		}

		virtual ~FWavePlayerError() = default;

		static const FName ErrorType;
	};

	const FName FWavePlayerError::ErrorType = FName(TEXT("WavePlayerError"));
	
	class FWavePlayerOperator : public TExecutableOperator<FWavePlayerOperator>
	{
	public:
		FWavePlayerOperator(
			const FOperatorSettings& InSettings,
			const FWaveAssetReadRef& InWave,
			const FTriggerReadRef& InTrigger,
			const float& InGraphSamplerate)
			: OperatorSettings(InSettings)
			, TrigIn(InTrigger)
			, Wave(InWave)
			, AudioBufferL(FAudioBufferWriteRef::CreateNew(InSettings))
			, AudioBufferR(FAudioBufferWriteRef::CreateNew(InSettings))
			, TrigggerOnDone(FTriggerWriteRef::CreateNew(InSettings))
			, InputSampleRate(InWave->SoundWaveProxy.IsValid()? InWave->SoundWaveProxy->GetSampleRate() : -1)
			, OutputSampleRate(InGraphSamplerate)
			, OutputBlockSizeInFrames(InSettings.GetNumFramesPerBlock())
		{
			check(OutputSampleRate && InputSampleRate); // divide by zero when calculating SrcRatio!
			check(AudioBufferL->Num() == OutputBlockSizeInFrames && AudioBufferR->Num() == OutputBlockSizeInFrames);

			if (Wave->SoundWaveProxy.IsValid())
			{
				CurrentSoundWaveName = Wave->SoundWaveProxy->GetFName();

				DecoderTrio = Wave->CreateDecoderTrio(OutputSampleRate, OutputBlockSizeInFrames);

				const int32 NumChannels = Wave->SoundWaveProxy->GetNumChannels();
				CircularDecoderOutputBuffer.SetCapacity(OutputBlockSizeInFrames * NumChannels * 2);
			}


		}

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(TEXT("Audio"), FWaveAssetReadRef(Wave));
			InputDataReferences.AddDataReadReference(TEXT("TrigIn"), FBopReadRef(TrigIn));
			return InputDataReferences;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(TEXT("AudioLeft"), FAudioBufferReadRef(AudioBufferL));
			OutputDataReferences.AddDataReadReference(TEXT("AudioRight"), FAudioBufferReadRef(AudioBufferR));
			OutputDataReferences.AddDataReadReference(TEXT("Done"), FTriggerReadRef(TrigggerOnDone));
			return OutputDataReferences;
		}


		void Execute()
		{
			TrigggerOnDone->AdvanceBlock();

			// see if we have a new soundwave input
			FName NewSoundWaveName = FName();
			int32 NumChannels = 1;
			if (Wave->IsSoundWaveValid())
			{
				NewSoundWaveName = Wave->SoundWaveProxy->GetFName();
			}

			if (NewSoundWaveName != CurrentSoundWaveName)
			{
				DecoderTrio = Wave->CreateDecoderTrio(OutputSampleRate, OutputBlockSizeInFrames);
				InputSampleRate = Wave->SoundWaveProxy->GetSampleRate();

				NumChannels = Wave->SoundWaveProxy->GetNumChannels();
				CircularDecoderOutputBuffer.SetCapacity(OutputBlockSizeInFrames * NumChannels * 2);

				CurrentSoundWaveName = NewSoundWaveName;
			}

			// zero output buffers
			FMemory::Memzero(AudioBufferL->GetData(), OutputBlockSizeInFrames * sizeof(float));
			FMemory::Memzero(AudioBufferR->GetData(), OutputBlockSizeInFrames * sizeof(float));

			TrigIn->ExecuteBlock(
				// OnPreBop
				[&](int32 StartFrame, int32 EndFrame)
				{
 					if (bIsPlaying)
 					{
						ExecuteInternal(StartFrame, EndFrame);
					}
				},
				// OnBop
				[&](int32 StartFrame, int32 EndFrame)
				{
					if (!bIsPlaying)
					{
						bIsPlaying = true;
					}
					else
					{
						DecoderTrio = Wave->CreateDecoderTrio(OutputSampleRate, OutputBlockSizeInFrames);
					}

					ExecuteInternal(StartFrame, EndFrame);
				}
			);
		}


		void ExecuteInternal(int32 StartFrame, int32 EndFrame)
		{
			float* FinalOutputLeft = AudioBufferL->GetData() + StartFrame;
			float* FinalOutputRight = AudioBufferR->GetData() + StartFrame;

			const float FsInToFsOutRatio = (InputSampleRate / OutputSampleRate); // TODO: account for pitch param

			const uint32 NumInputChannels = Wave->SoundWaveProxy.IsValid() ? Wave->SoundWaveProxy->GetNumChannels() : 1;
			const uint32 NumOutputChannels = 2; // forcing stereo output
			const uint32 NumOutputFrames = EndFrame - StartFrame;
			const uint32 NumOutputSamples = NumOutputFrames * NumOutputChannels;
			const uint32 NumSamplesInDecodeBlock = OutputBlockSizeInFrames * NumInputChannels;
			uint32 NumSamplesToDecode = NumInputChannels * NumOutputFrames * FsInToFsOutRatio;

			if (NumSamplesToDecode % 2)
			{
				++NumSamplesToDecode;
			}
			

			const bool bNeedsSRC = (InputSampleRate != OutputSampleRate);
			const bool bNeedsUpmix = (NumInputChannels == 1);
			const bool bNeedsDeinterleave = !bNeedsUpmix;



			// Decode audio if we need to (see if we are done)
			while (bIsPlaying && !bDecoderIsDone && (CircularDecoderOutputBuffer.Num() < NumSamplesToDecode))
			{
				// get more audio from the decoder
				Audio::IDecoderOutput::FPushedAudioDetails Details;
				bDecoderIsDone = (DecoderTrio.Decoder->Decode() == Audio::IDecoder::EDecodeResult::Finished);

				TempBufferA.Reset(NumSamplesInDecodeBlock);
				TempBufferA.AddZeroed(NumSamplesInDecodeBlock);
				const int32 NumSamplesDecoded = DecoderTrio.Output->PopAudio(MakeArrayView(TempBufferA.GetData(), NumSamplesInDecodeBlock), Details);

				// push that (interleaved) audio to the (interleaved) circular buffer
				CircularDecoderOutputBuffer.Push(TempBufferA.GetData(), NumSamplesDecoded);
			}

			// now that we have enough audio decoded, pop off the circular buffer into an interleaved, pre-src temp buffer
			// (It is possible that CircularDecoderOutputBuffer.Num() < NumOutputSamples if the decoder is dry...)
			const uint32 NumSamplesToPop = FMath::Min(CircularDecoderOutputBuffer.Num(), NumSamplesToDecode);

			// (...if that's the case, it means the sound is done)
			if (NumSamplesToPop < NumOutputChannels)
			{
				bIsPlaying = false;
				TrigggerOnDone->BopFrame(NumSamplesToPop / NumInputChannels);
			}

			// TODO: special-cases to reduce temp buffer usage where we don't need them

			const uint32 NumPostSrcSamples = NumOutputFrames * NumInputChannels;
			TempBufferA.Reset(NumSamplesToPop); // pre-src buffer 
			TempBufferA.AddZeroed(NumSamplesToPop);

			// pop into pre-src buffer
			CircularDecoderOutputBuffer.Pop(TempBufferA.GetData(), NumSamplesToPop);

			// holds result of sample rate conversion
			float* PostSrcBufferPtr = TempBufferA.GetData(); // assume no SRC

			int32 NumFramesConverted = NumSamplesToPop / NumInputChannels;

			if (bNeedsSRC)
			{
				// post-src buffer
				TempBufferB.Reset(NumPostSrcSamples);
				TempBufferB.AddZeroed(NumPostSrcSamples);
				PostSrcBufferPtr = TempBufferB.GetData();

				// perform SRC
				Resampler.Init(Audio::EResamplingMethod::Linear, 1.f / FsInToFsOutRatio, NumInputChannels);

				int32 Error = Resampler.ProcessAudio(TempBufferA.GetData(), NumSamplesToPop / NumInputChannels, false, PostSrcBufferPtr, NumOutputFrames, NumFramesConverted);
				if (Error)
				{
					bIsPlaying = false;
					bDecoderIsDone = false;
					DecoderTrio = {};
				}
			}

			// We may not have NumOutputSamples to play, and our output buffers have already been zero-ed out.
			// at this point we are outputting NumFramesConverted

			// perform channel conversion (output is forced to be stereo)
			// mono->stereo?
			if (bNeedsUpmix)
			{
				// TODO: attenuate by -3 dB?
				// copy to Left & Right output buffers
				FMemory::Memcpy(FinalOutputLeft, PostSrcBufferPtr, sizeof(float) * NumFramesConverted);
				FMemory::Memcpy(FinalOutputRight, PostSrcBufferPtr, sizeof(float) * NumFramesConverted);
			}
			else if (bNeedsDeinterleave)
			{
				for (int32 i = 0; i < NumFramesConverted; ++i)
				{
					// de-interleave each stereo frame into output buffers
					FinalOutputLeft[i] = PostSrcBufferPtr[(i << 1)];
					FinalOutputRight[i] = PostSrcBufferPtr[(i << 1) + 1];
				}
			}
		}

	private:
		const FOperatorSettings OperatorSettings;

		// i/o
		FTriggerReadRef TrigIn;
		FWaveAssetReadRef Wave;

		FAudioBufferWriteRef AudioBufferL;
		FAudioBufferWriteRef AudioBufferR;
		FTriggerWriteRef TrigggerOnDone;

		// src
		Audio::FResampler Resampler;

		// Decoder/IO. 
		FWaveAsset::FDecoderTrio DecoderTrio;

		Audio::TCircularAudioBuffer<float> CircularDecoderOutputBuffer;
		TArray<float> TempBufferA;
		TArray<float> TempBufferB;

		FName CurrentSoundWaveName{ };

		float InputSampleRate{ 0.f };
		const float OutputSampleRate{ 0.f };
		const int32 OutputBlockSizeInFrames{ 0 };
		

		bool bIsPlaying{ false };
		bool bDecoderIsDone{ false };

	};

	TUniquePtr<IOperator> FWavePlayerNode::FOperatorFactory::CreateOperator(
		const FCreateOperatorParams& InParams, 
		FBuildErrorArray& OutErrors) 
	{
		using namespace Audio;

		const FWavePlayerNode& WaveNode = static_cast<const FWavePlayerNode&>(InParams.Node);

		const FDataReferenceCollection& InputDataRefs = InParams.InputDataReferences;

		// Trigger input
		FTriggerReadRef TriggerPlay = InputDataRefs.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("TrigIn"), InParams.OperatorSettings);

		// Initialize decoder
		FWaveAssetReadRef Wave = InputDataRefs.GetDataReadReferenceOrConstruct<FWaveAsset>(TEXT("Wave"));
		
		if (!Wave->IsSoundWaveValid())
		{
			AddBuildError<FWavePlayerError>(OutErrors, WaveNode, LOCTEXT("NoSoundWave", "No Sound Wave"));

		}
		else if (Wave->SoundWaveProxy->GetNumChannels() != 1)
		{
			AddBuildError<FWavePlayerError>(OutErrors, WaveNode, LOCTEXT("WavePlayerCurrentlyOnlySuportsMonoAssets", "Wave Player Currently Only Supports Mono Assets"));
		}

		return MakeUnique<FWavePlayerOperator>(
			  InParams.OperatorSettings
			, Wave
			, TriggerPlay
			, InParams.OperatorSettings.GetSampleRate()
			);
	}

	FVertexInterface FWavePlayerNode::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<FWaveAsset>(TEXT("Wave"), LOCTEXT("WaveTooltip", "The Wave to be decoded")),
				TInputDataVertexModel<FTrigger>(TEXT("TrigIn"), LOCTEXT("TrigInTooltip", "Trigger the playing of the input wave."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("AudioLeft"), LOCTEXT("AudioTooltip", "The output audio")),
				TOutputDataVertexModel<FAudioBuffer>(TEXT("AudioRight"), LOCTEXT("AudioTooltip", "The output audio")),
				TOutputDataVertexModel<FTrigger>(TEXT("Done"), LOCTEXT("TriggerToolTip", "Trigger that notifies when the sound is done playing"))
			)
		);
	}

	const FNodeInfo& FWavePlayerNode::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeInfo
		{
			FNodeInfo Info;
			Info.ClassName = FName(TEXT("Wave Player"));
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = LOCTEXT("Metasound_WavePlayerNodeDescription", "Plays a supplied Wave");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeInfo Info = InitNodeInfo();

		return Info;
	}

	FWavePlayerNode::FWavePlayerNode(const FString& InName)
		:	FNode(InName, GetNodeInfo())
		,	Factory(MakeOperatorFactoryRef<FWavePlayerNode::FOperatorFactory>())
		,	Interface(DeclareVertexInterface())
	{
	}

	FWavePlayerNode::FWavePlayerNode(const FNodeInitData& InInitData)
		: FWavePlayerNode(InInitData.InstanceName)
	{
	}

	FOperatorFactorySharedRef FWavePlayerNode::GetDefaultOperatorFactory() const 
	{
		return Factory;
	}



	const FVertexInterface& FWavePlayerNode::GetVertexInterface() const
	{
		return Interface;
	}

	bool FWavePlayerNode::SetVertexInterface(const FVertexInterface& InInterface)
	{
		return InInterface == Interface;
	}

	bool FWavePlayerNode::IsVertexInterfaceSupported(const FVertexInterface& InInterface) const
	{
		return InInterface == Interface;
	}

	METASOUND_REGISTER_NODE(FWavePlayerNode)

} // namespace Metasound
#undef LOCTEXT_NAMESPACE // MetasoundWaveNode
