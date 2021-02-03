// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundTriggerPipeNode.h"

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTime.h"
#include "MetasoundTrigger.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"


namespace Metasound
{
	METASOUND_REGISTER_NODE(FTriggerPipeNode)

	class FTriggerPipeOperator : public TExecutableOperator<FTriggerPipeOperator>
	{
	public:
		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FTriggerPipeOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerReset, const FTriggerReadRef& InTriggerIn, const FFloatTimeReadRef& InDelay);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		TArray<int32> SamplesUntilTrigger;

		FTriggerReadRef TriggerIn;
		FTriggerReadRef TriggerReset;
		FTriggerWriteRef TriggerOut;

		FFloatTimeReadRef Delay;

		float FramesPerBlock;
		float SampleRate;
	};

	FTriggerPipeOperator::FTriggerPipeOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerReset, const FTriggerReadRef& InTriggerIn, const FFloatTimeReadRef& InDelay)
	:	TriggerIn(InTriggerIn)
	,	TriggerReset(InTriggerReset)
	,	TriggerOut(FTriggerWriteRef::CreateNew(InSettings))
	,	Delay(InDelay)
	,	FramesPerBlock(InSettings.GetNumFramesPerBlock())
	,	SampleRate(InSettings.GetSampleRate())
	{
	}

	FDataReferenceCollection FTriggerPipeOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(TEXT("Delay"), FFloatTimeReadRef(Delay));
		InputDataReferences.AddDataReadReference(TEXT("In"), FTriggerReadRef(TriggerIn));
		InputDataReferences.AddDataReadReference(TEXT("Reset"), FTriggerReadRef(TriggerIn));
		return InputDataReferences;
	}

	FDataReferenceCollection FTriggerPipeOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(TEXT("Out"), FTriggerReadRef(TriggerOut));

		return OutputDataReferences;
	}

	void FTriggerPipeOperator::Execute()
	{
		// Advance internal counter to get rid of old triggers.
		TriggerOut->AdvanceBlock();

		TriggerIn->ExecuteBlock(
			[](int32 StartFrame, int32 EndFrame)
			{
			},
			[this](int32 StartFrame, int32 EndFrame)
			{
				SamplesUntilTrigger.AddUnique(Delay->GetNumSamples(SampleRate));
			}
		);

		TriggerReset->ExecuteBlock(
			[](int32 StartFrame, int32 EndFrame)
			{
			},
			[this](int32 StartFrame, int32 EndFrame)
			{
				// Iterate backward and only remove delayed triggers that occur
				// after the reset trigger's start frame.
				for (int32 i = SamplesUntilTrigger.Num() - 1; i >= 0; --i)
				{
					const int32 SamplesRemaining = SamplesUntilTrigger[i] - FramesPerBlock;
					if (SamplesRemaining >= StartFrame)
					{
						SamplesUntilTrigger.RemoveAtSwap(i);
					}
				}
			}
		);

		for (int32 i = SamplesUntilTrigger.Num() - 1; i >= 0; --i)
		{
			const int32 SamplesRemaining = SamplesUntilTrigger[i] - FramesPerBlock;
			if (SamplesRemaining >= 0)
			{
				SamplesUntilTrigger[i] -= FramesPerBlock;
			}
			else
			{
				TriggerOut->TriggerFrame(SamplesRemaining + (int32)FramesPerBlock);
				SamplesUntilTrigger.RemoveAtSwap(i);
			}
		}
	}

	TUniquePtr<IOperator> FTriggerPipeOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FTriggerPipeNode& DelayNode = static_cast<const FTriggerPipeNode&>(InParams.Node);
		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;

		auto GetOrConstructTime = [&](const FInputVertexInterface& InputVertices, const FString& InputName, ETimeResolution Resolution = ETimeResolution::Seconds)
		{
			float DefaultValue = InputVertices[InputName].GetDefaultValue().Value.Get<float>();
			return InputCollection.GetDataReadReferenceOrConstruct<FFloatTime>(InputName, DefaultValue, Resolution);
		};

		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();
		FFloatTimeReadRef Delay = GetOrConstructTime(InputInterface, TEXT("Delay"));
		FTriggerReadRef TriggerIn = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("In"), InParams.OperatorSettings);
		FTriggerReadRef TriggerReset = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("Reset"), InParams.OperatorSettings);

		return MakeUnique<FTriggerPipeOperator>(InParams.OperatorSettings, TriggerReset, TriggerIn, Delay);
	}

	const FVertexInterface& FTriggerPipeOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FFloatTime>(TEXT("Delay"), LOCTEXT("DelayTooltip", "Time to delay and execute deferred input trigger execution(s) in seconds."), 1.0f),
				TInputDataVertexModel<FTrigger>(TEXT("In"), LOCTEXT("TriggerInTooltip", "Trigger to execute at a future time by the given delay amount.")),
				TInputDataVertexModel<FTrigger>(TEXT("Reset"), LOCTEXT("TriggerDelayResetInTooltip", "Resets the trigger delay, clearing any pending execution tasks."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FTrigger>(TEXT("Out"), LOCTEXT("TriggerOutTooltip", "The deferred output trigger"))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FTriggerPipeOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = FNodeClassName(StandardNodes::Namespace, "Pipe", "Trigger");
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("PipeTriggerNode_NodeDisplayName", "Trigger Pipe");
			Info.Description = LOCTEXT("Metasound_DelayNodeDescription", "Delays execution of the input trigger by the given delay for all input trigger executions.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();
		return Info;
	}

	FTriggerPipeNode::FTriggerPipeNode(const FNodeInitData& InInitData)
		: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<FTriggerPipeOperator>())
	{
	}
}

#undef LOCTEXT_NAMESPACE // MetasoundTriggerPipeNode
