// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOscNode.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundPrimitives.h"

#define LOCTEXT_NAMESPACE "MetasoundOscNode"

namespace Metasound
{
	class FOscOperator : public TExecutableOperator<FOscOperator>
	{
		public:
			FOscOperator(const FOperatorSettings& InSettings, const FFrequencyReadRef& InFrequency)
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
				const float PhaseDelta = Frequency->GetRadiansPerSample(OperatorSettings.SampleRate);
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

			FFrequencyReadRef Frequency;
			FAudioBufferWriteRef AudioBuffer;

			FDataReferenceCollection InputDataReferences;
			FDataReferenceCollection OutputDataReferences;
	};

	const FName FOscNode::ClassName = FName(TEXT("Osc"));

	TUniquePtr<IOperator> FOscNode::FOperatorFactory::CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) 
	{
		const FOscNode& OscNode = static_cast<const FOscNode&>(InNode);
		FFrequencyReadRef Frequency(OscNode.GetDefaultFrequency(), EFrequencyResolution::Hertz);

		if (InInputDataReferences.ContainsDataReadReference<FFrequency>(TEXT("Frequency")))
		{
			Frequency = InInputDataReferences.GetDataReadReference<FFrequency>(TEXT("Frequency"));
		}

		return MakeUnique<FOscOperator>(InOperatorSettings, Frequency);
	}

	FOscNode::FOscNode(const FString& InName, float InDefaultFrequency)
	:	FNode(InName)
	,	DefaultFrequency(InDefaultFrequency)
	{
		AddInputDataVertexDescription<FFrequency>(TEXT("Frequency"), LOCTEXT("FrequencyTooltip", "The frequency of oscillator."));
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
