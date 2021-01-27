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
#include "MetasoundTime.h"
#include "MetasoundTrigger.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"


namespace Metasound
{
	METASOUND_REGISTER_NODE(FTriggerPipeNode)

	class FTriggerPipeOperator : public TExecutableOperator<FTriggerPipeOperator>
	{
		public:
			static const FNodeInfo& GetNodeInfo();
			static FVertexInterface DeclareVertexInterface();
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

		for (int32 i = SamplesUntilTrigger.Num() - 1; i >= 0; --i)
		{
			int32 SamplesRemaining = SamplesUntilTrigger[i] - FramesPerBlock;
			if (SamplesRemaining > 0.0f)
			{
				SamplesUntilTrigger[i] -= FramesPerBlock;
			}
			else
			{
				TriggerOut->TriggerFrame(SamplesRemaining + (int32)FramesPerBlock);
				SamplesUntilTrigger.RemoveAtSwap(i);
			}
		}

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
				SamplesUntilTrigger.Empty();
			}
		);
	}

	TUniquePtr<IOperator> FTriggerPipeOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FTriggerPipeNode& DelayNode = static_cast<const FTriggerPipeNode&>(InParams.Node);

		FFloatTimeReadRef Delay = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FFloatTime>(TEXT("Delay"), DelayNode.GetDefaultDelayInSeconds());
		FTriggerReadRef TriggerIn = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("In"), InParams.OperatorSettings);
		FTriggerReadRef TriggerReset = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("Reset"), InParams.OperatorSettings);

		return MakeUnique<FTriggerPipeOperator>(InParams.OperatorSettings, TriggerReset, TriggerIn, Delay);
	}

	FVertexInterface FTriggerPipeOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FFloatTime>(TEXT("Delay"), LOCTEXT("DelayTooltip", "Time to delay and execute deferred input trigger execution(s) in seconds.")),
				TInputDataVertexModel<FTrigger>(TEXT("In"), LOCTEXT("TriggerInTooltip", "Trigger to execute at a future time by the given delay amount.")),
				TInputDataVertexModel<FTrigger>(TEXT("Reset"), LOCTEXT("TriggerDelayResetInTooltip", "Resets the trigger delay, clearing any pending execution tasks."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FTrigger>(TEXT("Out"), LOCTEXT("TriggerOutTooltip", "The deferred output trigger"))
			)
		);

		return Interface;
	}

	const FNodeInfo& FTriggerPipeOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeInfo
		{
			FNodeInfo Info;
			Info.ClassName = "Trigger Pipe";
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = LOCTEXT("Metasound_DelayNodeDescription", "Delays execution of the input trigger by the given delay for all input trigger executions.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeInfo Info = InitNodeInfo();
		return Info;
	}

	FTriggerPipeNode::FTriggerPipeNode(const FString& InName, float InDefaultDelayInSeconds)
	:	FNodeFacade(InName, TFacadeOperatorClass<FTriggerPipeOperator>())
	,	DefaultDelay(InDefaultDelayInSeconds)
	{
	}

	FTriggerPipeNode::FTriggerPipeNode(const FNodeInitData& InInitData)
		: FTriggerPipeNode(InInitData.InstanceName, 0.0f)
	{
	}

	float FTriggerPipeNode::GetDefaultDelayInSeconds() const
	{
		return DefaultDelay;
	}
}

#undef LOCTEXT_NAMESPACE // MetasoundTriggerPipeNode
