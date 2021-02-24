// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundDelayNode.h"

#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundTime.h"
#include "MetasoundAudioBuffer.h"
#include "DSP/Delay.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace Delay
	{
		static const TCHAR* InParamNameAudioInput = TEXT("In");
		static const TCHAR* InParamNameDelayTime = TEXT("Delay Time");
		static const TCHAR* InParamNameDryLevel = TEXT("Dry Level");
		static const TCHAR* InParamNameWetLevel = TEXT("Wet Level");
		static const TCHAR* InParamNameFeedbackAmount = TEXT("Feedback");
		static const TCHAR* OutParamNameAudio = TEXT("Out");

		// TODO: make this a static vertex
		static float MaxDelaySeconds = 5.0f;
	}

	class FDelayOperator : public TExecutableOperator<FDelayOperator>
	{
	public:

		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FDelayOperator(const FOperatorSettings& InSettings, 
			const FAudioBufferReadRef& InAudioInput, 
			const FTimeReadRef& InDelayTime, 
			const FFloatReadRef& InDryLevel, 
			const FFloatReadRef& InWetLevel, 
			const FFloatReadRef& InFeedback);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		float GetInputDelayTimeMsec() const;

		// The input audio buffer
		FAudioBufferReadRef AudioInput;

		// The amount of delay time
		FTimeReadRef DelayTime;

		// The the dry level
		FFloatReadRef DryLevel;

		// The the wet level
		FFloatReadRef WetLevel;

		// The feedback amount
		FFloatReadRef Feedback;

		// The audio output
		FAudioBufferWriteRef AudioOutput;

		// The internal delay buffer
		Audio::FDelay DelayBuffer;

		// The current delay time
		float PrevDelayTimeMsec;

		// Feedback sample
		float FeedbackSample;
	};

	FDelayOperator::FDelayOperator(const FOperatorSettings& InSettings, 
		const FAudioBufferReadRef& InAudioInput, 
		const FTimeReadRef& InDelayTime,
		const FFloatReadRef& InDryLevel, 
		const FFloatReadRef& InWetLevel, 
		const FFloatReadRef& InFeedback)

		: AudioInput(InAudioInput)
		, DelayTime(InDelayTime)
		, DryLevel(InDryLevel)
		, WetLevel(InWetLevel)
		, Feedback(InFeedback)
		, AudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
		, PrevDelayTimeMsec(GetInputDelayTimeMsec())
		, FeedbackSample(0.0f)
	{
		DelayBuffer.Init(InSettings.GetSampleRate(), Delay::MaxDelaySeconds);
		DelayBuffer.SetDelayMsec(PrevDelayTimeMsec);
	}

	FDataReferenceCollection FDelayOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(Delay::InParamNameAudioInput, FAudioBufferReadRef(AudioInput));
		InputDataReferences.AddDataReadReference(Delay::InParamNameDelayTime, FTimeReadRef(DelayTime));
		InputDataReferences.AddDataReadReference(Delay::InParamNameDryLevel, FFloatReadRef(DryLevel));
		InputDataReferences.AddDataReadReference(Delay::InParamNameWetLevel, FFloatReadRef(WetLevel));
		InputDataReferences.AddDataReadReference(Delay::InParamNameFeedbackAmount, FFloatReadRef(Feedback));

		return InputDataReferences;
	}

	FDataReferenceCollection FDelayOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(Delay::OutParamNameAudio, FAudioBufferReadRef(AudioOutput));
		return OutputDataReferences;
	}

	float FDelayOperator::GetInputDelayTimeMsec() const
	{
		// Clamp the delay time to the max delay allowed
		return 1000.0f * FMath::Clamp((float)DelayTime->GetSeconds(), 0.0f, Delay::MaxDelaySeconds);
	}

	void FDelayOperator::Execute()
	{
		// Get clamped delay time
		float CurrentInputDelayTime = GetInputDelayTimeMsec();

		// Check to see if our delay amount has changed
		if (!FMath::IsNearlyEqual(PrevDelayTimeMsec, CurrentInputDelayTime))
		{
			PrevDelayTimeMsec = CurrentInputDelayTime;
			DelayBuffer.SetEasedDelayMsec(PrevDelayTimeMsec);
		}

		const float* InputAudio = AudioInput->GetData();

		float* OutputAudio = AudioOutput->GetData();
		int32 NumFrames = AudioInput->Num();

		// Clamp the feedback amount to make sure it's bounded. Clamp to a number slightly less than 1.0
		float FeedbackAmount = FMath::Clamp(*Feedback, 0.0f, 1.0f - SMALL_NUMBER);
		float CurrentDryLevel = FMath::Clamp(*DryLevel, 0.0f, 1.0f);
		float CurrentWetLevel = FMath::Clamp(*WetLevel, 0.0f, 1.0f);

		if (FMath::IsNearlyZero(FeedbackAmount))
		{
			FeedbackSample = 0.0f;

			for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
			{
				OutputAudio[FrameIndex] = CurrentWetLevel * DelayBuffer.ProcessAudioSample(InputAudio[FrameIndex]) + CurrentDryLevel * OutputAudio[FrameIndex];
			}
		}
		else
		{
			// There is some amount of feedback so we do the feedback mixing
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
			{
				OutputAudio[FrameIndex] = CurrentWetLevel * DelayBuffer.ProcessAudioSample(InputAudio[FrameIndex] + FeedbackSample * FeedbackAmount) + CurrentDryLevel * OutputAudio[FrameIndex];
				FeedbackSample = OutputAudio[FrameIndex];
			}
		}
	}

	const FVertexInterface& FDelayOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(Delay::InParamNameAudioInput, LOCTEXT("AudioInputTooltip", "Audio input.")),
				TInputDataVertexModel<FTime>(Delay::InParamNameDelayTime, LOCTEXT("DelayTimeTooltip", "The amount of time to delay the audio."), 1.0f),
				TInputDataVertexModel<float>(Delay::InParamNameDryLevel, LOCTEXT("DryLevelTooltip", "The dry level of the delay."), 0.0f),
				TInputDataVertexModel<float>(Delay::InParamNameWetLevel, LOCTEXT("FeedbackTooltip", "The wet level of the delay."), 1.0f),
				TInputDataVertexModel<float>(Delay::InParamNameFeedbackAmount, LOCTEXT("FeedbackTooltip", "Feedback amount."), 0.0f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(Delay::OutParamNameAudio, LOCTEXT("DelayOutputTooltip", "Audio output."))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FDelayOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Delay"), Metasound::StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_DelayDisplayName", "Delay");
			Info.Description = LOCTEXT("Metasound_DelayNodeDescription", "Delays an audio buffer by the specified amount.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FDelayOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FDelayNode& DelayNode = static_cast<const FDelayNode&>(InParams.Node);
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FAudioBufferReadRef AudioIn = InputCollection.GetDataReadReferenceOrConstruct<FAudioBuffer>(Delay::InParamNameAudioInput, InParams.OperatorSettings);
		FTimeReadRef DelayTime = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<FTime, float>(InputInterface, Delay::InParamNameDelayTime);
		FFloatReadRef DryLevel = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, Delay::InParamNameDryLevel);
		FFloatReadRef WetLevel = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, Delay::InParamNameWetLevel);
		FFloatReadRef Feedback = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, Delay::InParamNameFeedbackAmount);

		return MakeUnique<FDelayOperator>(InParams.OperatorSettings, AudioIn, DelayTime, DryLevel, WetLevel, Feedback);
	}

	FDelayNode::FDelayNode(const FNodeInitData& InitData)
		: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FDelayOperator>())
	{
	}

	METASOUND_REGISTER_NODE(FDelayNode)
}

#undef LOCTEXT_NAMESPACE
