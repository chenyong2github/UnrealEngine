// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundBufferAddFloatNode.h"

#include "DSP/BufferVectorOperations.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"

#define LOCTEXT_NAMESPACE "MetasoundBufferAddFloatNode"

namespace Metasound
{
	class FBufferAddFloatOperator : public TExecutableOperator<FBufferAddFloatOperator>
	{
		public:
			static const FNodeClassMetadata& GetNodeInfo();

			static FVertexInterface DeclareVertexInterface();

			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			FBufferAddFloatOperator(const FOperatorSettings& InSettings, const FAudioBufferReadRef& InBuffer, const FFloatReadRef& InAddend)
			:	Buffer(InBuffer)
			,	Addend(InAddend)
			,	OutputBuffer(FAudioBufferWriteRef::CreateNew(InSettings))
			{
			}

			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection InputDataReferences;
				InputDataReferences.AddDataReadReference(TEXT("In"), FAudioBufferReadRef(Buffer));
				InputDataReferences.AddDataReadReference(TEXT("Addend"), FFloatReadRef(Addend));
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
				FMemory::Memcpy(OutputBuffer->GetData(), Buffer->GetData(), sizeof(float) * OutputBuffer->Num());

				const int32 SIMDRemainder = OutputBuffer->Num() % AUDIO_SIMD_FLOAT_ALIGNMENT;
				const int32 SIMDCount = OutputBuffer->Num() - SIMDRemainder;

				Audio::AddConstantToBufferInplace(OutputBuffer->GetData(), SIMDCount, *Addend);

				for (int32 i = SIMDCount; i < OutputBuffer->Num(); ++i)
				{
					OutputBuffer->GetData()[i] += *Addend;
				}
			}

		private:
			FAudioBufferReadRef Buffer;
			FFloatReadRef Addend;
			FAudioBufferWriteRef OutputBuffer;
	};

	FBufferAddFloatNode::FBufferAddFloatNode(const FString& InInstanceName, const FGuid& InInstanceID)
	:	FNodeFacade(InInstanceName, InInstanceID, TFacadeOperatorClass<FBufferAddFloatOperator>())
	{
	}

	FBufferAddFloatNode::FBufferAddFloatNode(const FNodeInitData& InInitData)
	:	FBufferAddFloatNode(InInitData.InstanceName, InInitData.InstanceID)
	{
	}

	FVertexInterface FBufferAddFloatOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(TEXT("Buffer"), LOCTEXT("InputBufferTooltip", "Input buffer.")),
				TInputDataVertexModel<float>(TEXT("Addend"), LOCTEXT("InputAddendTooltip", "Input addend."), 0.0f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), LOCTEXT("OutpuBufferTooltip", "Add float value to buffer."))
			)
		);

		return Interface;
	}

	TUniquePtr<IOperator> FBufferAddFloatOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FBufferAddFloatNode& BufferAddFloatNode = static_cast<const FBufferAddFloatNode&>(InParams.Node);
		const FDataReferenceCollection& InputCol = InParams.InputDataReferences;

		FAudioBufferReadRef InputBuffer = InputCol.GetDataReadReferenceOrConstruct<FAudioBuffer>(TEXT("Buffer"), FAudioBuffer(InParams.OperatorSettings.GetNumFramesPerBlock()));
		FFloatReadRef InputGain = InputCol.GetDataReadReferenceOrConstruct<float>(TEXT("Addend"), 0.0f);

		return MakeUnique<FBufferAddFloatOperator>(InParams.OperatorSettings, InputBuffer, InputGain);
	}

	const FNodeClassMetadata& FBufferAddFloatOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeDisplayStyle DisplayStyle;
			DisplayStyle.ImageName = "MetasoundEditor.Graph.Node.Math.Add";
			DisplayStyle.bShowName = false;
			DisplayStyle.bShowInputNames = false;
			DisplayStyle.bShowOutputNames = false;

			FNodeClassMetadata Info;
			Info.ClassName = {Metasound::StandardNodes::Namespace, TEXT("Add"), TEXT("FloatToAudioBuffer")};
			Info.DisplayStyle = DisplayStyle;
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("BufferAddFloat_NodeDisplayName", "Add");
			Info.Description = LOCTEXT("BufferAddFloat_NodeDescription", "Add Float to AudioBuffer.");
			Info.CategoryHierarchy = { LOCTEXT("Metasound_MathCategory", "Math") };
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	};

	METASOUND_REGISTER_NODE(FBufferAddFloatNode);
}

#undef LOCTEXT_NAMESPACE //MetasoundBufferAddFloatNode
