// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsRewards.h"

#include "LearningAgentsTrainer.h"
#include "LearningAgentsType.h"
#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningRewardObject.h"
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
	template<typename RewardUObject, typename RewardFObject, typename... InArgTypes>
	RewardUObject* AddReward(ULearningAgentsTrainer* AgentTrainer, FName Name, InArgTypes&& ...Args)
	{
		if (!AgentTrainer)
		{
			UE_LOG(LogLearning, Error, TEXT("AgentTrainer is nullptr"));
			return nullptr;
		}

		RewardUObject* Reward = NewObject<RewardUObject>(AgentTrainer, Name);

		Reward->RewardObject = MakeShared<RewardFObject>(
			Reward->GetFName(),
			AgentTrainer->GetAgentType()->GetInstanceData().ToSharedRef(),
			AgentTrainer->GetAgentType()->GetMaxInstanceNum(),
			Forward<InArgTypes>(Args)...);

		AgentTrainer->AddReward(Reward, Reward->RewardObject.ToSharedRef());

		return Reward;
	}
}

//------------------------------------------------------------------

UFloatReward* UFloatReward::AddFloatReward(ULearningAgentsTrainer* AgentTrainer, FName Name, float Weight)
{
	return UE::Learning::Agents::Private::AddReward<UFloatReward, UE::Learning::FFloatReward>(AgentTrainer, Name, Weight);
}

void UFloatReward::SetFloatReward(int32 AgentId, float Reward)
{
	TLearningArrayView<1, float> View = RewardObject->InstanceData->View(RewardObject->ValueHandle);

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

	View[AgentId] = Reward;
}

#if ENABLE_VISUAL_LOG
void UFloatReward::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UFloatReward::VisualLog);

	const TLearningArrayView<1, const float> ValueView = RewardObject->InstanceData->ConstView(RewardObject->ValueHandle);
	const TLearningArrayView<1, const float> WeightView = RewardObject->InstanceData->ConstView(RewardObject->WeightHandle);
	const TLearningArrayView<1, const float> RewardView = RewardObject->InstanceData->ConstView(RewardObject->RewardHandle);

	if (const ULearningAgentsTrainer* AgentTrainer = Cast<ULearningAgentsTrainer>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentTrainer->GetAgent(Instance)))
			{
				UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
					Actor->GetActorLocation(),
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nWeight: [% 6.2f]\nValue: [% 6.2f]\nReward: [% 6.3f]"),
					Instance,
					WeightView[Instance],
					ValueView[Instance],
					RewardView[Instance]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UScalarVelocityReward* UScalarVelocityReward::AddScalarVelocityReward(ULearningAgentsTrainer* AgentTrainer, FName Name, float Weight, float Scale)
{
	return UE::Learning::Agents::Private::AddReward<UScalarVelocityReward, UE::Learning::FScalarVelocityReward>(AgentTrainer, Name, Weight, Scale);
}

void UScalarVelocityReward::SetScalarVelocityReward(int32 AgentId, float Velocity)
{
	TLearningArrayView<1, float> VelocityView = RewardObject->InstanceData->View(RewardObject->VelocityHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= VelocityView.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, VelocityView.Num<0>() - 1);
		return;
	}

	VelocityView[AgentId] = Velocity;
}

#if ENABLE_VISUAL_LOG
void UScalarVelocityReward::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UScalarVelocityReward::VisualLog);

	const TLearningArrayView<1, const float> VelocityView = RewardObject->InstanceData->ConstView(RewardObject->VelocityHandle);
	const TLearningArrayView<1, const float> WeightView = RewardObject->InstanceData->ConstView(RewardObject->WeightHandle);
	const TLearningArrayView<1, const float> ScaleView = RewardObject->InstanceData->ConstView(RewardObject->ScaleHandle);
	const TLearningArrayView<1, const float> RewardView = RewardObject->InstanceData->ConstView(RewardObject->RewardHandle);

	if (const ULearningAgentsTrainer* AgentTrainer = Cast<ULearningAgentsTrainer>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<const AActor>(AgentTrainer->GetAgent(Instance)))
			{
				UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
					Actor->GetActorLocation(),
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nWeight: [% 6.2f]\nScale: [% 6.2f]\nVelocity: [% 6.2f]\nReward: [% 6.3f]"),
					Instance,
					WeightView[Instance],
					ScaleView[Instance],
					VelocityView[Instance],
					RewardView[Instance]);
			}
		}
	}
}
#endif

ULocalDirectionalVelocityReward* ULocalDirectionalVelocityReward::AddLocalDirectionalVelocityReward(ULearningAgentsTrainer* AgentTrainer, FName Name, float Weight, float Scale, FVector Axis)
{
	return UE::Learning::Agents::Private::AddReward<ULocalDirectionalVelocityReward, UE::Learning::FLocalDirectionalVelocityReward>(AgentTrainer, Name, Weight, Scale, Axis);
}

void ULocalDirectionalVelocityReward::SetLocalDirectionalVelocityReward(int32 AgentId, FVector Velocity, FRotator RelativeRotation)
{
	TLearningArrayView<1, FVector> VelocityView = RewardObject->InstanceData->View(RewardObject->VelocityHandle);
	TLearningArrayView<1, FQuat> RelativeRotationView = RewardObject->InstanceData->View(RewardObject->RelativeRotationHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= VelocityView.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, VelocityView.Num<0>() - 1);
		return;
	}

	VelocityView[AgentId] = Velocity;
	RelativeRotationView[AgentId] = FQuat::MakeFromRotator(RelativeRotation);
}

#if ENABLE_VISUAL_LOG
void ULocalDirectionalVelocityReward::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULocalDirectionalVelocityReward::VisualLog);

	const TLearningArrayView<1, const FVector> VelocityView = RewardObject->InstanceData->View(RewardObject->VelocityHandle);
	const TLearningArrayView<1, const FQuat> RelativeRotationView = RewardObject->InstanceData->View(RewardObject->RelativeRotationHandle);
	const TLearningArrayView<1, const float> WeightView = RewardObject->InstanceData->ConstView(RewardObject->WeightHandle);
	const TLearningArrayView<1, const float> ScaleView = RewardObject->InstanceData->ConstView(RewardObject->ScaleHandle);
	const TLearningArrayView<1, const float> RewardView = RewardObject->InstanceData->ConstView(RewardObject->RewardHandle);

	if (const ULearningAgentsTrainer* AgentTrainer = Cast<ULearningAgentsTrainer>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentTrainer->GetAgent(Instance)))
			{
				const FVector Velocity = VelocityView[Instance];
				const FQuat RelativeRotation = RelativeRotationView[Instance];
				const FVector LocalVelocity = RelativeRotation.UnrotateVector(Velocity);
				const FVector Direction = RelativeRotation.RotateVector(RewardObject->Axis);

				UE_VLOG_ARROW(this, LogLearning, Display,
					Actor->GetActorLocation(),
					Actor->GetActorLocation() + Velocity,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
					Actor->GetActorLocation() + Velocity,
					VisualLogColor.ToFColor(true),
					TEXT("Velocity: [% 6.3f % 6.3f % 6.3f]\nLocal Velocity: [% 6.3f % 6.3f % 6.3f]"),
					Velocity.X, Velocity.Y, Velocity.Z,
					LocalVelocity.X, LocalVelocity.Y, LocalVelocity.Z);

				UE_VLOG_ARROW(this, LogLearning, Display,
					Actor->GetActorLocation(),
					Actor->GetActorLocation() + 100.0f * Direction,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
					Actor->GetActorLocation() + 100.0f * Direction,
					VisualLogColor.ToFColor(true),
					TEXT("Direction: [% 6.3f % 6.3f % 6.3f]\nLocal Direction: [% 6.3f % 6.3f % 6.3f]"),
					Direction.X, Direction.Y, Direction.Z,
					RewardObject->Axis.X, RewardObject->Axis.Y, RewardObject->Axis.Z);

				UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
					Actor->GetActorLocation(),
					RelativeRotation,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nDot Product: [% 6.3f]\nWeight: [% 6.2f]\nScale: [% 6.2f]\nReward: [% 6.3f]"),
					Instance,
					LocalVelocity.Dot(RewardObject->Axis),
					WeightView[Instance],
					ScaleView[Instance],
					RewardView[Instance]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UPlanarPositionDifferencePenalty* UPlanarPositionDifferencePenalty::AddPlanarPositionDifferencePenalty(ULearningAgentsTrainer* AgentTrainer, FName Name, float Weight, float Scale, float Threshold, FVector Axis0, FVector Axis1)
{
	return UE::Learning::Agents::Private::AddReward<UPlanarPositionDifferencePenalty, UE::Learning::FPlanarPositionDifferencePenalty>(AgentTrainer, Name, Weight, Scale, Threshold, Axis0, Axis1);
}

void UPlanarPositionDifferencePenalty::SetPlanarPositionDifferencePenalty(int32 AgentId, FVector Position0, FVector Position1)
{
	TLearningArrayView<1, FVector> Position0View = RewardObject->InstanceData->View(RewardObject->Position0Handle);
	TLearningArrayView<1, FVector> Position1View = RewardObject->InstanceData->View(RewardObject->Position1Handle);

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

	Position0View[AgentId] = Position0;
	Position1View[AgentId] = Position1;
}

#if ENABLE_VISUAL_LOG
void UPlanarPositionDifferencePenalty::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UPlanarPositionDifferencePenalty::VisualLog);

	const TLearningArrayView<1, const FVector> Position0View = RewardObject->InstanceData->ConstView(RewardObject->Position0Handle);
	const TLearningArrayView<1, const FVector> Position1View = RewardObject->InstanceData->ConstView(RewardObject->Position1Handle);
	const TLearningArrayView<1, const float> WeightView = RewardObject->InstanceData->ConstView(RewardObject->WeightHandle);
	const TLearningArrayView<1, const float> ScaleView = RewardObject->InstanceData->ConstView(RewardObject->ScaleHandle);
	const TLearningArrayView<1, const float> ThresholdView = RewardObject->InstanceData->ConstView(RewardObject->ThresholdHandle);
	const TLearningArrayView<1, const float> RewardView = RewardObject->InstanceData->ConstView(RewardObject->RewardHandle);

	if (const ULearningAgentsTrainer* AgentTrainer = Cast<ULearningAgentsTrainer>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentTrainer->GetAgent(Instance)))
			{
				const FVector Position0 = Position0View[Instance];
				const FVector Position1 = Position1View[Instance];

				const FVector PlanarPosition0 = FVector(RewardObject->Axis0.Dot(Position0), RewardObject->Axis1.Dot(Position0), 0.0f);
				const FVector PlanarPosition1 = FVector(RewardObject->Axis0.Dot(Position1), RewardObject->Axis1.Dot(Position1), 0.0f);

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
					RewardObject->Axis0,
					RewardObject->Axis1,
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
					RewardObject->Axis0,
					RewardObject->Axis1,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_VLOG_SEGMENT(this, LogLearning, Display,
					Position0,
					Position1,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nDistance: [% 6.3f]\nPlanar Distance: [% 6.3f]\nWeight: [% 6.2f]\nScale: [% 6.2f]\nThreshold: [% 6.2f]\nReward: [% 6.3f]"),
					Instance,
					FVector::Distance(Position0, Position1),
					FVector::Distance(PlanarPosition0, PlanarPosition1),
					WeightView[Instance],
					ScaleView[Instance],
					ThresholdView[Instance],
					RewardView[Instance]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UPositionArraySimilarityReward* UPositionArraySimilarityReward::AddPositionArraySimilarityReward(ULearningAgentsTrainer* AgentTrainer, FName Name, int32 PositionNum, float Weight, float Scale)
{
	return UE::Learning::Agents::Private::AddReward<UPositionArraySimilarityReward, UE::Learning::FPositionArraySimilarityReward>(AgentTrainer, Name, PositionNum, Weight, Scale);
}

void UPositionArraySimilarityReward::SetPositionArraySimilarityReward(
	int32 AgentId,
	const TArray<FVector>& Positions0,
	const TArray<FVector>& Positions1,
	FVector RelativePosition0,
	FVector RelativePosition1,
	FRotator RelativeRotation0,
	FRotator RelativeRotation1)
{
	TLearningArrayView<2, FVector> Positions0View = RewardObject->InstanceData->View(RewardObject->Positions0Handle);
	TLearningArrayView<2, FVector> Positions1View = RewardObject->InstanceData->View(RewardObject->Positions1Handle);
	TLearningArrayView<1, FVector> RelativePosition0View = RewardObject->InstanceData->View(RewardObject->RelativePosition0Handle);
	TLearningArrayView<1, FVector> RelativePosition1View = RewardObject->InstanceData->View(RewardObject->RelativePosition1Handle);
	TLearningArrayView<1, FQuat> RelativeRotation0View = RewardObject->InstanceData->View(RewardObject->RelativeRotation0Handle);
	TLearningArrayView<1, FQuat> RelativeRotation1View = RewardObject->InstanceData->View(RewardObject->RelativeRotation1Handle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= Positions0View.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, Positions0View.Num<0>() - 1);
		return;
	}

	const int32 PositionNum = Positions0View.Num<1>();

	if (Positions0.Num() != PositionNum || Positions1.Num() != PositionNum)
	{
		UE_LOG(LogLearning, Error, TEXT("Incorrect number of positions in array. Got %i and %i, expected %i."), Positions0.Num(), Positions1.Num(), PositionNum);
		return;
	}

	RelativePosition0View[AgentId] = RelativePosition0;
	RelativePosition1View[AgentId] = RelativePosition1;
	RelativeRotation0View[AgentId] = FQuat::MakeFromRotator(RelativeRotation0);
	RelativeRotation1View[AgentId] = FQuat::MakeFromRotator(RelativeRotation1);
	UE::Learning::Array::Copy(Positions0View[AgentId], TLearningArrayView<1, const FVector>(Positions0));
	UE::Learning::Array::Copy(Positions1View[AgentId], TLearningArrayView<1, const FVector>(Positions1));
}

#if ENABLE_VISUAL_LOG
void UPositionArraySimilarityReward::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UPositionArraySimilarityReward::VisualLog);

	const TLearningArrayView<2, const FVector> Positions0View = RewardObject->InstanceData->ConstView(RewardObject->Positions0Handle);
	const TLearningArrayView<2, const FVector> Positions1View = RewardObject->InstanceData->ConstView(RewardObject->Positions1Handle);
	const TLearningArrayView<1, const FVector> RelativePosition0View = RewardObject->InstanceData->ConstView(RewardObject->RelativePosition0Handle);
	const TLearningArrayView<1, const FVector> RelativePosition1View = RewardObject->InstanceData->ConstView(RewardObject->RelativePosition1Handle);
	const TLearningArrayView<1, const FQuat> RelativeRotation0View = RewardObject->InstanceData->ConstView(RewardObject->RelativeRotation0Handle);
	const TLearningArrayView<1, const FQuat> RelativeRotation1View = RewardObject->InstanceData->ConstView(RewardObject->RelativeRotation1Handle);
	const TLearningArrayView<1, const float> WeightView = RewardObject->InstanceData->ConstView(RewardObject->WeightHandle);
	const TLearningArrayView<1, const float> ScaleView = RewardObject->InstanceData->ConstView(RewardObject->ScaleHandle);
	const TLearningArrayView<1, const float> ThresholdView = RewardObject->InstanceData->ConstView(RewardObject->ThresholdHandle);
	const TLearningArrayView<1, const float> RewardView = RewardObject->InstanceData->ConstView(RewardObject->RewardHandle);

	const int32 PositionNum = Positions0View.Num<1>();
		
	if (const ULearningAgentsTrainer* AgentTrainer = Cast<ULearningAgentsTrainer>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentTrainer->GetAgent(Instance)))
			{
				const FVector RelativePosition0 = RelativePosition0View[Instance];
				const FVector RelativePosition1 = RelativePosition1View[Instance];
				const FQuat RelativeRotation0 = RelativeRotation0View[Instance];
				const FQuat RelativeRotation1 = RelativeRotation1View[Instance];

				for (int32 PositionIdx = 0; PositionIdx < PositionNum; PositionIdx++)
				{
					const FVector Position0 = Positions0View[Instance][PositionIdx];
					const FVector Position1 = Positions1View[Instance][PositionIdx];

					const FVector LocalPosition0 = RelativeRotation0.UnrotateVector(Positions0View[Instance][PositionIdx] - RelativePosition0);
					const FVector LocalPosition1 = RelativeRotation1.UnrotateVector(Positions1View[Instance][PositionIdx] - RelativePosition1);

					UE_VLOG_LOCATION(this, LogLearning, Display,
						Position0,
						10.0f,
						VisualLogColor.ToFColor(true),
						TEXT("Position0: [% 6.1f % 6.1f % 6.1f]\nLocal Position0: [% 6.1f % 6.1f % 6.1f]"),
						Position0.X, Position0.Y, Position0.Z,
						LocalPosition0.X, LocalPosition0.Y, LocalPosition0.Z);

					UE_VLOG_LOCATION(this, LogLearning, Display,
						Position1,
						10.0f,
						VisualLogColor.ToFColor(true),
						TEXT("Position1: [% 6.1f % 6.1f % 6.1f]\nLocal Position1: [% 6.1f % 6.1f % 6.1f]"),
						Position1.X, Position1.Y, Position1.Z,
						LocalPosition1.X, LocalPosition1.Y, LocalPosition1.Z);

					UE_VLOG_SEGMENT(this, LogLearning, Display,
						Position0,
						Position1,
						VisualLogColor.ToFColor(true),
						TEXT("Distance: [% 6.1f]\nLocal Distance: [% 6.1f]"),
						FVector::Distance(Position0, Position1),
						FVector::Distance(LocalPosition0, LocalPosition1));
				}

				UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
					RelativePosition0,
					RelativeRotation0,
					VisualLogColor.ToFColor(true),
					TEXT("Relative Transform 0"));

				UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
					RelativePosition1,
					RelativeRotation1,
					VisualLogColor.ToFColor(true),
					TEXT("Relative Transform 1"));

				UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
					Actor->GetActorLocation(),
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nWeight: [% 6.2f]\nScale: [% 6.2f]\nThreshold: [% 6.2f]\nReward: [% 6.3f]"),
					Instance,
					WeightView[Instance],
					ScaleView[Instance],
					ThresholdView[Instance],
					RewardView[Instance]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

#undef UE_LEARNING_AGENTS_VLOG_STRING
#undef UE_LEARNING_AGENTS_VLOG_TRANSFORM
#undef UE_LEARNING_AGENTS_VLOG_PLANE