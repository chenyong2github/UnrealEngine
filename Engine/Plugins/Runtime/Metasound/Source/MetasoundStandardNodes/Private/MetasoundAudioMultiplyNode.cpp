// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAudioMultiplyNode.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"
#include "DSP/BufferVectorOperations.h"

#define LOCTEXT_NAMESPACE "MetasoundAudioMultiplyNode"

namespace Metasound
{
	class FAudioMultiplyOperator : public TExecutableOperator<FAudioMultiplyOperator>
	{
		public:
			static const FNodeInfo& GetNodeInfo();

			static FVertexInterface DeclareVertexInterface();

			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			FAudioMultiplyOperator(const FOperatorSettings& InSettings, const FAudioBufferReadRef& InBuffer1, const FAudioBufferReadRef& InBuffer2)
			:	OperatorSettings(InSettings)
			,	InputBuffer1(InBuffer1)
			,	InputBuffer2(InBuffer2)
			,	OutputBuffer(FAudioBufferWriteRef::CreateNew(InSettings))
			{
				check(OutputBuffer->Num() == InSettings.GetNumFramesPerBlock());
				check(InputBuffer1->Num() == InSettings.GetNumFramesPerBlock());
				check(InputBuffer2->Num() == InSettings.GetNumFramesPerBlock());

				OutputDataReferences.AddDataReadReference(TEXT("Audio"), FAudioBufferReadRef(OutputBuffer));
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
				using namespace Audio;

				// TODO: add buffer vector op to multipy two buffers (not in place). 
				// TODO: what to do about RESTRICT? This could be something for the builder to keep in mind.
				FMemory::Memcpy(OutputBuffer->GetData(), InputBuffer1->GetData(), sizeof(float) * OperatorSettings.GetNumFramesPerBlock());
				MultiplyBuffersInPlace(*InputBuffer2, *OutputBuffer);
			}

		private:
			const FOperatorSettings OperatorSettings;

			FAudioBufferReadRef InputBuffer1;
			FAudioBufferReadRef InputBuffer2;
			FAudioBufferWriteRef OutputBuffer;
			FDataReferenceCollection OutputDataReferences;
			FDataReferenceCollection InputDataReferences;
	};

	FAudioMultiplyNode::FAudioMultiplyNode(const FString& InInstanceName)
	:	FNodeFacade(InInstanceName, TFacadeOperatorClass<FAudioMultiplyOperator>())
	{
	}

	FAudioMultiplyNode::FAudioMultiplyNode(const FNodeInitData& InInitData)
	:	FAudioMultiplyNode(InInitData.InstanceName)
	{
	}

	FVertexInterface FAudioMultiplyOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(TEXT("InputBuffer1"), LOCTEXT("InputBuffer1Tooltip", "Input buffer to multiply.")),
				TInputDataVertexModel<FAudioBuffer>(TEXT("InputBuffer2"), LOCTEXT("InputBuffer2Tooltip", "Input buffer to multiply."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Audio"), LOCTEXT("OutpuBufferTooltip", "The output audio."))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FAudioMultiplyOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FAudioMultiplyNode& AudioMultiplyNode = static_cast<const FAudioMultiplyNode&>(InParams.Node);


		FAudioBuffer Ones(InParams.OperatorSettings.GetNumFramesPerBlock());

		// initialize default array to all ones. 
		float* Data = Ones.GetData();
		for (int32 i = 0; i < InParams.OperatorSettings.GetNumFramesPerBlock(); i++)
		{
			Data[i] = 1.f;
		}

		// TODO: return a helper that uses implicit conversion so that return types can be deduced without templates.

		const FDataReferenceCollection& InputCol = InParams.InputDataReferences;

		FAudioBufferReadRef InputBuffer1 = InputCol.GetDataReadReferenceOrConstruct<FAudioBuffer>(TEXT("InputBuffer1"), Ones);
		FAudioBufferReadRef InputBuffer2 = InputCol.GetDataReadReferenceOrConstruct<FAudioBuffer>(TEXT("InputBuffer2"), Ones);

		return MakeUnique<FAudioMultiplyOperator>(InParams.OperatorSettings, InputBuffer1, InputBuffer2);
	}

	const FNodeInfo& FAudioMultiplyOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeInfo
		{
			FNodeInfo Info;
			Info.ClassName = FName(TEXT("AudioMultiply"));
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = LOCTEXT("AudioMultiply_NodeDescription", "Multiply two audio streams, sample by sample."),
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeInfo Info = InitNodeInfo();

		return Info;
	};

	METASOUND_REGISTER_NODE(FAudioMultiplyNode);
}

#undef LOCTEXT_NAMESPACE //MetasoundAudioMultiplyNode
