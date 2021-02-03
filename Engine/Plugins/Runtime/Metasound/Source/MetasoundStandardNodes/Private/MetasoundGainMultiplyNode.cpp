// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGainMultiplyNode.h"

#include "DSP/BufferVectorOperations.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"

#define LOCTEXT_NAMESPACE "MetasoundGainMultiplyNode"

namespace Metasound
{
	class FGainMultiplyOperator : public TExecutableOperator<FGainMultiplyOperator>
	{
		public:
			static const FNodeClassMetadata& GetNodeInfo();

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
				LastGain = *Gain;
			}

			virtual FDataReferenceCollection GetInputs() const override
			{
				FDataReferenceCollection InputDataReferences;
				InputDataReferences.AddDataReadReference(TEXT("In"), FAudioBufferReadRef(Buffer));
				InputDataReferences.AddDataReadReference(TEXT("Gain"), FFloatReadRef(Gain));
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
				if (!bInit)
				{
					bInit = true;
					LastGain = *Gain;
				}

				FMemory::Memcpy(OutputBuffer->GetData(), Buffer->GetData(), sizeof(float) * BlockSize);

				const int32 SIMDRemainder = OutputBuffer->Num() % AUDIO_SIMD_FLOAT_ALIGNMENT;
				const int32 SIMDCount = OutputBuffer->Num() - SIMDRemainder;

				Audio::FadeBufferFast(OutputBuffer->GetData(), SIMDCount, LastGain, *Gain);

				for (int32 i = SIMDCount; i < OutputBuffer->Num(); ++i)
				{
					OutputBuffer->GetData()[i] *= *Gain;
				}

				LastGain = *Gain;
			}

		private:
			FAudioBufferReadRef Buffer;
			FFloatReadRef Gain;
			FAudioBufferWriteRef OutputBuffer;

			float LastGain = 1.0f;

			bool bInit = false;

			int32 BlockSize;
	};

	FGainMultiplyNode::FGainMultiplyNode(const FString& InInstanceName, const FGuid& InInstanceID)
	:	FNodeFacade(InInstanceName, InInstanceID, TFacadeOperatorClass<FGainMultiplyOperator>())
	{
	}

	FGainMultiplyNode::FGainMultiplyNode(const FNodeInitData& InInitData)
	:	FGainMultiplyNode(InInitData.InstanceName, InInitData.InstanceID)
	{
	}

	FVertexInterface FGainMultiplyOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FAudioBuffer>(TEXT("Buffer"), LOCTEXT("InputBufferTooltip", "Input buffer.")),
				TInputDataVertexModel<float>(TEXT("Gain"), LOCTEXT("InputGainTooltip", "Input gain."), 1.0f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FAudioBuffer>(TEXT("Out"), LOCTEXT("OutpuBufferTooltip", "Output buffer with gain multiplier applied."))
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

	const FNodeClassMetadata& FGainMultiplyOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeDisplayStyle DisplayStyle;
			DisplayStyle.ImageName = "MetasoundEditor.Graph.Node.Math.Multiply";
			DisplayStyle.bShowName = false;
			DisplayStyle.bShowInputNames = false;
			DisplayStyle.bShowOutputNames = false;

			FNodeClassMetadata Info;
			Info.ClassName = FNodeClassName(StandardNodes::Namespace, TEXT("Multiply"), TEXT("AudioBufferByGain"));
			Info.DisplayStyle = DisplayStyle;
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("GainMultiply_NodeDisplayName", "Multiply Audio by Float");
			Info.Description = LOCTEXT("GainMultiply_NodeDescription", "Multiply AudioBuffer by float.");
			Info.CategoryHierarchy = { LOCTEXT("Metasound_MathCategory", "Math") };
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	};

	METASOUND_REGISTER_NODE(FGainMultiplyNode);
}

#undef LOCTEXT_NAMESPACE //MetasoundGainMultiplyNode
