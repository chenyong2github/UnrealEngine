// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundTriggerDelayNode.h"

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
#include "MetasoundSampleCounter.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundStandardNodesCategories.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	METASOUND_REGISTER_NODE(FTriggerDelayNode)

	class FTriggerDelayOperator : public TExecutableOperator<FTriggerDelayOperator>
	{
		public:
			static const FNodeClassMetadata& GetNodeInfo();
			static const FVertexInterface& GetVertexInterface();
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			FTriggerDelayOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerReset, const FTriggerReadRef& InTriggerIn, const FTimeReadRef& InDelay);

			virtual FDataReferenceCollection GetInputs() const override;
			virtual FDataReferenceCollection GetOutputs() const override;

			void Execute();

		private:
			FSampleCounter NextTriggerCounter;
			FSampleCount LastFrameReset;

			FTriggerReadRef TriggerIn;
			FTriggerReadRef TriggerReset;
			FTriggerWriteRef TriggerOut;

			FTimeReadRef Delay;

			FSampleCount FramesPerBlock;
	};

	FTriggerDelayOperator::FTriggerDelayOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerReset, const FTriggerReadRef& InTriggerIn, const FTimeReadRef& InDelay)
	:	NextTriggerCounter(-1, InSettings.GetSampleRate())
	,	LastFrameReset(-1)
	,	TriggerIn(InTriggerIn)
	,	TriggerReset(InTriggerReset)
	,	TriggerOut(FTriggerWriteRef::CreateNew(InSettings))
	,	Delay(InDelay)
	,	FramesPerBlock(InSettings.GetNumFramesPerBlock())
	{
	}

	FDataReferenceCollection FTriggerDelayOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(TEXT("Delay"), FTimeReadRef(Delay));
		InputDataReferences.AddDataReadReference(TEXT("In"), FTriggerReadRef(TriggerIn));
		InputDataReferences.AddDataReadReference(TEXT("Reset"), FTriggerReadRef(TriggerIn));
		return InputDataReferences;
	}

	FDataReferenceCollection FTriggerDelayOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(TEXT("Out"), FTriggerReadRef(TriggerOut));

		return OutputDataReferences;
	}

	void FTriggerDelayOperator::Execute()
	{
		// Advance internal counter to get rid of old triggers.
		TriggerOut->AdvanceBlock();

		LastFrameReset = -1;

		TriggerReset->ExecuteBlock(
			[&](int32 StartFrame, int32 EndFrame)
			{
			},
			[this](int32 StartFrame, int32 EndFrame)
			{
				LastFrameReset = StartFrame;
			}
		);

		TriggerIn->ExecuteBlock(
			[&](int32 StartFrame, int32 EndFrame)
			{
			},
			[this](int32 StartFrame, int32 EndFrame)
			{
				if (StartFrame > LastFrameReset)
				{
					NextTriggerCounter.SetNumSamples(*Delay);
					LastFrameReset = -1;
				}
				else
				{
					NextTriggerCounter.SetNumSamples(-1);
				}
			}
		);

		if (NextTriggerCounter.GetNumSamples() >= 0)
		{
			FSampleCount SamplesRemaining = NextTriggerCounter.GetNumSamples() - FramesPerBlock;
			if (SamplesRemaining > 0.0f)
			{
				NextTriggerCounter -= FramesPerBlock;
			}
			else
			{
				TriggerOut->TriggerFrame(SamplesRemaining + (int32)FramesPerBlock);
				NextTriggerCounter.SetNumSamples(-1);
			}
		}
	}

	TUniquePtr<IOperator> FTriggerDelayOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FTriggerDelayNode& DelayNode = static_cast<const FTriggerDelayNode&>(InParams.Node);

		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();
		FTimeReadRef Delay = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTime, float>(InputInterface, TEXT("Delay"));

		FTriggerReadRef TriggerIn = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("In"), InParams.OperatorSettings);
		FTriggerReadRef TriggerReset = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("Reset"), InParams.OperatorSettings);

		return MakeUnique<FTriggerDelayOperator>(InParams.OperatorSettings, TriggerReset, TriggerIn, Delay);
	}

	const FVertexInterface& FTriggerDelayOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FTime>(TEXT("Delay"), LOCTEXT("TriggerDelayTooltip", "Time to delay and execute deferred trigger in seconds."), 1.0f),
				TInputDataVertexModel<FTrigger>(TEXT("In"), LOCTEXT("TriggerDelayInTooltip", "Triggers delay.")),
				TInputDataVertexModel<FTrigger>(TEXT("Reset"), LOCTEXT("TriggerDelayResetInTooltip", "Resets the trigger delay, clearing the execution task if pending."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FTrigger>(TEXT("Out"), LOCTEXT("TriggerOutTooltip", "The deferred output trigger"))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FTriggerDelayOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = {Metasound::StandardNodes::Namespace, TEXT("Trigger Delay"), TEXT("")};
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_TriggerDelayNodeDisplayName", "Trigger Delay");
			Info.Description = LOCTEXT("Metasound_TriggerDelayNodeDescription", "Executes output trigger after the given delay time from the most recent execution of the input trigger .");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();
			Info.CategoryHierarchy.Emplace(StandardNodes::TriggerUtils);

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();
		return Info;
	}

	FTriggerDelayNode::FTriggerDelayNode(const FNodeInitData& InInitData)
	:	FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<FTriggerDelayOperator>())
	{
	}
}

#undef LOCTEXT_NAMESPACE // MetasoundTriggerDelayNode
