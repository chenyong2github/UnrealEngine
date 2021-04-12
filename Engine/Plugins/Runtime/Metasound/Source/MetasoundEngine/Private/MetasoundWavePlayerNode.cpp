// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioResampler.h"
#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "MetasoundBuildError.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundLog.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundWave.h"
#include "MetasoundTrigger.h"
#include "MetasoundEngineNodesNames.h"

#define LOCTEXT_NAMESPACE "MetasoundWaveNode"

namespace Metasound
{
	namespace WavePlayerVertexNames
	{
		static const TCHAR* InputTriggerPlayName = TEXT("Play");
		static const TCHAR* InputTriggerStopName = TEXT("Stop");
		static const TCHAR* InputWaveAssetName = TEXT("Wave Asset");
		static const TCHAR* InputStartTimeName = TEXT("Start Time");
		static const TCHAR* InputPitchShiftName = TEXT("Pitch Shift");
		static const TCHAR* InputLoopName = TEXT("Loop");
		static const TCHAR* InputLoopStartName = TEXT("Loop Start");
		static const TCHAR* InputLoopDurationName = TEXT("Loop Duration");
		//static const TCHAR* InputStartTimeAsPercentName = TEXT("Start/Loop Times As Percent");

		static const TCHAR* OutputTriggerOnPlayName = TEXT("On Play");
		static const TCHAR* OutputTriggerOnDoneName = TEXT("On Finished");
		static const TCHAR* OutputTriggerOnNearlyDoneName = TEXT("On Nearly Finished");
		static const TCHAR* OutputTriggerOnLoopedName = TEXT("On Looped");
		static const TCHAR* OutputTriggerOnCuePointName = TEXT("On Cue Point");
		static const TCHAR* OutputCuePointIDName = TEXT("Cue Point ID");
		static const TCHAR* OutputCuePointLabelName = TEXT("Cue Point Label");
		static const TCHAR* OutputLoopPercentName = TEXT("Loop Percent");
		static const TCHAR* OutputPlaybackLocationName = TEXT("Playback Location");
		static const TCHAR* OutputAudioLeftName = TEXT("Out Left");
		static const TCHAR* OutputAudioRightName = TEXT("Out Right");

		static FText InputTriggerPlayTT = LOCTEXT("PlayTT", "Play the wave player.");
		static FText InputTriggerStopTT = LOCTEXT("StopTT", "Stop the wave player.");
		static FText InputWaveAssetTT = LOCTEXT("WaveTT", "The wave asset to be real-time decoded.");
		static FText InputStartTimeTT = LOCTEXT("StartTimeTT", "Time into the wave asset to start (seek) the wave asset. For real-time decoding, the wave asset must be set to seekable!)");
		static FText InputPitchShiftTT = LOCTEXT("PitchShiftTT", "The pitch shift to use for the wave asset in semitones.");
		static FText InputLoopTT = LOCTEXT("LoopTT", "Whether or not to loop between the start and specified end times.");
		static FText InputLoopStartTT = LOCTEXT("LoopStartTT", "When to start the loop. Will be a percentage if \"Start Time As Percent\" is true.");
		static FText InputLoopDurationTT = LOCTEXT("LoopDurationTT", "The duration of the loop when wave player is enabled for looping. A value of -1.0 will loop the whole wave asset.");
		//static FText InputStartTimeAsPercentTT = LOCTEXT("StartTimeAsPercentTT", "Whether to treat the start time and loop start times as a percentage of total wave duration vs seconds (default).");

		static FText OutputTriggerOnPlayTT = LOCTEXT("OnPlayTT", "Triggers when Play is triggered.");
		static FText OutputTriggerOnDoneTT = LOCTEXT("OnDoneTT", "Triggers when the wave played has finished playing.");
		static FText OutputTriggerOnNearlyDoneTT = LOCTEXT("OnNearlyDoneTT", "Triggers when the wave played has almost finished playing (the block before it finishes). Allows time for logic to trigger different variations to play seamlessly.");
		static FText OutputTriggerOnLoopedTT = LOCTEXT("OnLoopedTT", "Triggers when the wave player has looped.");
		static FText OutputTriggerOnCuePointTT = LOCTEXT("OnCuePointTT", "Triggers when a wave cue point was hit during playback.");
		static FText OutputCuePointIDTT = LOCTEXT("CuePointIDTT", "The cue point ID that was triggered.");
		static FText OutputCuePointLabelTT = LOCTEXT("CuePointLabelTT", "The cue point label that was triggered (if there was a label parsed in the imported .wav file).");
		static FText OutputLoopPercentTT = LOCTEXT("LoopPercentTT", "Returns the current loop percent if looping is enabled.");
		static FText OutputPlaybackLocationTT = LOCTEXT("PlaybackLocationTT", "Returns the absolute position of the wave playback as a precentage of wave duration.");
		static FText OutputAudioLeftNameTT = LOCTEXT("AudioLeftTT", "The left channel audio output. Mono wave assets will be upmixed to dual stereo.");
		static FText OutputAudioRightNameTT = LOCTEXT("AudioRightTT", "The right channel audio output. Mono wave assets will be upmixed to dual stereo.");

	}

	class FWavePlayerNode : public FNode
	{
		class FOperatorFactory : public IOperatorFactory
		{
			virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;
		};

	public:
		static FVertexInterface DeclareVertexInterface();
		static const FNodeClassMetadata& GetNodeInfo();

		FWavePlayerNode(const FString& InName, const FGuid& InInstanceID);
		FWavePlayerNode(const FNodeInitData& InInitData);
		virtual ~FWavePlayerNode() = default;

		virtual FOperatorFactorySharedRef GetDefaultOperatorFactory() const override;
		virtual const FVertexInterface& GetVertexInterface() const override;
		virtual bool SetVertexInterface(const FVertexInterface& InInterface) override;
		virtual bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const override;

	private:
		FOperatorFactorySharedRef Factory;
		FVertexInterface Interface;
	};

	struct FWavePlayerOpArgs
	{
		FOperatorSettings Settings;
		FTriggerReadRef PlayTrigger;
		FTriggerReadRef StopTrigger;
		FWaveAssetReadRef WaveAsset;
		FTimeReadRef StartTime;
		FFloatReadRef PitchShift;
		FBoolReadRef bLoop;
		FTimeReadRef LoopStartTime;
		FTimeReadRef LoopDurationSeconds;
		//FBoolReadRef bStartTimeAsPercentage;
	};

	class FWavePlayerOperator : public TExecutableOperator<FWavePlayerOperator>
	{	
	public:
		static constexpr float MaxPitchShiftOctaves = 6.0f;

		FWavePlayerOperator(const FWavePlayerOpArgs& InArgs)
			: OperatorSettings(InArgs.Settings)
			, PlayTrigger(InArgs.PlayTrigger)
			, StopTrigger(InArgs.StopTrigger)
			, WaveAsset(InArgs.WaveAsset)
			, StartTime(InArgs.StartTime)
			//, bStartTimeAsPercentage(InArgs.bStartTimeAsPercentage)
			, PitchShift(InArgs.PitchShift)
			, bLoop(InArgs.bLoop)
			, LoopStartTime(InArgs.LoopStartTime)
			, LoopDurationSeconds(InArgs.LoopDurationSeconds)
			, TriggerOnPlay(FTriggerWriteRef::CreateNew(InArgs.Settings))
			, TriggerOnDone(FTriggerWriteRef::CreateNew(InArgs.Settings))
			, TriggerOnNearlyDone(FTriggerWriteRef::CreateNew(InArgs.Settings))
			, TriggerOnLooped(FTriggerWriteRef::CreateNew(InArgs.Settings))
			, TriggerOnCuePoint(FTriggerWriteRef::CreateNew(InArgs.Settings))
			, CuePointID(FInt32WriteRef::CreateNew(0))
			, CuePointLabel(FStringWriteRef::CreateNew(TEXT("")))
			, LoopPercent(FFloatWriteRef::CreateNew(0.0f))
			, PlaybackLocation(FFloatWriteRef::CreateNew(0.0f))
			, AudioBufferL(FAudioBufferWriteRef::CreateNew(InArgs.Settings))
			, AudioBufferR(FAudioBufferWriteRef::CreateNew(InArgs.Settings))
			, OutputSampleRate(InArgs.Settings.GetSampleRate())
			, OutputBlockSizeInFrames(InArgs.Settings.GetNumFramesPerBlock())
		{
			check(OutputSampleRate);
			check(AudioBufferL->Num() == OutputBlockSizeInFrames && AudioBufferR->Num() == OutputBlockSizeInFrames);
		}

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace WavePlayerVertexNames;

			FDataReferenceCollection InputDataReferences;
			InputDataReferences.AddDataReadReference(InputTriggerPlayName, PlayTrigger);
			InputDataReferences.AddDataReadReference(InputTriggerStopName, StopTrigger);
			InputDataReferences.AddDataReadReference(InputWaveAssetName, WaveAsset);
			InputDataReferences.AddDataReadReference(InputStartTimeName, StartTime);
			InputDataReferences.AddDataReadReference(InputPitchShiftName, PitchShift);
			InputDataReferences.AddDataReadReference(InputLoopName, bLoop);
			InputDataReferences.AddDataReadReference(InputLoopStartName, LoopStartTime);
			InputDataReferences.AddDataReadReference(InputLoopDurationName, LoopDurationSeconds);
			//InputDataReferences.AddDataReadReference(InputStartTimeAsPercentName, bStartTimeAsPercentage);
			return InputDataReferences;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace WavePlayerVertexNames;

			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(OutputTriggerOnPlayName, TriggerOnPlay);
			OutputDataReferences.AddDataReadReference(OutputTriggerOnDoneName, TriggerOnDone);
			OutputDataReferences.AddDataReadReference(OutputTriggerOnNearlyDoneName, TriggerOnNearlyDone);
			OutputDataReferences.AddDataReadReference(OutputTriggerOnLoopedName, TriggerOnLooped);
			OutputDataReferences.AddDataReadReference(OutputTriggerOnCuePointName, TriggerOnCuePoint);
			OutputDataReferences.AddDataReadReference(OutputCuePointIDName, CuePointID);
			OutputDataReferences.AddDataReadReference(OutputCuePointLabelName, CuePointLabel);
			OutputDataReferences.AddDataReadReference(OutputLoopPercentName, LoopPercent);
			OutputDataReferences.AddDataReadReference(OutputPlaybackLocationName, PlaybackLocation);
			OutputDataReferences.AddDataReadReference(OutputAudioLeftName, AudioBufferL);
			OutputDataReferences.AddDataReadReference(OutputAudioRightName, AudioBufferR);
			return OutputDataReferences;
		}

		float UpdateAndGetLoopStartTime()
		{
			float LoopSeekTime = 0.0f;
// 			if (*bStartTimeAsPercentage)
// 			{
// 				float LoopStartTimeClamped = FMath::Clamp((float)LoopStartTime->GetSeconds(), 0.0f, 1.0f);
// 				LoopSeekTime = LoopStartTimeClamped * SoundAssetDurationSeconds;
// 			}
// 			else
			{
				LoopSeekTime = FMath::Clamp((float)LoopStartTime->GetSeconds(), 0.0f, SoundAssetDurationSeconds);
			}

			LoopStartFrame = SoundAssetSampleRate * LoopSeekTime;
			FramesToConsumePlayBeforeLooping = LoopStartFrame;

			if (!bLooped && PlaybackStartFrame > LoopStartFrame)
			{
				FramesToConsumePlayBeforeLooping += (SoundAssetNumFrames - PlaybackStartFrame);
			}

			return LoopSeekTime;
		}

		void Execute()
		{
			TriggerOnPlay->AdvanceBlock();
			TriggerOnDone->AdvanceBlock();
			TriggerOnNearlyDone->AdvanceBlock();
			TriggerOnCuePoint->AdvanceBlock();
			TriggerOnLooped->AdvanceBlock();


			// zero output buffers
			FMemory::Memzero(AudioBufferL->GetData(), OutputBlockSizeInFrames * sizeof(float));
			FMemory::Memzero(AudioBufferR->GetData(), OutputBlockSizeInFrames * sizeof(float));

			// Parse triggers and render audio
			int32 PlayTrigIndex = 0;
			int32 NextPlayFrame = 0;
			const int32 NumPlayTrigs = PlayTrigger->NumTriggeredInBlock();

			int32 StopTrigIndex = 0;
			int32 NextStopFrame = 0;
			const int32 NumStopTrigs = StopTrigger->NumTriggeredInBlock();

			int32 CurrAudioFrame = 0;
			int32 NextAudioFrame = 0;

			while (NextAudioFrame < (OutputBlockSizeInFrames -1))
			{
				const int32 NoTrigger = (OutputBlockSizeInFrames << 1);

				// get the next Play and Stop indices
				// (play)
				if (PlayTrigIndex < NumPlayTrigs)
				{
					NextPlayFrame = (*PlayTrigger)[PlayTrigIndex];
				}
				else
				{
					NextPlayFrame = NoTrigger;
				}

				// (stop)
				if (StopTrigIndex < NumStopTrigs)
				{
					NextStopFrame = (*StopTrigger)[StopTrigIndex];
				}
				else
				{
					NextStopFrame = NoTrigger;
				}

				// determine the next audio frame we are going to render up to
				NextAudioFrame = FMath::Min(NextPlayFrame, NextStopFrame);

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
				if (CurrAudioFrame == NextPlayFrame)
				{
					if (*StartTime > 0.0f)
					{
						ExecuteSeekRequest();
					}
					TriggerOnPlay->TriggerFrame(CurrAudioFrame);

					StartPlaying();
					++PlayTrigIndex;
				}

				if (CurrAudioFrame == NextStopFrame)
				{
					bIsPlaying = false;
					TriggerOnDone->TriggerFrame(CurrAudioFrame);
					++StopTrigIndex;
				}
			}
		}

		void StartPlaying()
		{
			if (IsWaveValid())
			{
				// Copy the wave asset off on init in case the user changes it while we're playing it.
				// We'll only check for new wave assets when the current one finishes for sample accurate concantenation
				CurrentWaveAsset = *WaveAsset;
				SoundAssetSampleRate = CurrentWaveAsset->GetSampleRate();
				SoundAssetDurationSeconds = CurrentWaveAsset->GetDuration();
				SoundAssetNumFrames = CurrentWaveAsset->GetNumFrames();
				CurrentSoundWaveName = CurrentWaveAsset->GetFName();

				PlaybackStartFrame = FMath::Clamp((float)StartTime->GetSeconds(), 0.0f, SoundAssetDurationSeconds) / SoundAssetSampleRate;

				float StartTimeSeconds = 0.0f;
				const FSoundWaveProxyPtr& WaveProxy = CurrentWaveAsset.GetSoundWaveProxy();
// 				if (*bStartTimeAsPercentage)
// 				{
// 
// 					float Percentage = FMath::Clamp((float)StartTime->GetSeconds(), 0.0f, 1.0f);
// 					StartTimeSeconds = Percentage * WaveProxy->GetDuration();
// 				}
// 				else
				{
					StartTimeSeconds = FMath::Clamp((float)StartTime->GetSeconds(), 0.0f, WaveProxy->GetDuration());
				}

				PlaybackStartFrame = (uint32)(StartTimeSeconds * SoundAssetSampleRate);
				UpdateAndGetLoopStartTime();

				StartDecoder(StartTimeSeconds, true /* bLogFailures */);

				if (!bIsPlaying)
				{
					bIsPlaying = Decoder.CanGenerateAudio();
				}
			}

		}

		void ExecuteSeekRequest()
		{
			// TODO: Don't do full decoder reset (as performed below) and instead seek decoder.
			// ex: Decoder.SeekToTime(FMath::Max(0.f, *SeekTimeSeconds));
			if (!bIsPlaying)
			{
				StartPlaying();
			}
		}

		bool IsWaveValidInternal(const FWaveAsset& InWaveAsset, bool bReportToLog = false) const
		{
			if (!InWaveAsset.IsSoundWaveValid())
			{
				if (bReportToLog)
				{
					UE_LOG(LogMetaSound, Error, TEXT("Failed to initialize SoundWave decoder. WavePlayerNode references invalid or missing Wave."));
				}
				return false;
			}

			const FSoundWaveProxyPtr& WaveProxy = InWaveAsset.GetSoundWaveProxy();

			// WavePlayerNode currently only supports mono or stereo inputs
			const int32 NumChannels = WaveProxy->GetNumChannels();
			if (NumChannels > 2)
			{
				if (bReportToLog)
				{
					UE_LOG(LogMetaSound, Error, TEXT("Failed to initialize SoundWave decoder. WavePlayerNode only supports 2 channels max [%s: %d Channels])"), *WaveProxy->GetFullName(), NumChannels);
				}
				return false;
			}

			return true;
		}

		bool IsCurrentWaveValid(bool bReportToLog = false) const
		{
			return IsWaveValidInternal(CurrentWaveAsset, bReportToLog);
		}

		bool IsWaveValid(bool bReportToLog = false) const
		{
			return IsWaveValidInternal(*WaveAsset, bReportToLog);
		}

		float GetPlaybackRateFromPitchShift(float InPitchShift)
		{
			return FMath::Pow(2.0f, InPitchShift / 12.0f);
		}

		bool StartDecoder(float SeekTimeSeconds, bool bLogFailures = false)
		{
			if (IsCurrentWaveValid(bLogFailures))
			{
				const FSoundWaveProxyPtr& WaveProxy = CurrentWaveAsset.GetSoundWaveProxy();

				CuePoints = WaveProxy->GetCuePoints();

				float PitchShiftClamped = FMath::Clamp(*PitchShift, -12.0f * MaxPitchShiftOctaves, 12.0f * MaxPitchShiftOctaves);

				Audio::FSimpleDecoderWrapper::InitParams Params;
				Params.OutputBlockSizeInFrames = OutputBlockSizeInFrames;
				Params.OutputSampleRate = OutputSampleRate;
				Params.MaxPitchShiftMagnitudeAllowedInOctaves = 6.f;
				Params.InitialPitchShiftSemitones = PitchShiftClamped;
				Params.StartTimeSeconds = SeekTimeSeconds;

				bool bRetainExistingSamples = false;

				return Decoder.Initialize(Params, WaveProxy, bRetainExistingSamples);
			}

			return false;
		}

		bool SeekDecoder(float SeekTimeSeconds, bool bLogFailures = false)
		{
			if (IsCurrentWaveValid(bLogFailures))
			{
				const FSoundWaveProxyPtr& WaveProxy = CurrentWaveAsset.GetSoundWaveProxy();

				CuePoints = WaveProxy->GetCuePoints();

				float PitchShiftClamped = FMath::Clamp(*PitchShift, -12.0f * MaxPitchShiftOctaves, 12.0f * MaxPitchShiftOctaves);

				Audio::FSimpleDecoderWrapper::InitParams Params;
				Params.OutputBlockSizeInFrames = OutputBlockSizeInFrames;
				Params.OutputSampleRate = OutputSampleRate;
				Params.MaxPitchShiftMagnitudeAllowedInOctaves = 6.f;
				Params.InitialPitchShiftSemitones = PitchShiftClamped;
				Params.StartTimeSeconds = SeekTimeSeconds;

				bool bRetainExistingSamples = true;

				return Decoder.Initialize(Params, WaveProxy, bRetainExistingSamples);
			}

			return false;
		}


		int32 ExecuteInternal(int32 StartFrame, int32 EndFrame)
		{
			// note: output is hard-coded to stereo (dual-mono)
			float* FinalOutputLeft = AudioBufferL->GetData() + StartFrame;
			float* FinalOutputRight = AudioBufferR->GetData() + StartFrame;
			int32 NumOutputFramesToGenerate = (EndFrame - StartFrame);
			int32 NumFramesGenerated = 0;

			const bool bCanDecodeWave = IsCurrentWaveValid();
			if (bCanDecodeWave)
			{
				ensure(Decoder.CanGenerateAudio());

				const int32 NumInputChannels = CurrentWaveAsset->GetNumChannels();
				const bool bNeedsUpmix = (NumInputChannels == 1);
				const bool bNeedsDeinterleave = !bNeedsUpmix;

				const int32 NumSamplesToGenerate = NumOutputFramesToGenerate * NumInputChannels;
				
				float PitchShiftClamped = FMath::Clamp(*PitchShift, -12.0f * MaxPitchShiftOctaves, 12.0f * MaxPitchShiftOctaves);

				PostSrcBuffer.Reset(NumSamplesToGenerate);
				PostSrcBuffer.AddZeroed(NumSamplesToGenerate);
				float* PostSrcBufferPtr = PostSrcBuffer.GetData();

				// Update looping state which may have changed
				bool bIsLooping = *bLoop;

				// If we're looping we got some extra logic we need to do before we generate audio
				if (*bLoop)
				{
					float LoopDuration = SoundAssetDurationSeconds;
					if (LoopDurationSeconds->GetSeconds() > 0.0f)
					{
						LoopDuration = FMath::Clamp((float)LoopDurationSeconds->GetSeconds(), 0.0f, SoundAssetDurationSeconds);
					}
					
					// Give the loop frame a minimum width
					LoopNumFrames = (uint32)FMath::Max(LoopDuration * SoundAssetSampleRate, 10.0f);

					// We need to look for the case where we are about to loop and we don't want to "overshoot" the loop
					// So we need to check the number output frames to generate against how much is left in our loop
					if (NumTotalDecodedFramesInLoop < LoopNumFrames)
					{
						// The number of frames left in the loop we need to consume from the source file
						int32 NumFramesLeftToConsumeInLoop = (int32)LoopNumFrames - NumTotalDecodedFramesInLoop;

						// Translate that to a generated frames count taking into account the pitch scale and the playback rate.
						// Note this is going to be slightly inaccurate due to pitch interpolation... but it should be super close
						// and w/ loop cross fading implemented, won't matter too much.
						float PlaybackRate = GetPlaybackRateFromPitchShift(PitchShiftClamped);

						float SampleRateRatioWithPitchScale = PlaybackRate * SoundAssetSampleRate / OutputSampleRate;
						int32 NumFramesLeftToGenerateForLoop = SampleRateRatioWithPitchScale * NumFramesLeftToConsumeInLoop;

						// Generate the min of the requested frames to generate and the amount we need to finish the loop
						NumOutputFramesToGenerate = FMath::Min(NumOutputFramesToGenerate, NumFramesLeftToGenerateForLoop);
					}
					else
					{
						NumOutputFramesToGenerate = 1;
					}
				}
				else
				{
					LoopNumFrames = 0;
					NumTotalDecodedFramesInLoop = 0;
				}

				int32 NumFramesConsumed = 0;
				if (NumOutputFramesToGenerate > 0)
				{
					NumFramesGenerated = Decoder.GenerateAudio(PostSrcBufferPtr, NumOutputFramesToGenerate, NumFramesConsumed, 100.0f * PitchShiftClamped, bIsLooping) / NumInputChannels;

					// Update loop and playback state logic based on the number of frames consumed
					FramesConsumedSinceStart += NumFramesConsumed;

					int32 PrevConsumedFrameCount = CurrentConsumedFrameCount;
					CurrentConsumedFrameCount = (CurrentConsumedFrameCount + NumFramesConsumed) % SoundAssetNumFrames;

					*PlaybackLocation = SoundAssetDurationSeconds * ((float)CurrentConsumedFrameCount / SoundAssetNumFrames);

					// Check for any cue trigger events 
					FSoundWaveCuePoint OutCuePoint;
					int32 OutFrameOffset = INDEX_NONE;
					if (GetCuePointForBlock(PrevConsumedFrameCount, CurrentConsumedFrameCount, OutCuePoint, OutFrameOffset))
					{
						// Write the outputs
						*CuePointID = OutCuePoint.CuePointID;
						*CuePointLabel = OutCuePoint.Label;

						// Do the trigger
						TriggerOnCuePoint->TriggerFrame(StartFrame + OutFrameOffset);
					}

					// Check for nearly done trigger logic. If we're not looping and the next render block will likely finish the file.
					if (!bIsLooping && (CurrentConsumedFrameCount + OutputBlockSizeInFrames) >= SoundAssetNumFrames)
					{
						TriggerOnNearlyDone->TriggerFrame(StartFrame + NumFramesGenerated);
					}

				}

				// If we're looping and we've made it past the loop start point and we've not yet looped, check the looping condition
				// Note: we need to handle the case where the "start time" of the player is past the loop start point. In that case
				// we allow the audio file to wrap around and loop from the beginning before checking the loop logic
				if (bIsLooping && (bLooped || FramesConsumedSinceStart >= FramesToConsumePlayBeforeLooping))
				{					
					NumTotalDecodedFramesInLoop += NumFramesConsumed;

					if (NumTotalDecodedFramesInLoop >= LoopNumFrames || !NumOutputFramesToGenerate)
					{
						// We've looped now so we will continue to loop between the loop start and end points
						bLooped = true;

						// Trigger that we looped
						TriggerOnLooped->TriggerFrame(StartFrame + NumFramesGenerated);
					
						// Update the loop start frame based on any recent inputs
						float LoopSeekTime = UpdateAndGetLoopStartTime();

						// Update our current loop frame
						CurrentConsumedFrameCount = LoopStartFrame;

						// Reset our decoded frame counting for sub loop
						NumTotalDecodedFramesInLoop = 0;

						*LoopPercent = 0.0f;

						// Now seek to the loop start frame
						SeekDecoder(LoopSeekTime);
					}
					else
					{
						*LoopPercent = (float)NumTotalDecodedFramesInLoop / LoopNumFrames;
					}
				}
				else
				{
					*LoopPercent = 0.0f;
				}
				
				// handle decoder having completed during it's decode
				const bool bReachedEOF = !Decoder.CanGenerateAudio() || (NumFramesGenerated < NumOutputFramesToGenerate);
				if (bReachedEOF && !bIsLooping)
				{
					// Check the wave file input to see if it has changed... if it has, we want to restart everything up!
					bool bConcatenatedPlayback = false;
					if (WaveAsset->IsSoundWaveValid())
					{
						const FName SoundWaveName = (*WaveAsset)->GetFName();
						if (SoundWaveName != CurrentSoundWaveName)
						{
							StartPlaying();
							bConcatenatedPlayback = true;
						}
					}

					if (!bConcatenatedPlayback)
					{
						bIsPlaying = false;
						TriggerOnDone->TriggerFrame(StartFrame + NumFramesGenerated);
					}
				}

				if (bNeedsUpmix)
				{
					// Dual mono output
					FMemory::Memcpy(FinalOutputLeft, PostSrcBufferPtr, sizeof(float)* NumFramesGenerated);
					FMemory::Memcpy(FinalOutputRight, PostSrcBufferPtr, sizeof(float) * NumFramesGenerated);
				}
				else if (bNeedsDeinterleave)
				{
					for (int32 i = 0; i < NumFramesGenerated; ++i)
					{
						// deinterleave each stereo frame into output buffers
						FinalOutputLeft[i] = PostSrcBufferPtr[(i << 1)];
						FinalOutputRight[i] = PostSrcBufferPtr[(i << 1) + 1];
					}
				}
			}
			else
			{
				FMemory::Memzero(FinalOutputLeft, sizeof(float) * NumOutputFramesToGenerate);
				FMemory::Memzero(FinalOutputRight, sizeof(float) * NumOutputFramesToGenerate);
			}

			return NumFramesGenerated;
		}

	private:

		// Called to get the cue point in the given block. Will return the first cue point if multiple cues are in the same block.
		// Note: we can't currently support sub-block cue triggers due to cue ID and cue label being block-rate params. 
		bool GetCuePointForBlock(int32 InStartFrame, int32 InEndFrame, FSoundWaveCuePoint& OutCuePoint, int32& OutFrameOffset)
		{
			for (FSoundWaveCuePoint& CuePoint : CuePoints)
			{
				if (CuePoint.FramePosition >= InStartFrame && CuePoint.FramePosition < InEndFrame)
				{
					OutCuePoint = CuePoint;
					OutFrameOffset = CuePoint.FramePosition - InStartFrame;
					return true;
				}
			}
			// No cue points in this block
			return false;
		}

		const FOperatorSettings OperatorSettings;

		// i/o
		FTriggerReadRef PlayTrigger;
		FTriggerReadRef StopTrigger;
		FWaveAssetReadRef WaveAsset;
		FTimeReadRef StartTime;
		//FBoolReadRef bStartTimeAsPercentage;
		FFloatReadRef PitchShift;
		FBoolReadRef bLoop;
		FTimeReadRef LoopStartTime;
		FTimeReadRef LoopDurationSeconds;

		FTriggerWriteRef TriggerOnPlay;
		FTriggerWriteRef TriggerOnDone;
		FTriggerWriteRef TriggerOnNearlyDone;
		FTriggerWriteRef TriggerOnLooped;
		FTriggerWriteRef TriggerOnCuePoint;
		FInt32WriteRef CuePointID;
		FStringWriteRef CuePointLabel;
		FFloatWriteRef LoopPercent;
		FFloatWriteRef PlaybackLocation;
		FAudioBufferWriteRef AudioBufferL;
		FAudioBufferWriteRef AudioBufferR;

		// source decode
		Audio::FAlignedFloatBuffer PostSrcBuffer;
		Audio::FSimpleDecoderWrapper Decoder;

		FName CurrentSoundWaveName;
			
		float SoundAssetSampleRate = 0.0f;
		float SoundAssetDurationSeconds = 0.0f;
		uint32 SoundAssetNumFrames = 0;

		const float OutputSampleRate = 0.f;
		const int32 OutputBlockSizeInFrames = 0;
		
		TArray<FSoundWaveCuePoint> CuePoints;
		TArray<FSoundWaveCuePoint> CuePointsInCurrentBock;

		// Data to track decoder
		uint32 PlaybackStartFrame = 0;
		uint32 CurrentConsumedFrameCount = 0;
		uint32 FramesConsumedSinceStart = 0;
		uint32 NumTotalDecodedFramesInLoop = 0;
		uint32 LoopStartFrame = 0;
		uint32 FramesToConsumePlayBeforeLooping = 0;
		uint32 LoopNumFrames = 0;
		bool bLooped = false;
		bool bIsPlaying = false;

		FWaveAsset CurrentWaveAsset;
	};

	TUniquePtr<IOperator> FWavePlayerNode::FOperatorFactory::CreateOperator(
		const FCreateOperatorParams& InParams, 
		FBuildErrorArray& OutErrors) 
	{
		using namespace Audio;
		using namespace WavePlayerVertexNames;

		const FWavePlayerNode& WaveNode = static_cast<const FWavePlayerNode&>(InParams.Node);

		const FDataReferenceCollection& Inputs = InParams.InputDataReferences;

		FWavePlayerOpArgs Args =
		{
			InParams.OperatorSettings,
			Inputs.GetDataReadReferenceOrConstruct<FTrigger>(InputTriggerPlayName, InParams.OperatorSettings),
			Inputs.GetDataReadReferenceOrConstruct<FTrigger>(InputTriggerStopName, InParams.OperatorSettings),
			Inputs.GetDataReadReferenceOrConstruct<FWaveAsset>(InputWaveAssetName),
			Inputs.GetDataReadReferenceOrConstruct<FTime>(InputStartTimeName),
			Inputs.GetDataReadReferenceOrConstruct<float>(InputPitchShiftName),
			Inputs.GetDataReadReferenceOrConstruct<bool>(InputLoopName),
			Inputs.GetDataReadReferenceOrConstruct<FTime>(InputLoopStartName),
			Inputs.GetDataReadReferenceOrConstruct<FTime>(InputLoopDurationName)
			//Inputs.GetDataReadReferenceOrConstruct<bool>(InputStartTimeAsPercentName)
		};

		return MakeUnique<FWavePlayerOperator>(Args);
	}

	FVertexInterface FWavePlayerNode::DeclareVertexInterface()
	{
		using namespace WavePlayerVertexNames;

		return FVertexInterface(
			FInputVertexInterface(
				TInputDataVertexModel<FTrigger>(InputTriggerPlayName, InputTriggerPlayTT),
				TInputDataVertexModel<FTrigger>(InputTriggerStopName, InputTriggerStopTT),
				TInputDataVertexModel<FWaveAsset>(InputWaveAssetName, InputWaveAssetTT),
				TInputDataVertexModel<FTime>(InputStartTimeName, InputStartTimeTT, 0.0f),
				TInputDataVertexModel<float>(InputPitchShiftName, InputPitchShiftTT, 0.0f),
				TInputDataVertexModel<bool>(InputLoopName, InputLoopTT, false),
				TInputDataVertexModel<FTime>(InputLoopStartName, InputLoopStartTT, 0.0f),
				TInputDataVertexModel<FTime>(InputLoopDurationName, InputLoopDurationTT, -1.0f)
				//TInputDataVertexModel<bool>(InputStartTimeAsPercentName, InputStartTimeAsPercentTT, false)
				),
			FOutputVertexInterface(
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnPlayName, OutputTriggerOnPlayTT),
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnDoneName, OutputTriggerOnDoneTT),
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnNearlyDoneName, OutputTriggerOnNearlyDoneTT),
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnLoopedName, OutputTriggerOnLoopedTT),
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnCuePointName, OutputTriggerOnCuePointTT),
				TOutputDataVertexModel<int32>(OutputCuePointIDName, OutputCuePointIDTT),
				TOutputDataVertexModel<FString>(OutputCuePointLabelName, OutputCuePointLabelTT),
				TOutputDataVertexModel<float>(OutputLoopPercentName, OutputAudioRightNameTT),
				TOutputDataVertexModel<float>(OutputPlaybackLocationName, OutputPlaybackLocationTT),
				TOutputDataVertexModel<FAudioBuffer>(OutputAudioLeftName, OutputAudioLeftNameTT),
				TOutputDataVertexModel<FAudioBuffer>(OutputAudioRightName, OutputAudioRightNameTT)
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
			Info.DisplayName = LOCTEXT("Metasound_WavePlayerNodeDisplayName", "Wave Player");
			Info.Description = LOCTEXT("Metasound_WavePlayerNodeDescription", "Plays a wave asset.");
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
