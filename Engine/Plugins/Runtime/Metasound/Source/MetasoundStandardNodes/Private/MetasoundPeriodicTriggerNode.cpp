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

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	METASOUND_REGISTER_NODE(FPeriodicTriggerNode)

	class FPeriodicTriggerOperator : public TExecutableOperator<FPeriodicTriggerOperator>
	{
		public:
			static const FNodeInfo& GetNodeInfo();
			static FVertexInterface DeclareVertexInterface();
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			static constexpr float MinimumPeriodSeconds = 0.001f;
			static constexpr float MinimumPeriodSamples = 20.f;

			FPeriodicTriggerOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerEnable, const FTriggerReadRef& InTriggerDisable, const FFloatTimeReadRef& InPeriod);

			virtual FDataReferenceCollection GetInputs() const override;

			virtual FDataReferenceCollection GetOutputs() const override;

			void Execute();

		private:
			bool bEnabled;

			FTriggerWriteRef TriggerOut;

			FTriggerReadRef TriggerEnable;
			FTriggerReadRef TriggerDisable;

			FFloatTimeReadRef Period;

			float ExecuteDurationInSamples;
			float SampleRate;
			float SampleCountdown;
	};

	FPeriodicTriggerOperator::FPeriodicTriggerOperator(const FOperatorSettings& InSettings, const FTriggerReadRef& InTriggerEnable, const FTriggerReadRef& InTriggerDisable, const FFloatTimeReadRef& InPeriod)
	:	bEnabled(false)
	,	TriggerOut(FTriggerWriteRef::CreateNew(InSettings))
	,	TriggerEnable(InTriggerEnable)
	,	TriggerDisable(InTriggerDisable)
	,	Period(InPeriod)
	,	ExecuteDurationInSamples(InSettings.GetNumFramesPerBlock())
	,	SampleRate(InSettings.GetSampleRate())
	,	SampleCountdown(0.f)
	{
	}

	FDataReferenceCollection FPeriodicTriggerOperator::GetInputs() const
	{
		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(TEXT("Period"), FFloatTimeReadRef(Period));
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
			}
		);

		TriggerDisable->ExecuteBlock([](int32, int32) { },
			[this](int32 StartFrame, int32 EndFrame)
			{
				bEnabled = false;
			}
		);

		// Advance internal counter to get rid of old bops.
		TriggerOut->AdvanceBlock();

		if (bEnabled)
		{
			float PeriodInSamples = FMath::Max(Period->GetSeconds(), MinimumPeriodSeconds) * SampleRate;

			PeriodInSamples = FMath::Max(PeriodInSamples, MinimumPeriodSamples);

			while ((SampleCountdown - ExecuteDurationInSamples) <= 0.f)
			{
				uint32 Frame = FMath::RoundToInt(SampleCountdown);
				TriggerOut->BopFrame(Frame);
				SampleCountdown += PeriodInSamples;
			}

			SampleCountdown -= ExecuteDurationInSamples;
		}
	}


	TUniquePtr<IOperator> FPeriodicTriggerOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FPeriodicTriggerNode& PeriodicBopNode = static_cast<const FPeriodicTriggerNode&>(InParams.Node);

		FFloatTimeReadRef Period = FFloatTimeReadRef::CreateNew(PeriodicBopNode.GetDefaultPeriodInSeconds(), ETimeResolution::Seconds);

		if (InParams.InputDataReferences.ContainsDataReadReference<FFloatTime>(TEXT("Period")))
		{
			Period = InParams.InputDataReferences.GetDataReadReference<FFloatTime>(TEXT("Period"));
		}

		FTriggerReadRef TriggerEnable = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FBop>(TEXT("Activate"), InParams.OperatorSettings);
		FTriggerReadRef TriggerDisable = InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FBop>(TEXT("Deactivate"), InParams.OperatorSettings);

		return MakeUnique<FPeriodicTriggerOperator>(InParams.OperatorSettings, TriggerEnable, TriggerDisable, Period);
	}

	FVertexInterface FPeriodicTriggerOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FFloatTime>(TEXT("Period"), LOCTEXT("PeriodTooltip", "The period to trigger in seconds.")),
				TInputDataVertexModel<FBop>(TEXT("Activate"), LOCTEXT("TriggerEnableTooltip", "Enables executing periodic output triggers.")),
				TInputDataVertexModel<FBop>(TEXT("Deactivate"), LOCTEXT("TriggerDisableTooltip", "Disables executing periodic output triggers."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FBop>(TEXT("Out"), LOCTEXT("TriggerOutTooltip", "The periodically generated output trigger"))
			)
		);

		return Interface;
	}

	const FNodeInfo& FPeriodicTriggerOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeInfo
		{
			FNodeInfo Info;
			Info.ClassName = "PeriodicBop";
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.Description = LOCTEXT("Metasound_PeriodicBopNodeDescription", "Emits a bop periodically based on the period duration given.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = DeclareVertexInterface();

			return Info;
		};

		static const FNodeInfo Info = InitNodeInfo();

		return Info;
	}


	FPeriodicTriggerNode::FPeriodicTriggerNode(const FString& InName, float InDefaultPeriodInSeconds)
	:	FNodeFacade(InName, TFacadeOperatorClass<FPeriodicTriggerOperator>())
	,	DefaultPeriod(InDefaultPeriodInSeconds)
	{
	}

	FPeriodicTriggerNode::FPeriodicTriggerNode(const FNodeInitData& InInitData)
		: FPeriodicTriggerNode(InInitData.InstanceName, 1.0f)
	{}

	float FPeriodicTriggerNode::GetDefaultPeriodInSeconds() const
	{
		return DefaultPeriod;
	}
}

#undef LOCTEXT_NAMESPACE //MetasoundPeriodicBopNode
