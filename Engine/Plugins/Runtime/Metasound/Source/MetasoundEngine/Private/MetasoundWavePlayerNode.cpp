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
			const FTriggerReadRef& InTrigger,
			const FFloatReadRef InPitchShiftCents)
			: OperatorSettings(InSettings)
			, TrigIn(InTrigger)
			, Wave(InWave)
			, PitchShiftCents(InPitchShiftCents)
			, AudioBufferL(FAudioBufferWriteRef::CreateNew(InSettings))
			, AudioBufferR(FAudioBufferWriteRef::CreateNew(InSettings))
			, TrigggerOnDone(FTriggerWriteRef::CreateNew(InSettings))
			, OutputSampleRate(InSettings.GetSampleRate())
			, OutputBlockSizeInFrames(InSettings.GetNumFramesPerBlock())
		{
			check(OutputSampleRate);
			check(AudioBufferL->Num() == OutputBlockSizeInFrames && AudioBufferR->Num() == OutputBlockSizeInFrames);

			if (Wave->SoundWaveProxy.IsValid())
			{
				CurrentSoundWaveName = Wave->SoundWaveProxy->GetFName();
			}


		}

		virtual FDataReferenceCollection GetInputs() const override
		{
			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(TEXT("Audio"), FWaveAssetReadRef(Wave));
			InputDataReferences.AddDataReadReference(TEXT("TrigIn"), FTriggerReadRef(TrigIn));
			InputDataReferences.AddDataReadReference(TEXT("PitchShiftCents"), FFloatReadRef(PitchShiftCents));
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
			if (Wave->IsSoundWaveValid())
			{
				NewSoundWaveName = Wave->SoundWaveProxy->GetFName();
			}

			if (NewSoundWaveName != CurrentSoundWaveName)
			{
				ResetDecoder();
				CurrentSoundWaveName = NewSoundWaveName;
			}

			// zero output buffers
			FMemory::Memzero(AudioBufferL->GetData(), OutputBlockSizeInFrames * sizeof(float));
			FMemory::Memzero(AudioBufferR->GetData(), OutputBlockSizeInFrames * sizeof(float));

			TrigIn->ExecuteBlock(
				// OnPreTrigger
				[&](int32 StartFrame, int32 EndFrame)
				{
 					if (bIsPlaying)
 					{
						ExecuteInternal(StartFrame, EndFrame);
					}
				},
				// OnTrigger
				[&](int32 StartFrame, int32 EndFrame)
				{
					ResetDecoder();

					if (!bIsPlaying)
					{
						bIsPlaying = Decoder.CanGenerateAudio();
					}

					ExecuteInternal(StartFrame, EndFrame);
				}
			);
		}

		bool ResetDecoder()
		{
			Audio::FSimpleDecoderWrapper::InitParams Params;
			Params.OutputBlockSizeInFrames = OutputBlockSizeInFrames;
			Params.OutputSampleRate = OutputSampleRate;
			Params.MaxPitchShiftMagnitudeAllowedInOctaves = 4.f;

			if (false == Wave->IsSoundWaveValid())
			{
				return false;
			}

			return Decoder.Initialize(Params, *Wave->SoundWaveProxy);
		}


		void ExecuteInternal(int32 StartFrame, int32 EndFrame)
		{
			bool bCanDecodeWave = Wave->IsSoundWaveValid() && (Wave->SoundWaveProxy->GetNumChannels() <= 2); // only support mono or stereo inputs

			// note: output is hard-coded to stereo (dual-mono)
			float* FinalOutputLeft = AudioBufferL->GetData() + StartFrame;
			float* FinalOutputRight = AudioBufferR->GetData() + StartFrame;
			const int32 NumOutputFrames = (EndFrame - StartFrame);

			if (bCanDecodeWave)
			{
				ensure(Decoder.CanGenerateAudio());


				const int32 NumInputChannels = Wave->SoundWaveProxy->GetNumChannels();
				const bool bNeedsUpmix = (NumInputChannels == 1);
				const bool bNeedsDeinterleave = !bNeedsUpmix;

				const int32 NumSamplesToGenerate = NumOutputFrames * NumInputChannels; // (stereo output)

				PostSrcBuffer.Reset(NumSamplesToGenerate);
				PostSrcBuffer.AddZeroed(NumSamplesToGenerate);
				float* PostSrcBufferPtr = PostSrcBuffer.GetData();

				int32 NumFramesDecoded = Decoder.GenerateAudio(PostSrcBufferPtr, NumOutputFrames, *PitchShiftCents); // TODO: pitch shift

				// TODO: handle decoder having completed during it's decode
				if (!Decoder.CanGenerateAudio() ||  (NumFramesDecoded < NumOutputFrames))
				{
					bIsPlaying = false;
					TrigggerOnDone->TriggerFrame(StartFrame + NumFramesDecoded);
				}

				if (bNeedsUpmix)
				{
					// TODO: attenuate by -3 dB?
					// copy to Left & Right output buffers
					FMemory::Memcpy(FinalOutputLeft, PostSrcBufferPtr, sizeof(float) * NumOutputFrames);
					FMemory::Memcpy(FinalOutputRight, PostSrcBufferPtr, sizeof(float) * NumOutputFrames);
				}
				else if (bNeedsDeinterleave)
				{
					for (int32 i = 0; i < NumOutputFrames; ++i)
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
		}

	private:
		const FOperatorSettings OperatorSettings;

		// i/o
		FTriggerReadRef TrigIn;
		FWaveAssetReadRef Wave;
		FFloatReadRef PitchShiftCents;

		FAudioBufferWriteRef AudioBufferL;
		FAudioBufferWriteRef AudioBufferR;
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

		FTriggerReadRef TriggerPlay = InputDataRefs.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("TrigIn"), InParams.OperatorSettings);
		FWaveAssetReadRef Wave = InputDataRefs.GetDataReadReferenceOrConstruct<FWaveAsset>(TEXT("Wave"));
		FFloatReadRef PitchShiftCents = InputDataRefs.GetDataReadReferenceOrConstruct<float>(TEXT("PitchShiftCents"));
		
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
			, PitchShiftCents
			);
	}

	FVertexInterface FWavePlayerNode::DeclareVertexInterface()
	{
		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<FWaveAsset>(TEXT("Wave"), LOCTEXT("WaveTooltip", "The Wave to be decoded")),
				TInputDataVertexModel<FTrigger>(TEXT("TrigIn"), LOCTEXT("TrigInTooltip", "Trigger the playing of the input wave.")),
				TInputDataVertexModel<float>(TEXT("PitchShiftCents"), LOCTEXT("PitchShiftCentsTooltip", "Pitch Shift in cents."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("AudioLeft"), LOCTEXT("AudioTooltip", "The output audio")),
				TOutputDataVertexModel<FAudioBuffer>(TEXT("AudioRight"), LOCTEXT("AudioTooltip", "The output audio")),
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
