// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWaveShaperNode.h"

#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundAudioBuffer.h"
#include "DSP/WaveShaper.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace WaveShaperNode
	{
		static const TCHAR* InParamNameAudioInput = TEXT("In");
		static const TCHAR* InParamNameWaveShapeAmount = TEXT("Amount");
		static const TCHAR* InParamNameWaveShapeBias = TEXT("Bias");
		static const TCHAR* InParamNameOutputGain = TEXT("OutputGain");
		static const TCHAR* InParamNameType = TEXT("Type");
		static const TCHAR* OutParamNameAudio = TEXT("Out");
	}

	using namespace WaveShaperNode;

	DECLARE_METASOUND_ENUM(Audio::EWaveShaperType, Audio::EWaveShaperType::ATan, METASOUNDSTANDARDNODES_API, FEnumEWaveShaperType, FEnumWaveShaperTypeInfo, FEnumWaveShaperReadRef, FEnumWaveShaperWriteRef);

	DEFINE_METASOUND_ENUM_BEGIN(Audio::EWaveShaperType, FEnumEWaveShaperType, "WaveShaperType")
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EWaveShaperType::Sin, LOCTEXT("SinDescription", "Sine"), LOCTEXT("SinTT", "Sine WaveShaper")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EWaveShaperType::ATan, LOCTEXT("ATanDescription", "Inverse Tangent"), LOCTEXT("ATanTT", "Inverse Tangent WaveShaper")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EWaveShaperType::Tanh, LOCTEXT("TanHDescription", "Hyperbolic Tangent"), LOCTEXT("TanHTT", "Hyperbolic Tangent WaveShaper")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EWaveShaperType::Cubic, LOCTEXT("CubicDescription", "Cubic Polynomial"), LOCTEXT("CubicTT", "Cubic Polynomial WaveShaper")),
		DEFINE_METASOUND_ENUM_ENTRY(Audio::EWaveShaperType::HardClip, LOCTEXT("HarClipDescription", "Hard Clip"), LOCTEXT("HardClipTT", "Hard Clipper"))
		DEFINE_METASOUND_ENUM_END()

	class FWaveShaperOperator : public TExecutableOperator<FWaveShaperOperator>
	{
	public:

		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FWaveShaperOperator(const FOperatorSettings& InSettings, 
			const FAudioBufferReadRef& InAudioInput, 
			const FFloatReadRef& InWaveShapeAmount,
			const FFloatReadRef& InBias,
			const FFloatReadRef& InOutputGain,
			const FEnumWaveShaperReadRef& InType);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		// The input audio buffer
		FAudioBufferReadRef AudioInput;

		// The amount that the wave is shaped
		FFloatReadRef Amount;

		// DC offset to apply before gain
		FFloatReadRef Bias;

		// The amount of gain
		FFloatReadRef OutputGain;

		// What algorithm to use
		FEnumWaveShaperReadRef Type;

		// The audio output
		FAudioBufferWriteRef AudioOutput;

		// The internal WaveShaper
		Audio::FWaveShaper WaveShaper;
	};

	FWaveShaperOperator::FWaveShaperOperator(const FOperatorSettings& InSettings, 
		const FAudioBufferReadRef& InAudioInput, 
		const FFloatReadRef& InAmount,
		const FFloatReadRef& InBias,
		const FFloatReadRef& InOutputGain,
		const FEnumWaveShaperReadRef& InType)
		: AudioInput(InAudioInput)
		, Amount(InAmount)
		, Bias(InBias)
		, OutputGain(InOutputGain)
		, Type(InType)
		, AudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
	{
		WaveShaper.Init(InSettings.GetSampleRate());
	}

	FDataReferenceCollection FWaveShaperOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(InParamNameAudioInput, AudioInput);
		InputDataReferences.AddDataReadReference(InParamNameWaveShapeAmount, Amount);
		InputDataReferences.AddDataReadReference(InParamNameWaveShapeBias, Bias);
		InputDataReferences.AddDataReadReference(InParamNameOutputGain, OutputGain);
		InputDataReferences.AddDataReadReference(InParamNameType, Type);

		return InputDataReferences;
	}

	FDataReferenceCollection FWaveShaperOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(OutParamNameAudio, AudioOutput);
		return OutputDataReferences;
	}

	void FWaveShaperOperator::Execute()
	{
		const float* InputAudio = AudioInput->GetData();

		float* OutputAudio = AudioOutput->GetData();
		int32 NumFrames = AudioInput->Num();

		WaveShaper.SetAmount(*Amount);
		WaveShaper.SetBias(*Bias);
		WaveShaper.SetOutputGainLinear(*OutputGain);
		WaveShaper.SetType(*Type);

		WaveShaper.ProcessAudioBuffer(InputAudio, OutputAudio, NumFrames);
	}

	const FVertexInterface& FWaveShaperOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(InParamNameAudioInput, LOCTEXT("AudioInputTT", "Audio input.")),
				TInputDataVertexModel<float>(InParamNameWaveShapeAmount, LOCTEXT("WaveShapeAmountTT", "The amount of wave shaping to apply."), 1.0f),
				TInputDataVertexModel<float>(InParamNameWaveShapeBias, LOCTEXT("WaveShaperBiasTT", "DC offset to apply before wave shaping."), 0.0f),
				TInputDataVertexModel<float>(InParamNameOutputGain, LOCTEXT("WaveShaperGainTT", "The amount of gain to apply after processing."), 1.0f),
				TInputDataVertexModel<FEnumEWaveShaperType>(InParamNameType, LOCTEXT("WaveShaperTypeTT", "Algorithm to use to process the audio."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(OutParamNameAudio, LOCTEXT("WaveShaperOutputTT", "Audio output."))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FWaveShaperOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("WaveShaper"), Metasound::StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_WaveShaperDisplayName", "WaveShaper");
			Info.Description = LOCTEXT("Metasound_WaveShaperNodeDescription", "Applies non-linear shaping to the audio input.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FWaveShaperOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FWaveShaperNode& WaveShaperNode = static_cast<const FWaveShaperNode&>(InParams.Node);
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FAudioBufferReadRef AudioIn = InputCollection.GetDataReadReferenceOrConstruct<FAudioBuffer>(InParamNameAudioInput, InParams.OperatorSettings);
		FFloatReadRef InAmount = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, InParamNameWaveShapeAmount);
		FFloatReadRef InBias = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, InParamNameWaveShapeBias);
		FFloatReadRef OutGain = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, InParamNameOutputGain);
		FEnumWaveShaperReadRef InType = InputCollection.GetDataReadReferenceOrConstruct<FEnumEWaveShaperType>(InParamNameType);

		return MakeUnique<FWaveShaperOperator>(InParams.OperatorSettings, AudioIn, InAmount, InBias, OutGain, InType);
	}

	FWaveShaperNode::FWaveShaperNode(const FNodeInitData& InitData)
		: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FWaveShaperOperator>())
	{
	}

	METASOUND_REGISTER_NODE(FWaveShaperNode)
}

#undef LOCTEXT_NAMESPACE
