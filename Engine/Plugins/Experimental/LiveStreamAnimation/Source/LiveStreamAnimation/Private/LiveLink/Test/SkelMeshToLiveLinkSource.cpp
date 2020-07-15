// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LiveLink/Test/SkelMeshToLiveLinkSource.h"
#include "LiveStreamAnimationSubsystem.h"

#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "ILiveLinkClient.h"
#include "Components/SkeletalMeshComponent.h"
#include "ReferenceSkeleton.h"
#include "Engine/SkeletalMesh.h"

ULiveLinkTestSkelMeshTrackerComponent::ULiveLinkTestSkelMeshTrackerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULiveLinkTestSkelMeshTrackerComponent::StartTrackingSkelMesh(class USkeletalMeshComponent* InSkelMeshComp, FName InLiveLinkSubjectName)
{
	using namespace LiveStreamAnimation;

	StopTrackingSkelMesh();

	TSharedPtr<const FSkelMeshToLiveLinkSource> PinnedSource = Source.Pin();
	if (!PinnedSource.IsValid())
	{
		UWorld* World = GetWorld();

		if (ULiveStreamAnimationSubsystem* Subsystem = UGameInstance::GetSubsystem<ULiveStreamAnimationSubsystem>(World ? World->GetGameInstance() : nullptr))
		{
			Source = Subsystem->GetOrCreateSkelMeshToLiveLinkSource();
			PinnedSource = Source.Pin();
		}

		if (!PinnedSource.IsValid())
		{
			return;
		}
	}

	if (InSkelMeshComp && InLiveLinkSubjectName != NAME_None)
	{
		if (const FReferenceSkeleton * RefSkel = InSkelMeshComp->SkeletalMesh ? &InSkelMeshComp->SkeletalMesh->RefSkeleton : nullptr)
		{
			if (ILiveLinkClient * LiveLinkClient = PinnedSource->GetLiveLinkClient())
			{
				SkelMeshComp = InSkelMeshComp;
				SubjectName = InLiveLinkSubjectName;

				const TArray<FMeshBoneInfo>& RawRefBoneInfo = RefSkel->GetRefBoneInfo();

				FLiveLinkSkeletonStaticData SkeletonData;
				SkeletonData.BoneNames.Reserve(RawRefBoneInfo.Num());
				SkeletonData.BoneParents.Reserve(RawRefBoneInfo.Num());

				for (const FMeshBoneInfo& BoneInfo : RawRefBoneInfo)
				{
					SkeletonData.BoneNames.Add(BoneInfo.Name);
					SkeletonData.BoneParents.Add(BoneInfo.ParentIndex);
				}

				FLiveLinkStaticDataStruct StaticData;
				StaticData.InitializeWith(&SkeletonData);

				LiveLinkClient->PushSubjectStaticData_AnyThread(GetSubjectKey(), ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
				PrimaryComponentTick.AddPrerequisite(InSkelMeshComp, InSkelMeshComp->PrimaryComponentTick);
			}
		}
	}
}

void ULiveLinkTestSkelMeshTrackerComponent::StopTrackingSkelMesh()
{
	using namespace LiveStreamAnimation;

	if (SkelMeshComp)
	{
		PrimaryComponentTick.RemovePrerequisite(SkelMeshComp, SkelMeshComp->PrimaryComponentTick);
	}

	TSharedPtr<const FSkelMeshToLiveLinkSource> PinnedSource = Source.Pin();
	if (PinnedSource.IsValid())
	{
		if (ILiveLinkClient * LiveLinkClient = PinnedSource->GetLiveLinkClient())
		{
			LiveLinkClient->RemoveSubject_AnyThread(GetSubjectKey());
		}
	}
}

void ULiveLinkTestSkelMeshTrackerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	using namespace LiveStreamAnimation;

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (SkelMeshComp == nullptr)
	{
		return;
	}

	TSharedPtr<const FSkelMeshToLiveLinkSource> PinnedSource = Source.Pin();
	if (ILiveLinkClient * LiveLinkClient = PinnedSource->GetLiveLinkClient())
	{
		FLiveLinkAnimationFrameData Frames;
		Frames.Transforms = SkelMeshComp->GetBoneSpaceTransforms();
		FLiveLinkFrameDataStruct FrameData;
		FrameData.InitializeWith(&Frames);

		LiveLinkClient->PushSubjectFrameData_AnyThread(GetSubjectKey(), MoveTemp(FrameData));
	}
}

ILiveLinkClient* ULiveLinkTestSkelMeshTrackerComponent::GetLiveLinkClient() const
{
	using namespace LiveStreamAnimation;

	TSharedPtr<const FSkelMeshToLiveLinkSource> PinnedSource = Source.Pin();
	return PinnedSource.IsValid() ? PinnedSource->GetLiveLinkClient() : nullptr;
}

FLiveLinkSubjectKey ULiveLinkTestSkelMeshTrackerComponent::GetSubjectKey() const
{
	using namespace LiveStreamAnimation;

	TSharedPtr<const FSkelMeshToLiveLinkSource> PinnedSource = Source.Pin();
	return FLiveLinkSubjectKey(PinnedSource->GetGuid(), SubjectName);
}