// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundOscNode.h"

#include "DSP/Dsp.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"


namespace Metasound
{
	class FOscOperator : public TExecutableOperator<FOscOperator>
	{
	public:

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);
		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();

		FOscOperator(const FOperatorSettings& InSettings, const FFloatReadRef& InFrequency, const FBoolReadRef& InActivate);

		virtual FDataReferenceCollection GetInputs() const override;

		virtual FDataReferenceCollection GetOutputs() const override;

		void Execute();

	private:
		// Returns true if there was a phase wrap this update
		FORCEINLINE bool WrapPhase(float InPhaseInc, float& OutPhase)
		{
			bool Result = false;
			if (InPhaseInc > 0.0f && OutPhase >= 1.0f)
			{
				OutPhase = FMath::Fmod(OutPhase, 1.0f);
				Result = true;
			}
			else if (InPhaseInc < 0.0f && OutPhase <= 0.0f)
			{
				OutPhase = FMath::Fmod(OutPhase, 1.0f) + 1.0f;
				Result = true;
			}
			return Result;
		}

		static constexpr const float TwoPi = 2.f * PI;

		// The current phase of oscillator (between 0.0 and 1.0)
		float Phase;
		float OneOverSampleRate;
		float Nyquist;

		FFloatReadRef Frequency;
		FBoolReadRef Enabled;
		FAudioBufferWriteRef AudioBuffer;

		static constexpr const TCHAR* EnabledPinName = TEXT("Enabled");
		static constexpr const TCHAR* FrequencyPinName = TEXT("Frequency");
		static constexpr const TCHAR* AudioOutPinName = TEXT("Audio");
	};

	FOscOperator::FOscOperator(const FOperatorSettings& InSettings, const FFloatReadRef& InFrequency, const FBoolReadRef& InEnabled)
		: Phase(0.f)
		, OneOverSampleRate(1.f / InSettings.GetSampleRate())
		, Nyquist(InSettings.GetSampleRate() / 2.0f)
		, Frequency(InFrequency)
		, Enabled(InEnabled)
		, AudioBuffer(FAudioBufferWriteRef::CreateNew(InSettings))
	{
		check(AudioBuffer->Num() == InSettings.GetNumFramesPerBlock());
	}

	FDataReferenceCollection FOscOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(FrequencyPinName, FFloatReadRef(Frequency));
		InputDataReferences.AddDataReadReference(EnabledPinName, FBoolReadRef(Enabled));

		return InputDataReferences;
	}

	FDataReferenceCollection FOscOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(AudioOutPinName, FAudioBufferReadRef(AudioBuffer));
		return OutputDataReferences;
	}

	void FOscOperator::Execute()
	{
		// Clamp frequencies into Nyquist range
		const float Freq = FMath::Clamp(*Frequency, -Nyquist, Nyquist);
		const float PhaseInc = Freq * OneOverSampleRate;
		float* Data = AudioBuffer->GetData();

		FMemory::Memzero(Data, AudioBuffer->Num() * sizeof(float));

		if (*Enabled)
		{
			for (int32 i = 0; i < AudioBuffer->Num(); i++)
			{
				// This is borrowed from the FOsc class with the intention to eventually recreate that functionality here.
				// We don't wish to use it directly as it has a virtual call for each sample. Phase is in cents with the intent
				// to support other oscillator types in time.

				const float Radians = (Phase * TwoPi) - PI;
				Data[i] = Audio::FastSin3(-1.0f * Radians);
				Phase += PhaseInc;
				WrapPhase(PhaseInc, Phase);
			}
		}
	}

	const FVertexInterface& FOscOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<float>(FrequencyPinName, LOCTEXT("OscFrequencyDescription", "The frequency of oscillator."), 440.f),
				TInputDataVertexModel<bool>(EnabledPinName, LOCTEXT("OscActivateDescription", "Enable the oscilator."), true)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Audio"), LOCTEXT("AudioTooltip", "The output audio"))
			)
		);

		return Interface;
	}


	const FNodeClassMetadata& FOscOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = {Metasound::StandardNodes::Namespace, TEXT("Osc"), Metasound::StandardNodes::AudioVariant};
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_OscNodeDisplayName", "Oscillator");
			Info.Description = LOCTEXT("Metasound_OscNodeDescription", "Emits sinusoidal audio.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FOscNode::FOscNode(const FNodeInitData& InInitData)
		: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<FOscOperator>())
	{
	}

	TUniquePtr<IOperator> FOscOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
	{
		const FOscNode& OscNode = static_cast<const FOscNode&>(InParams.Node);
		const FDataReferenceCollection& InputCol = InParams.InputDataReferences;
		const FOperatorSettings& Settings = InParams.OperatorSettings;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FFloatReadRef Frequency = InputCol.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, FrequencyPinName);
		FBoolReadRef Enabled = InputCol.GetDataReadReferenceOrConstructWithVertexDefault<bool>(InputInterface, EnabledPinName);

		return MakeUnique<FOscOperator>(InParams.OperatorSettings, Frequency, Enabled);
	}

	METASOUND_REGISTER_NODE(FOscNode);
}
#undef LOCTEXT_NAMESPACE //MetasoundOscNode

