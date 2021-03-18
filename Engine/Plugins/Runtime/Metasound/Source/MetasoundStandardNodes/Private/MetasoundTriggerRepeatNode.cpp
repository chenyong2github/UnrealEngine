// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundTriggerRepeatNode.h"

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
	METASOUND_REGISTER_NODE(FTriggerRepeatNode)

	namespace TriggerRepeatVertexNames
	{
		const FString& GetInputStartName()
		{
			static FString Name = TEXT("Start");
			return Name;
		}

		const FText& GetInputStartDescription()
		{
			static FText Desc = LOCTEXT("TriggerEnableTooltip", "Starts executing periodic output triggers.");
			return Desc;
		}

		const FString& GetInputStopName()
		{
			static FString Name = TEXT("Stop");
			return Name;
		}

		const FText& GetInputStopDescription()
		{
			static FText Desc = LOCTEXT("TriggerDisableTooltip", "Stops executing periodic output triggers.");
			return Desc;
		}

		const FString& GetInputPeriodName()
		{
			static FString Name = TEXT("Period");
			return Name;
		}

		const FText& GetInputPeriodDescription()
		{
			static FText Desc = LOCTEXT("PeriodTooltip", "The period to trigger in seconds.");
			return Desc;
		}

		const FString& GetOutputTriggerName()
		{
			static FString Name = TEXT("Out");
			return Name;
		}

		const FText& GetOutputTriggerDescription()
		{
			static FText Desc = LOCTEXT("TriggerOutTooltip", "The periodically generated output trigger");
			return Desc;
		}
	}


	class FTriggerRepeatOperator : public TExecutableOperator<FTriggerRepeatOperator>
	{
		public:
			static const FNodeClassMetadata& GetNodeInfo();
			static const FVertexInterface& GetVertexInterface();
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			FTriggerRepeatOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerEnable, const FTriggerReadRef& InTriggerDisable, const FTimeReadRef& InPeriod);

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

	FTriggerRepeatOperator::FTriggerRepeatOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerEnable, const FTriggerReadRef& InTriggerDisable, const FTimeReadRef& InPeriod)
	:	bEnabled(false)
	,	TriggerOut(FTriggerWriteRef::CreateNew(InSettings))
	,	TriggerEnable(InTriggerEnable)
	,	TriggerDisable(InTriggerDisable)
	,	Period(InPeriod)
	,	BlockSize(InSettings.GetNumFramesPerBlock())
	,	SampleCounter(0, InSettings.GetSampleRate())
	{
	}

	FDataReferenceCollection FTriggerRepeatOperator::GetInputs() const
	{
		using namespace TriggerRepeatVertexNames;

		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(GetInputStartName(), FTriggerReadRef(TriggerEnable));
		InputDataReferences.AddDataReadReference(GetInputStopName(), FTriggerReadRef(TriggerDisable));
		InputDataReferences.AddDataReadReference(GetInputPeriodName(), FTimeReadRef(Period));
		return InputDataReferences;
	}

	FDataReferenceCollection FTriggerRepeatOperator::GetOutputs() const
	{
		using namespace TriggerRepeatVertexNames;

		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(GetOutputTriggerName(), FTriggerReadRef(TriggerOut));

		return OutputDataReferences;
	}

	void FTriggerRepeatOperator::Execute()
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


	TUniquePtr<IOperator> FTriggerRepeatOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		using namespace TriggerRepeatVertexNames;

		const FTriggerRepeatNode& PeriodicTriggerNode = static_cast<const FTriggerRepeatNode&>(InParams.Node);
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FTriggerReadRef TriggerEnable = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(GetInputStartName(), InParams.OperatorSettings);
		FTriggerReadRef TriggerDisable = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FTrigger>(GetInputStopName(), InParams.OperatorSettings);
		FTimeReadRef Period = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTime, float>(InputInterface, GetInputPeriodName());

		return MakeUnique<FTriggerRepeatOperator>(InParams.OperatorSettings, TriggerEnable, TriggerDisable, Period);
	}

	const FVertexInterface& FTriggerRepeatOperator::GetVertexInterface()
	{
		using namespace TriggerRepeatVertexNames;

		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FTrigger>(GetInputStartName(), GetInputStartDescription()),
				TInputDataVertexModel<FTrigger>(GetInputStopName(), GetInputStopDescription()),
				TInputDataVertexModel<FTime>(GetInputPeriodName(), GetInputPeriodDescription(), 0.2f)
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FTrigger>(GetOutputTriggerName(), GetOutputTriggerDescription())
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FTriggerRepeatOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::StandardNodes::Namespace, TEXT("TriggerRepeat"), TEXT("") };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = LOCTEXT("Metasound_PeriodicTriggerNodeDisplayName", "Trigger Repeat");
			Info.Description = LOCTEXT("Metasound_PeriodicTriggerNodeDescription", "Emits a trigger periodically based on the period duration given.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();

			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	FTriggerRepeatNode::FTriggerRepeatNode(const FNodeInitData& InInitData)
	:	FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<FTriggerRepeatOperator>())
	{
	}
}

#undef LOCTEXT_NAMESPACE //MetasoundPeriodicTriggerNode
