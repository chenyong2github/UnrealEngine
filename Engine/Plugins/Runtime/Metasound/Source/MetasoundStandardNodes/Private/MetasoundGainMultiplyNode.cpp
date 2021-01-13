// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGainMultiplyNode.h"

#include "DSP/BufferVectorOperations.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"

#define LOCTEXT_NAMESPACE "MetasoundGainMultiplyNode"

namespace Metasound
{
	class FGainMultiplyOperator : public TExecutableOperator<FGainMultiplyOperator>
	{
		public:
			static const FNodeInfo& GetNodeInfo();

			static FVertexInterface DeclareVertexInterface();

			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			FGainMultiplyOperator(const FOperatorSettings& InSettings, const FAudioBufferReadRef& InBuffer, const FFloatReadRef& InGain)
			:	Buffer(InBuffer)
			,	Gain(InGain)
			,	OutputBuffer(FAudioBufferWriteRef::CreateNew(InSettings))
			,	BlockSize(InSettings.GetNumFramesPerBlock())
			{
				check(OutputBuffer->Num() == BlockSize);
				check(Buffer->Num() == BlockSize);
			}

			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection InputDataReferences;
				return InputDataReferences;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				FDataReferenceCollection OutputDataReferences;
				OutputDataReferences.AddDataReadReference(TEXT("Out"), FAudioBufferReadRef(OutputBuffer));
				return OutputDataReferences;
			}

			void Execute()
			{
				FMemory::Memcpy(OutputBuffer->GetData(), Buffer->GetData(), sizeof(float) * BlockSize);
				Audio::MultiplyBufferByConstantInPlace(*OutputBuffer, *Gain);
			}

		private:
			FAudioBufferReadRef Buffer;
			FFloatReadRef Gain;
			FAudioBufferWriteRef OutputBuffer;
			int32 BlockSize;
	};

	FGainMultiplyNode::FGainMultiplyNode(const FString& InInstanceName)
	:	FNodeFacade(InInstanceName, TFacadeOperatorClass<FGainMultiplyOperator>())
	{
	}

	FGainMultiplyNode::FGainMultiplyNode(const FNodeInitData& InInitData)
	:	FGainMultiplyNode(InInitData.InstanceName)
	{
	}

	FVertexInterface FGainMultiplyOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(TEXT("Buffer"), LOCTEXT("InputBuffer1Tooltip", "Input buffer.")),
				TInputDataVertexModel<float>(TEXT("Gain"), LOCTEXT("InputGainTooltip", "Input gain."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), LOCTEXT("OutpuBufferTooltip", "The output audio."))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FGainMultiplyOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FGainMultiplyNode& GainMultiplyNode = static_cast<const FGainMultiplyNode&>(InParams.Node);
		const FDataReferenceCollection& InputCol = InParams.InputDataReferences;

		FAudioBufferReadRef InputBuffer = InputCol.GetDataReadReferenceOrConstruct<FAudioBuffer>(TEXT("Buffer"), FAudioBuffer(InParams.OperatorSettings.GetNumFramesPerBlock()));
		FFloatReadRef InputGain = InputCol.GetDataReadReferenceOrConstruct<float>(TEXT("Gain"), 1.0f);

		return MakeUnique<FGainMultiplyOperator>(InParams.OperatorSettings, InputBuffer, InputGain);
	}

	const FNodeInfo& FGainMultiplyOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeInfo
		{
			FNodeInfo Info;
			Info.ClassName = FName(TEXT("Gain Multiply"));
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = LOCTEXT("GainMultiply_NodeDescription", "Multiply audio buffer by float."),
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeInfo Info = InitNodeInfo();

		return Info;
	};

	METASOUND_REGISTER_NODE(FGainMultiplyNode);
}

#undef LOCTEXT_NAMESPACE //MetasoundGainMultiplyNode
