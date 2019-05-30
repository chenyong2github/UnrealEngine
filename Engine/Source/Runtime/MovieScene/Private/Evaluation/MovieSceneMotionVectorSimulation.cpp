// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneMotionVectorSimulation.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneCommonHelpers.h"
#include "Components/SceneComponent.h"
#include "Rendering/MotionVectorSimulation.h"
#include "Evaluation/MovieScenePlayback.h"

struct FSharedMotionSimulationData : IPersistentEvaluationData
{
	FSharedMotionSimulationData()
		: bIsEnabled(false)
	{}

	static FSharedPersistentDataKey GetSharedDataId()
	{
		static FMovieSceneSharedDataId ID = FMovieSceneSharedDataId::Allocate();
		return FSharedPersistentDataKey(ID, FMovieSceneEvaluationOperand());
	}

	static void SetEnabled(FPersistentEvaluationData& PersistentData, bool bInIsEnabled)
	{
		FSharedPersistentDataKey DataKey = GetSharedDataId();
		if (bInIsEnabled)
		{
			PersistentData.GetOrAdd<FSharedMotionSimulationData>(DataKey).bIsEnabled = bInIsEnabled;
		}
		else
		{
			PersistentData.Reset(DataKey);
		}
	}

	static bool IsEnabled(const FPersistentEvaluationData& PersistentData)
	{
		const FSharedMotionSimulationData* Data = PersistentData.Find<FSharedMotionSimulationData>(GetSharedDataId());
		return Data && Data->bIsEnabled;
	}

	bool bIsEnabled;
};

void IMovieSceneMotionVectorSimulation::EnableThisFrame(FPersistentEvaluationData& PersistentData)
{
	FSharedMotionSimulationData::SetEnabled(PersistentData, true);
}

bool IMovieSceneMotionVectorSimulation::IsEnabled(const FPersistentEvaluationData& PersistentData, const FMovieSceneContext& Context)
{
	return !Context.IsSilent() && FSharedMotionSimulationData::IsEnabled(PersistentData) && FMotionVectorSimulation::IsEnabled();
}

FFrameTime IMovieSceneMotionVectorSimulation::GetSimulationTime(const FMovieSceneContext& Context)
{
	FFrameTime DeltaTime     = FMath::Max(Context.GetDelta(), 0.00833333334 * Context.GetFrameRate());
	FFrameTime SimulatedTime = Context.GetOffsetTime(DeltaTime);

	return SimulatedTime;
}

void FMovieSceneMotionVectorSimulation::PreserveSimulatedMotion(bool bShouldPreserveTransforms)
{
	bPreserveTransforms = bShouldPreserveTransforms;
}

void FMovieSceneMotionVectorSimulation::Add(USceneComponent* Component, const FTransform& SimulatedTransform, FName SocketName)
{
	TransformData.Add(Component, FSimulatedTransform(SimulatedTransform, SocketName));
}

void FMovieSceneMotionVectorSimulation::Apply(IMovieScenePlayer& Player)
{
	TSet<USceneComponent*> RootComponents;

	for (auto It = TransformData.CreateIterator(); It; ++It)
	{
		// If this is a socket transform, we want to add the component as a whole, to ensure that anything attached to this socket gets simulated correctly

		USceneComponent* Component = Cast<USceneComponent>(It.Key().ResolveObjectPtr());
		if (!Component || HavePreviousTransformForParent(Component))
		{
			continue;
		}

		RootComponents.Add(Component);
	}

	for (USceneComponent* Component : RootComponents)
	{
		FTransform ParentToWorld = FTransform::Identity;

		USceneComponent* ParentComp = Component->GetAttachParent();
		FName AttachSocket = Component->GetAttachSocketName();
		if (ParentComp)
		{
			FTransform ParentTransform = ParentComp->GetSocketTransform(AttachSocket, RTS_World);
			if (!Component->bAbsoluteLocation)
			{
				ParentToWorld.SetTranslation(ParentTransform.GetTranslation());
			}
			if (!Component->bAbsoluteRotation)
			{
				ParentToWorld.SetRotation(ParentTransform.GetRotation());
			}
			if (!Component->bAbsoluteScale)
			{
				ParentToWorld.SetScale3D(ParentTransform.GetScale3D());
			}
		}

		ApplySimulatedTransforms(Component, GetTransform(Component) * ParentToWorld);
	}

	if (!bPreserveTransforms)
	{
		TransformData.Reset();
	}

	FPersistentEvaluationData PersistentDataProxy(Player);
	FSharedMotionSimulationData::SetEnabled(PersistentDataProxy, false);
}

FTransform FMovieSceneMotionVectorSimulation::GetTransform(USceneComponent* Component)
{
	FObjectKey Key(Component);
	for (auto It = TransformData.CreateConstKeyIterator(Key); It; ++It)
	{
		if (It.Value().SocketName == NAME_None)
		{
			return It.Value().Transform;
		}
	}

	return Component->GetRelativeTransform();
}

FTransform FMovieSceneMotionVectorSimulation::GetSocketTransform(USceneComponent* Component, FName SocketName)
{
	FObjectKey Key(Component);
	for (auto It = TransformData.CreateConstKeyIterator(Key); It; ++It)
	{
		if (It.Value().SocketName == SocketName)
		{
			return It.Value().Transform;
		}
	}

	return Component->GetSocketTransform(SocketName, RTS_Component);
}

bool FMovieSceneMotionVectorSimulation::HavePreviousTransformForParent(USceneComponent* InComponent) const
{
	USceneComponent* Parent = InComponent->GetAttachParent();
	return Parent && (TransformData.Contains(Parent) || HavePreviousTransformForParent(Parent));
}

void FMovieSceneMotionVectorSimulation::ApplySimulatedTransforms(USceneComponent* InComponent, const FTransform& InPreviousTransform)
{
	check(InComponent);
	FMotionVectorSimulation::Get().SetPreviousTransform(InComponent, InPreviousTransform);

	for (USceneComponent* Child : InComponent->GetAttachChildren())
	{
		FName AttachSocketName = Child->GetAttachSocketName();

		FTransform SocketTransform = AttachSocketName == NAME_None ? FTransform::Identity : GetSocketTransform(InComponent, AttachSocketName);
		FTransform ParentToWorld = SocketTransform * InPreviousTransform;

		if (Child->bAbsoluteLocation)
		{
			ParentToWorld.SetTranslation(FVector::ZeroVector);
		}
		if (Child->bAbsoluteRotation)
		{
			ParentToWorld.SetRotation(FQuat::Identity);
		}
		if (Child->bAbsoluteScale)
		{
			ParentToWorld.SetScale3D(FVector(1.f, 1.f, 1.f));
		}

		FTransform ChildTransform = GetTransform(Child);
		ApplySimulatedTransforms(Child, ChildTransform * ParentToWorld);
	}
}