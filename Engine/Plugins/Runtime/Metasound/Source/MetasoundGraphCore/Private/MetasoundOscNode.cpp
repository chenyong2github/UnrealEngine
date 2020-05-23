// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOscNode.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundDataReferenceTypes.h"

#define LOCTEXT_NAMESPACE "MetasoundOscNode"

namespace Metasound
{
	class FOscOperator : public TExecutableOperator<FOscOperator>
	{
		public:
			FOscOperator(const FOperatorSettings& InSettings, const FFloatReadRef& InFrequency)
			:	OperatorSettings(InSettings)
			,	TwoPi(2.f * PI)
			,	Phase(0.f)
			,	Frequency(InFrequency)
			,	AudioBuffer(InSettings.FramesPerExecute)
			{
				check(AudioBuffer->Num() == InSettings.FramesPerExecute);

				OutputDataReferences.AddDataReadReference(TEXT("Audio"), FAudioBufferReadRef(AudioBuffer));
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
				const float PhaseDelta = *Frequency * 2 * PI / OperatorSettings.SampleRate;
				float* Data = AudioBuffer->GetData();

				for (int32 i = 0; i < OperatorSettings.FramesPerExecute; i++)
				{
					Data[i] = FMath::Sin(Phase);
					Phase += PhaseDelta;
				}

				Phase -= FMath::FloorToFloat(Phase / TwoPi) * TwoPi;
			}

		private:
			const FOperatorSettings OperatorSettings;
			const float TwoPi;
			float Phase;

			FFloatReadRef Frequency;
			FAudioBufferWriteRef AudioBuffer;

			FDataReferenceCollection InputDataReferences;
			FDataReferenceCollection OutputDataReferences;
	};

	const FName FOscNode::ClassName = FName(TEXT("Osc"));

	TUniquePtr<IOperator> FOscNode::FOperatorFactory::CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) 
	{
		const FOscNode& OscNode = static_cast<const FOscNode&>(InNode);
		FFloatReadRef Frequency(OscNode.GetDefaultFrequency());

		if (InInputDataReferences.ContainsDataReadReference<FFloat>(TEXT("Frequency")))
		{
			Frequency = InInputDataReferences.GetDataReadReference<FFloat>(TEXT("Frequency"));
		}

		return MakeUnique<FOscOperator>(InOperatorSettings, Frequency);
	}

	FOscNode::FOscNode(const FString& InName, float InDefaultFrequency)
	:	FNode(InName)
	,	DefaultFrequency(InDefaultFrequency)
	{
		AddInputDataVertexDescription<FFloat>(TEXT("Frequency"), LOCTEXT("FrequencyTooltip", "The frequency of oscillator in Hz."));
		AddOutputDataVertexDescription<FAudioBuffer>(TEXT("Audio"), LOCTEXT("AudioTooltip", "The output audio"));
	}

	FOscNode::~FOscNode()
	{
	}

	float FOscNode::GetDefaultFrequency() const
	{
		return DefaultFrequency;
	}

	const FName& FOscNode::GetClassName() const
	{
		return ClassName;
	}

	IOperatorFactory& FOscNode::GetDefaultOperatorFactory() 
	{
		return Factory;
	}
}
#undef LOCTEXT_NAMESPACE //MetasoundOscNode
