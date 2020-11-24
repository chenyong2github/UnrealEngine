// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOscNode.h"

#include "MetasoundAudioBuffer.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundFrequency.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	class FOscOperator : public TExecutableOperator<FOscOperator>
	{
		public:

			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);
			static const FNodeInfo& GetNodeInfo();
			static FVertexInterface DeclareVertexInterface();

			FOscOperator(const FOperatorSettings& InSettings, const FFrequencyReadRef& InFrequency);

			virtual const FDataReferenceCollection& GetInputs() const override;

			virtual const FDataReferenceCollection& GetOutputs() const override;

			void Execute();

		private:
			const FOperatorSettings OperatorSettings;
			const float TwoPi;
			float Phase;

			FFrequencyReadRef Frequency;
			FAudioBufferWriteRef AudioBuffer;

			FDataReferenceCollection InputDataReferences;
			FDataReferenceCollection OutputDataReferences;
	};

	FOscOperator::FOscOperator(const FOperatorSettings& InSettings, const FFrequencyReadRef& InFrequency)
	:	OperatorSettings(InSettings)
	,	TwoPi(2.f * PI)
	,	Phase(0.f)
	,	Frequency(InFrequency)
	,	AudioBuffer(FAudioBufferWriteRef::CreateNew(InSettings))
	{
		check(AudioBuffer->Num() == InSettings.GetNumFramesPerBlock());

		OutputDataReferences.AddDataReadReference(TEXT("Audio"), FAudioBufferReadRef(AudioBuffer));
	}

	const FDataReferenceCollection& FOscOperator::GetInputs() const
	{
		return InputDataReferences;
	}

	const FDataReferenceCollection& FOscOperator::GetOutputs() const
	{
		return OutputDataReferences;
	}

	void FOscOperator::Execute()
	{
		const float PhaseDelta = Frequency->GetRadiansPerSample(OperatorSettings.GetSampleRate());
		float* Data = AudioBuffer->GetData();

		for (int32 i = 0; i < OperatorSettings.GetNumFramesPerBlock(); i++)
		{
			Data[i] = FMath::Sin(Phase);
			Phase += PhaseDelta;
		}

		Phase -= FMath::FloorToFloat(Phase / TwoPi) * TwoPi;
	}

	FVertexInterface FOscOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FFrequency>(TEXT("Frequency"), LOCTEXT("OscFrequencyDescription", "The frequency of oscillator."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Audio"), LOCTEXT("AudioTooltip", "The output audio"))
			)
		);

		return Interface;
	}


	const FNodeInfo& FOscOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeInfo
		{
			FNodeInfo Info;
			Info.ClassName = FName(TEXT("Osc"));
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = LOCTEXT("Metasound_OscNodeDescription", "Emits an audio signal of a sinusoid.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeInfo Info = InitNodeInfo();

		return Info;
	}


	FOscNode::FOscNode(const FString& InName, float InDefaultFrequency)
	:	FNodeFacade(InName, TFacadeOperatorClass<FOscOperator>())
	,	DefaultFrequency(InDefaultFrequency)
	{
	}

	FOscNode::FOscNode(const FNodeInitData& InInitData)
		: FOscNode(InInitData.InstanceName, 440.0f)
	{
	}

	float FOscNode::GetDefaultFrequency() const
	{
		return DefaultFrequency;
	}


	TUniquePtr<IOperator> FOscOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		const FOscNode& OscNode = static_cast<const FOscNode&>(InParams.Node);
		const FDataReferenceCollection& InputCol = InParams.InputDataReferences;

		FFrequencyReadRef Frequency = InputCol.GetDataReadReferenceOrConstruct<FFrequency>(TEXT("Frequency"), OscNode.GetDefaultFrequency(), EFrequencyResolution::Hertz);

		return MakeUnique<FOscOperator>(InParams.OperatorSettings, Frequency);
	}

	METASOUND_REGISTER_NODE(FOscNode);
}
#undef LOCTEXT_NAMESPACE //MetasoundOscNode
