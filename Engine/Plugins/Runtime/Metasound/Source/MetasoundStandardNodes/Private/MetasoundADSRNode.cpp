// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundADSRNode.h"

#include "DSP/Envelope.h"
#include "Internationalization/Text.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundBop.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundTime.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	METASOUND_REGISTER_NODE(FADSRNode)

	class FADSROperator : public TExecutableOperator<FADSROperator>
	{
		public:
			struct FADSRReferences
			{
				FFloatTimeReadRef Attack;
				FFloatTimeReadRef Decay;
				FFloatReadRef SustainLevel;
				FFloatTimeReadRef Release;
			};

			static const FNodeInfo& GetNodeInfo();
			static FVertexInterface DeclareVertexInterface();
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			FADSROperator(const FOperatorSettings& InSettings, const FBopReadRef& InTriggerAttack, const FBopReadRef& InTriggerRelease, const FADSRReferences& InADSRData);

			virtual FDataReferenceCollection GetInputs() const override;
			virtual FDataReferenceCollection GetOutputs() const override;
			void Execute();

		private:

			void GenerateEnvelope(int32 InStartFrame, int32 InEndFrame);
			void Start();

			// TODO: write envelope gen for metasound more suited to this processing structure. 
			Audio::FEnvelope Envelope;

			FBopReadRef TriggerAttack;
			FBopReadRef TriggerRelease;

			FBopWriteRef TriggerAttackComplete;
			FBopWriteRef TriggerDecayComplete;
			FBopWriteRef TriggerReleaseComplete;

			bool bReleased;

			FSampleTime TimeUntilRelease;
			FSampleTime TimePerBlock;
			FADSRReferences ADSRReferences;
			FAudioBufferWriteRef EnvelopeBuffer;
	};

	FADSROperator::FADSROperator(const FOperatorSettings& InSettings, const FBopReadRef& InTriggerAttack, const FBopReadRef& InTriggerRelease, const FADSRReferences& InADSRData)
	:	TriggerAttack(InTriggerAttack)
	,	TriggerRelease(InTriggerRelease)
	,	TriggerAttackComplete(FBopWriteRef::CreateNew(InSettings))
	,	TriggerDecayComplete(FBopWriteRef::CreateNew(InSettings))
	,	TriggerReleaseComplete(FBopWriteRef::CreateNew(InSettings))
	,	bReleased(false)
	,	TimeUntilRelease(0, InSettings.GetSampleRate())
	,	TimePerBlock(InSettings.GetNumFramesPerBlock(), InSettings.GetSampleRate())
	,	ADSRReferences(InADSRData)
	,	EnvelopeBuffer(FAudioBufferWriteRef::CreateNew(InSettings.GetNumFramesPerBlock()))
	{
		check(EnvelopeBuffer->Num() == InSettings.GetNumFramesPerBlock());

		Envelope.Init(InSettings.GetSampleRate());
	}

	FDataReferenceCollection FADSROperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(TEXT("Attack"), FFloatTimeReadRef(ADSRReferences.Attack));
		InputDataReferences.AddDataReadReference(TEXT("Decay"), FFloatTimeReadRef(ADSRReferences.Decay));
		InputDataReferences.AddDataReadReference(TEXT("Release"), FFloatTimeReadRef(ADSRReferences.Release));
		InputDataReferences.AddDataReadReference(TEXT("Sustain Level"), FFloatReadRef(ADSRReferences.SustainLevel));
		InputDataReferences.AddDataReadReference(TEXT("Trigger Attack"), FBopReadRef(TriggerAttack));
		InputDataReferences.AddDataReadReference(TEXT("Trigger Release"), FBopReadRef(TriggerRelease));
		return InputDataReferences;
	}

	FDataReferenceCollection FADSROperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(TEXT("Envelope"), FAudioBufferReadRef(EnvelopeBuffer));
		OutputDataReferences.AddDataReadReference(TEXT("Attack Complete"), FBopReadRef(TriggerAttackComplete));
		OutputDataReferences.AddDataReadReference(TEXT("Decay Complete"), FBopReadRef(TriggerDecayComplete));
		OutputDataReferences.AddDataReadReference(TEXT("Release Complete"), FBopReadRef(TriggerReleaseComplete));
		return OutputDataReferences;
	}

	void FADSROperator::GenerateEnvelope(int32 InStartFrame, int32 InEndFrame)
	{
		float* EnvelopeData = EnvelopeBuffer->GetData();
		const bool bWasDone = Envelope.IsDone();

		Audio::FEnvelope::EEnvelopeState EnvState = Envelope.GetState();

		int32 AttackCompleteFrame = -1;
		int32 DecayCompleteFrame = -1;
		int32 ReleaseCompleteFrame = -1;

		TimeUntilRelease -= TimePerBlock;

		auto GenerateLambda = [&] (int32 InFrame)
		{
			const float EnvValue = Envelope.Generate();
			if (Envelope.GetState() != EnvState)
			{
				switch (EnvState)
				{
				case Audio::FEnvelope::EEnvelopeState::Attack:
					AttackCompleteFrame = InFrame;
					break;

				case Audio::FEnvelope::EEnvelopeState::Decay:
					DecayCompleteFrame = InFrame;
					break;

				case Audio::FEnvelope::EEnvelopeState::Release:
					ReleaseCompleteFrame = InFrame;
					break;

				default:
					break;
				}

				EnvState = Envelope.GetState();
			}

			return EnvValue;
		};

		for (int32 i = InStartFrame; i < InEndFrame; i++)
		{
			EnvelopeData[i] = GenerateLambda(i);
		}

		if (AttackCompleteFrame >= 0)
		{
			TriggerAttackComplete->BopFrame(AttackCompleteFrame);
		}

		if (DecayCompleteFrame >= 0)
		{
			TriggerDecayComplete->BopFrame(DecayCompleteFrame);
		}

		if (Envelope.IsDone() && !bWasDone && ReleaseCompleteFrame >= 0)
		{
			TriggerReleaseComplete->BopFrame(ReleaseCompleteFrame);
		}
	}

	void FADSROperator::Execute()
	{
		Envelope.SetAttackTime(ADSRReferences.Attack->GetMilliseconds());
		Envelope.SetDecayTime(ADSRReferences.Decay->GetMilliseconds());
		Envelope.SetReleaseTime(ADSRReferences.Release->GetMilliseconds());
		Envelope.SetSustainGain(*ADSRReferences.SustainLevel);

		TriggerAttackComplete->AdvanceBlock();
		TriggerDecayComplete->AdvanceBlock();
		TriggerReleaseComplete->AdvanceBlock();

		TriggerRelease->ExecuteBlock(
			// OnPreBop
			[&](int32 StartFrame, int32 EndFrame)
			{
			},
			// OnBop
			[&](int32 StartFrame, int32 EndFrame)
			{
				if (!bReleased)
				{
					bReleased = true;
					TimeUntilRelease = *(ADSRReferences.Release);

					const int32 ReleaseFrame = TimeUntilRelease.GetNumSamples();
					if (ReleaseFrame < EndFrame)
					{
						GenerateEnvelope(StartFrame, ReleaseFrame);
						Envelope.Stop();
						GenerateEnvelope(ReleaseFrame, EndFrame);
					}
					else
					{
						Envelope.Stop();
						GenerateEnvelope(StartFrame, EndFrame);
					}
				}
			}
		);

		TriggerAttack->ExecuteBlock(
			// OnPreBop
			[&](int32 StartFrame, int32 EndFrame)
			{
				GenerateEnvelope(StartFrame, EndFrame);
			},
			// OnBop
			[&](int32 StartFrame, int32 EndFrame)
			{
				Start();
				if (!Envelope.IsDone())
				{
					GenerateEnvelope(StartFrame, EndFrame);
				}
			}
		);
	}

	void FADSROperator::Start()
	{
		if (Envelope.IsDone())
		{
			Envelope.Start();
			bReleased = false;
		}
	}

	FVertexInterface FADSROperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FFloatTime>(TEXT("Attack"), LOCTEXT("AttackDurationTooltip", "Attack duration.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Decay"), LOCTEXT("DecayDurationTooltip", "Decay duration.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Release"), LOCTEXT("ReleaseDurationTooltip", "Release duration.")),
				TInputDataVertexModel<float>(TEXT("Sustain Level"), LOCTEXT("SustainLevelTooltip", "Sustain level [0.0f, 1.0f].")),
				TInputDataVertexModel<FBop>(TEXT("Trigger Attack"), LOCTEXT("BopTooltip", "Trigger the envelope's attack.")),
				TInputDataVertexModel<FBop>(TEXT("Trigger Release"), LOCTEXT("BopTooltip", "Trigger the envelope's release."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Envelope"), LOCTEXT("EnvelopeTooltip", "The output envelope")),
				TOutputDataVertexModel<FBop>(TEXT("Attack Complete"), LOCTEXT("AttackCompleteTooltip", "Triggered when the envelope attack is complete")),
				TOutputDataVertexModel<FBop>(TEXT("Decay Complete"), LOCTEXT("DecayCompleteTooltip", "Triggered when the envelope decay is complete")),
				TOutputDataVertexModel<FBop>(TEXT("Release Complete"), LOCTEXT("ReleaseCompleteTooltip", "Triggered when the envelope is released"))
			)
		);

		return Interface;
	}

	const FNodeInfo& FADSROperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeInfo
		{
			FNodeInfo Info;
			Info.ClassName = FName(TEXT("ADSR"));
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = LOCTEXT("Metasound_ADSRNodeDescription", "Emits an ADSR (Attack, decay, sustain, & release) envelope when bopped.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeInfo Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FADSROperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FADSRNode& ADSRNode = static_cast<const FADSRNode&>(InParams.Node);
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;

		// TODO: Could no-op this if the bop is not connected.
		FBopReadRef TriggerAttack = InputCollection.GetDataReadReferenceOrConstruct<FBop>(TEXT("Trigger Attack"), InParams.OperatorSettings);
		FBopReadRef TriggerRelease = InputCollection.GetDataReadReferenceOrConstruct<FBop>(TEXT("Trigger Release"), InParams.OperatorSettings);

		// TODO: If none of these are connected, could pre-generate ADSR envelope and return a different operator. 
		FADSRReferences ADSRReferences = 
		{
			InputCollection.GetDataReadReferenceOrConstruct<FFloatTime>(TEXT("Attack"), ADSRNode.GetDefaultAttackMs(), ETimeResolution::Milliseconds),
			InputCollection.GetDataReadReferenceOrConstruct<FFloatTime>(TEXT("Decay"), ADSRNode.GetDefaultDecayMs(), ETimeResolution::Milliseconds),
			InputCollection.GetDataReadReferenceOrConstruct<float>(TEXT("Sustain Level"), ADSRNode.GetDefaultSustainLevel()),
			InputCollection.GetDataReadReferenceOrConstruct<FFloatTime>(TEXT("Release"), ADSRNode.GetDefaultReleaseMs(), ETimeResolution::Milliseconds),
		};
	
		return MakeUnique<FADSROperator>(InParams.OperatorSettings, TriggerAttack, TriggerRelease, ADSRReferences);
	}

	FADSRNode::FADSRNode(const FString& InName, float InDefaultAttackMs, float InDefaultDecayMs, float InDefaultSustainLevel, float InDefaultReleaseMs)
	:	FNodeFacade(InName, TFacadeOperatorClass<FADSROperator>())
	,	DefaultSustainLevel(InDefaultSustainLevel)
	,	DefaultAttackMs(InDefaultAttackMs)
	,	DefaultDecayMs(InDefaultDecayMs)
	,	DefaultReleaseMs(InDefaultReleaseMs)
	{
	}

	FADSRNode::FADSRNode(const FNodeInitData& InitData)
		: FADSRNode(InitData.InstanceName, 10.0f, 20.0f, 0.7f, 20.0f)
	{
	}

	FADSRNode::~FADSRNode()
	{
	}

	float FADSRNode::GetDefaultSustainLevel() const
	{
		return DefaultSustainLevel;
	}

	float FADSRNode::GetDefaultAttackMs() const
	{
		return DefaultAttackMs;
	}

	float FADSRNode::GetDefaultDecayMs() const
	{
		return DefaultDecayMs;
	}

	float FADSRNode::GetDefaultReleaseMs() const
	{
		return DefaultReleaseMs;
	}
}

#undef LOCTEXT_NAMESPACE
