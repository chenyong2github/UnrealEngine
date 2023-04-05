// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsActions.h"

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
	UE_VLOG_OBOX(Owner, Category, Verbosity, FBox(-25.0f * (Axis0 + Axis1), 25.0f * (Axis0 + Axis1)), FTransform(Rotation, Location, FVector::OneVector).ToMatrixNoScale(), Color, Format, ##__VA_ARGS__)

namespace UE::Learning::Agents::Private
{
	template<typename ActionUObject, typename ActionFObject, typename... InArgTypes>
	ActionUObject* AddAction(ULearningAgentsType* AgentType, FName Name, InArgTypes&& ...Args)
	{
		if (!AgentType)
		{
			UE_LOG(LogLearning, Error, TEXT("AgentType is nullptr"));
			return nullptr;
		}

		ActionUObject* Action = NewObject<ActionUObject>(AgentType, Name);

		Action->FeatureObject = MakeShared<ActionFObject>(
			Action->GetFName(),
			AgentType->GetInstanceData().ToSharedRef(),
			AgentType->GetMaxInstanceNum(),
			Forward<InArgTypes>(Args)...);

		AgentType->AddAction(Action, Action->FeatureObject.ToSharedRef());

		return Action;
	}
}

//------------------------------------------------------------------

UFloatAction* UFloatAction::AddFloatAction(ULearningAgentsType* AgentType, FName Name, float Scale)
{
	return UE::Learning::Agents::Private::AddAction<UFloatAction, UE::Learning::FFloatFeature>(AgentType, Name, 1, Scale);
}

float UFloatAction::GetFloatAction(int32 AgentId)
{
	const TLearningArrayView<2, const float> View = FeatureObject->InstanceData->ConstView(FeatureObject->ValueHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return 0.0f;
	}

	if (AgentId < 0 || AgentId >= View.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, View.Num<0>() - 1);
		return 0.0f;
	}

	return View[AgentId][0];
}

void UFloatAction::SetFloatAction(int32 AgentId, float Value)
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
void UFloatAction::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UFloatAction::VisualLog);

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
					FeatureObject->Scale,
					ValueView[Instance][0],
					FeatureView[Instance][0]);
			}
		}
	}
}
#endif

//------------------------------------------------------------------

UVectorAction* UVectorAction::AddVectorAction(ULearningAgentsType* AgentType, FName Name, float Scale)
{
	return UE::Learning::Agents::Private::AddAction<UVectorAction, UE::Learning::FFloatFeature>(AgentType, Name, 3, Scale);
}

FVector UVectorAction::GetVectorAction(int32 AgentId)
{
	const TLearningArrayView<2, const float> View = FeatureObject->InstanceData->ConstView(FeatureObject->ValueHandle);

	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId is invalid (INDEX_NONE)"));
		return FVector::ZeroVector;
	}

	if (AgentId < 0 || AgentId >= View.Num<0>())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %d is out of index. Valid range [0, %d]."), AgentId, View.Num<0>() - 1);
		return FVector::ZeroVector;
	}

	return FVector(View[AgentId][0], View[AgentId][1], View[AgentId][2]);
}

void UVectorAction::SetVectorAction(int32 AgentId, FVector InAction)
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

	View[AgentId][0] = InAction.X;
	View[AgentId][1] = InAction.Y;
	View[AgentId][2] = InAction.Z;
}

#if ENABLE_VISUAL_LOG
void UVectorAction::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(UVectorAction::VisualLog);

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

URotationVectorArrayAction* URotationVectorArrayAction::AddRotationVectorArrayAction(ULearningAgentsType* AgentType, FName Name, int32 RotationVectorNum, float Scale)
{
	return UE::Learning::Agents::Private::AddAction<URotationVectorArrayAction, UE::Learning::FRotationVectorFeature>(AgentType, Name, RotationVectorNum, FMath::DegreesToRadians(Scale));
}

void URotationVectorArrayAction::GetRotationVectorArrayAction(int32 AgentId, TArray<FVector>& OutRotationVectors)
{
	const TLearningArrayView<2, const FVector> View = FeatureObject->InstanceData->ConstView(FeatureObject->RotationVectorsHandle);

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

	OutRotationVectors.SetNumUninitialized(View.Num<1>());
	for (int32 RotationVectorIdx = 0; RotationVectorIdx < View.Num<1>(); RotationVectorIdx++)
	{
		OutRotationVectors[RotationVectorIdx] = View[AgentId][RotationVectorIdx];
	}
}

void URotationVectorArrayAction::GetRotationVectorArrayActionAsQuats(int32 AgentId, TArray<FQuat>& OutRotations)
{
	const TLearningArrayView<2, const FVector> View = FeatureObject->InstanceData->ConstView(FeatureObject->RotationVectorsHandle);

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

	OutRotations.SetNumUninitialized(View.Num<1>());
	for (int32 RotationVectorIdx = 0; RotationVectorIdx < View.Num<1>(); RotationVectorIdx++)
	{
		OutRotations[RotationVectorIdx] = FQuat::MakeFromRotationVector(View[AgentId][RotationVectorIdx]);
	}
}

void URotationVectorArrayAction::GetRotationVectorArrayActionAsRotators(int32 AgentId, TArray<FRotator>& OutRotations)
{
	const TLearningArrayView<2, const FVector> View = FeatureObject->InstanceData->ConstView(FeatureObject->RotationVectorsHandle);

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

	OutRotations.SetNumUninitialized(View.Num<1>());
	for (int32 RotationVectorIdx = 0; RotationVectorIdx < View.Num<1>(); RotationVectorIdx++)
	{
		OutRotations[RotationVectorIdx] = FQuat::MakeFromRotationVector(View[AgentId][RotationVectorIdx]).Rotator();
	}
}

#if ENABLE_VISUAL_LOG
void URotationVectorArrayAction::VisualLog(const UE::Learning::FIndexSet Instances) const
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(URotationVectorArrayAction::VisualLog);

	const TLearningArrayView<2, const FVector> ValueView = FeatureObject->InstanceData->ConstView(FeatureObject->RotationVectorsHandle);
	const TLearningArrayView<2, const float> FeatureView = FeatureObject->InstanceData->ConstView(FeatureObject->FeatureHandle);

	const int32 RotationVectorNum = ValueView.Num<1>();

	if (const ULearningAgentsType* AgentType = Cast<ULearningAgentsType>(GetOuter()))
	{
		for (const int32 Instance : Instances)
		{
			if (const AActor* Actor = Cast<AActor>(AgentType->GetAgent(Instance)))
			{
				for (int32 RotationVectorIdx = 0; RotationVectorIdx < RotationVectorNum; RotationVectorIdx++)
				{
					const FVector Offset = FVector(0.0, 10.0f * RotationVectorIdx, 0.0f);
					const FVector RotationVector = ValueView[Instance][RotationVectorIdx];

					UE_VLOG_LOCATION(this, LogLearning, Display,
						Actor->GetActorLocation() + Offset,
						2.5f,
						VisualLogColor.ToFColor(true),
						TEXT(""));

					UE_VLOG_ARROW(this, LogLearning, Display,
						Actor->GetActorLocation() + Offset,
						Actor->GetActorLocation() + Offset + RotationVector,
						VisualLogColor.ToFColor(true),
						TEXT(""));

					UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
						Actor->GetActorLocation() + Offset + RotationVector,
						VisualLogColor.ToFColor(true),
						TEXT("Rotation Vector: [% 6.4f % 6.4f % 6.4f]"),
						RotationVector.X, RotationVector.Y, RotationVector.Z);

				}

				if (RotationVectorNum > 0)
				{
					UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
						Actor->GetActorLocation(),
						VisualLogColor.ToFColor(true),
						TEXT("Agent %i\nScale: [% 6.2f]\nEncoded: [% 6.3f % 6.3f % 6.3f ...]"),
						Instance,
						FeatureObject->Scale,
						FeatureView[Instance][0], FeatureView[Instance][1], FeatureView[Instance][2]);
				}
				else
				{
					UE_LEARNING_AGENTS_VLOG_STRING(this, LogLearning, Display,
						Actor->GetActorLocation(),
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

#undef UE_LEARNING_AGENTS_VLOG_STRING
#undef UE_LEARNING_AGENTS_VLOG_TRANSFORM
#undef UE_LEARNING_AGENTS_VLOG_PLANE