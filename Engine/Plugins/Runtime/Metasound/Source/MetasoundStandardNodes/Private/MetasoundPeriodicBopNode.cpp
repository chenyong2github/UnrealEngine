// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundPeriodicBopNode.h"

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
	METASOUND_REGISTER_NODE(FPeriodicBopNode)

	class FPeriodicBopOperator : public TExecutableOperator<FPeriodicBopOperator>
	{
		public:
			static const FNodeInfo& GetNodeInfo();
			static FVertexInterface DeclareVertexInterface();
			static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

			static constexpr float MinimumPeriodSeconds = 0.001f;
			static constexpr float MinimumPeriodSamples = 20.f;

			FPeriodicBopOperator(const FOperatorSettings& InSettings, const FFloatTimeReadRef& InPeriod);

			virtual const FDataReferenceCollection& GetInputs() const override;

			virtual const FDataReferenceCollection& GetOutputs() const override;

			void Execute();

		private:

			FOperatorSettings OperatorSettings;
			FBopWriteRef Bop;
			FFloatTimeReadRef Period;

			float ExecuteDurationInSamples;
			float SampleCountdown;

			FDataReferenceCollection InputDataReferences;
			FDataReferenceCollection OutputDataReferences;
	};

	FPeriodicBopOperator::FPeriodicBopOperator(const FOperatorSettings& InSettings, const FFloatTimeReadRef& InPeriod)
	:	OperatorSettings(InSettings)
	,	Bop(FBopWriteRef::CreateNew(InSettings))
	,	Period(InPeriod)
	,	ExecuteDurationInSamples(InSettings.GetNumFramesPerBlock())
	,	SampleCountdown(0.f)
	{
		OutputDataReferences.AddDataReadReference(TEXT("Bop"), FBopReadRef(Bop));
	}

	const FDataReferenceCollection& FPeriodicBopOperator::GetInputs() const
	{
		return InputDataReferences;
	}

	const FDataReferenceCollection& FPeriodicBopOperator::GetOutputs() const
	{
		return OutputDataReferences;
	}

	void FPeriodicBopOperator::Execute()
	{
		// Advance internal counter to get rid of old bops.
		Bop->AdvanceBlock();

		float PeriodInSamples = FMath::Max(Period->GetSeconds(), MinimumPeriodSeconds) * OperatorSettings.GetSampleRate();

		PeriodInSamples = FMath::Max(PeriodInSamples, MinimumPeriodSamples);

		while ((SampleCountdown - ExecuteDurationInSamples) <= 0.f)
		{
			uint32 Frame = FMath::RoundToInt(SampleCountdown);
			Bop->BopFrame(Frame);
			SampleCountdown += PeriodInSamples;
		}

		SampleCountdown -= ExecuteDurationInSamples;
	}


	TUniquePtr<IOperator> FPeriodicBopOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		const FPeriodicBopNode& PeriodicBopNode = static_cast<const FPeriodicBopNode&>(InParams.Node);

		FFloatTimeReadRef Period = FFloatTimeReadRef::CreateNew(PeriodicBopNode.GetDefaultPeriodInSeconds(), ETimeResolution::Seconds);

		if (InParams.InputDataReferences.ContainsDataReadReference<FFloatTime>(TEXT("Period")))
		{
			Period = InParams.InputDataReferences.GetDataReadReference<FFloatTime>(TEXT("Period"));
		}

		return MakeUnique<FPeriodicBopOperator>(InParams.OperatorSettings, Period);
	}

	FVertexInterface FPeriodicBopOperator::DeclareVertexInterface()
	{
		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertexModel<FFloatTime>(TEXT("Period"), LOCTEXT("PeriodTooltip", "The period of the bops in seconds."))
			),
			FOutputVertexInterface(
				TOutputDataVertexModel<FBop>(TEXT("Bop"), LOCTEXT("BopTooltip", "The output bop"))
			)
		);

		return Interface;
	}

	const FNodeInfo& FPeriodicBopOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeInfo
		{
			FNodeInfo Info;
			Info.ClassName = FName(TEXT("PeriodicBop"));
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


	FPeriodicBopNode::FPeriodicBopNode(const FString& InName, float InDefaultPeriodInSeconds)
	:	FNodeFacade(InName, TFacadeOperatorClass<FPeriodicBopOperator>())
	,	DefaultPeriod(InDefaultPeriodInSeconds)
	{
	}

	FPeriodicBopNode::FPeriodicBopNode(const FNodeInitData& InInitData)
		: FPeriodicBopNode(InInitData.InstanceName, 1.0f)
	{}

	float FPeriodicBopNode::GetDefaultPeriodInSeconds() const
	{
		return DefaultPeriod;
	}

}

#undef LOCTEXT_NAMESPACE //MetasoundPeriodicBopNode
