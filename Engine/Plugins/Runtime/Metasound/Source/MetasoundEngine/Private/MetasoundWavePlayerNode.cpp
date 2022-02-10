// Copyright Epic Games, Inc. All Rights Reserved.

#include "DecoderInputFactory.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/ConvertDeinterleave.h"
#include "DSP/MultichannelBuffer.h"
#include "DSP/MultichannelLinearResampler.h"
#include "IAudioCodec.h"
#include "MetasoundBuildError.h"
#include "MetasoundEngineNodesNames.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundLog.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTrace.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"
#include "MetasoundWave.h"
#include "MetasoundWaveProxyReader.h"

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

		static FText InputTriggerPlayTT = METASOUND_LOCTEXT("PlayTT", "Play the wave player.");
		static FText InputTriggerStopTT = METASOUND_LOCTEXT("StopTT", "Stop the wave player.");
		static FText InputWaveAssetTT = METASOUND_LOCTEXT("WaveTT", "The wave asset to be real-time decoded.");
		static FText InputStartTimeTT = METASOUND_LOCTEXT("StartTimeTT", "Time into the wave asset to start (seek) the wave asset. For real-time decoding, the wave asset must be set to seekable!)");
		static FText InputPitchShiftTT = METASOUND_LOCTEXT("PitchShiftTT", "The pitch shift to use for the wave asset in semitones.");
		static FText InputLoopTT = METASOUND_LOCTEXT("LoopTT", "Whether or not to loop between the start and specified end times.");
		static FText InputLoopStartTT = METASOUND_LOCTEXT("LoopStartTT", "When to start the loop.");
		static FText InputLoopDurationTT = METASOUND_LOCTEXT("LoopDurationTT", "The duration of the loop when wave player is enabled for looping. A negative value will loop the whole wave asset.");

		static FText OutputTriggerOnPlayTT = METASOUND_LOCTEXT("OnPlayTT", "Triggers when Play is triggered.");
		static FText OutputTriggerOnDoneTT = METASOUND_LOCTEXT("OnDoneTT", "Triggers when the wave played has finished playing.");
		static FText OutputTriggerOnNearlyDoneTT = METASOUND_LOCTEXT("OnNearlyDoneTT", "Triggers when the wave played has almost finished playing (the block before it finishes). Allows time for logic to trigger different variations to play seamlessly.");
		static FText OutputTriggerOnLoopedTT = METASOUND_LOCTEXT("OnLoopedTT", "Triggers when the wave player has looped.");
		static FText OutputTriggerOnCuePointTT = METASOUND_LOCTEXT("OnCuePointTT", "Triggers when a wave cue point was hit during playback.");
		static FText OutputCuePointIDTT = METASOUND_LOCTEXT("CuePointIDTT", "The cue point ID that was triggered.");
		static FText OutputCuePointLabelTT = METASOUND_LOCTEXT("CuePointLabelTT", "The cue point label that was triggered (if there was a label parsed in the imported .wav file).");
		static FText OutputLoopPercentTT = METASOUND_LOCTEXT("LoopPercentTT", "Returns the current loop percent if looping is enabled.");
		static FText OutputPlaybackLocationTT = METASOUND_LOCTEXT("PlaybackLocationTT", "Returns the absolute position of the wave playback as a precentage of wave duration.");
		static FText OutputAudioLeftNameTT = METASOUND_LOCTEXT("AudioLeftTT", "The left channel audio output. Mono wave assets will be upmixed to dual stereo.");
		static FText OutputAudioRightNameTT = METASOUND_LOCTEXT("AudioRightTT", "The right channel audio output. Mono wave assets will be upmixed to dual stereo.");
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

		FWavePlayerNode(const FVertexName& InName, const FGuid& InInstanceID);
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

	namespace WavePlayerNodePrivate
	{
		int32 GetCuePointFrame(const FSoundWaveCuePoint& InPoint)
		{
			return InPoint.FramePosition;
		}


		/** FSourceBufferState tracks the current frame and loop indices held in 
		 * a circular buffer. It describes how the content of a circular buffer
		 * relates to the frame indices of an FWaveProxyReader 
		 *
		 * FSourceBufferState is tied to the implementation of the FWaveProxyReader
		 * and TCircularAudioBuffer<>, and thus does not serve much purpose outside
		 * of this wave player node.
		 *
		 * However, it does provide a convenient place to perform frame counting
		 * arithmetic that would otherwise make code more difficult to read.
		 */
		struct FSourceBufferState
		{
			FSourceBufferState() = default;

			/** Construct a FSourceBufferState
			 *
			 * @param InStartFrameIndex - The frame index in the wave corresponding to the first frame in the circular buffer.
			 * @param InNumFrames - The number of frames in the circular buffer.
			 * @param bIsLooping - True if the wave player is looping, false if not.
			 * @param InLoopStartFrameIndexInWave - Frame index in the wave corresponding to a loop start.
			 * @param InLoopEndFrameIndexInWave - Frame index in the wave corresponding to a loop end.
			 * @param InEOFFrameIndexInWave - Frame index in the wave corresponding to the end of the file.
			 */
			FSourceBufferState(int32 InStartFrameIndex, int32 InNumFrames, bool bIsLooping, int32 InLoopStartFrameIndexInWave, int32 InLoopEndFrameIndexInWave, int32 InEOFFrameIndexInWave)
			{
				check(InStartFrameIndex >= 0);
				check(InNumFrames >= 0);
				check(InLoopStartFrameIndexInWave >= 0);
				check(InLoopEndFrameIndexInWave >= 0);
				check(InEOFFrameIndexInWave >= 0);

				StartFrameIndex = InStartFrameIndex;
				EndFrameIndex = InStartFrameIndex; // Initialize to starting frame index. Will be adjusted during call to Append()
				EOFFrameIndexInBuffer = InEOFFrameIndexInWave - InStartFrameIndex;
				LoopEndFrameIndexInBuffer = InLoopEndFrameIndexInWave - InStartFrameIndex;
				LoopStartFrameIndexInWave = InLoopStartFrameIndexInWave;
				LoopEndFrameIndexInWave = InLoopEndFrameIndexInWave;
				EOFFrameIndexInWave = InEOFFrameIndexInWave;

				Append(InNumFrames, bIsLooping);
			}

			/** Construct an FSourceBufferState
			 *
			 * @param ProxyReader - The wave proxy reader producing the audio.
			 * @parma InSourceBuffer - The audio buffer holding a range of samples popped from the reader.
			 */
			FSourceBufferState(const FWaveProxyReader& ProxyReader, const Audio::FMultichannelCircularBuffer& InSourceBuffer)
			: FSourceBufferState(ProxyReader.GetFrameIndex(), Audio::GetMultichannelBufferNumFrames(InSourceBuffer), ProxyReader.IsLooping(), ProxyReader.GetLoopStartFrameIndex(), ProxyReader.GetLoopEndFrameIndex(), ProxyReader.GetNumFramesInWave())
			{
			}

			/** Track frames removed from the circular buffer. This generally coincides
			 * with a Pop(...) call to the circular buffer. */
			void Advance(int32 InNumFrames, bool bIsLooping)
			{
				check(InNumFrames >= 0);

				StartFrameIndex += InNumFrames;
				if (bIsLooping)
				{
					StartFrameIndex = WrapLoop(StartFrameIndex);
				}

				EOFFrameIndexInBuffer = EOFFrameIndexInWave - StartFrameIndex;
				LoopEndFrameIndexInBuffer = LoopEndFrameIndexInWave - StartFrameIndex;
			}


			/** Track frames appended to the source buffer. This generally coincides
			 * with a Push(...) call to the circular buffer. */
			void Append(int32 InNumFrames, bool bIsLooping)
			{
				check(InNumFrames >= 0);
				EndFrameIndex += InNumFrames;

				if (bIsLooping)
				{
					EndFrameIndex = WrapLoop(EndFrameIndex);
				}
			}

			/** Update loop frame indices. */
			void SetLoopFrameIndices(int32 InLoopStartFrameIndexInWave, int32 InLoopEndFrameIndexInWave)
			{
				LoopStartFrameIndexInWave = InLoopStartFrameIndexInWave;
				LoopEndFrameIndexInWave = InLoopEndFrameIndexInWave;
				LoopEndFrameIndexInBuffer = LoopEndFrameIndexInWave - StartFrameIndex;
			}

			/** Update loop frame indices. */
			void SetLoopFrameIndices(const FWaveProxyReader& InProxyReader)
			{
				SetLoopFrameIndices(InProxyReader.GetLoopStartFrameIndex(), InProxyReader.GetLoopEndFrameIndex());
			}

			/** Returns the corresponding frame index in the wave which corresponds
			 * to the first frame in the circular buffer. 
			 */
			FORCEINLINE int32 GetStartFrameIndex() const
			{
				return StartFrameIndex;
			}

			/** Returns the corresponding frame index in the wave which corresponds
			 * to the end frame in the circular buffer (non-inclusive).
			 */
			FORCEINLINE int32 GetEndFrameIndex() const
			{
				return EndFrameIndex;
			}

			/** Returns the frame index in the wave where the loop starts */
			FORCEINLINE int32 GetLoopStartFrameIndexInWave() const
			{
				return LoopStartFrameIndexInWave;
			}

			/** Returns the frame index in the wave where the loop end*/
			FORCEINLINE int32 GetLoopEndFrameIndexInWave() const
			{
				return LoopEndFrameIndexInWave;
			}

			/** Returns the end-of-file frame index in the wave. */
			FORCEINLINE int32 GetEOFFrameIndexInWave() const
			{
				return EOFFrameIndexInWave;
			}

			/** Returns the frame index in the circular buffer which represents
			 * the end of file in the wave. */
			FORCEINLINE int32 GetEOFFrameIndexInBuffer() const
			{
				return EOFFrameIndexInBuffer;
			}

			/** Returns the frame index in the circular buffer which represents
			 * the ending loop frame index in the wave. */
			FORCEINLINE int32 GetLoopEndFrameIndexInBuffer() const
			{
				return LoopEndFrameIndexInBuffer;
			}

			/** Returns the ratio of the current frame index divided by the total
			 * number of frames in the wave. */
			FORCEINLINE float GetPlaybackFraction() const
			{
				const float PlaybackFraction = static_cast<float>(StartFrameIndex) / FMath::Max(static_cast<float>(EOFFrameIndexInWave), 1.f);
				return FMath::Max(0.f, PlaybackFraction);
			}

			/** Returns the ratio of the relative position of the current frame 
			 * index to the start loop frame index, divided by the number of frames
			 * in the loop.
			 * This value can be negative if the current frame index is less
			 * than the first loop frame index. 
			 */
			FORCEINLINE float GetLoopFraction() const
			{
				const float LoopNumFrames = static_cast<float>(FMath::Max(1, LoopEndFrameIndexInWave - LoopStartFrameIndexInWave));
				const float LoopRelativeLocation = static_cast<float>(StartFrameIndex - LoopStartFrameIndexInWave);

				return LoopRelativeLocation / LoopNumFrames;
			}

			/** Map a index representing a frame in a wave file to an index representing
			 * a frame in the associated circular buffer. */
			FORCEINLINE int32 MapFrameInWaveToFrameInBuffer(int32 InFrameIndexInWave, bool bIsLooping) const
			{
				if (!bIsLooping || (InFrameIndexInWave >= StartFrameIndex))
				{
					return InFrameIndexInWave - StartFrameIndex;
				}
				else
				{
					const int32 NumFramesFromStartToLoopEnd = LoopEndFrameIndexInWave - StartFrameIndex;
					const int32 NumFramesFromLoopStartToFrameIndex = InFrameIndexInWave - LoopStartFrameIndexInWave;
					return NumFramesFromStartToLoopEnd + NumFramesFromLoopStartToFrameIndex;
				}
			}

		private:

			int32 WrapLoop(int32 InSourceFrameIndex)
			{
				int32 Overshot = InSourceFrameIndex - LoopEndFrameIndexInWave;
				if (Overshot > 0)
				{
					InSourceFrameIndex = LoopStartFrameIndexInWave + Overshot;
				}
				return InSourceFrameIndex;
			}

			int32 StartFrameIndex = INDEX_NONE;
			int32 EndFrameIndex = INDEX_NONE;
			int32 EOFFrameIndexInBuffer = INDEX_NONE;
			int32 LoopEndFrameIndexInBuffer = INDEX_NONE;
			int32 EOFFrameIndexInWave = INDEX_NONE;
			int32 LoopStartFrameIndexInWave = INDEX_NONE;
			int32 LoopEndFrameIndexInWave = INDEX_NONE;
		};

		/** FSourceEvents contains the frame indices of wave events. 
		 * Indices are INDEX_NONE if they are unset. 
		 */
		struct FSourceEvents
		{
			/** Frame index of a loop end. */
			int32 OnLoopFrameIndex = INDEX_NONE;
			/** Frame index of an end of file. */
			int32 OnEOFFrameIndex = INDEX_NONE;
			/** Frame index of a cue points. */
			int32 OnCuePointFrameIndex = INDEX_NONE;
			/** Cue point associated with OnCuePointFrameIndex. */
			const FSoundWaveCuePoint* CuePoint = nullptr;

			/** Clear all frame indices and associated data. */
			void Reset()
			{
				OnLoopFrameIndex = INDEX_NONE;
				OnEOFFrameIndex = INDEX_NONE;
				OnCuePointFrameIndex = INDEX_NONE;
				CuePoint = nullptr;
			}
		};
	}

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
		FTimeReadRef LoopDuration;
	};

	/** MetaSound operator for the wave player node. */
	class FWavePlayerOperator : public TExecutableOperator<FWavePlayerOperator>
	{	
	public:

		// Maximum absolute pitch shift in octaves. 
		static constexpr float MaxAbsPitchShiftInOctaves = 6.0f;
		// Maximum decode size in frames. 
		static constexpr int32 MaxDecodeSizeInFrames = 8192;
		// Number of output audio channels. 
		static constexpr int32 NumOutputChannels = 2;
		// Block size for deinterleaving audio. 
		static constexpr int32 DeinterleaveBlockSizeInFrames = 512;

		FWavePlayerOperator(const FWavePlayerOpArgs& InArgs)
			: OperatorSettings(InArgs.Settings)
			, PlayTrigger(InArgs.PlayTrigger)
			, StopTrigger(InArgs.StopTrigger)
			, WaveAsset(InArgs.WaveAsset)
			, StartTime(InArgs.StartTime)
			, PitchShift(InArgs.PitchShift)
			, bLoop(InArgs.bLoop)
			, LoopStartTime(InArgs.LoopStartTime)
			, LoopDuration(InArgs.LoopDuration)
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
		{
			// Hold on to a view of the output audio. Audio buffers are only writable
			// by this object and will not be reallocated. 
			OutputAudioView.Emplace(AudioBufferL->GetData(), AudioBufferL->Num());
			OutputAudioView.Emplace(AudioBufferR->GetData(), AudioBufferR->Num());

			check(OutputAudioView.Num() == NumOutputChannels);
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
			InputDataReferences.AddDataReadReference(InputLoopDurationName, LoopDuration);
			return InputDataReferences;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace WavePlayerVertexNames;

			FDataReferenceCollection OutputDataReferences;
			OutputDataReferences.AddDataReadReference(OutputTriggerOnPlayName, PlayTrigger);
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


		void Execute()
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FWavePlayerNode::Execute);

			// Advance all triggers owned by this operator. 
			TriggerOnDone->AdvanceBlock();
			TriggerOnNearlyDone->AdvanceBlock();
			TriggerOnCuePoint->AdvanceBlock();
			TriggerOnLooped->AdvanceBlock();

			// Update wave proxy reader with any new looping bounds. 
			if (WaveProxyReader.IsValid())
			{
				WaveProxyReader->SetIsLooping(*bLoop);
				WaveProxyReader->SetLoopStartTime(LoopStartTime->GetSeconds());
				WaveProxyReader->SetLoopDuration(LoopDuration->GetSeconds());
				SourceState.SetLoopFrameIndices(*WaveProxyReader);
			}

			// Update resampler with new frame ratio. 
			if (Resampler.IsValid())
			{
				Resampler->SetFrameRatio(GetFrameRatio(), OperatorSettings.GetNumFramesPerBlock());
			}

			// zero output buffers
			FMemory::Memzero(AudioBufferL->GetData(), OperatorSettings.GetNumFramesPerBlock() * sizeof(float));
			FMemory::Memzero(AudioBufferR->GetData(), OperatorSettings.GetNumFramesPerBlock() * sizeof(float));

			// Performs execution per sub block based on triggers.
			ExecuteSubblocks();

			// Updates output playhead information
			UpdatePlaybackLocation();
		}

	private:

		void ExecuteSubblocks()
		{
			// Parse triggers and render audio
			int32 PlayTrigIndex = 0;
			int32 NextPlayFrame = 0;
			const int32 NumPlayTrigs = PlayTrigger->NumTriggeredInBlock();

			int32 StopTrigIndex = 0;
			int32 NextStopFrame = 0;
			const int32 NumStopTrigs = StopTrigger->NumTriggeredInBlock();

			int32 CurrAudioFrame = 0;
			int32 NextAudioFrame = 0;
			const int32 LastAudioFrame = OperatorSettings.GetNumFramesPerBlock() - 1;
			const int32 NoTrigger = OperatorSettings.GetNumFramesPerBlock() << 1;

			while (NextAudioFrame < LastAudioFrame)
			{
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
					NextAudioFrame = OperatorSettings.GetNumFramesPerBlock();
				}

				// render audio (while loop handles looping audio)
				while (CurrAudioFrame != NextAudioFrame)
				{
					if (bIsPlaying)
					{
						RenderFrameRange(CurrAudioFrame, NextAudioFrame);
					}
					CurrAudioFrame = NextAudioFrame;
				}

				// execute the next trigger
				if (CurrAudioFrame == NextPlayFrame)
				{
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

		void RenderFrameRange(int32 StartFrame, int32 EndFrame)
		{
			using namespace Audio;

			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FWavePlayerNode::RenderFrameRange);

			// Assume this is set to true and checked by outside callers
			check(bIsPlaying);

			const int32 NumFramesToGenerate = EndFrame - StartFrame;
			if (NumFramesToGenerate > 0)
			{
				// Trigger any events that occur within this frame range
				TriggerUpcomingEvents(StartFrame, NumFramesToGenerate, SourceState);

				// Generate audio
				FMultichannelBufferView BufferToGenerate = SliceMultichannelBufferView(OutputAudioView, StartFrame, NumFramesToGenerate);
				GeneratePitchedAudio(BufferToGenerate);

				// Check if the source is empty.
				if (!(*bLoop))
				{
					bIsPlaying = (SourceState.GetStartFrameIndex() <= SourceState.GetEOFFrameIndexInWave());
				}
			}
		}

		void UpdatePlaybackLocation()
		{
			*PlaybackLocation = SourceState.GetPlaybackFraction();

			if (*bLoop)
			{
				*LoopPercent = SourceState.GetLoopFraction();
			}
			else
			{
				*LoopPercent = 0.f;
			}
		}

		float GetPitchShiftClamped() const
		{
			return FMath::Clamp(*PitchShift, -12.0f * MaxAbsPitchShiftInOctaves, 12.0f * MaxAbsPitchShiftInOctaves);
		}

		float GetPitchShiftFrameRatio() const
		{
			return FMath::Pow(2.0f, GetPitchShiftClamped() / 12.0f);
		}

		// Updates the sample rate frame ratio. Used when a new wave proxy reader
		// is created. 
		void UpdateSampleRateFrameRatio() 
		{
			SampleRateFrameRatio = 1.f;

			if (WaveProxyReader.IsValid())
			{
				float SourceSampleRate = WaveProxyReader->GetSampleRate();
				if (SourceSampleRate > 0.f)
				{
					float TargetSampleRate = OperatorSettings.GetSampleRate();
					if (TargetSampleRate > 0.f)
					{
						SampleRateFrameRatio = SourceSampleRate / OperatorSettings.GetSampleRate();
					}
				}
			}
		}

		float GetSampleRateFrameRatio() const
		{
			return SampleRateFrameRatio;
		}

		float GetFrameRatio() const
		{
			return GetSampleRateFrameRatio() * GetPitchShiftFrameRatio();
		}

		float GetMaxPitchShiftFrameRatio() const
		{
			return FMath::Pow(2.0f, MaxAbsPitchShiftInOctaves);
		}

		float GetMaxFrameRatio() const
		{
			return GetSampleRateFrameRatio() * GetMaxPitchShiftFrameRatio();
		}

		// Start playing the current wave by creating a wave proxy reader and
		// recreating the DSP stack.
		void StartPlaying()
		{
			using namespace WavePlayerNodePrivate;
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FWavePlayerNode::StartPlaying);

			// MetasoundWavePlayerNode DSP Stack
			//
			// Legend:
			// 	[ObjectName] - An Object which generates or process audio.
			// 	(BufferName) - A buffer which holds audio.
			//
			// [WaveProxyReader]->(InterleavedBuffer)->[ConvertDeinterleave]->(DeinterleavedBuffer)->(SourceCircularBuffer)->[Resampler]->(AudioOutputView)
			//

			// Copy the wave asset off on init in case the user changes it while we're playing it.
			// We'll only check for new wave assets when the current one finishes for sample accurate concantenation
			CurrentWaveAsset = *WaveAsset;
			FSoundWaveProxyPtr WaveProxy = CurrentWaveAsset.GetSoundWaveProxy();

			bOnNearlyDoneTriggeredForWave = false;
			bIsPlaying = false;

			// Reset dsp stack.
			ResetSourceBufferAndState();
			WaveProxyReader.Reset();
			ConvertDeinterleave.Reset();
			Resampler.Reset();
			SortedCuePoints.Reset();

			if (WaveProxy.IsValid())
			{
				UE_LOG(LogMetaSound, Verbose, TEXT("Starting Sound: '%s'"), *CurrentWaveAsset->GetFName().ToString());

				// Create local sorted copy of cue points.
				SortedCuePoints = WaveProxy->GetCuePoints();
				Algo::SortBy(SortedCuePoints, WavePlayerNodePrivate::GetCuePointFrame);
				
				// Create the wave proxy reader.
				FWaveProxyReader::FSettings WaveReaderSettings;
				WaveReaderSettings.MaxDecodeSizeInFrames = MaxDecodeSizeInFrames;
				WaveReaderSettings.StartTimeInSeconds = StartTime->GetSeconds();
				WaveReaderSettings.LoopStartTimeInSeconds = LoopStartTime->GetSeconds();
				WaveReaderSettings.LoopDurationInSeconds = LoopDuration->GetSeconds(); 
				WaveReaderSettings.bIsLooping = *bLoop;

				WaveProxyReader = FWaveProxyReader::Create(WaveProxy.ToSharedRef(), WaveReaderSettings);

				if (WaveProxyReader.IsValid())
				{
					UpdateSampleRateFrameRatio();
					int32 WaveProxyNumChannels = WaveProxyReader->GetNumChannels();

					if (WaveProxyNumChannels > 0)
					{
						// Create buffer for interleaved audio
						int32 InterleavedBufferNumSamples = WaveProxyNumChannels * DeinterleaveBlockSizeInFrames;
						InterleavedBuffer.Reset(InterleavedBufferNumSamples);
						InterleavedBuffer.AddUninitialized(InterleavedBufferNumSamples);

						NumDeinterleaveChannels = NumOutputChannels;

						// Create algorithm for channel conversion and deinterleave 
						Audio::FConvertDeinterleaveParams ConvertDeinterleaveParams;
						ConvertDeinterleaveParams.NumInputChannels = WaveProxyReader->GetNumChannels();
						ConvertDeinterleaveParams.NumOutputChannels = NumDeinterleaveChannels;
						// Original implementation of MetaSound WavePlayer upmixed 
						// mono using FullVolume. In the future, the mono upmix 
						// method may be exposed as a node input to facilitate 
						// better control.
						ConvertDeinterleaveParams.MonoUpmixMethod = Audio::EChannelMapMonoUpmixMethod::FullVolume;
						ConvertDeinterleave = Audio::IConvertDeinterleave::Create(ConvertDeinterleaveParams);
						Audio::SetMultichannelBufferSize(NumDeinterleaveChannels, DeinterleaveBlockSizeInFrames, DeinterleavedBuffer);

						// Initialize source buffer
						int32 FrameCapacity = DeinterleaveBlockSizeInFrames + FMath::CeilToInt(GetMaxFrameRatio() * OperatorSettings.GetNumFramesPerBlock());
						Audio::SetMultichannelCircularBufferCapacity(NumOutputChannels, FrameCapacity, SourceCircularBuffer);
						SourceState = FSourceBufferState(*WaveProxyReader, SourceCircularBuffer);

						// Create a resampler.
						Resampler = MakeUnique<Audio::FMultichannelLinearResampler>(NumDeinterleaveChannels);
						Resampler->SetFrameRatio(GetFrameRatio(), 0 /* NumFramesToInperolate */);

						// Need to add upmixing if this is not true
						check(NumDeinterleaveChannels == NumOutputChannels);
					}
				}
			}

			// If everything was created successfully, start playing.
			bIsPlaying = WaveProxyReader.IsValid() && ConvertDeinterleave.IsValid() && Resampler.IsValid();
		}

		/** Removes all samples from the source buffer and resets SourceState. */
		void ResetSourceBufferAndState()
		{
			using namespace WavePlayerNodePrivate;
			using namespace Audio;

			SourceState = FSourceBufferState();
			for (TCircularAudioBuffer<float>& ChannelCircularBuffer : SourceCircularBuffer)
			{
				ChannelCircularBuffer.SetNum(0);
			}
		}

		/** Generates audio from the wave proxy reader.
		 *
		 * @param OutBuffer - Buffer to place generated audio.
		 * @param OutSourceState - Source state for tracking state of OutBuffer.
		 */
		void GenerateSourceAudio(Audio::FMultichannelCircularBuffer& OutBuffer, WavePlayerNodePrivate::FSourceBufferState& OutSourceState)
		{
			using namespace WavePlayerNodePrivate;

			if (bIsPlaying)
			{
				const int32 NumExistingFrames = Audio::GetMultichannelBufferNumFrames(OutBuffer);
				const int32 NumSamplesToGenerate = DeinterleaveBlockSizeInFrames * WaveProxyReader->GetNumChannels();
 				check(NumSamplesToGenerate == InterleavedBuffer.Num())

				WaveProxyReader->PopAudio(InterleavedBuffer);
				ConvertDeinterleave->ProcessAudio(InterleavedBuffer, DeinterleavedBuffer);

				for (int32 ChannelIndex = 0; ChannelIndex < NumDeinterleaveChannels; ChannelIndex++)
				{
					OutBuffer[ChannelIndex].Push(DeinterleavedBuffer[ChannelIndex]);
				}
				OutSourceState.Append(DeinterleaveBlockSizeInFrames, *bLoop);
			}
			else
			{
				OutSourceState = FSourceBufferState();
			}
		}


		/** Updates frame indices of events if the event occurs in the source within
		 * the frame range. The frame range begins with the start frame in InSourceState
		 * and continues for InNumSourceFrames in the source buffer. 
		 *
		 * @param InSourceState - Description of the current state of the source buffer.
		 * @param InNumSourceFrames - Number of frames to inspect.
		 * @param OutEvents - Event structure to fill out. 
		 */
		void MapSourceEventsIfInRange(const WavePlayerNodePrivate::FSourceBufferState& InSourceState, int32 InNumSourceFrames, WavePlayerNodePrivate::FSourceEvents& OutEvents)
		{
			OutEvents.Reset();

			// Loop end
			if (*bLoop && FMath::IsWithin(SourceState.GetLoopEndFrameIndexInBuffer(), 0, InNumSourceFrames))
			{
				OutEvents.OnLoopFrameIndex = FMath::RoundToInt(Resampler->MapInputFrameToOutputFrame(SourceState.GetLoopEndFrameIndexInBuffer()));
			}

			// End of file
			if (FMath::IsWithin(SourceState.GetEOFFrameIndexInBuffer(), 0, InNumSourceFrames))
			{
				OutEvents.OnEOFFrameIndex = FMath::RoundToInt(Resampler->MapInputFrameToOutputFrame(SourceState.GetEOFFrameIndexInBuffer()));
			}

			// Map Cue point. Since only one can be mapped, map the first one found.
			// The first cue point found has the best chance of being rendered.
			int32 SearchStartFrameIndexInWave = InSourceState.GetStartFrameIndex();
			int32 SearchEndFrameIndexInWave = SearchStartFrameIndexInWave + InNumSourceFrames;

			const bool bFramesCrossLoopBoundary = *bLoop && (OutEvents.OnLoopFrameIndex != INDEX_NONE);
			if (bFramesCrossLoopBoundary)
			{
				SearchEndFrameIndexInWave = SearchStartFrameIndexInWave + SourceState.GetLoopEndFrameIndexInBuffer();
			}

			OutEvents.CuePoint = FindCuePoint(SearchStartFrameIndexInWave, SearchEndFrameIndexInWave);

			if (bFramesCrossLoopBoundary)
			{
				SearchStartFrameIndexInWave = SourceState.GetLoopStartFrameIndexInWave();
				int32 RemainingFrames = InNumSourceFrames - SourceState.GetLoopEndFrameIndexInBuffer();
				SearchEndFrameIndexInWave = RemainingFrames;

				// Only override OutEvents.CuePoint if one exists in this subsection
				// of the buffer.
				if (const FSoundWaveCuePoint* CuePoint = FindCuePoint(SearchStartFrameIndexInWave, SearchEndFrameIndexInWave))
				{
					if (nullptr == OutEvents.CuePoint)
					{
						OutEvents.CuePoint = CuePoint;
					}
					else 
					{
						UE_LOG(LogMetaSound, Verbose, TEXT("Skipping cue point \"%s\" at frame %d due to multiple cue points in same render block"), *CuePoint->Label, CuePoint->FramePosition);
					}
				}
			}

			if (nullptr != OutEvents.CuePoint)
			{
				int32 CuePointFrameInBuffer = SourceState.MapFrameInWaveToFrameInBuffer(OutEvents.CuePoint->FramePosition, *bLoop);
				OutEvents.OnCuePointFrameIndex = FMath::RoundToInt(Resampler->MapInputFrameToOutputFrame(CuePointFrameInBuffer));
			}
		}

		// Search for cue points in frame range. Return the first cue point in the frame range.
		const FSoundWaveCuePoint* FindCuePoint(int32 InStartFrameInWave, int32 InEndFrameInWave)
		{
			int32 LowerBoundIndex = Algo::LowerBoundBy(SortedCuePoints, InStartFrameInWave, WavePlayerNodePrivate::GetCuePointFrame);
			int32 UpperBoundIndex = Algo::LowerBoundBy(SortedCuePoints, InEndFrameInWave, WavePlayerNodePrivate::GetCuePointFrame);

			if (LowerBoundIndex < UpperBoundIndex)
			{
				// Inform about skipped cue points. 
				for (int32 i = LowerBoundIndex + 1; i < UpperBoundIndex; i++)
				{
					const FSoundWaveCuePoint& CuePoint = SortedCuePoints[i];

					UE_LOG(LogMetaSound, Verbose, TEXT("Skipping cue point \"%s\" at frame %d due to multiple cue points in same render block"), *CuePoint.Label, CuePoint.FramePosition);
				}
				return &SortedCuePoints[LowerBoundIndex];
			}

			return nullptr;
		}

		// Check the expected output positions for various sample accurate events
		// before resampling. 
		//
		// Note: The resampler can only accurately map samples *before* processing 
		// audio because processing audio modifies the internal state of the resampler.
		void TriggerUpcomingEvents(int32 InOperatorStartFrame, int32 InNumFrames, const WavePlayerNodePrivate::FSourceBufferState& InState)
		{
			WavePlayerNodePrivate::FSourceEvents Events;

			// Check extra frames to hit the 
			const int32 NumOutputFramesToCheck = (2 * OperatorSettings.GetNumFramesPerBlock() + 1) - InOperatorStartFrame;
			const int32 NumSourceFramesToCheck = FMath::CeilToInt(Resampler->MapOutputFrameToInputFrame(NumOutputFramesToCheck));

			// Selectively map events in the source buffer to frame indices in the
			// resampled output buffer. 
			MapSourceEventsIfInRange(SourceState, NumSourceFramesToCheck, Events);

			// Check whether to trigger loops based on actual number of output frames 
			if (*bLoop)
			{
				if (FMath::IsWithin(Events.OnLoopFrameIndex, 0, InNumFrames))
				{
					TriggerOnLooped->TriggerFrame(InOperatorStartFrame + Events.OnLoopFrameIndex);
				}
			}
			else
			{
				const int32 IsNearlyDoneStartFrameIndex = OperatorSettings.GetNumFramesPerBlock() - InOperatorStartFrame;
				const int32 IsNearlyDoneEndFrameIndex = IsNearlyDoneStartFrameIndex + OperatorSettings.GetNumFramesPerBlock();

				if (FMath::IsWithin(Events.OnEOFFrameIndex, 0, InNumFrames))
				{
					TriggerOnDone->TriggerFrame(InOperatorStartFrame + Events.OnEOFFrameIndex);
				}
				else if (FMath::IsWithin(Events.OnEOFFrameIndex, IsNearlyDoneStartFrameIndex, IsNearlyDoneEndFrameIndex))
				{
					// Protect against triggering OnNearlyDone multiple times
					// in the scenario where significant pitch shift changes drastically
					// alter the predicted OnDone frame between render blocks.
					if (!bOnNearlyDoneTriggeredForWave)
					{
						TriggerOnNearlyDone->TriggerFrame(InOperatorStartFrame + Events.OnEOFFrameIndex - OperatorSettings.GetNumFramesPerBlock());
						bOnNearlyDoneTriggeredForWave = true;
					}
				}
			}

			if (nullptr != Events.CuePoint)
			{
				if (FMath::IsWithin(Events.OnCuePointFrameIndex, 0, InNumFrames))
				{
					if (!TriggerOnCuePoint->IsTriggeredInBlock())
					{
						*CuePointID = Events.CuePoint->CuePointID;
						*CuePointLabel = Events.CuePoint->Label;
						TriggerOnCuePoint->TriggerFrame(InOperatorStartFrame + Events.OnCuePointFrameIndex);
					}
					else
					{
						UE_LOG(LogMetaSound, Verbose, TEXT("Skipping cue point \"%s\" at frame %d due to multiple cue points in same render block"), *Events.CuePoint->Label, Events.CuePoint->FramePosition);
					}
				}
			}
		}


		void GeneratePitchedAudio(Audio::FMultichannelBufferView& OutBuffer)
		{
			using namespace Audio;

			// Outside callers should ensure that bIsPlaying is true if calling this function.
			check(bIsPlaying);

			int32 NumFramesRequested = GetMultichannelBufferNumFrames(OutBuffer);
			int32 NumSourceFramesAvailable = GetMultichannelBufferNumFrames(SourceCircularBuffer);

			while (NumFramesRequested > 0)
			{
				// Determine how many frames are needed to produce the output.
				int32 NumSourceFramesNeeded = Resampler->GetNumInputFramesNeededToProduceOutputFrames(NumFramesRequested + 1);
				if (NumSourceFramesNeeded > NumSourceFramesAvailable)
				{
					// Generate more source audio, but still may not be enough to produce all requested frames.
					GenerateSourceAudio(SourceCircularBuffer, SourceState);
				}
				NumSourceFramesAvailable = GetMultichannelBufferNumFrames(SourceCircularBuffer);

				// Resample frames. 
				int32 NumFramesProduced = Resampler->ProcessAndConsumeAudio(SourceCircularBuffer, OutBuffer);
				if (NumFramesProduced < 1)
				{
					UE_LOG(LogMetaSound, Error, TEXT("Aborting currently playing metasound wave %s. Failed to produce any resampled audio frames with %d input frames and a frame ratio of %f."), *CurrentWaveAsset->GetFName().ToString(), NumSourceFramesAvailable, GetFrameRatio());
					bIsPlaying = false;
					break;
				}

				// Update sample counters
				int32 NewNumSourceFramesAvailable = GetMultichannelBufferNumFrames(SourceCircularBuffer);
				int32 NumSourceFramesConsumed = NumSourceFramesAvailable - NewNumSourceFramesAvailable;
				NumSourceFramesAvailable = NewNumSourceFramesAvailable;
				NumFramesRequested -= NumFramesProduced;

				SourceState.Advance(NumSourceFramesConsumed, *bLoop);

				// Shift buffer if there are more samples to create
				if (NumFramesRequested > 0)
				{
					ShiftMultichannelBufferView(NumFramesProduced, OutBuffer);
				}
			}
		}

		const FOperatorSettings OperatorSettings;

		// i/o
		FTriggerReadRef PlayTrigger;
		FTriggerReadRef StopTrigger;
		FWaveAssetReadRef WaveAsset;
		FTimeReadRef StartTime;
		FFloatReadRef PitchShift;
		FBoolReadRef bLoop;
		FTimeReadRef LoopStartTime;
		FTimeReadRef LoopDuration;

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

		TUniquePtr<FWaveProxyReader> WaveProxyReader;
		TUniquePtr<Audio::IConvertDeinterleave> ConvertDeinterleave;
		TUniquePtr<Audio::FMultichannelLinearResampler> Resampler;

		FWaveAsset CurrentWaveAsset;
		TArray<FSoundWaveCuePoint> SortedCuePoints;
		Audio::FAlignedFloatBuffer InterleavedBuffer;
		Audio::FMultichannelBuffer DeinterleavedBuffer;
		Audio::FMultichannelCircularBuffer SourceCircularBuffer;
		Audio::FMultichannelBufferView OutputAudioView;

		WavePlayerNodePrivate::FSourceBufferState SourceState;
		float SampleRateFrameRatio = 1.f;
		int32 NumDeinterleaveChannels;
		bool bOnNearlyDoneTriggeredForWave = false;
		bool bIsPlaying = false;
		
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
				),
			FOutputVertexInterface(
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnPlayName, OutputTriggerOnPlayTT),
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnDoneName, OutputTriggerOnDoneTT),
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnNearlyDoneName, OutputTriggerOnNearlyDoneTT),
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnLoopedName, OutputTriggerOnLoopedTT),
				TOutputDataVertexModel<FTrigger>(OutputTriggerOnCuePointName, OutputTriggerOnCuePointTT),
				TOutputDataVertexModel<int32>(OutputCuePointIDName, OutputCuePointIDTT),
				TOutputDataVertexModel<FString>(OutputCuePointLabelName, OutputCuePointLabelTT),
				TOutputDataVertexModel<float>(OutputLoopPercentName, OutputLoopPercentTT),
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
			Info.DisplayName = METASOUND_LOCTEXT("Metasound_WavePlayerNodeDisplayName", "Wave Player");
			Info.Description = METASOUND_LOCTEXT("Metasound_WavePlayerNodeDescription", "Plays a wave asset.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FWavePlayerNode::FWavePlayerNode(const FVertexName& InName, const FGuid& InInstanceID)
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


