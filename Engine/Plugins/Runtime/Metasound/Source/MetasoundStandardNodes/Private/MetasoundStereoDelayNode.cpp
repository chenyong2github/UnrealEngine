// Copyright Epic Games, Inc. All Rights Reserved.

#include "Internationalization/Text.h"
#include "MetasoundEnumRegistrationMacro.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundTime.h"
#include "MetasoundAudioBuffer.h"
#include "DSP/Delay.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundFacade.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_StereoDelayNode"

namespace Metasound
{
	namespace StereoDelay
	{
		static const TCHAR* InParamNameAudioInputLeft = TEXT("In Left");
		static const TCHAR* InParamNameAudioInputRight = TEXT("In Right");
		static const TCHAR* InParamNameDelayMode = TEXT("Delay Mode");
		static const TCHAR* InParamNameDelayTime = TEXT("Delay Time");
		static const TCHAR* InParamNameDelayRatio = TEXT("Delay Ratio");
		static const TCHAR* InParamNameDryLevel = TEXT("Dry Level");
		static const TCHAR* InParamNameWetLevel = TEXT("Wet Level");
		static const TCHAR* InParamNameFeedbackAmount = TEXT("Feedback");
		static const TCHAR* OutParamNameAudioLeft = TEXT("Out Left");
		static const TCHAR* OutParamNameAudioRight = TEXT("Out Right");

		static float MaxDelaySeconds = 5.0f;
	}

	enum class EStereoDelayMode
	{
		Normal = 0,
		Cross,
		PingPong,
	};

	DECLARE_METASOUND_ENUM(EStereoDelayMode, EStereoDelayMode::Normal, METASOUNDSTANDARDNODES_API,
		FEnumStereoDelayMode, FEnumStereoDelayModeInfo, FStereoDelayModeReadRef, FEnumStereoDelayModeWriteRef);

	DEFINE_METASOUND_ENUM_BEGIN(EStereoDelayMode, FEnumStereoDelayMode, "StereoDelayMode")
		DEFINE_METASOUND_ENUM_ENTRY(EStereoDelayMode::Normal, "StereoDelayModeNormalDescription", "Normal", "StereoDelayModeNormalDescriptionTT", "Left input mixes with left delay output and feeds to left output."),
		DEFINE_METASOUND_ENUM_ENTRY(EStereoDelayMode::Cross, "StereoDelayModeCrossDescription", "Cross", "StereoDelayModeCrossDescriptionTT", "Left input mixes with right delay output and feeds to right output."),
		DEFINE_METASOUND_ENUM_ENTRY(EStereoDelayMode::PingPong, "StereoDelayModePingPongDescription", "Ping Pong", "StereoDelayModePingPongDescriptionTT", "Left input mixes with left delay output and feeds to right output."),
		DEFINE_METASOUND_ENUM_END()

	class FStereoDelayOperator : public TExecutableOperator<FStereoDelayOperator>
	{
	public:

		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FStereoDelayOperator(const FOperatorSettings& InSettings, 
			const FAudioBufferReadRef& InLeftAudioInput, 
			const FAudioBufferReadRef& InRightAudioInput,
			const FStereoDelayModeReadRef& InStereoDelayMode,
			const FTimeReadRef& InDelayTime,
			const FFloatReadRef& InDelayRatio,
			const FFloatReadRef& InDryLevel, 
			const FFloatReadRef& InWetLevel, 
			const FFloatReadRef& InFeedback);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		float GetInputDelayTimeMsecClamped() const;
		float GetInputDelayRatioClamped() const;

		// The input audio buffer
		FAudioBufferReadRef LeftAudioInput;
		FAudioBufferReadRef RightAudioInput;

		// Which stereo delay mode to render the audio delay with
		FStereoDelayModeReadRef StereoDelayMode;

		// The amount of delay time
		FTimeReadRef DelayTime;

		// The stereo delay ratio
		FFloatReadRef DelayRatio;

		// The the dry level
		FFloatReadRef DryLevel;

		// The the wet level
		FFloatReadRef WetLevel;

		// The feedback amount
		FFloatReadRef Feedback;

		// The audio output
		FAudioBufferWriteRef LeftAudioOutput;
		FAudioBufferWriteRef RightAudioOutput;

		// The internal delay buffer
		Audio::FDelay LeftDelayBuffer;
		Audio::FDelay RightDelayBuffer;

		// The current delay time and delay ratio
		float PrevDelayTimeMsec;
		float PrevDelayRatio;
	};

	FStereoDelayOperator::FStereoDelayOperator(const FOperatorSettings& InSettings, 
		const FAudioBufferReadRef& InLeftAudioInput, 
		const FAudioBufferReadRef& InRightAudioInput,
		const FStereoDelayModeReadRef& InStereoDelayMode,
		const FTimeReadRef& InDelayTime,
		const FFloatReadRef& InDelayRatio,
		const FFloatReadRef& InDryLevel,
		const FFloatReadRef& InWetLevel, 
		const FFloatReadRef& InFeedback)

		: LeftAudioInput(InLeftAudioInput)
		, RightAudioInput(InRightAudioInput)
		, StereoDelayMode(InStereoDelayMode)
		, DelayTime(InDelayTime)
		, DelayRatio(InDelayRatio)
		, DryLevel(InDryLevel)
		, WetLevel(InWetLevel)
		, Feedback(InFeedback)
		, LeftAudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
		, RightAudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
		, PrevDelayTimeMsec(GetInputDelayTimeMsecClamped())
		, PrevDelayRatio(GetInputDelayRatioClamped())
	{
		LeftDelayBuffer.Init(InSettings.GetSampleRate(), StereoDelay::MaxDelaySeconds);
		LeftDelayBuffer.SetDelayMsec(PrevDelayTimeMsec * (1.0f + PrevDelayRatio));
		RightDelayBuffer.Init(InSettings.GetSampleRate(), StereoDelay::MaxDelaySeconds);
		RightDelayBuffer.SetDelayMsec(PrevDelayTimeMsec * (1.0f - PrevDelayRatio));
	}

	FDataReferenceCollection FStereoDelayOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(StereoDelay::InParamNameAudioInputLeft, FAudioBufferReadRef(LeftAudioInput));
		InputDataReferences.AddDataReadReference(StereoDelay::InParamNameAudioInputRight, FAudioBufferReadRef(RightAudioInput));
		InputDataReferences.AddDataReadReference(StereoDelay::InParamNameDelayMode, FStereoDelayModeReadRef(StereoDelayMode));
		InputDataReferences.AddDataReadReference(StereoDelay::InParamNameDelayTime, FTimeReadRef(DelayTime));
		InputDataReferences.AddDataReadReference(StereoDelay::InParamNameDelayRatio, FFloatReadRef(DelayRatio));
		InputDataReferences.AddDataReadReference(StereoDelay::InParamNameDryLevel, FFloatReadRef(DryLevel));
		InputDataReferences.AddDataReadReference(StereoDelay::InParamNameWetLevel, FFloatReadRef(WetLevel));
		InputDataReferences.AddDataReadReference(StereoDelay::InParamNameFeedbackAmount, FFloatReadRef(Feedback));

		return InputDataReferences;
	}

	FDataReferenceCollection FStereoDelayOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(StereoDelay::OutParamNameAudioLeft, FAudioBufferReadRef(LeftAudioOutput));
		OutputDataReferences.AddDataReadReference(StereoDelay::OutParamNameAudioRight, FAudioBufferReadRef(RightAudioOutput));
		return OutputDataReferences;
	}

	float FStereoDelayOperator::GetInputDelayTimeMsecClamped() const
	{
		// Clamp the delay time to the max delay allowed
		return 1000.0f * FMath::Clamp((float)DelayTime->GetSeconds(), 0.0f, StereoDelay::MaxDelaySeconds);
	}

	float FStereoDelayOperator::GetInputDelayRatioClamped() const
	{
		return FMath::Clamp(*DelayRatio, -1.0f, 1.0f);
	}

	void FStereoDelayOperator::Execute()
	{
		// Get clamped delay time
		float CurrentInputDelayTime = GetInputDelayTimeMsecClamped();
		float CurrentDelayRatio = GetInputDelayRatioClamped();

		// Check to see if our delay amount has changed
		if (!FMath::IsNearlyEqual(PrevDelayTimeMsec, CurrentInputDelayTime) || !FMath::IsNearlyEqual(PrevDelayRatio, CurrentDelayRatio))
		{
			PrevDelayTimeMsec = CurrentInputDelayTime;
			PrevDelayRatio = CurrentDelayRatio;
			LeftDelayBuffer.SetEasedDelayMsec(PrevDelayTimeMsec * (1.0f + PrevDelayRatio));
			RightDelayBuffer.SetEasedDelayMsec(PrevDelayTimeMsec * (1.0f - PrevDelayRatio));
		}

		const float* LeftInput = LeftAudioInput->GetData();
		const float* RightInput = RightAudioInput->GetData();

		float* LeftOutput = LeftAudioOutput->GetData();
		float* RightOutput = RightAudioOutput->GetData();

		int32 NumFrames = LeftAudioInput->Num();

		// Clamp the feedback amount to make sure it's bounded. Clamp to a number slightly less than 1.0
		float FeedbackAmount = FMath::Clamp(*Feedback, 0.0f, 1.0f - SMALL_NUMBER);
		float CurrentDryLevel = FMath::Clamp(*DryLevel, 0.0f, 1.0f);
		float CurrentWetLevel = FMath::Clamp(*WetLevel, 0.0f, 1.0f);

		if (FMath::IsNearlyZero(FeedbackAmount))
		{
			// if pingpong
			switch (*StereoDelayMode)
			{
				// Normal feeds left to left and right to right
				case EStereoDelayMode::Normal:
				{
					for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
					{
						LeftOutput[FrameIndex] = CurrentWetLevel * LeftDelayBuffer.ProcessAudioSample(LeftInput[FrameIndex]) + CurrentDryLevel * LeftInput[FrameIndex];
						RightOutput[FrameIndex] = CurrentWetLevel * RightDelayBuffer.ProcessAudioSample(RightInput[FrameIndex]) + CurrentDryLevel * RightInput[FrameIndex];
					}
				}
				break;

				// No-feedback Cross and ping-pong feeds right input to left and left input to right
				case EStereoDelayMode::Cross:
				case EStereoDelayMode::PingPong:
				{
					for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
					{
						// Ping pong feeds right to left and left to right
						LeftOutput[FrameIndex] = CurrentWetLevel * LeftDelayBuffer.ProcessAudioSample(RightInput[FrameIndex]) + CurrentDryLevel * LeftInput[FrameIndex];
						RightOutput[FrameIndex] = CurrentWetLevel * RightDelayBuffer.ProcessAudioSample(LeftInput[FrameIndex]) + CurrentDryLevel * RightInput[FrameIndex];
					}
				}
				break;
			}
		}
		else
		{
			// TODO: support different delay cross-modes via enum, currently default to pingpong
			switch (*StereoDelayMode)
			{
				case EStereoDelayMode::Normal:
				{
						
					for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
					{
						float LeftDelayIn = LeftInput[FrameIndex] + FeedbackAmount * LeftDelayBuffer.Read();
						float RightDelayIn = RightInput[FrameIndex] + FeedbackAmount * RightDelayBuffer.Read();

						LeftOutput[FrameIndex] = CurrentWetLevel * LeftDelayBuffer.ProcessAudioSample(LeftDelayIn) + CurrentDryLevel * LeftInput[FrameIndex];
						RightOutput[FrameIndex] = CurrentWetLevel * RightDelayBuffer.ProcessAudioSample(RightDelayIn) + CurrentDryLevel * RightInput[FrameIndex];
					}
				}
				break;

				case EStereoDelayMode::Cross:
				{
					for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
					{
						float LeftDelayIn = RightInput[FrameIndex] + FeedbackAmount * LeftDelayBuffer.Read();
						float RightDelayIn = LeftInput[FrameIndex] + FeedbackAmount * RightDelayBuffer.Read();

						LeftOutput[FrameIndex] = CurrentWetLevel * LeftDelayBuffer.ProcessAudioSample(LeftDelayIn) + CurrentDryLevel * LeftInput[FrameIndex];
						RightOutput[FrameIndex] = CurrentWetLevel * RightDelayBuffer.ProcessAudioSample(RightDelayIn) + CurrentDryLevel * RightInput[FrameIndex];
					}
				}
				break;

				case EStereoDelayMode::PingPong:
				{
					for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
					{
						float LeftDelayIn = RightInput[FrameIndex] + FeedbackAmount * RightDelayBuffer.Read();
						float RightDelayIn = LeftInput[FrameIndex] + FeedbackAmount * LeftDelayBuffer.Read();

						LeftOutput[FrameIndex] = CurrentWetLevel * LeftDelayBuffer.ProcessAudioSample(LeftDelayIn) + CurrentDryLevel * LeftInput[FrameIndex];
						RightOutput[FrameIndex] = CurrentWetLevel * RightDelayBuffer.ProcessAudioSample(RightDelayIn) + CurrentDryLevel * RightInput[FrameIndex];
					}
				}
				break;
			}
		}
	}

	const FVertexInterface& FStereoDelayOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(StereoDelay::InParamNameAudioInputLeft, METASOUND_LOCTEXT("LeftAudioInputTooltip", "Left channel audio input.")),
				TInputDataVertexModel<FAudioBuffer>(StereoDelay::InParamNameAudioInputRight, METASOUND_LOCTEXT("RightAudioInputTooltip", "Right channel audio input.")),
				TInputDataVertexModel<FEnumStereoDelayMode>(StereoDelay::InParamNameDelayMode, METASOUND_LOCTEXT("DelayModeTooltip", "Delay mode.")),
				TInputDataVertexModel<FTime>(StereoDelay::InParamNameDelayTime, METASOUND_LOCTEXT("DelayTimeTooltip", "The amount of time to delay the audio."), 1.0f),
				TInputDataVertexModel<float>(StereoDelay::InParamNameDelayRatio, METASOUND_LOCTEXT("DelayRatioTooltip", "Delay spread for left and right channels. Allows left and right channels to have differential delay amounts. Useful for stereo channel decorrelation"), 0.0f),
				TInputDataVertexModel<float>(StereoDelay::InParamNameDryLevel, METASOUND_LOCTEXT("DryLevelTooltip", "The dry level of the delay."), 0.0f),
				TInputDataVertexModel<float>(StereoDelay::InParamNameWetLevel, METASOUND_LOCTEXT("WetLevelTooltip", "The wet level of the delay."), 1.0f),
				TInputDataVertexModel<float>(StereoDelay::InParamNameFeedbackAmount, METASOUND_LOCTEXT("FeedbackTooltip", "Feedback amount."), 0.0f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(StereoDelay::OutParamNameAudioLeft, METASOUND_LOCTEXT("LeftDelayOutputTooltip", "Left channel audio output.")),
				TOutputDataVertexModel<FAudioBuffer>(StereoDelay::OutParamNameAudioRight, METASOUND_LOCTEXT("RightDelayOutputTooltip", "Right channel audio output."))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FStereoDelayOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { StandardNodes::Namespace, TEXT("Stereo Delay"), StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = METASOUND_LOCTEXT("Metasound_StereoDelayDisplayName", "Stereo Delay");
			Info.Description = METASOUND_LOCTEXT("Metasound_StereoDelayNodeDescription", "Delays a stereo audio buffer by the specified amount.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();
			Info.CategoryHierarchy.Emplace(NodeCategories::Delays);
			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FStereoDelayOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FAudioBufferReadRef LeftAudioIn = InputCollection.GetDataReadReferenceOrConstruct<FAudioBuffer>(StereoDelay::InParamNameAudioInputLeft, InParams.OperatorSettings);
		FAudioBufferReadRef RightAudioIn = InputCollection.GetDataReadReferenceOrConstruct<FAudioBuffer>(StereoDelay::InParamNameAudioInputRight, InParams.OperatorSettings);
		FStereoDelayModeReadRef StereoDelayMode = InputCollection.GetDataReadReferenceOrConstruct<FEnumStereoDelayMode>(StereoDelay::InParamNameDelayMode);
		FTimeReadRef DelayTime = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<FTime>(InputInterface, StereoDelay::InParamNameDelayTime, InParams.OperatorSettings);
		FFloatReadRef DelayRatio = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, StereoDelay::InParamNameDelayRatio, InParams.OperatorSettings);
		FFloatReadRef DryLevel = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, StereoDelay::InParamNameDryLevel, InParams.OperatorSettings);
		FFloatReadRef WetLevel = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, StereoDelay::InParamNameWetLevel, InParams.OperatorSettings);
		FFloatReadRef Feedback = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, StereoDelay::InParamNameFeedbackAmount, InParams.OperatorSettings);

		return MakeUnique<FStereoDelayOperator>(InParams.OperatorSettings, LeftAudioIn, RightAudioIn, StereoDelayMode, DelayTime, DelayRatio, DryLevel, WetLevel, Feedback);
	}


	class FStereoDelayNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FStereoDelayNode(const FNodeInitData& InitData)
			: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FStereoDelayOperator>())
		{
		}
	};

	METASOUND_REGISTER_NODE(FStereoDelayNode)
}

#undef LOCTEXT_NAMESPACE //MetasoundStandardNodes_DelayNode
