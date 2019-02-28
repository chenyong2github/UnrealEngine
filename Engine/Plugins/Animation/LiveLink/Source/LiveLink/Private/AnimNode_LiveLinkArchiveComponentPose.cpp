// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimNode_LiveLinkArchiveComponentPose.h"

#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"

#include "LiveLinkRemapAsset.h"

FAnimNode_LiveLinkArchiveComponentPose::FAnimNode_LiveLinkArchiveComponentPose()
	: RetargetAsset(ULiveLinkRemapAsset::StaticClass())
	, CurrentRetargetAsset(nullptr)
	, CurrentLiveLinkArchiveComponent(nullptr)
{
}

void FAnimNode_LiveLinkArchiveComponentPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	CurrentRetargetAsset = nullptr;
}

void FAnimNode_LiveLinkArchiveComponentPose::PreUpdate(const UAnimInstance* InAnimInstance)
{
	if (!CurrentLiveLinkArchiveComponent)
	{
		AActor* Actor = InAnimInstance->GetOwningActor();
		if (Actor)
		{
			const TSet<UActorComponent*>& ActorOwnedComponents = Actor->GetComponents();
			for (UActorComponent* OwnedComponent : ActorOwnedComponents)
			{
				ULiveLinkArchiveComponent* PotentialArchiveComponent = Cast<ULiveLinkArchiveComponent>(OwnedComponent);
				if (PotentialArchiveComponent && (PotentialArchiveComponent->ArchiveName.IsEqual(ArchiveNameBinding)))
				{
					CurrentLiveLinkArchiveComponent = PotentialArchiveComponent;
				}
			}
		}
	}
}

void FAnimNode_LiveLinkArchiveComponentPose::Update_AnyThread(const FAnimationUpdateContext & Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);

	// Accumulate Delta time from update
	CachedDeltaTime += Context.GetDeltaTime();

	// Protection as a class graph pin does not honour rules on abstract classes and NoClear
	if (!RetargetAsset.Get() || RetargetAsset.Get()->HasAnyClassFlags(CLASS_Abstract))
	{
		RetargetAsset = ULiveLinkRemapAsset::StaticClass();
	}

	if (!CurrentRetargetAsset || RetargetAsset != CurrentRetargetAsset->GetClass())
	{
		CurrentRetargetAsset = NewObject<ULiveLinkRetargetAsset>(Context.AnimInstanceProxy->GetAnimInstanceObject(), *RetargetAsset);
		CurrentRetargetAsset->Initialize();
	}
}

void FAnimNode_LiveLinkArchiveComponentPose::Evaluate_AnyThread(FPoseContext& Output)
{
	Output.ResetToRefPose();

	if (!CurrentRetargetAsset || !CurrentLiveLinkArchiveComponent)
	{
		return;
	}
	
	const double WorldTime = FPlatformTime::Seconds();
	bool bDidFindArchivedFrame;
	FLiveLinkSubjectFrame FoundArchiveFrameCache;
	
	CurrentLiveLinkArchiveComponent->GetSubjectDataAtWorldTime(WorldTime, bDidFindArchivedFrame, FoundArchiveFrameCache);
	if(bDidFindArchivedFrame)
	{
		CurrentRetargetAsset->BuildPoseForSubject(CachedDeltaTime, FoundArchiveFrameCache, Output.Pose, Output.Curve);
		CachedDeltaTime = 0.f; // Reset so that if we evaluate again we don't "create" time inside of the retargeter
	}
}