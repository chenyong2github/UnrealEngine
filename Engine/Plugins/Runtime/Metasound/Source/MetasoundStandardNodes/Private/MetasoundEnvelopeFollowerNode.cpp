// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEnvelopeFollowerNode.h"

#include "Algo/MaxElement.h"
#include "Internationalization/Text.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundEnvelopeFollowerTypes.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundEnumRegistrationMacro.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundTime.h"
#include "MetasoundVertex.h"
#include "MetasoundParamHelper.h"
#include "DSP/EnvelopeFollower.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes_EnvelopeFollower"

namespace Metasound
{
	namespace EnvelopeFollower
	{
		static const TCHAR* InParamNameAudioInput = TEXT("In");
		static const TCHAR* InParamNameAttackTime = TEXT("Attack Time");
		static const TCHAR* InParamNameReleaseTime = TEXT("Release Time");
		static const TCHAR* InParamNameFollowMode = TEXT("Peak Mode");
		static const TCHAR* OutParamNameEnvelope = TEXT("Envelope");
	}
	METASOUND_PARAM(OutputAudioEnvelope, "Audio Envelope", "The output envelope value of the audio signal (audio rate).");

	class FEnvelopeFollowerOperator : public TExecutableOperator<FEnvelopeFollowerOperator>
	{
	public:

		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FEnvelopeFollowerOperator(const FOperatorSettings& InSettings,
			const FAudioBufferReadRef& InAudioInput,
			const FTimeReadRef& InAttackTime,
			const FTimeReadRef& InReleaseTime,
			const FEnvelopePeakModeReadRef& InEnvelopeMode);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		// The input audio buffer
		FAudioBufferReadRef AudioInput;

		// The amount of attack time
		FTimeReadRef AttackTimeInput;

		// The amount of release time
		FTimeReadRef ReleaseTimeInput;

		// The Envelope-Following method
		FEnvelopePeakModeReadRef FollowModeInput;

		// The envelope outputs
		FFloatWriteRef EnvelopeFloatOutput;
		FAudioBufferWriteRef EnvelopeAudioOutput;

		// The envelope follower DSP object
		Audio::FEnvelopeFollower EnvelopeFollower;

		double PrevAttackTime = 0.0;
		double PrevReleaseTime = 0.0;
		EEnvelopePeakMode PrevFollowMode = EEnvelopePeakMode::Peak;
	};

	FEnvelopeFollowerOperator::FEnvelopeFollowerOperator(const FOperatorSettings& InSettings,
		const FAudioBufferReadRef& InAudioInput,
		const FTimeReadRef& InAttackTime,
		const FTimeReadRef& InReleaseTime,
		const FEnvelopePeakModeReadRef& InEnvelopeMode)
		: AudioInput(InAudioInput)
		, AttackTimeInput(InAttackTime)
		, ReleaseTimeInput(InReleaseTime)
		, FollowModeInput(InEnvelopeMode)
		, EnvelopeFloatOutput(FFloatWriteRef::CreateNew())
		, EnvelopeAudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
	{
		PrevAttackTime = FMath::Max(FTime::ToMilliseconds(*AttackTimeInput), 0.0);
		PrevReleaseTime = FMath::Max(FTime::ToMilliseconds(*ReleaseTimeInput), 0.0);

		Audio::FEnvelopeFollowerInitParams EnvelopeParamsInitParams;
	
		EnvelopeParamsInitParams.SampleRate = InSettings.GetSampleRate();
		EnvelopeParamsInitParams.NumChannels = 1;
		EnvelopeParamsInitParams.AttackTimeMsec = PrevAttackTime;
		EnvelopeParamsInitParams.ReleaseTimeMsec = PrevReleaseTime;

		EnvelopeFollower.Init(EnvelopeParamsInitParams);
	}

	FDataReferenceCollection FEnvelopeFollowerOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(EnvelopeFollower::InParamNameAudioInput, AudioInput);
		InputDataReferences.AddDataReadReference(EnvelopeFollower::InParamNameAttackTime, AttackTimeInput);
		InputDataReferences.AddDataReadReference(EnvelopeFollower::InParamNameReleaseTime, ReleaseTimeInput);
		InputDataReferences.AddDataReadReference(EnvelopeFollower::InParamNameFollowMode, FollowModeInput);

		return InputDataReferences;
	}

	FDataReferenceCollection FEnvelopeFollowerOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(EnvelopeFollower::OutParamNameEnvelope, EnvelopeFloatOutput);
		OutputDataReferences.AddDataReadReference(METASOUND_GET_PARAM_NAME(OutputAudioEnvelope), EnvelopeAudioOutput);
		return OutputDataReferences;
	}

	void FEnvelopeFollowerOperator::Execute()
	{
		// Check for any input changes
		double CurrentAttackTime = FMath::Max(FTime::ToMilliseconds(*AttackTimeInput), 0.0);
		if (!FMath::IsNearlyEqual(CurrentAttackTime, PrevAttackTime))
		{
			PrevAttackTime = CurrentAttackTime;
			EnvelopeFollower.SetAttackTime(CurrentAttackTime);
		}

		double CurrentReleaseTime = FMath::Max(FTime::ToMilliseconds(*ReleaseTimeInput), 0.0);
		if (!FMath::IsNearlyEqual(CurrentReleaseTime, PrevReleaseTime))
		{
			PrevReleaseTime = CurrentReleaseTime;
			EnvelopeFollower.SetReleaseTime(CurrentReleaseTime);
		}

		if (PrevFollowMode != *FollowModeInput)
		{
			PrevFollowMode = *FollowModeInput;
			switch (PrevFollowMode)
			{
			case EEnvelopePeakMode::MeanSquared:
			default:
				EnvelopeFollower.SetMode(Audio::EPeakMode::Type::MeanSquared);
				break;
			case EEnvelopePeakMode::RootMeanSquared:
				EnvelopeFollower.SetMode(Audio::EPeakMode::Type::RootMeanSquared);
				break;
			case EEnvelopePeakMode::Peak:
				EnvelopeFollower.SetMode(Audio::EPeakMode::Type::Peak);
				break;
			}
		}

		// Process the audio through the envelope follower
		EnvelopeFollower.ProcessAudio(AudioInput->GetData(), AudioInput->Num(), EnvelopeAudioOutput->GetData());
		if (const float* MaxElement = Algo::MaxElement(EnvelopeFollower.GetEnvelopeValues()))
		{
			*EnvelopeFloatOutput = *MaxElement;
		}
		else
		{
			*EnvelopeFloatOutput = 0.f;
		}
	}

	const FVertexInterface& FEnvelopeFollowerOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(EnvelopeFollower::InParamNameAudioInput, METASOUND_LOCTEXT("AudioInputToolTT", "Audio input.")),
				TInputDataVertexModel<FTime>(EnvelopeFollower::InParamNameAttackTime, METASOUND_LOCTEXT("AttackTimeTT", "The attack time of the envelope follower."), 0.01f),
				TInputDataVertexModel<FTime>(EnvelopeFollower::InParamNameReleaseTime, METASOUND_LOCTEXT("ReleaseTimeTT", "The release time of the envelope follower."), 0.1f),
				TInputDataVertexModel<FEnumEnvelopePeakMode>(EnvelopeFollower::InParamNameFollowMode, METASOUND_LOCTEXT("FollowModeTT", "The following-method of the envelope follower."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<float>(EnvelopeFollower::OutParamNameEnvelope, METASOUND_LOCTEXT("EnvelopeFollowerOutputTT", "The output envelope value of the audio signal.")),
				TOutputDataVertexModel<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_TT(OutputAudioEnvelope))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FEnvelopeFollowerOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { StandardNodes::Namespace, TEXT("Envelope Follower"), TEXT("") };
			Info.MajorVersion = 1;
			Info.MinorVersion = 3;
			Info.DisplayName = METASOUND_LOCTEXT("Metasound_EnvelopeFollowerDisplayName", "Envelope Follower");
			Info.Description = METASOUND_LOCTEXT("Metasound_EnvelopeFollowerDescription", "Outputs an envelope from an input audio signal.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FEnvelopeFollowerOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FEnvelopeFollowerNode& EnvelopeFollowerNode = static_cast<const FEnvelopeFollowerNode&>(InParams.Node);
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FAudioBufferReadRef AudioIn = InputCollection.GetDataReadReferenceOrConstruct<FAudioBuffer>(EnvelopeFollower::InParamNameAudioInput, InParams.OperatorSettings);
		FTimeReadRef AttackTime = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<FTime>(InputInterface, EnvelopeFollower::InParamNameAttackTime, InParams.OperatorSettings);
		FTimeReadRef ReleaseTime = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<FTime>(InputInterface, EnvelopeFollower::InParamNameReleaseTime, InParams.OperatorSettings);
		FEnvelopePeakModeReadRef EnvelopeModeIn = InputCollection.GetDataReadReferenceOrConstruct<FEnumEnvelopePeakMode>(EnvelopeFollower::InParamNameFollowMode);


		return MakeUnique<FEnvelopeFollowerOperator>(InParams.OperatorSettings, AudioIn, AttackTime, ReleaseTime, EnvelopeModeIn);
	}

	FEnvelopeFollowerNode::FEnvelopeFollowerNode(const FNodeInitData& InitData)
		: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FEnvelopeFollowerOperator>())
	{
	}

	METASOUND_REGISTER_NODE(FEnvelopeFollowerNode)
}

#undef LOCTEXT_NAMESPACE
