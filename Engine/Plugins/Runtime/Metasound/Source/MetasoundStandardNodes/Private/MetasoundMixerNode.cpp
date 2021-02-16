// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundMixerNode.h"

#include "DSP/Dsp.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"


namespace Metasound
{
	class FMixerOperator : public TExecutableOperator<FMixerOperator>
	{
	public:
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);
		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();	

		FMixerOperator(const FOperatorSettings& InSettings, TArray<FAudioBufferReadRef>&& InBuffers, TArray<FFloatReadRef>&& InGains);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;

		void Execute();

	private:		
		// TODO: Remove when we get dynamic pin support. (if you update this, update GetVertexInterface also).
		static constexpr const int32 NumHardcodedInputs = 4;

		static FString GetInputAudioPinName(int32 InPinIndex)
		{
			return FString::Printf(TEXT("In %d"), InPinIndex + 1);
		}
		static FString GetInputGainPinName(int32 InPinIndex)
		{
			return FString::Printf(TEXT("Gain %d"), InPinIndex + 1);
		}

		TArray<FAudioBufferReadRef> AudioBuffersIn;
		TArray<FFloatReadRef> BufferMixGains;
		FAudioBufferWriteRef AudioBufferOut;
		
		static constexpr const TCHAR* AudioOutPinName = TEXT("Out");
	};

	FMixerOperator::FMixerOperator(const FOperatorSettings& InSettings, TArray<FAudioBufferReadRef>&& InBuffers, TArray<FFloatReadRef>&& InGains)
		: AudioBuffersIn(MoveTemp(InBuffers))
		, BufferMixGains(MoveTemp(InGains))
		, AudioBufferOut(FAudioBufferWriteRef::CreateNew(InSettings))
	{
		check(AudioBufferOut->Num() == InSettings.GetNumFramesPerBlock());
		check(BufferMixGains.Num() == AudioBuffersIn.Num());
	}

	FDataReferenceCollection FMixerOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		for (int32 i = 0; i < AudioBuffersIn.Num(); ++i)
		{
			InputDataReferences.AddDataReadReference(GetInputAudioPinName(i), AudioBuffersIn[i]);
			InputDataReferences.AddDataReadReference(GetInputGainPinName(i), BufferMixGains[i]);
		}
		return InputDataReferences;
	}

	FDataReferenceCollection FMixerOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(AudioOutPinName, FAudioBufferReadRef(AudioBufferOut));
		return OutputDataReferences;
	}

	void FMixerOperator::Execute()
	{	
		AudioBufferOut->Zero();
		for (int32 i = 0; i < AudioBuffersIn.Num(); ++i)
		{
			Audio::MixInBufferFast(AudioBuffersIn[i]->GetData(), AudioBufferOut->GetData(), AudioBufferOut->Num(), *BufferMixGains[i]);
		}
	}

	const FVertexInterface& FMixerOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				// 1
				TInputDataVertexModel<FAudioBuffer>(GetInputAudioPinName(0), LOCTEXT("MixerInputDescription1", "Audio Input 1 of the Mixer")),		// Can't have dynamic pins yet, so just hard code 4.
				TInputDataVertexModel<float>(GetInputGainPinName(0), LOCTEXT("MixerGainDescription1", ""), 1.0f),

				// 2
				TInputDataVertexModel<FAudioBuffer>(GetInputAudioPinName(1), LOCTEXT("MixerInputDescription2", "Audio Input 2 of the Mixer")),
				TInputDataVertexModel<float>(GetInputGainPinName(1), LOCTEXT("MixerGainDescription2", ""), 1.0f),

				// 3
				TInputDataVertexModel<FAudioBuffer>(GetInputAudioPinName(2), LOCTEXT("MixerInputDescription3", "Audio Input 3 of the Mixer")),
				TInputDataVertexModel<float>(GetInputGainPinName(2), LOCTEXT("MixerGainDescription3", ""), 1.0f),

				// 4
				TInputDataVertexModel<FAudioBuffer>(GetInputAudioPinName(3), LOCTEXT("MixerInputDescription3", "Audio Input 4 of the Mixer")),	
				TInputDataVertexModel<float>(GetInputGainPinName(3), LOCTEXT("MixerGainDescription4", ""), 1.0f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(AudioOutPinName, LOCTEXT("AudioOutTooltip", "Audio Ouput from the mixer"))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FMixerOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("Mixer"), Metasound::StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_MixerNodeDisplayName", "Mixer");
			Info.Description = LOCTEXT("Metasound_MixerNodeDescription", "Mixes input audio together");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FMixerNode::FMixerNode(const FNodeInitData& InInitData)
		: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<FMixerOperator>())
	{
	}

	TUniquePtr<IOperator> FMixerOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FMixerNode& Node = static_cast<const FMixerNode&>(InParams.Node);
		const FDataReferenceCollection& InputCol = InParams.InputDataReferences;
		const FOperatorSettings& Settings = InParams.OperatorSettings;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		// TODO: Remove this string manipulation when we get dynamic pin support.
		TArray<FAudioBufferReadRef> InputBuffers;
		TArray<FFloatReadRef> InputGains;
		for (int32 i = 0; i < NumHardcodedInputs; ++i)
		{
			const FString AudioPinName = GetInputAudioPinName(i);
			const FString GainPinName = GetInputGainPinName(i);
			if (InputCol.ContainsDataReadReference<FAudioBuffer>(AudioPinName))
			{
				// Only Create buffers if there's something connected to it.
				InputBuffers.Emplace(InputCol.GetDataReadReference<FAudioBuffer>(AudioPinName));
				
				// Make sure for every valid/connected pin, we have a corresponding gain, even if its defaulted.
				InputGains.Emplace(InputCol.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, GainPinName));
			}
		}
		
		// Make a node even if we don't have any inputs to make sure we don't fail graph compilation.
		return MakeUnique<FMixerOperator>(InParams.OperatorSettings, MoveTemp(InputBuffers), MoveTemp(InputGains));
	}

	METASOUND_REGISTER_NODE(FMixerNode);
}
#undef LOCTEXT_NAMESPACE //MetasoundMixerNode

