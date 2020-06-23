// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundPeriodicBopNode.h"

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundPrimitives.h"

#define LOCTEXT_NAMESPACE "MetasoundPeriodicBopNode"

namespace Metasound
{
	class FPeriodicBopOperator : public TExecutableOperator<FPeriodicBopOperator>
	{
		public:
			static constexpr float MinimumPeriodSeconds = 0.001f;
			static constexpr float MinimumPeriodSamples = 20.f;

			FPeriodicBopOperator(const FOperatorSettings& InSettings, const FFloatTimeReadRef& InPeriod)
			:	OperatorSettings(InSettings)
			,	Period(InPeriod)
			,	ExecuteDurationInSamples(InSettings.FramesPerExecute)
			,	SampleCountdown(0.f)
			{
				OutputDataReferences.AddDataReadReference(TEXT("Bop"), FBopReadRef(Bop));
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
				// Advance internal counter to get rid of old bops.
				Bop->Advance(OperatorSettings.FramesPerExecute);

				float PeriodInSamples = FMath::Max(Period->GetSeconds(), MinimumPeriodSeconds) * OperatorSettings.SampleRate;

				PeriodInSamples = FMath::Max(PeriodInSamples, MinimumPeriodSamples);

				while ((SampleCountdown - ExecuteDurationInSamples) <= 0.f)
				{
					uint32 Frame = FMath::RoundToInt(SampleCountdown);
					Bop->BopFrame(Frame);
					SampleCountdown += PeriodInSamples;
				}

				SampleCountdown -= ExecuteDurationInSamples;
			}

		private:

			FOperatorSettings OperatorSettings;
			FBopWriteRef Bop;
			FFloatTimeReadRef Period;

			float ExecuteDurationInSamples;
			float SampleCountdown;

			FDataReferenceCollection InputDataReferences;
			FDataReferenceCollection OutputDataReferences;
	};


	TUniquePtr<IOperator> FPeriodicBopNode::FOperatorFactory::CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
	{
		const FPeriodicBopNode& PeriodicBopNode = static_cast<const FPeriodicBopNode&>(InNode);
		FFloatTimeReadRef Period(PeriodicBopNode.GetDefaultPeriodInSeconds(), ETimeResolution::Seconds);

		if (InInputDataReferences.ContainsDataReadReference<FFloatTime>(TEXT("Period")))
		{
			Period = InInputDataReferences.GetDataReadReference<FFloatTime>(TEXT("Period"));
		}

		return MakeUnique<FPeriodicBopOperator>(InOperatorSettings, Period);
	}

	const FName FPeriodicBopNode::ClassName = FName(TEXT("PeriodicBop"));

	FPeriodicBopNode::FPeriodicBopNode(const FString& InName, float InDefaultPeriodInSeconds)
	:	FNode(InName)
	,	DefaultPeriod(InDefaultPeriodInSeconds)
	{
		AddInputDataVertexDescription<FFloatTime>(TEXT("Period"), LOCTEXT("PeriodTooltip", "The period of the bops in seconds."));
		AddOutputDataVertexDescription<FBop>(TEXT("Bop"), LOCTEXT("BopTooltip", "The output bop"));
	}

	FPeriodicBopNode::~FPeriodicBopNode()
	{
	}

	float FPeriodicBopNode::GetDefaultPeriodInSeconds() const
	{
		return DefaultPeriod;
	}

	const FName& FPeriodicBopNode::GetClassName() const
	{
		return ClassName;
	}

	IOperatorFactory& FPeriodicBopNode::GetDefaultOperatorFactory()
	{
		return Factory;
	}
}

#undef LOCTEXT_NAMESPACE //MetasoundPeriodicBopNode
