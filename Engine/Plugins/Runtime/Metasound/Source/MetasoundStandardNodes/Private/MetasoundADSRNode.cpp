// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundADSRNode.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundTime.h"
#include "MetasoundDataReferenceTypes.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "DSP/Envelope.h"

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
			const FOperatorSettings OperatorSettings;

			// TODO: write envelope gen for metasound more suited to this processing structure. 
			Audio::FEnvelope Envelope;

			FBopReadRef Bop;
			int32 StopEnvelopePos;
			FADSRReferences ADSRReferences;
			FAudioBufferWriteRef EnvelopeBuffer;

			FDataReferenceCollection OutputDataReferences;
			FDataReferenceCollection InputDataReferences;
	};

	FADSROperator::FADSROperator(const FOperatorSettings& InSettings, const FBopReadRef& InBop, const FADSRReferences& InADSRData)
	:	OperatorSettings(InSettings)
	,	Bop(InBop)
	,	StopEnvelopePos(-1)
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

	void FADSROperator::Execute()
	{
		Envelope.SetAttackTime(ADSRReferences.Attack->GetMilliseconds());
		Envelope.SetDecayTime(ADSRReferences.Decay->GetMilliseconds());
		Envelope.SetReleaseTime(ADSRReferences.Release->GetMilliseconds());

		float ADSRMilliseconds = ADSRReferences.Attack->GetMilliseconds();
		ADSRMilliseconds += ADSRReferences.Decay->GetMilliseconds();
		ADSRMilliseconds += ADSRReferences.Sustain->GetMilliseconds();

		// TODO: Bops need to be sorted.
		// TODO: Make convenient way to do bop loops like this since it might happen alot.
		// TODO: Bops need to be done on 4-sample boundaries to allow for SIMD.
	

		int32 BopIndex = 0;
		int32 StartPos = 0;
		int32 NextBop = BopIndex < Bop->Num() ? (*Bop)[BopIndex] : OperatorSettings.GetNumFramesPerBlock() + 1;
		int32 EndPos = FMath::Min(OperatorSettings.GetNumFramesPerBlock(), NextBop);
		
		if (StopEnvelopePos > 0)
		{
			StopEnvelopePos -= OperatorSettings.GetNumFramesPerBlock();
			EndPos = FMath::Min(StopEnvelopePos, EndPos);
		}

		float* EnvelopeData = EnvelopeBuffer->GetData();

		// TODO: GetNumFramesPerBlock() need to be on 4 sample boundaries to allow for SIMD.
		// TODO: just make GetNumFramesPerBlock() an int32 instead of a uint32
		while (StartPos < (int32)OperatorSettings.GetNumFramesPerBlock())
		{
			int32 i = StartPos;
			for (; i < EndPos; i++)
			{
				EnvelopeData[i] = Envelope.Generate();
			}

			StartPos = EndPos;
			
			if (EndPos == StopEnvelopePos)
			{
				// Stop sustain, start release.
				Envelope.Stop();
				StopEnvelopePos = -1;
				EndPos = FMath::Min((int32)OperatorSettings.GetNumFramesPerBlock(), NextBop);
			}

			if (EndPos == NextBop)
			{
				// Trigger next envelope. 
				Envelope.Start();
				StopEnvelopePos = EndPos + FMath::RoundToInt(OperatorSettings.GetSampleRate() * 0.001f * ADSRMilliseconds);

				BopIndex++;

				NextBop = BopIndex < Bop->Num() ? (*Bop)[BopIndex] : OperatorSettings.GetNumFramesPerBlock() + 1;
				EndPos = FMath::Min(StopEnvelopePos, FMath::Min((int32)OperatorSettings.GetNumFramesPerBlock(), NextBop));
			}
		}
	}

	const FNodeInfo& FADSROperator::GetNodeInfo()
	{
		static const FNodeInfo Info = {
			FName(TEXT("ADSR")),
			LOCTEXT("Metasound_ADSRNodeDescription", "Emits an ADSR (Attack, decay, sustain, & release) envelope when bopped."),
			PluginAuthor,
			PluginNodeMissingPrompt
		};

		return Info;
	}

	FVertexInterface FADSROperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FBop>(TEXT("Bop"), LOCTEXT("BopTooltip", "Trigger for envelope.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Attack"), LOCTEXT("AttackTooltip", "Attack time in milliseconds.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Decay"), LOCTEXT("DecayTooltip", "Decay time in milliseconds.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Sustain"), LOCTEXT("SustainTooltip", "Sustain time in milliseconds.")),
				TInputDataVertexModel<FFloatTime>(TEXT("Release"), LOCTEXT("ReleaseTooltip", "Release time in milliseconds."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Envelope"), LOCTEXT("EnvelopeTooltip", "The output envelope"))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FADSROperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FADSRNode& ADSRNode = static_cast<const FADSRNode&>(InParams.Node);


		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;

		// TODO: Could no-op this if the bop is not connected.
		FBopReadRef Bop = InputCollection.GetDataReadReferenceOrConstruct<FBop>(TEXT("Bop"));

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
