// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundMIDIToFreqNode.h"

#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "DSP/Dsp.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace MIDIToFrequencyVertexNames
	{
		const FString& GetInputMIDIName()
		{
			static FString Name = TEXT("MIDI");
			return Name;
		}

		const FText& GetInputMIDIDescription()
		{
			static FText Desc = LOCTEXT("MIDIToFrequencyInputMIDIName", "A value representing a MIDI note value.");
			return Desc;
		}

		const FString& GetOutputFrequencyName()
		{
			static FString Name = TEXT("Frequency");
			return Name;
		}

		const FText& GetOutputFrequencyDescription()
		{
			static FText Desc = LOCTEXT("MIDITOFrequencyNodeOutputFrequencyName", "Output frequency value in hertz that corresponds to the input MIDI note value.");
			return Desc;
		}
	}

	class FMIDIToFreqOperator : public TExecutableOperator<FMIDIToFreqOperator>
	{
	public:

		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FMIDIToFreqOperator(const FOperatorSettings& InSettings, const FFloatReadRef& InMIDINote);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		// The input midi value
		FFloatReadRef MIDINote;

		// The output frequency
		FFloatWriteRef FreqOutput;

		// Cached midi note value. Used to catch if the value changes to recompute freq output.
		int32 PrevMidiNote;
	};

	FMIDIToFreqOperator::FMIDIToFreqOperator(const FOperatorSettings& InSettings, const FFloatReadRef& InMIDINote)
		: MIDINote(InMIDINote)
		, FreqOutput(FFloatWriteRef::CreateNew(Audio::GetFrequencyFromMidi(*InMIDINote)))
		, PrevMidiNote(*InMIDINote)
	{
	}

	FDataReferenceCollection FMIDIToFreqOperator::GetInputs() const
	{
		using namespace MIDIToFrequencyVertexNames;

		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(GetInputMIDIName(), MIDINote);

		return InputDataReferences;
	}

	FDataReferenceCollection FMIDIToFreqOperator::GetOutputs() const
	{
		using namespace MIDIToFrequencyVertexNames;

		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(GetOutputFrequencyName(), FreqOutput);
		return OutputDataReferences;
	}

	void FMIDIToFreqOperator::Execute()
	{
		// Only do anything if the midi note changes
		if (*MIDINote != PrevMidiNote)
		{
			PrevMidiNote = *MIDINote;

			*FreqOutput = Audio::GetFrequencyFromMidi(FMath::Clamp(*MIDINote, 0.0f, 127.0f));
		}
	}

	const FVertexInterface& FMIDIToFreqOperator::GetVertexInterface()
	{
		using namespace MIDIToFrequencyVertexNames;

		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<float>(GetInputMIDIName(), GetInputMIDIDescription(), 60)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<float>(GetOutputFrequencyName(), GetOutputFrequencyDescription())
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FMIDIToFreqOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("MIDI To Frequency"), TEXT("") };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_MidiToFreqDisplayName", "MIDI To Frequency");
			Info.Description = LOCTEXT("Metasound_MidiToFreqNodeDescription", "Converts a MIDI note value to a frequency (hz) value.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FMIDIToFreqOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		using namespace MIDIToFrequencyVertexNames;

		const FMIDIToFreqNode& MIDIToFreqNode = static_cast<const FMIDIToFreqNode&>(InParams.Node);
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FFloatReadRef InMIDINote = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, GetInputMIDIName(), InParams.OperatorSettings);

		return MakeUnique<FMIDIToFreqOperator>(InParams.OperatorSettings, InMIDINote);
	}

	FMIDIToFreqNode::FMIDIToFreqNode(const FNodeInitData& InitData)
		: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FMIDIToFreqOperator>())
	{
	}

	METASOUND_REGISTER_NODE(FMIDIToFreqNode)
}

#undef LOCTEXT_NAMESPACE
