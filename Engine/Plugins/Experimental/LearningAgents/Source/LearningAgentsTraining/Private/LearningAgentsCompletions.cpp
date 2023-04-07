// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsCompletions.h"

#include "LearningAgentsTrainer.h"
#include "LearningAgentsType.h"
#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningCompletion.h"
#include "LearningCompletionObject.h"
#include "LearningLog.h"

#include "GameFramework/Actor.h"
#include "VisualLogger/VisualLogger.h"

#define UE_LEARNING_AGENTS_VLOG_STRING(Owner, Category, Verbosity, Location, Color, Format, ...) \
	UE_VLOG_LOCATION(Owner, Category, Verbosity, Location, 0.0f, Color, Format, ##__VA_ARGS__)

#define UE_LEARNING_AGENTS_VLOG_TRANSFORM(Owner, Category, Verbosity, Location, Rotation, Color, Format, ...) \
	UE_VLOG_SEGMENT(Owner, Category, Verbosity, Location, Location + 15.0f * Rotation.RotateVector(FVector::ForwardVector), FColor::Red, TEXT("")); \
	UE_VLOG_SEGMENT(Owner, Category, Verbosity, Location, Location + 15.0f * Rotation.RotateVector(FVector::RightVector), FColor::Green, TEXT("")); \
	UE_VLOG_SEGMENT(Owner, Category, Verbosity, Location, Location + 15.0f * Rotation.RotateVector(FVector::UpVector), FColor::Blue, TEXT("")); \
	UE_VLOG_OBOX(Owner, Category, Verbosity, FBox(10.0f * FVector(-1, -1, -1), 10.0f * FVector(1, 1, 1)), FTransform(Rotation, Location, FVector::OneVector).ToMatrixNoScale(), Color, TEXT("")); \
	UE_LEARNING_AGENTS_VLOG_STRING(Owner, Category, Verbosity, Location + FVector(0.0f, 0.0f, 20.0f), Color, Format, ##__VA_ARGS__)

#define UE_LEARNING_AGENTS_VLOG_PLANE(Owner, Category, Verbosity, Location, Rotation, Axis0, Axis1, Color, Format, ...) \
	UE_VLOG_OBOX(Owner, Category, Verbosity, FBox(-25.0f * (Axis0 + Axis1), 25.0f * (Axis0 + Axis1)), FTransform(Rotation, Location, FVector::OneVector).ToMatrixNoScale(), Color, Format, ##__VA_ARGS__)

namespace UE::Learning::Agents::Private
{
	template<typename CompletionUObject, typename CompletionFObject, typename... InArgTypes>
	CompletionUObject* AddCompletion(ULearningAgentsTrainer* AgentTrainer, const FName Name, InArgTypes&& ...Args)
	{
		if (!AgentTrainer)
		{
			UE_LOG(LogLearning, Error, TEXT("AgentTrainer is nullptr")); return nullptr;
		}

		CompletionUObject* Completion = NewObject<CompletionUObject>(AgentTrainer, Name);

		Completion->CompletionObject = MakeShared<CompletionFObject>(
			Completion->GetFName(),
			AgentTrainer->GetAgentType()->GetInstanceData().ToSharedRef(),
			AgentTrainer->GetAgentType()->GetMaxInstanceNum(),
			Forward<InArgTypes>(Args)...);

		AgentTrainer->AddCompletion(Completion, Completion->CompletionObject.ToSharedRef());

		return Completion;
	}
}

//------------------------------------------------------------------

UConditionalCompletion* UConditionalCompletion::AddConditionalCompletion(ULearningAgentsTrainer* AgentTrainer, const FName Name, const ELearningAgentsCompletion InCompletionMode)
{
	return UE::Learning::Agents::Private::AddCompletion<UConditionalCompletion, UE::Learning::FConditionalCompletion>(
		AgentTrainer,
		Name,
		InCompletionMode == ELearningAgentsCompletion::Termination ?
		UE::Learning::ECompletionMode::Terminated :
		UE::Learning::ECompletionMode::Truncated);
}

void UConditionalCompletion::SetConditionalCompletion(const int32 AgentId, const bool bIsComplete)
{
	const TLearningArrayView<1, bool> View = CompletionObject->InstanceData->View(CompletionObject->ConditionHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= View.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, View.Num<0>() - 1);
		return;
	}

	View[AgentId] = bIsComplete;
}

#if ENABLE_VISUAL_LOG
void UConditionalCompletion::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UConditionalCompletion::VisualLog);

	const TLearningArrayView<1, const bool> ConditionView = CompletionObject->InstanceData->ConstView(CompletionObject->ConditionHandle);
	const TLearningArrayView<1, const UE::Learning::ECompletionMode> CompletionView = CompletionObject->InstanceData->ConstView(CompletionObject->CompletionHandle);

	if (const ULearningAgentsTrainer* AgentTrainer = Cast<ULearningAgentsTrainer>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentTrainer->GetAgent(Instance)))
			{
				UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
					Actor->GetActorLocation(),
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nCondition: %s\nCompletion: %s"),
					Instance,
					ConditionView[Instance] ? TEXT("true") : TEXT("false"),
					UE::Learning::Completion::CompletionModeString(CompletionView[Instance]));
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UPlanarPositionDifferenceCompletion* UPlanarPositionDifferenceCompletion::AddPlanarPositionDifferenceCompletion(
	ULearningAgentsTrainer* AgentTrainer,
	const FName Name,
	const float Threshold,
	const ELearningAgentsCompletion InCompletionMode,
	const FVector Axis0,
	const FVector Axis1)
{
	return UE::Learning::Agents::Private::AddCompletion<UPlanarPositionDifferenceCompletion, UE::Learning::FPlanarPositionDifferenceCompletion>(
		AgentTrainer,
		Name,
		1,
		Threshold,
		InCompletionMode == ELearningAgentsCompletion::Termination ?
		UE::Learning::ECompletionMode::Terminated :
		UE::Learning::ECompletionMode::Truncated,
		Axis0,
		Axis1);
}

void UPlanarPositionDifferenceCompletion::SetPlanarPositionDifferenceCompletion(const int32 AgentId, const FVector Position0, const FVector Position1)
{
	const TLearningArrayView<2, FVector> Position0View = CompletionObject->InstanceData->View(CompletionObject->Position0Handle);
	const TLearningArrayView<2, FVector> Position1View = CompletionObject->InstanceData->View(CompletionObject->Position1Handle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= Position0View.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, Position0View.Num<0>() - 1);
		return;
	}

	Position0View[AgentId][0] = Position0;
	Position1View[AgentId][0] = Position1;
}

#if ENABLE_VISUAL_LOG
void UPlanarPositionDifferenceCompletion::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UPlanarPositionDifferenceCompletion::VisualLog);

	const TLearningArrayView<2, const FVector> Position0View = CompletionObject->InstanceData->ConstView(CompletionObject->Position0Handle);
	const TLearningArrayView<2, const FVector> Position1View = CompletionObject->InstanceData->ConstView(CompletionObject->Position1Handle);
	const TLearningArrayView<1, const float> ThresholdView = CompletionObject->InstanceData->ConstView(CompletionObject->ThresholdHandle);
	const TLearningArrayView<1, const UE::Learning::ECompletionMode> CompletionView = CompletionObject->InstanceData->ConstView(CompletionObject->CompletionHandle);

	if (const ULearningAgentsTrainer* AgentTrainer = Cast<ULearningAgentsTrainer>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentTrainer->GetAgent(Instance)))
			{
				const FVector Position0 = Position0View[Instance][0];
				const FVector Position1 = Position1View[Instance][0];

				const FVector PlanarPosition0 = FVector(CompletionObject->Axis0.Dot(Position0), CompletionObject->Axis1.Dot(Position0), 0.0f);
				const FVector PlanarPosition1 = FVector(CompletionObject->Axis0.Dot(Position1), CompletionObject->Axis1.Dot(Position1), 0.0f);

				UE_VLOG_LOCATION(this, LogLearning, Display,
					Position0,
					10.0f,
					VisualLogColor.ToFColor(true),
					TEXT("Position0: [% 6.1f % 6.1f % 6.1f]\nPlanar Position0: [% 6.1f % 6.1f]"),
					Position0.X, Position0.Y, Position0.Z,
					PlanarPosition0.X, PlanarPosition0.Y);

				UE_LEARNING_AGENTS_VLOG_PLANE(this, LogLearning, Display,
					Position0,
					FQuat::Identity,
					CompletionObject->Axis0,
					CompletionObject->Axis1,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_VLOG_LOCATION(this, LogLearning, Display,
					Position1,
					10.0f,
					VisualLogColor.ToFColor(true),
					TEXT("Position1: [% 6.1f % 6.1f % 6.1f]\nPlanar Position1: [% 6.1f % 6.1f]"),
					Position1.X, Position1.Y, Position1.Z,
					PlanarPosition1.X, PlanarPosition1.Y);

				UE_LEARNING_AGENTS_VLOG_PLANE(this, LogLearning, Display,
					Position1,
					FQuat::Identity,
					CompletionObject->Axis0,
					CompletionObject->Axis1,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_VLOG_SEGMENT(this, LogLearning, Display,
					Position0,
					Position1,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nDistance: [% 6.3f]\nPlanar Distance: [% 6.3f]\nThreshold: [% 6.2f]\nCompletion: %s"),
					Instance,
					FVector::Distance(Position0, Position1),
					FVector::Distance(PlanarPosition0, PlanarPosition1),
					ThresholdView[Instance],
					UE::Learning::Completion::CompletionModeString(CompletionView[Instance]));
			}
		}
	}
}
#endif

//------------------------------------------------------------------

#undef UE_LEARNING_AGENTS_VLOG_STRING
#undef UE_LEARNING_AGENTS_VLOG_TRANSFORM
#undef UE_LEARNING_AGENTS_VLOG_PLANE
