// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAudioMultiplyNode.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundDataReferenceTypes.h"
#include "DSP/BufferVectorOperations.h"

#define LOCTEXT_NAMESPACE "MetasoundAudioMultiplyNode"

namespace Metasound
{

	class FAudioMultiplyOperator : public TExecutableOperator<FAudioMultiplyOperator>
	{
		public:
			FAudioMultiplyOperator(const FOperatorSettings& InSettings, const FAudioBufferReadRef& InBuffer1, const FAudioBufferReadRef& InBuffer2)
			:	OperatorSettings(InSettings)
			,	InputBuffer1(InBuffer1)
			,	InputBuffer2(InBuffer2)
			,	OutputBuffer(InSettings.FramesPerExecute)
			{
				check(OutputBuffer->Num() == InSettings.FramesPerExecute);
				check(InputBuffer1->Num() == InSettings.FramesPerExecute);
				check(InputBuffer2->Num() == InSettings.FramesPerExecute);

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
				FMemory::Memcpy(OutputBuffer->GetData(), InputBuffer1->GetData(), sizeof(float) * OperatorSettings.FramesPerExecute);
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

	const FName FAudioMultiplyNode::ClassName = FName(TEXT("AudioMultiply"));

	TUniquePtr<IOperator> FAudioMultiplyNode::FOperatorFactory::CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) 
	{
		const FAudioMultiplyNode& AudioMultiplyNode = static_cast<const FAudioMultiplyNode&>(InNode);

		FAudioBufferReadRef InputBuffer1;
		FAudioBufferReadRef InputBuffer2;

		FAudioBuffer Ones(InOperatorSettings.FramesPerExecute);

		// initialize default array to all ones. 
		float* Data = Ones.GetData();
		for (int32 i = 0; i < InOperatorSettings.FramesPerExecute; i++)
		{
			Data[i] = 1.f;
		}

		if (!SetReadableRefIfInCollection(TEXT("InputBuffer1"), InInputDataReferences, InputBuffer1))
		{
			InputBuffer1 = FAudioBufferReadRef(Ones);
		}

		if (!SetReadableRefIfInCollection(TEXT("InputBuffer2"), InInputDataReferences, InputBuffer2))
		{
			InputBuffer2 = FAudioBufferReadRef(Ones);
		}

		return MakeUnique<FAudioMultiplyOperator>(InOperatorSettings, InputBuffer1, InputBuffer2);
	}

	FAudioMultiplyNode::FAudioMultiplyNode(const FString& InName)
	:	FNode(InName)
	{
		AddInputDataVertex<FAudioBuffer>(TEXT("InputBuffer1"), LOCTEXT("InputBuffer1Tooltip", "Input buffer to multiply."));
		AddInputDataVertex<FAudioBuffer>(TEXT("InputBuffer2"), LOCTEXT("InputBuffer2Tooltip", "Input buffer to multiply."));

		AddOutputDataVertex<FAudioBuffer>(TEXT("Audio"), LOCTEXT("OutpuBufferTooltip", "The output audio."));
	}

	FAudioMultiplyNode::~FAudioMultiplyNode()
	{
	}

	const FName& FAudioMultiplyNode::GetClassName() const
	{
		return ClassName;
	}

	IOperatorFactory& FAudioMultiplyNode::GetDefaultOperatorFactory() 
	{
		return Factory;
	}
}

#undef LOCTEXT_NAMESPACE //MetasoundAudioMultiplyNode
