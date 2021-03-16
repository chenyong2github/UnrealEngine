// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundWavePlayerNode.h"

#include "AudioResampler.h"
#include "MetasoundBuildError.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundWave.h"
#include "MetasoundTrigger.h"
#include "MetasoundEngineNodesNames.h"


#define LOCTEXT_NAMESPACE "MetasoundWaveNode"

// static const int32 SMOOTH = -1;
// static int32 NoDiscontinuities(const float* start, int32 numframes, const float thresh = 0.3f)
// {
// 	for (int32 i = 0; i < numframes - 1; ++i)
// 	{
// 		float delta = FMath::Abs(start[i] - start[i + 1]);
// 		if (delta > thresh)
// 		{
// 			return i;
// 		}
// 	}
// 	return SMOOTH; // all good!
// }

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
			const FTriggerReadRef& InPlayTrigger,
			const FTriggerReadRef& InStopTrigger,
			const FTriggerReadRef& InSeekTrigger,
			const FFloatReadRef& InSeekTimeSeconds,
			const FFloatReadRef& InPitchShiftSemiTones,
			const FBoolReadRef& InLoop
		)
			: OperatorSettings(InSettings)
			, PlayTrig(InPlayTrigger)
			, StopTrig(InStopTrigger)
			, SeekTrig(InSeekTrigger)
			, Wave(InWave)
			, SeekTimeSeconds(InSeekTimeSeconds)
			, PitchShiftSemiTones(InPitchShiftSemiTones)
			, IsLooping(InLoop)
			, AudioBufferL(FAudioBufferWriteRef::CreateNew(InSettings))
			, AudioBufferR(FAudioBufferWriteRef::CreateNew(InSettings))
			, TrigggerOnLooped(FTriggerWriteRef::CreateNew(InSettings))
			, TrigggerOnDone(FTriggerWriteRef::CreateNew(InSettings))
			, OutputSampleRate(InSettings.GetSampleRate())
			, OutputBlockSizeInFrames(InSettings.GetNumFramesPerBlock())
		{
			check(OutputSampleRate);
			check(AudioBufferL->Num() == OutputBlockSizeInFrames && AudioBufferR->Num() == OutputBlockSizeInFrames);

			if (Wave->IsSoundWaveValid())
			{
				CurrentSoundWaveName = (*Wave)->GetFName();
			}
		}

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(TEXT("Audio"), FWaveAssetReadRef(Wave));
			InputDataReferences.AddDataReadReference(TEXT("Play"), FTriggerReadRef(PlayTrig));
			InputDataReferences.AddDataReadReference(TEXT("Stop"), FTriggerReadRef(StopTrig));
			InputDataReferences.AddDataReadReference(TEXT("Seek"), FTriggerReadRef(SeekTrig));
			InputDataReferences.AddDataReadReference(TEXT("SeekTime"), FFloatReadRef(SeekTimeSeconds));
			InputDataReferences.AddDataReadReference(TEXT("PitchShift"), FFloatReadRef(PitchShiftSemiTones));
			InputDataReferences.AddDataReadReference(TEXT("Loop"), FBoolReadRef(IsLooping));
			return InputDataReferences;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(TEXT("AudioLeft"), FAudioBufferReadRef(AudioBufferL));
			OutputDataReferences.AddDataReadReference(TEXT("AudioRight"), FAudioBufferReadRef(AudioBufferR));
			OutputDataReferences.AddDataReadReference(TEXT("Looped"), FTriggerReadRef(TrigggerOnLooped));
			OutputDataReferences.AddDataReadReference(TEXT("Done"), FTriggerReadRef(TrigggerOnDone));
			return OutputDataReferences;
		}


		void Execute()
		{
			TrigggerOnDone->AdvanceBlock();
			TrigggerOnLooped->AdvanceBlock();

			// see if we have a new soundwave input
			FName NewSoundWaveName = FName();
			if (Wave->IsSoundWaveValid())
			{
				NewSoundWaveName = (*Wave)->GetFName();
			}

			if (NewSoundWaveName != CurrentSoundWaveName)
			{
				ResetDecoder();
				CurrentSoundWaveName = NewSoundWaveName;
			}

			// zero output buffers
			FMemory::Memzero(AudioBufferL->GetData(), OutputBlockSizeInFrames * sizeof(float));
			FMemory::Memzero(AudioBufferR->GetData(), OutputBlockSizeInFrames * sizeof(float));

			// Parse triggers and render audio
			int32 PlayTrigIndex = 0;
			int32 NextPlayFrame = 0;
			const int32 NumPlayTrigs = PlayTrig->NumTriggeredInBlock();

			int32 StopTrigIndex = 0;
			int32 NextStopFrame = 0;
			const int32 NumStopTrigs = StopTrig->NumTriggeredInBlock();

			int32 SeekTrigIndex = 0;
			int32 NextSeekFrame = 0;
			const int32 NumSeekTrigs = SeekTrig->NumTriggeredInBlock();

			int32 CurrAudioFrame = 0;
			int32 NextAudioFrame = 0;

			while (NextAudioFrame < (OutputBlockSizeInFrames -1))
			{
				const int32 NoTrigger = (OutputBlockSizeInFrames << 1);

				// get the next Play and Seek indicies
				// (play)
				if (PlayTrigIndex < NumPlayTrigs)
				{
					NextPlayFrame = (*PlayTrig)[PlayTrigIndex];
				}
				else
				{
					NextPlayFrame = NoTrigger;
				}

				// (stop)
				if (StopTrigIndex < NumStopTrigs)
				{
					NextStopFrame = (*StopTrig)[StopTrigIndex];
				}
				else
				{
					NextStopFrame = NoTrigger;
				}

				// (seek)
				if (SeekTrigIndex < NumSeekTrigs)
				{
					NextSeekFrame = (*SeekTrig)[SeekTrigIndex];
				}
				else
				{
					NextSeekFrame = NoTrigger;
				}

				// determine the next audio frame we are going to render up to
				NextAudioFrame = FMath::Min(NextPlayFrame, NextStopFrame);
				NextAudioFrame = FMath::Min(NextAudioFrame, NextSeekFrame);

				// no more triggers, rendering to the end of the block
				if (NextAudioFrame == NoTrigger)
				{
					NextAudioFrame = OutputBlockSizeInFrames;
				}

				// render audio (while loop handles looping audio)
				while (CurrAudioFrame != NextAudioFrame)
				{
					if (bIsPlaying)
					{
						CurrAudioFrame += ExecuteInternal(CurrAudioFrame, NextAudioFrame);
					}
					else
					{
						CurrAudioFrame = NextAudioFrame;
					}
				}

				// execute the next trigger
				if (CurrAudioFrame == NextSeekFrame)
				{
					ExecuteSeekRequest();

					++SeekTrigIndex;
				}

				if (CurrAudioFrame == NextPlayFrame)
				{
					StartPlaying();
					++PlayTrigIndex;
				}

				if (CurrAudioFrame == NextStopFrame)
				{
					bIsPlaying = false;
					ResetDecoder();
					++StopTrigIndex;
				}
			}
		}

		void StartPlaying()
		{
			ResetDecoder();

			if (!bIsPlaying)
			{
				bIsPlaying = Decoder.CanGenerateAudio();
			}
		}

		void ExecuteSeekRequest()
		{
			// TODO: get this to work w/o full decoder reset
			// Decoder.SeekToTime(FMath::Max(0.f, *SeekTimeSeconds));

			// in the mean time, using this instead:
			if (!bIsPlaying)
			{
				StartPlaying();
			}
		}

		bool ResetDecoder()
		{
			Audio::FSimpleDecoderWrapper::InitParams Params;
			Params.OutputBlockSizeInFrames = OutputBlockSizeInFrames;
			Params.OutputSampleRate = OutputSampleRate;
			Params.MaxPitchShiftMagnitudeAllowedInOctaves = 4.f;
			Params.StartTimeSeconds = FMath::Max(0.f, *SeekTimeSeconds);

			if (false == Wave->IsSoundWaveValid())
			{
				return false;
			}

			return Decoder.Initialize(Params, Wave->GetSoundWaveProxy());
		}


		int32 ExecuteInternal(int32 StartFrame, int32 EndFrame)
		{
			bool bCanDecodeWave = Wave->IsSoundWaveValid() && ((*Wave)->GetNumChannels() <= 2); // only support mono or stereo inputs

			// note: output is hard-coded to stereo (dual-mono)
			float* FinalOutputLeft = AudioBufferL->GetData() + StartFrame;
			float* FinalOutputRight = AudioBufferR->GetData() + StartFrame;
			const int32 NumOutputFrames = (EndFrame - StartFrame);
			int32 NumFramesDecoded = 0;

			if (bCanDecodeWave)
			{
				ensure(Decoder.CanGenerateAudio());

				const int32 NumInputChannels = (*Wave)->GetNumChannels();
				const bool bNeedsUpmix = (NumInputChannels == 1);
				const bool bNeedsDeinterleave = !bNeedsUpmix;

				const int32 NumSamplesToGenerate = NumOutputFrames * NumInputChannels; // (stereo output)

				PostSrcBuffer.Reset(NumSamplesToGenerate);
				PostSrcBuffer.AddZeroed(NumSamplesToGenerate);
				float* PostSrcBufferPtr = PostSrcBuffer.GetData();

				bool bIsLooping = *IsLooping;
				NumFramesDecoded = Decoder.GenerateAudio(PostSrcBufferPtr, NumOutputFrames, (*PitchShiftSemiTones * 100.f), bIsLooping) / NumInputChannels;

				// handle decoder having completed during it's decode
				const bool bReachedEOF = !Decoder.CanGenerateAudio() || (NumFramesDecoded < NumOutputFrames);
				if (bReachedEOF && !bIsLooping)
				{
					bIsPlaying = false;
					TrigggerOnDone->TriggerFrame(StartFrame + NumFramesDecoded);
				}
				else if (bReachedEOF && bIsLooping)
				{
					TrigggerOnLooped->TriggerFrame(StartFrame + NumFramesDecoded);
				}

				if (bNeedsUpmix)
				{
					// TODO: attenuate by -3 dB?
					// copy to Left & Right output buffers
					FMemory::Memcpy(FinalOutputLeft, PostSrcBufferPtr, sizeof(float) * NumFramesDecoded);
					FMemory::Memcpy(FinalOutputRight, PostSrcBufferPtr, sizeof(float) * NumFramesDecoded);
				}
				else if (bNeedsDeinterleave)
				{
					for (int32 i = 0; i < NumFramesDecoded; ++i)
					{
						// de-interleave each stereo frame into output buffers
						FinalOutputLeft[i] = PostSrcBufferPtr[(i << 1)];
						FinalOutputRight[i] = PostSrcBufferPtr[(i << 1) + 1];
					}
				}
			}
			else
			{
				FMemory::Memzero(FinalOutputLeft, sizeof(float) * NumOutputFrames);
				FMemory::Memzero(FinalOutputRight, sizeof(float) * NumOutputFrames);
			}

			return NumFramesDecoded;
		}

	private:
		const FOperatorSettings OperatorSettings;

		// i/o
		FTriggerReadRef PlayTrig;
		FTriggerReadRef StopTrig;
		FTriggerReadRef SeekTrig;
		FWaveAssetReadRef Wave;
		FFloatReadRef SeekTimeSeconds;
		FFloatReadRef PitchShiftSemiTones;
		FBoolReadRef IsLooping;

		FAudioBufferWriteRef AudioBufferL;
		FAudioBufferWriteRef AudioBufferR;
		FTriggerWriteRef TrigggerOnLooped;
		FTriggerWriteRef TrigggerOnDone;

		// source decode
		TArray<float> PostSrcBuffer;
		Audio::FSimpleDecoderWrapper Decoder;

		FName CurrentSoundWaveName{ };
			
		const float OutputSampleRate{ 0.f };
		const int32 OutputBlockSizeInFrames{ 0 };
		
		bool bIsPlaying{ false };
	};

	TUniquePtr<IOperator> FWavePlayerNode::FOperatorFactory::CreateOperator(
		const FCreateOperatorParams& InParams, 
		FBuildErrorArray& OutErrors) 
	{
		using namespace Audio;

		const FWavePlayerNode& WaveNode = static_cast<const FWavePlayerNode&>(InParams.Node);

		const FDataReferenceCollection& InputDataRefs = InParams.InputDataReferences;

		FTriggerReadRef TriggerPlay = InputDataRefs.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("Play"), InParams.OperatorSettings);
		FTriggerReadRef TriggerStop = InputDataRefs.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("Stop"), InParams.OperatorSettings);
		FTriggerReadRef TriggerSeek = InputDataRefs.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("Seek"), InParams.OperatorSettings);
		FWaveAssetReadRef Wave = InputDataRefs.GetDataReadReferenceOrConstruct<FWaveAsset>(TEXT("Wave"));
		FFloatReadRef SeekTimeSeconds = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("SeekTime"), 0.f);
		FFloatReadRef PitchShiftSemiTones = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("PitchShift"));
		FBoolReadRef IsLooping = InputDataRefs.GetDataReadReferenceOrConstruct<bool>(TEXT("Loop"), false);

		if (!Wave->IsSoundWaveValid())
		{
			AddBuildError<FWavePlayerError>(OutErrors, WaveNode, LOCTEXT("NoSoundWave", "No Sound Wave"));

		}
		else if ((*Wave)->GetNumChannels() > 2)
		{
			AddBuildError<FWavePlayerError>(OutErrors, WaveNode, LOCTEXT("WavePlayerCurrentlyOnlySuportsMonoOrStereoAssets", "Wave Player Currently Only Supports Mono Or Stereo Assets"));
		}

		return MakeUnique<FWavePlayerOperator>(
			  InParams.OperatorSettings
			, Wave
			, TriggerPlay
			, TriggerStop
			, TriggerSeek
			, SeekTimeSeconds
			, PitchShiftSemiTones
			, IsLooping
			);
	}

	FVertexInterface FWavePlayerNode::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<FWaveAsset>(TEXT("Wave"), LOCTEXT("WaveTooltip", "The Wave to be decoded")),
				TInputDataVertexModel<FTrigger>(TEXT("Play"), LOCTEXT("PlayTooltip", "Play the input wave.")),
				TInputDataVertexModel<FTrigger>(TEXT("Stop"), LOCTEXT("StopTooltip", "Stop the input wave.")),
				TInputDataVertexModel<FTrigger>(TEXT("Seek"), LOCTEXT("PlayTooltip", "Trigger the playing of the input wave.")),
				TInputDataVertexModel<float>(TEXT("SeekTime"), LOCTEXT("SeekTimeTooltip", "Seek time in seconds.")),
				TInputDataVertexModel<float>(TEXT("PitchShift"), LOCTEXT("PitchShiftTooltip", "Pitch Shift in semi-tones.")),
				TInputDataVertexModel<bool>(TEXT("Loop"), LOCTEXT("LoopTooltip", "Wave will loop if true"))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("AudioLeft"), LOCTEXT("AudioTooltip", "The output audio")),
				TOutputDataVertexModel<FAudioBuffer>(TEXT("AudioRight"), LOCTEXT("AudioTooltip", "The output audio")),
				TOutputDataVertexModel<FTrigger>(TEXT("Looped"), LOCTEXT("TriggerToolTip", "Trigger that notifies when the sound is has looped")),
				TOutputDataVertexModel<FTrigger>(TEXT("Done"), LOCTEXT("TriggerToolTip", "Trigger that notifies when the sound is done playing"))
			)
		);
	}

	const FNodeClassMetadata& FWavePlayerNode::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::EngineNodes::Namespace, TEXT("Wave Player"), Metasound::EngineNodes::StereoVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_WavePlayerNodeDisplayName", "Wave Player Node");
			Info.Description = LOCTEXT("Metasound_WavePlayerNodeDescription", "Plays a supplied Wave");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FWavePlayerNode::FWavePlayerNode(const FString& InName, const FGuid& InInstanceID)
		:	FNode(InName, InInstanceID, GetNodeInfo())
		,	Factory(MakeOperatorFactoryRef<FWavePlayerNode::FOperatorFactory>())
		,	Interface(DeclareVertexInterface())
	{
	}

	FWavePlayerNode::FWavePlayerNode(const FNodeInitData& InInitData)
		: FWavePlayerNode(InInitData.InstanceName, InInitData.InstanceID)
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
