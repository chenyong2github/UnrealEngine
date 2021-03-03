// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundPeriodicTriggerNode.h"

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

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	METASOUND_REGISTER_NODE(FPeriodicTriggerNode)

	class FPeriodicTriggerOperator : public TExecutableOperator<FPeriodicTriggerOperator>
	{
		public:
			static const FNodeClassMetadata& GetNodeInfo();
			static const FVertexInterface& GetVertexInterface();
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			FPeriodicTriggerOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerEnable, const FTriggerReadRef& InTriggerDisable, const FTimeReadRef& InPeriod);

			virtual FDataReferenceCollection GetInputs() const override;

			virtual FDataReferenceCollection GetOutputs() const override;

			void Execute();

		private:
			bool bEnabled;

			FTriggerWriteRef TriggerOut;

			FTriggerReadRef TriggerEnable;
			FTriggerReadRef TriggerDisable;

			FTimeReadRef Period;

			FSampleCount BlockSize;
			FSampleCounter SampleCounter;
	};

	FPeriodicTriggerOperator::FPeriodicTriggerOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerEnable, const FTriggerReadRef& InTriggerDisable, const FTimeReadRef& InPeriod)
	:	bEnabled(false)
	,	TriggerOut(FTriggerWriteRef::CreateNew(InSettings))
	,	TriggerEnable(InTriggerEnable)
	,	TriggerDisable(InTriggerDisable)
	,	Period(InPeriod)
	,	BlockSize(InSettings.GetNumFramesPerBlock())
	,	SampleCounter(0, InSettings.GetSampleRate())
	{
	}

	FDataReferenceCollection FPeriodicTriggerOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(TEXT("Period"), FTimeReadRef(Period));
		InputDataReferences.AddDataReadReference(TEXT("Activate"), FTriggerReadRef(TriggerEnable));
		InputDataReferences.AddDataReadReference(TEXT("Deactivate"), FTriggerReadRef(TriggerDisable));
		return InputDataReferences;
	}

	FDataReferenceCollection FPeriodicTriggerOperator::GetOutputs() const
	{
		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(TEXT("Out"), FTriggerReadRef(TriggerOut));

		return OutputDataReferences;
	}

	void FPeriodicTriggerOperator::Execute()
	{
		TriggerEnable->ExecuteBlock([](int32, int32) { },
			[this](int32 StartFrame, int32 EndFrame)
			{
				bEnabled = true;
				SampleCounter.SetNumSamples(0);
			}
		);

		TriggerDisable->ExecuteBlock([](int32, int32) { },
			[this](int32 StartFrame, int32 EndFrame)
			{
				bEnabled = false;
			}
		);

		// Advance internal counter to get rid of old triggers.
		TriggerOut->AdvanceBlock();

		if (bEnabled)
		{
			FSampleCount PeriodInSamples = FSampleCounter::FromTime(*Period, SampleCounter.GetSampleRate()).GetNumSamples();

			// Time must march on, can't stay in the now forever.
			PeriodInSamples = FMath::Max(static_cast<FSampleCount>(1), PeriodInSamples);

			while ((SampleCounter - BlockSize).GetNumSamples() <= 0)
			{
				const int32 StartOffset = static_cast<int32>(SampleCounter.GetNumSamples());
				TriggerOut->TriggerFrame(StartOffset);
				SampleCounter += PeriodInSamples;
			}

			SampleCounter -= BlockSize;
		}
	}


	TUniquePtr<IOperator> FPeriodicTriggerOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FPeriodicTriggerNode& PeriodicTriggerNode = static_cast<const FPeriodicTriggerNode&>(InParams.Node);
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FTimeReadRef Period = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTime, float>(InputInterface, TEXT("Period"));
		FTriggerReadRef TriggerEnable = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("Activate"), InParams.OperatorSettings);
		FTriggerReadRef TriggerDisable = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(TEXT("Deactivate"), InParams.OperatorSettings);

		return MakeUnique<FPeriodicTriggerOperator>(InParams.OperatorSettings, TriggerEnable, TriggerDisable, Period);
	}

	const FVertexInterface& FPeriodicTriggerOperator::GetVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FTime>(TEXT("Period"), LOCTEXT("PeriodTooltip", "The period to trigger in seconds."), 1.0f),
				TInputDataVertexModel<FTrigger>(TEXT("Activate"), LOCTEXT("TriggerEnableTooltip", "Enables executing periodic output triggers.")),
				TInputDataVertexModel<FTrigger>(TEXT("Deactivate"), LOCTEXT("TriggerDisableTooltip", "Disables executing periodic output triggers."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FTrigger>(TEXT("Out"), LOCTEXT("TriggerOutTooltip", "The periodically generated output trigger"))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FPeriodicTriggerOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("PeriodicTrigger"), TEXT("") };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_PeriodicTriggerNodeDisplayName", "Periodic Trigger");
			Info.Description = LOCTEXT("Metasound_PeriodicTriggerNodeDescription", "Emits a trigger periodically based on the period duration given.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FPeriodicTriggerNode::FPeriodicTriggerNode(const FNodeInitData& InInitData)
	:	FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<FPeriodicTriggerOperator>())
	{
	}
}

#undef LOCTEXT_NAMESPACE //MetasoundPeriodicTriggerNode
