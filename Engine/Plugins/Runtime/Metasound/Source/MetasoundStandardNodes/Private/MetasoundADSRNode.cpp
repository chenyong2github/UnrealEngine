// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundADSRNode.h"

#include "DSP/Envelope.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundBop.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundTime.h"
#include "MetasoundNodeRegistrationMacro.h"

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
				FFloatTimeReadRef Sustain;
				FFloatTimeReadRef Release;
			};

			static const FNodeInfo& GetNodeInfo();
			static FVertexInterface DeclareVertexInterface();
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);


			FADSROperator(const FOperatorSettings& InSettings, const FBopReadRef& InBop, const FADSRReferences& InADSRData);

			virtual const FDataReferenceCollection& GetInputs() const override;
			virtual const FDataReferenceCollection& GetOutputs() const override;
			void Execute();

		private:

			void GenerateEnvelope(int32 InStartFrame, int32 InEndFrame);

			const FOperatorSettings OperatorSettings;

			// TODO: write envelope gen for metasound more suited to this processing structure. 
			Audio::FEnvelope Envelope;

			FBopReadRef Bop;
			bool bNotReleased;
			FSampleTime TimeTillRelease;
			FSampleTime TimePerBlock;
			FADSRReferences ADSRReferences;
			FAudioBufferWriteRef EnvelopeBuffer;

			FDataReferenceCollection OutputDataReferences;
			FDataReferenceCollection InputDataReferences;
	};

	FADSROperator::FADSROperator(const FOperatorSettings& InSettings, const FBopReadRef& InBop, const FADSRReferences& InADSRData)
	:	OperatorSettings(InSettings)
	,	Bop(InBop)
	,	bNotReleased(false)
	,	TimeTillRelease(0, InSettings.GetSampleRate())
	,	TimePerBlock(InSettings.GetNumFramesPerBlock(), InSettings.GetSampleRate())
	,	ADSRReferences(InADSRData)
	,	EnvelopeBuffer(FAudioBufferWriteRef::CreateNew(InSettings.GetNumFramesPerBlock()))
	{
		check(EnvelopeBuffer->Num() == InSettings.GetNumFramesPerBlock());

		OutputDataReferences.AddDataReadReference(TEXT("Envelope"), FAudioBufferReadRef(EnvelopeBuffer));

		Envelope.Init(OperatorSettings.GetSampleRate());
	}

	const FDataReferenceCollection& FADSROperator::GetInputs() const
	{
		return InputDataReferences;
	}

	const FDataReferenceCollection& FADSROperator::GetOutputs() const
	{
		return OutputDataReferences;
	}

	void FADSROperator::GenerateEnvelope(int32 InStartFrame, int32 InEndFrame)
	{
		float* EnvelopeData = EnvelopeBuffer->GetData();

		const int32 ReleaseFrame = TimeTillRelease.GetNumSamples();

		if (bNotReleased && (ReleaseFrame < InEndFrame))
		{
			// If the envelope is expected to be released during these frames,
			// need to split generation of envelope so envelope can be stopped.
			for (int32 i = InStartFrame; i < ReleaseFrame; i++)
			{
				EnvelopeData[i] = Envelope.Generate();
			}

			Envelope.Stop();

			bNotReleased = false;

			for (int32 i = FMath::Max(InStartFrame, ReleaseFrame); i < InEndFrame; i++)
			{
				EnvelopeData[i] = Envelope.Generate();
			}
		}
		else
		{
			for (int32 i = InStartFrame; i < InEndFrame; i++)
			{
				EnvelopeData[i] = Envelope.Generate();
			}
		}
	}


	void FADSROperator::Execute()
	{
		Envelope.SetAttackTime(ADSRReferences.Attack->GetMilliseconds());
		Envelope.SetDecayTime(ADSRReferences.Decay->GetMilliseconds());
		Envelope.SetReleaseTime(ADSRReferences.Release->GetMilliseconds());

		if (bNotReleased)
		{
			// Keep track of when next release should happen
			TimeTillRelease -= TimePerBlock;
		}
		
		Bop->ExecuteBlock(
			// OnPreBop
			[&](int32 StartFrame, int32 EndFrame) { GenerateEnvelope(StartFrame, EndFrame); },
			// OnBop
			[&](int32 StartFrame, int32 EndFrame)
			{
				Envelope.Start();

				// Capture attack, decay and sustain time (skip release).
				TimeTillRelease = *ADSRReferences.Attack + *ADSRReferences.Decay + *ADSRReferences.Sustain;

				bNotReleased = true;

				GenerateEnvelope(StartFrame, EndFrame);
			}
		);
	}

	FVertexInterface FADSROperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FBop>(TEXT("Bop"), LOCTEXT("BopTooltip", "Trigger for envelope.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Attack"), LOCTEXT("AttackTooltip", "Attack time.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Decay"), LOCTEXT("DecayTooltip", "Decay time.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Sustain"), LOCTEXT("SustainTooltip", "Sustain time.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Release"), LOCTEXT("ReleaseTooltip", "Release time."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Envelope"), LOCTEXT("EnvelopeTooltip", "The output envelope"))
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
		FBopReadRef Bop = InputCollection.GetDataReadReferenceOrConstruct<FBop>(TEXT("Bop"), InParams.OperatorSettings);

		// TODO: If none of these are connected, could pre-generate ADSR envelope and return a different operator. 
		FADSRReferences ADSRReferences = 
			{
				InputCollection.GetDataReadReferenceOrConstruct<FFloatTime>(TEXT("Attack"), ADSRNode.GetDefaultAttackMs(), ETimeResolution::Milliseconds),
				InputCollection.GetDataReadReferenceOrConstruct<FFloatTime>(TEXT("Decay"), ADSRNode.GetDefaultDecayMs(), ETimeResolution::Milliseconds),
				InputCollection.GetDataReadReferenceOrConstruct<FFloatTime>(TEXT("Sustain"), ADSRNode.GetDefaultSustainMs(), ETimeResolution::Milliseconds),
				InputCollection.GetDataReadReferenceOrConstruct<FFloatTime>(TEXT("Release"), ADSRNode.GetDefaultReleaseMs(), ETimeResolution::Milliseconds)
			};
	
		return MakeUnique<FADSROperator>(InParams.OperatorSettings, Bop, ADSRReferences);
	}

	FADSRNode::FADSRNode(const FString& InName, float InDefaultAttackMs, float InDefaultDecayMs, float InDefaultSustainMs, float InDefaultReleaseMs)
	:	FNodeFacade(InName, TFacadeOperatorClass<FADSROperator>())
	,	DefaultAttackMs(InDefaultAttackMs)
	,	DefaultDecayMs(InDefaultDecayMs)
	,	DefaultSustainMs(InDefaultSustainMs)
	,	DefaultReleaseMs(InDefaultReleaseMs)
	{
	}

	FADSRNode::FADSRNode(const FNodeInitData& InitData)
		: FADSRNode(InitData.InstanceName, 10.0f, 20.0f, 50.0f, 20.0f)
	{
	}

	FADSRNode::~FADSRNode()
	{
	}


	float FADSRNode::GetDefaultAttackMs() const
	{
		return DefaultAttackMs;
	}

	float FADSRNode::GetDefaultDecayMs() const
	{
		return DefaultDecayMs;
	}

	float FADSRNode::GetDefaultSustainMs() const
	{
		return DefaultSustainMs;
	}

	float FADSRNode::GetDefaultReleaseMs() const
	{
		return DefaultReleaseMs;
	}
}

#undef LOCTEXT_NAMESPACE
