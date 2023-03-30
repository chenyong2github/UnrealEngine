// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsObservations.h"

#include "LearningAgentsType.h"
#include "LearningArray.h"
#include "LearningArrayMap.h"
#include "LearningFeatureObject.h"
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
	UE_VLOG_OBOX(Owner, Category, Verbosity, FBox(25.0f * FVector(-1, -1, 0), 25.0f * FVector(1, 1, 0)), FTransform(Rotation, Location, FVector::OneVector).ToMatrixNoScale(), Color, Format, ##__VA_ARGS__)

namespace UE::Learning::Agents::Private
{
	template<typename ObservationUObject, typename ObservationFObject, typename... InArgTypes>
	ObservationUObject* AddObservation(ULearningAgentsType* AgentType, FName Name, InArgTypes&& ...Args)
	{
		if (!AgentType)
		{
			UE_LOG(LogLearning, Error, TEXT("AgentType is nullptr"));
			return nullptr;
		}

		ObservationUObject* Observation = NewObject<ObservationUObject>(AgentType, Name);

		Observation->FeatureObject = MakeShared<ObservationFObject>(
			Observation->GetFName(),
			AgentType->GetInstanceData().ToSharedRef(),
			AgentType->GetMaxInstanceNum(),
			Forward<InArgTypes>(Args)...);

		AgentType->AddObservation(Observation, Observation->FeatureObject.ToSharedRef());

		return Observation;
	}
}

//------------------------------------------------------------------

UFloatObservation* UFloatObservation::AddFloatObservation(ULearningAgentsType* AgentType, FName Name, float Scale)
{
	return UE::Learning::Agents::Private::AddObservation<UFloatObservation, UE::Learning::FFloatFeature>(AgentType, Name, 1, Scale);
}

void UFloatObservation::SetFloatObservation(int32 AgentId, float Value)
{
	TLearningArrayView<2, float> View = FeatureObject->InstanceData->View(FeatureObject->ValueHandle);

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

	View[AgentId][0] = Value;
}

#if ENABLE_VISUAL_LOG
void UFloatObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UFloatObservation::VisualLog);

	const TLearningArrayView<2, const float> ValueView = FeatureObject->InstanceData->ConstView(FeatureObject->ValueHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
					Actor->GetActorLocation(),
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nScale: [% 6.2f]\nValue: [% 6.2f]\nEncoded: [% 6.3f]"),
					Instance,
					ValueView[Instance][0],
					FeatureObject->Scale,
					FeatureView[Instance][0]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UVectorObservation* UVectorObservation::AddVectorObservation(ULearningAgentsType* AgentType, FName Name, float Scale)
{
	return UE::Learning::Agents::Private::AddObservation<UVectorObservation, UE::Learning::FFloatFeature>(AgentType, Name, 3, Scale);
}

void UVectorObservation::SetVectorObservation(int32 AgentId, FVector Vector)
{
	TLearningArrayView<2, float> View = FeatureObject->InstanceData->View(FeatureObject->ValueHandle);

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

	View[AgentId][0] = Vector.X;
	View[AgentId][1] = Vector.Y;
	View[AgentId][2] = Vector.Z;
}

#if ENABLE_VISUAL_LOG
void UVectorObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UVectorObservation::VisualLog);

	const TLearningArrayView<2, const float> ValueView = FeatureObject->InstanceData->ConstView(FeatureObject->ValueHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const FVector Vector(ValueView[Instance][0], ValueView[Instance][1], ValueView[Instance][2]);

				UE_VLOG_ARROW(this, LogLearning, Display,
					Actor->GetActorLocation(),
					Actor->GetActorLocation() + Vector,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
					Actor->GetActorLocation() + Vector,
					VisualLogColor.ToFColor(true),
					TEXT("Vector: [% 6.4f % 6.4f % 6.4f]"),
					Vector.X, Vector.Y, Vector.Z);

				UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
					Actor->GetActorLocation(),
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: [% 6.3f % 6.3f % 6.3f]"),
					Instance,
					FeatureObject->Scale,
					FeatureView[Instance][0], FeatureView[Instance][1], FeatureView[Instance][2]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UAngleObservation* UAngleObservation::AddAngleObservation(ULearningAgentsType* AgentType, FName Name, float Scale)
{
	return UE::Learning::Agents::Private::AddObservation<UAngleObservation, UE::Learning::FAngleFeature>(AgentType, Name, 1, Scale);
}

void UAngleObservation::SetAngleObservation(int32 AgentId, float Angle, float RelativeAngle)
{
	TLearningArrayView<2, float> AngleView = FeatureObject->InstanceData->View(FeatureObject->AngleHandle);
	TLearningArrayView<1, float> RelativeAngleView = FeatureObject->InstanceData->View(FeatureObject->RelativeAngleHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= AngleView.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, AngleView.Num<0>() - 1);
		return;
	}

	AngleView[AgentId][0] = FMath::DegreesToRadians(Angle);
	RelativeAngleView[AgentId] = FMath::DegreesToRadians(RelativeAngle);
}

#if ENABLE_VISUAL_LOG
void UAngleObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UAngleObservation::VisualLog);

	const TLearningArrayView<2, const float> AngleView = FeatureObject->InstanceData->ConstView(FeatureObject->AngleHandle);
	const TLearningArrayView<1, const float> RelativeAngleView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativeAngleHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const float Angle = AngleView[Instance][0];
				const float RelativeAngle = RelativeAngleView[Instance];

				UE_VLOG_CIRCLE(this, LogLearning, Display,
					Actor->GetActorLocation(),
					FVector::UpVector,
					50.0f,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_VLOG_SEGMENT(this, LogLearning, Display,
					Actor->GetActorLocation(),
					Actor->GetActorLocation() + 50.0f * FVector(FMath::Sin(RelativeAngle), FMath::Cos(RelativeAngle), 0.0f),
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_VLOG_LOCATION(this, LogLearning, Display,
					Actor->GetActorLocation() + 50.0f * FVector(FMath::Sin(RelativeAngle), FMath::Cos(RelativeAngle), 0.0f),
					2.5f,
					VisualLogColor.ToFColor(true),
					TEXT("Relative Angle: [% 6.1f]"),
					RelativeAngle);

				UE_VLOG_SEGMENT(this, LogLearning, Display,
					Actor->GetActorLocation(),
					Actor->GetActorLocation() + 50.0f * FVector(FMath::Sin(Angle), FMath::Cos(Angle), 0.0f),
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_VLOG_LOCATION(this, LogLearning, Display,
					Actor->GetActorLocation() + 50.0f * FVector(FMath::Sin(Angle), FMath::Cos(Angle), 0.0f),
					2.5f,
					VisualLogColor.ToFColor(true),
					TEXT("Angle: [% 6.1f]"),
					Angle);

				UE_VLOG_LOCATION(this, LogLearning, Display,
					Actor->GetActorLocation(),
					5.0f,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: [% 6.3f % 6.3f]"),
					Instance,
					FeatureObject->Scale,
					FeatureView[Instance][0], FeatureView[Instance][1]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UPlanarDirectionObservation* UPlanarDirectionObservation::AddPlanarDirectionObservation(ULearningAgentsType* AgentType, FName Name, float Scale, FVector Axis0, FVector Axis1)
{
	return UE::Learning::Agents::Private::AddObservation<UPlanarDirectionObservation, UE::Learning::FPlanarDirectionFeature>(AgentType, Name, 1, Scale, Axis0, Axis1);
}

void UPlanarDirectionObservation::SetPlanarDirectionObservation(int32 AgentId, FVector Direction, FRotator RelativeRotation)
{
	TLearningArrayView<2, FVector> DirectionView = FeatureObject->InstanceData->View(FeatureObject->DirectionHandle);
	TLearningArrayView<1, FQuat> RelativeRotationView = FeatureObject->InstanceData->View(FeatureObject->RelativeRotationHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= DirectionView.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, DirectionView.Num<0>() - 1);
		return;
	}

	RelativeRotationView[AgentId] = FQuat::MakeFromRotator(RelativeRotation);
	DirectionView[AgentId][0] = Direction;
}

#if ENABLE_VISUAL_LOG
void UPlanarDirectionObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UPlanarDirectionObservation::VisualLog);

	const TLearningArrayView<2, const FVector> DirectionView = FeatureObject->InstanceData->ConstView(FeatureObject->DirectionHandle);
	const TLearningArrayView<1, const FQuat> RelativeRotationView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativeRotationHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const FVector Direction = DirectionView[Instance][0];
				const FQuat RelativeRotation = RelativeRotationView[Instance];
				const FVector LocalDirection = RelativeRotation.UnrotateVector(Direction);

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
					LocalDirection.X, LocalDirection.Y, LocalDirection.Z);

				UE_LEARNING_AGENTS_VLOG_PLANE(this, LogLearning, Display,
					Actor->GetActorLocation(),
					RelativeRotation,
					FeatureObject->Axis0,
					FeatureObject->Axis1,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
					Actor->GetActorLocation(),
					RelativeRotation,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: [% 6.3f % 6.3f]"),
					Instance,
					FeatureObject->Scale,
					FeatureView[Instance][0], FeatureView[Instance][1]);
			}
		}
	}
}
#endif

UDirectionObservation* UDirectionObservation::AddDirectionObservation(ULearningAgentsType* AgentType, FName Name, float Scale)
{
	return UE::Learning::Agents::Private::AddObservation<UDirectionObservation, UE::Learning::FDirectionFeature>(AgentType, Name, 1, Scale);
}

void UDirectionObservation::SetDirectionObservation(int32 AgentId, FVector Direction, FRotator RelativeRotation)
{
	TLearningArrayView<2, FVector> DirectionView = FeatureObject->InstanceData->View(FeatureObject->DirectionHandle);
	TLearningArrayView<1, FQuat> RelativeRotationView = FeatureObject->InstanceData->View(FeatureObject->RelativeRotationHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= DirectionView.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, DirectionView.Num<0>() - 1);
		return;
	}

	RelativeRotationView[AgentId] = FQuat::MakeFromRotator(RelativeRotation);
	DirectionView[AgentId][0] = Direction;
}

#if ENABLE_VISUAL_LOG
void UDirectionObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UDirectionObservation::VisualLog);

	const TLearningArrayView<2, const FVector> DirectionView = FeatureObject->InstanceData->ConstView(FeatureObject->DirectionHandle);
	const TLearningArrayView<1, const FQuat> RelativeRotationView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativeRotationHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const FVector Direction = DirectionView[Instance][0];
				const FQuat RelativeRotation = RelativeRotationView[Instance];
				const FVector LocalDirection = RelativeRotation.UnrotateVector(Direction);

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
					LocalDirection.X, LocalDirection.Y, LocalDirection.Z);

				UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
					Actor->GetActorLocation(),
					RelativeRotation,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: [% 6.3f % 6.3f % 6.3f]"),
					Instance,
					FeatureObject->Scale,
					FeatureView[Instance][0], FeatureView[Instance][1], FeatureView[Instance][2]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UPlanarPositionObservation* UPlanarPositionObservation::AddPlanarPositionObservation(ULearningAgentsType* AgentType, FName Name, float Scale, FVector Axis0, FVector Axis1)
{
	return UE::Learning::Agents::Private::AddObservation<UPlanarPositionObservation, UE::Learning::FPlanarPositionFeature>(AgentType, Name, 1, Scale, Axis0, Axis1);
}

void UPlanarPositionObservation::SetPlanarPositionObservation(int32 AgentId, FVector Position, FVector RelativePosition, FRotator RelativeRotation)
{
	TLearningArrayView<2, FVector> PositionView = FeatureObject->InstanceData->View(FeatureObject->PositionHandle);
	TLearningArrayView<1, FVector> RelativePositionView = FeatureObject->InstanceData->View(FeatureObject->RelativePositionHandle);
	TLearningArrayView<1, FQuat> RelativeRotationView = FeatureObject->InstanceData->View(FeatureObject->RelativeRotationHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= PositionView.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, PositionView.Num<0>() - 1);
		return;
	}

	RelativePositionView[AgentId] = RelativePosition;
	RelativeRotationView[AgentId] = FQuat::MakeFromRotator(RelativeRotation);
	PositionView[AgentId][0] = Position;
}

#if ENABLE_VISUAL_LOG
void UPlanarPositionObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UPlanarPositionObservation::VisualLog);

	const TLearningArrayView<2, const FVector> PositionView = FeatureObject->InstanceData->ConstView(FeatureObject->PositionHandle);
	const TLearningArrayView<1, const FVector> RelativePositionView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativePositionHandle);
	const TLearningArrayView<1, const FQuat> RelativeRotationView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativeRotationHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const FVector Position = PositionView[Instance][0];
				const FVector RelativePosition = RelativePositionView[Instance];
				const FQuat RelativeRotation = RelativeRotationView[Instance];
				const FVector LocalPosition = RelativeRotation.UnrotateVector(Position - RelativePosition);

				UE_VLOG_LOCATION(this, LogLearning, Display,
					Position,
					10.0f,
					VisualLogColor.ToFColor(true),
					TEXT("Position: [% 6.1f % 6.1f % 6.1f]\nLocal Position: [% 6.1f % 6.1f % 6.1f]"),
					Position.X, Position.Y, Position.Z,
					LocalPosition.X, LocalPosition.Y, LocalPosition.Z);

				UE_VLOG_SEGMENT(this, LogLearning, Display,
					RelativePosition,
					Position,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_LEARNING_AGENTS_VLOG_PLANE(this, LogLearning, Display,
					RelativePosition,
					RelativeRotation,
					FeatureObject->Axis0,
					FeatureObject->Axis1,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
					RelativePosition,
					RelativeRotation,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: [% 6.3f % 6.3f]"),
					Instance,
					FeatureObject->Scale,
					FeatureView[Instance][0], FeatureView[Instance][1]);
			}
		}
	}
}
#endif

UPositionObservation* UPositionObservation::AddPositionObservation(ULearningAgentsType* AgentType, FName Name, float Scale)
{
	return UE::Learning::Agents::Private::AddObservation<UPositionObservation, UE::Learning::FPositionFeature>(AgentType, Name, 1, Scale);
}

void UPositionObservation::SetPositionObservation(int32 AgentId, FVector Position, FVector RelativePosition, FRotator RelativeRotation)
{
	TLearningArrayView<2, FVector> PositionView = FeatureObject->InstanceData->View(FeatureObject->PositionHandle);
	TLearningArrayView<1, FVector> RelativePositionView = FeatureObject->InstanceData->View(FeatureObject->RelativePositionHandle);
	TLearningArrayView<1, FQuat> RelativeRotationView = FeatureObject->InstanceData->View(FeatureObject->RelativeRotationHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= PositionView.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, PositionView.Num<0>() - 1);
		return;
	}

	RelativePositionView[AgentId] = RelativePosition;
	RelativeRotationView[AgentId] = FQuat::MakeFromRotator(RelativeRotation);
	PositionView[AgentId][0] = Position;
}

#if ENABLE_VISUAL_LOG
void UPositionObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UPositionObservation::VisualLog);

	const TLearningArrayView<2, const FVector> PositionView = FeatureObject->InstanceData->ConstView(FeatureObject->PositionHandle);
	const TLearningArrayView<1, const FVector> RelativePositionView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativePositionHandle);
	const TLearningArrayView<1, const FQuat> RelativeRotationView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativeRotationHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const FVector Position = PositionView[Instance][0];
				const FVector RelativePosition = RelativePositionView[Instance];
				const FQuat RelativeRotation = RelativeRotationView[Instance];
				const FVector LocalPosition = RelativeRotation.UnrotateVector(Position - RelativePosition);

				UE_VLOG_LOCATION(this, LogLearning, Display,
					Position,
					10.0f,
					VisualLogColor.ToFColor(true),
					TEXT("Position: [% 6.1f % 6.1f % 6.1f]\nLocal Position: [% 6.1f % 6.1f % 6.1f]"),
					Position.X, Position.Y, Position.Z,
					LocalPosition.X, LocalPosition.Y, LocalPosition.Z);

				UE_VLOG_SEGMENT(this, LogLearning, Display,
					RelativePosition,
					Position,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
					RelativePosition,
					RelativeRotation,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: [% 6.3f % 6.3f % 6.3f]"),
					Instance,
					FeatureObject->Scale,
					FeatureView[Instance][0], FeatureView[Instance][1], FeatureView[Instance][2]);
			}
		}
	}
}
#endif

UPlanarPositionArrayObservation* UPlanarPositionArrayObservation::AddPlanarPositionArrayObservation(ULearningAgentsType* AgentType, FName Name, int32 PositionNum, float Scale, FVector Axis0, FVector Axis1)
{
	return UE::Learning::Agents::Private::AddObservation<UPlanarPositionArrayObservation, UE::Learning::FPlanarPositionFeature>(AgentType, Name, PositionNum, Scale, Axis0, Axis1);
}

void UPlanarPositionArrayObservation::SetPlanarPositionArrayObservation(int32 AgentId, const TArray<FVector>& Positions, FVector RelativePosition, FRotator RelativeRotation)
{
	TLearningArrayView<2, FVector> PositionView = FeatureObject->InstanceData->View(FeatureObject->PositionHandle);
	TLearningArrayView<1, FVector> RelativePositionView = FeatureObject->InstanceData->View(FeatureObject->RelativePositionHandle);
	TLearningArrayView<1, FQuat> RelativeRotationView = FeatureObject->InstanceData->View(FeatureObject->RelativeRotationHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= PositionView.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, PositionView.Num<0>() - 1);
		return;
	}

	const int32 PositionNum = PositionView.Num<1>();

	if (Positions.Num() != PositionNum)
	{
		UE_LOG(LogLearning, Error, TEXT("Incorrect number of positions in array. Got %i, expected %i."), Positions.Num(), PositionNum);
		return;
	}

	RelativePositionView[AgentId] = RelativePosition;
	RelativeRotationView[AgentId] = FQuat::MakeFromRotator(RelativeRotation);
	UE::Learning::Array::Copy(PositionView[AgentId], TLearningArrayView<1, const FVector>(Positions));
}

#if ENABLE_VISUAL_LOG
void UPlanarPositionArrayObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UPlanarPositionArrayObservation::VisualLog);

	const TLearningArrayView<2, const FVector> PositionView = FeatureObject->InstanceData->ConstView(FeatureObject->PositionHandle);
	const TLearningArrayView<1, const FVector> RelativePositionView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativePositionHandle);
	const TLearningArrayView<1, const FQuat> RelativeRotationView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativeRotationHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	const int32 PositionNum = PositionView.Num<1>();

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const FVector RelativePosition = RelativePositionView[Instance];
				const FQuat RelativeRotation = RelativeRotationView[Instance];

				for (int32 PositionIdx = 0; PositionIdx < PositionNum; PositionIdx++)
				{
					const FVector LocalPosition = RelativeRotation.UnrotateVector(PositionView[Instance][PositionIdx] - RelativePosition);

					UE_VLOG_LOCATION(this, LogLearning, Display,
						PositionView[Instance][PositionIdx],
						10.0f,
						VisualLogColor.ToFColor(true),
						TEXT("Position: [% 6.1f % 6.1f % 6.1f]\nLocal Position: [% 6.1f % 6.1f % 6.1f]"),
						PositionView[Instance][PositionIdx].X, 
						PositionView[Instance][PositionIdx].Y, 
						PositionView[Instance][PositionIdx].Z,
						LocalPosition.X, LocalPosition.Y, LocalPosition.Z);

					UE_VLOG_SEGMENT(this, LogLearning, Display,
						RelativePosition,
						PositionView[Instance][PositionIdx],
						VisualLogColor.ToFColor(true),
						TEXT(""));
				}

				UE_LEARNING_AGENTS_VLOG_PLANE(this, LogLearning, Display,
					RelativePosition,
					RelativeRotation,
					FeatureObject->Axis0,
					FeatureObject->Axis1,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				if (PositionNum > 0)
				{
					UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
						RelativePosition,
						RelativeRotation,
						VisualLogColor.ToFColor(true),
						TEXT("Agent %i\nEncoded: [% 6.3f % 6.3f ...]"),
						Instance,
						FeatureObject->Scale,
						FeatureView[Instance][0], FeatureView[Instance][1]);
				}
				else
				{
					UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
						RelativePosition,
						RelativeRotation,
						VisualLogColor.ToFColor(true),
						TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: []"),
						Instance,
						FeatureObject->Scale);
				}
			}
		}
	}
}
#endif

UPositionArrayObservation* UPositionArrayObservation::AddPositionArrayObservation(ULearningAgentsType* AgentType, FName Name, int32 PositionNum, float Scale)
{
	return UE::Learning::Agents::Private::AddObservation<UPositionArrayObservation, UE::Learning::FPositionFeature>(AgentType, Name, PositionNum, Scale);
}

void UPositionArrayObservation::SetPositionArrayObservation(int32 AgentId, const TArray<FVector>& Positions, FVector RelativePosition, FRotator RelativeRotation)
{
	TLearningArrayView<2, FVector> PositionView = FeatureObject->InstanceData->View(FeatureObject->PositionHandle);
	TLearningArrayView<1, FVector> RelativePositionView = FeatureObject->InstanceData->View(FeatureObject->RelativePositionHandle);
	TLearningArrayView<1, FQuat> RelativeRotationView = FeatureObject->InstanceData->View(FeatureObject->RelativeRotationHandle);
	
	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return;
	}

	if (AgentId < 0 || AgentId >= PositionView.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, PositionView.Num<0>() - 1);
		return;
	}

	const int32 PositionNum = PositionView.Num<1>();

	if (Positions.Num() != PositionNum)
	{
		UE_LOG(LogLearning, Error, TEXT("Incorrect number of positions in array. Got %i, expected %i."), Positions.Num(), PositionNum);
		return;
	}

	RelativePositionView[AgentId] = RelativePosition;
	RelativeRotationView[AgentId] = FQuat::MakeFromRotator(RelativeRotation);
	UE::Learning::Array::Copy(PositionView[AgentId], TLearningArrayView<1, const FVector>(Positions));
}

#if ENABLE_VISUAL_LOG
void UPositionArrayObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UPositionArrayObservation::VisualLog);

	const TLearningArrayView<2, const FVector> PositionView = FeatureObject->InstanceData->ConstView(FeatureObject->PositionHandle);
	const TLearningArrayView<1, const FVector> RelativePositionView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativePositionHandle);
	const TLearningArrayView<1, const FQuat> RelativeRotationView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativeRotationHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	const int32 PositionNum = PositionView.Num<1>();

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const FVector RelativePosition = RelativePositionView[Instance];
				const FQuat RelativeRotation = RelativeRotationView[Instance];

				for (int32 PositionIdx = 0; PositionIdx < PositionNum; PositionIdx++)
				{
					const FVector LocalPosition = RelativeRotation.UnrotateVector(PositionView[Instance][PositionIdx] - RelativePosition);

					UE_VLOG_LOCATION(this, LogLearning, Display,
						PositionView[Instance][PositionIdx],
						10.0f,
						VisualLogColor.ToFColor(true),
						TEXT("Position: [% 6.1f % 6.1f % 6.1f]\nLocal Position: [% 6.1f % 6.1f % 6.1f]"),
						PositionView[Instance][PositionIdx].X, 
						PositionView[Instance][PositionIdx].Y, 
						PositionView[Instance][PositionIdx].Z,
						LocalPosition.X, LocalPosition.Y, LocalPosition.Z);

					UE_VLOG_SEGMENT(this, LogLearning, Display,
						RelativePosition,
						PositionView[Instance][PositionIdx],
						VisualLogColor.ToFColor(true),
						TEXT(""));
				}

				if (PositionNum > 0)
				{
					UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
						RelativePosition,
						RelativeRotation,
						VisualLogColor.ToFColor(true),
						TEXT("Agent %i\nEncoded: [% 6.3f % 6.3f % 6.3f ...]"),
						Instance,
						FeatureObject->Scale,
						FeatureView[Instance][0], FeatureView[Instance][1], FeatureView[Instance][2]);
				}
				else
				{
					UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
						RelativePosition,
						RelativeRotation,
						VisualLogColor.ToFColor(true),
						TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: []"),
						Instance,
						FeatureObject->Scale);
				}
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UPlanarVelocityObservation* UPlanarVelocityObservation::AddPlanarVelocityObservation(ULearningAgentsType* AgentType, FName Name, float Scale, FVector Axis0, FVector Axis1)
{
	return UE::Learning::Agents::Private::AddObservation<UPlanarVelocityObservation, UE::Learning::FPlanarVelocityFeature>(AgentType, Name, 1, Scale, Axis0, Axis1);
}

void UPlanarVelocityObservation::SetPlanarVelocityObservation(int32 AgentId, FVector Velocity, FRotator RelativeRotation)
{
	TLearningArrayView<2, FVector> VelocityView = FeatureObject->InstanceData->View(FeatureObject->VelocityHandle);
	TLearningArrayView<1, FQuat> RelativeRotationView = FeatureObject->InstanceData->View(FeatureObject->RelativeRotationHandle);

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

	VelocityView[AgentId][0] = Velocity;
	RelativeRotationView[AgentId] = FQuat::MakeFromRotator(RelativeRotation);
}

#if ENABLE_VISUAL_LOG
void UPlanarVelocityObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UPlanarVelocityObservation::VisualLog);

	const TLearningArrayView<2, const FVector> VelocityView = FeatureObject->InstanceData->ConstView(FeatureObject->VelocityHandle);
	const TLearningArrayView<1, const FQuat> RelativeRotationView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativeRotationHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const FVector Velocity = VelocityView[Instance][0];
				const FQuat RelativeRotation = RelativeRotationView[Instance];
				const FVector LocalVelocity = RelativeRotation.UnrotateVector(Velocity);

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

				UE_LEARNING_AGENTS_VLOG_PLANE(this, LogLearning, Display,
					Actor->GetActorLocation(),
					RelativeRotation,
					FeatureObject->Axis0,
					FeatureObject->Axis1,
					VisualLogColor.ToFColor(true),
					TEXT(""));

				UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
					Actor->GetActorLocation(),
					RelativeRotation,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: [% 6.3f % 6.3f]"),
					Instance,
					FeatureObject->Scale,
					FeatureView[Instance][0], FeatureView[Instance][1]);
			}
		}
	}
}
#endif

UVelocityObservation* UVelocityObservation::AddVelocityObservation(ULearningAgentsType* AgentType, FName Name, float Scale)
{
	return UE::Learning::Agents::Private::AddObservation<UVelocityObservation, UE::Learning::FVelocityFeature>(AgentType, Name, 1, Scale);
}

void UVelocityObservation::SetVelocityObservation(int32 AgentId, FVector Velocity, FRotator RelativeRotation)
{
	TLearningArrayView<2, FVector> VelocityView = FeatureObject->InstanceData->View(FeatureObject->VelocityHandle);
	TLearningArrayView<1, FQuat> RelativeRotationView = FeatureObject->InstanceData->View(FeatureObject->RelativeRotationHandle);

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

	VelocityView[AgentId][0] = Velocity;
	RelativeRotationView[AgentId] = FQuat::MakeFromRotator(RelativeRotation);
}

#if ENABLE_VISUAL_LOG
void UVelocityObservation::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UVelocityObservation::VisualLog);

	const TLearningArrayView<2, const FVector> VelocityView = FeatureObject->InstanceData->ConstView(FeatureObject->VelocityHandle);
	const TLearningArrayView<1, const FQuat> RelativeRotationView = FeatureObject->InstanceData->ConstView(FeatureObject->RelativeRotationHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				const FVector Velocity = VelocityView[Instance][0];
				const FQuat RelativeRotation = RelativeRotationView[Instance];
				const FVector LocalVelocity = RelativeRotation.UnrotateVector(Velocity);

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

				UE_LEARNING_AGENTS_VLOG_TRANSFORM(this, LogLearning, Display,
					Actor->GetActorLocation(),
					RelativeRotation,
					VisualLogColor.ToFColor(true),
					TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: [% 6.3f % 6.3f % 6.3f]"),
					Instance,
					FeatureObject->Scale,
					FeatureView[Instance][0], FeatureView[Instance][1], FeatureView[Instance][2]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

#undef UE_LEARNING_AGENTS_VLOG_STRING
#undef UE_LEARNING_AGENTS_VLOG_TRANSFORM
#undef UE_LEARNING_AGENTS_VLOG_PLANE
