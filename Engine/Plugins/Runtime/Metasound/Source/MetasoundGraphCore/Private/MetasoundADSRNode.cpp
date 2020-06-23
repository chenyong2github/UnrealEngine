// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundADSRNode.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundTime.h"
#include "MetasoundDataReferenceTypes.h"
#include "DSP/Envelope.h"

#define LOCTEXT_NAMESPACE "MetasoundADSRNode"

namespace Metasound
{
	struct FADSRDataReferences
	{
		FFloatTimeReadRef Attack;
		FFloatTimeReadRef Decay;
		FFloatTimeReadRef Sustain;
		FFloatTimeReadRef Release;
	};

	class FADSROperator : public TExecutableOperator<FADSROperator>
	{
		public:
			FADSROperator(const FOperatorSettings& InSettings, const FBopReadRef& InBop, const FADSRDataReferences& InADSRData)
			:	OperatorSettings(InSettings)
			,	Bop(InBop)
			,	StopEnvelopePos(-1)
			,	ADSRDataReferences(InADSRData)
			,	EnvelopeBuffer(InSettings.FramesPerExecute)
			{
				check(EnvelopeBuffer->Num() == InSettings.FramesPerExecute);

				OutputDataReferences.AddDataReadReference(TEXT("Envelope"), FAudioBufferReadRef(EnvelopeBuffer));

				Envelope.Init(OperatorSettings.SampleRate);
			}

			virtual const FDataReferenceCollection& GetInputs() const override
			{
				return InputDataReferences;
			}

			virtual const FDataReferenceCollection& GetOutputs() const override
			{
				return OutputDataReferences;
			}

			void Execute()
			{
				Envelope.SetAttackTime(ADSRDataReferences.Attack->GetMilliseconds());
				Envelope.SetDecayTime(ADSRDataReferences.Decay->GetMilliseconds());
				Envelope.SetReleaseTime(ADSRDataReferences.Release->GetMilliseconds());

				float ADSMilliseconds = ADSRDataReferences.Attack->GetMilliseconds();
				ADSMilliseconds += ADSRDataReferences.Decay->GetMilliseconds();
				ADSMilliseconds += ADSRDataReferences.Sustain->GetMilliseconds();

				// TODO: Bops need to be sorted.
				// TODO: Make convenient way to do bop loops like this since it might happen alot.
				// TODO: Bops need to be done on 4-sample boundaries to allow for SIMD.
			

				int32 BopIndex = 0;
				int32 StartPos = 0;
				int32 NextBop = BopIndex < Bop->Num() ? (*Bop)[BopIndex] : OperatorSettings.FramesPerExecute + 1;
				int32 EndPos = FMath::Min(OperatorSettings.FramesPerExecute, NextBop);
				
				if (StopEnvelopePos > 0)
				{
					StopEnvelopePos -= OperatorSettings.FramesPerExecute;
					EndPos = FMath::Min(StopEnvelopePos, EndPos);
				}

				float* EnvelopeData = EnvelopeBuffer->GetData();

				// TODO: FramesPerExecute need to be on 4 sample boundaries to allow for SIMD.
				// TODO: just make FramesPerExecute an int32 instead of a uint32
				while (StartPos < (int32)OperatorSettings.FramesPerExecute)
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
						EndPos = FMath::Min((int32)OperatorSettings.FramesPerExecute, NextBop);
					}

					if (EndPos == NextBop)
					{
						// Trigger next envelope. 
						Envelope.Start();
						StopEnvelopePos = EndPos + FMath::RoundToInt(OperatorSettings.SampleRate * 0.001f * ADSMilliseconds);

						BopIndex++;

						NextBop = BopIndex < Bop->Num() ? (*Bop)[BopIndex] : OperatorSettings.FramesPerExecute + 1;
						EndPos = FMath::Min(StopEnvelopePos, FMath::Min((int32)OperatorSettings.FramesPerExecute, NextBop));
					}
				}
			}

		private:
			const FOperatorSettings OperatorSettings;

			// TODO: write envelope gen for metasound more suited to this processing structure. 
			Audio::FEnvelope Envelope;

			FBopReadRef Bop;
			int32 StopEnvelopePos;
			FADSRDataReferences ADSRDataReferences;
			FAudioBufferWriteRef EnvelopeBuffer;

			FDataReferenceCollection OutputDataReferences;
			FDataReferenceCollection InputDataReferences;
	};

	const FName FADSRNode::ClassName = FName(TEXT("ADSR"));

	TUniquePtr<IOperator> FADSRNode::FOperatorFactory::CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) 
	{
		const FADSRNode& ADSRNode = static_cast<const FADSRNode&>(InNode);

		FBopReadRef Bop;

		// TODO: Could no-op this if the bop is not connected.
		SetReadableRefIfInCollection(TEXT("Bop"), InInputDataReferences, Bop);

		FADSRDataReferences ADSRDataReferences = 
			{
				FFloatTimeReadRef(ADSRNode.GetDefaultAttackMs(), ETimeResolution::Milliseconds),
				FFloatTimeReadRef(ADSRNode.GetDefaultDecayMs(), ETimeResolution::Milliseconds),
				FFloatTimeReadRef(ADSRNode.GetDefaultSustainMs(), ETimeResolution::Milliseconds),
				FFloatTimeReadRef(ADSRNode.GetDefaultReleaseMs(), ETimeResolution::Milliseconds)
			};

		// TODO: If none of these are connected, could pregenerate ADSR envelope and return a different operator. 
		SetReadableRefIfInCollection(TEXT("Attack"), InInputDataReferences, ADSRDataReferences.Attack);
		SetReadableRefIfInCollection(TEXT("Decay"), InInputDataReferences, ADSRDataReferences.Decay);
		SetReadableRefIfInCollection(TEXT("Sustain"), InInputDataReferences, ADSRDataReferences.Sustain);
		SetReadableRefIfInCollection(TEXT("Release"), InInputDataReferences, ADSRDataReferences.Release);

		return MakeUnique<FADSROperator>(InOperatorSettings, Bop, ADSRDataReferences);
	}

	FADSRNode::FADSRNode(const FString& InName, float InDefaultAttackMs, float InDefaultDecayMs, float InDefaultSustainMs, float InDefaultReleaseMs)
	:	FNode(InName)
	,	DefaultAttackMs(InDefaultAttackMs)
	,	DefaultDecayMs(InDefaultDecayMs)
	,	DefaultSustainMs(InDefaultSustainMs)
	,	DefaultReleaseMs(InDefaultReleaseMs)
	{
		AddInputDataVertexDescription<FBop>(TEXT("Bop"), LOCTEXT("BopTooltip", "Trigger for envelope."));
		AddInputDataVertexDescription<FFloatTime>(TEXT("Attack"), LOCTEXT("AttackTooltip", "Attack time in milliseconds."));
		AddInputDataVertexDescription<FFloatTime>(TEXT("Decay"), LOCTEXT("DecayTooltip", "Decay time in milliseconds."));
		AddInputDataVertexDescription<FFloatTime>(TEXT("Sustain"), LOCTEXT("SustainTooltip", "Sustain time in milliseconds."));
		AddInputDataVertexDescription<FFloatTime>(TEXT("Release"), LOCTEXT("ReleaseTooltip", "Release time in milliseconds."));

		AddOutputDataVertexDescription<FAudioBuffer>(TEXT("Envelope"), LOCTEXT("EnvelopeTooltip", "The output envelope"));
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

	const FName& FADSRNode::GetClassName() const
	{
		return ClassName;
	}

	IOperatorFactory& FADSRNode::GetDefaultOperatorFactory() 
	{
		return Factory;
	}
}

#undef LOCTEXT_NAMESPACE
