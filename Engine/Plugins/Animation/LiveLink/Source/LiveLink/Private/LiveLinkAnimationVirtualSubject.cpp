// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkAnimationVirtualSubject.h"

#include "ILiveLinkClient.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"


namespace LiveLinkAnimationVirtualSubjectUtils
{
	void AddToBoneNames(TArray<FName>& BoneNames, const TArray<FName>& NewBoneNames, const FName Prefix)
	{
		FString NameFormat = Prefix.ToString() + TEXT("_");

		BoneNames.Reserve(BoneNames.Num() + NewBoneNames.Num());

		for (const FName& NewBoneName : NewBoneNames)
		{
			BoneNames.Add(*(NameFormat + NewBoneName.ToString()));
		}
	}

	void AddToBoneParents(TArray<int32>& BoneParents, const TArray<int32>& NewBoneParents)
	{
		const int32 Offset = BoneParents.Num();

		BoneParents.Reserve(BoneParents.Num() + NewBoneParents.Num());

		for (int32 BoneParent : NewBoneParents)
		{
			// Here we are combining multiple bone hierarchies under one root bone
			// Each hierarchy is complete self contained so we have a simple calculation to perform
			// 1) Bones with out a parent get parented to root (-1 => 0 )
			// 2) Bones with a parent need and offset based on the current size of the buffer
			if (BoneParent == INDEX_NONE)
			{
				BoneParents.Add(0);
			}
			else
			{
				BoneParents.Add(BoneParent + Offset);
			}
		}
	}
}


ULiveLinkAnimationVirtualSubject::ULiveLinkAnimationVirtualSubject()
{
	Role = ULiveLinkAnimationRole::StaticClass();
	bInvalidate = true;
}

void ULiveLinkAnimationVirtualSubject::Update()
{
	Super::Update();

	TArray<FLiveLinkSubjectKey> ActiveSubjects = LiveLinkClient->GetSubjects(false, false);

	if (AreSubjectsValid(ActiveSubjects))
	{
		TArray<FLiveLinkSubjectFrameData> SubjectSnapshot;
		if (BuildSubjectSnapshot(SubjectSnapshot))
		{
			BuildSkeleton(SubjectSnapshot);
			BuildFrame(SubjectSnapshot);
		}

	}
}

bool ULiveLinkAnimationVirtualSubject::AreSubjectsValid(const TArray<FLiveLinkSubjectKey>& InActiveSubjects) const
{
	if (Subjects.Num() <= 0)
	{
		return false;
	}

	bool bValid = true;

	for (const FName SubjectName : Subjects)
	{
		const FLiveLinkSubjectKey* FoundPtr = InActiveSubjects.FindByPredicate(
			[SubjectName](const FLiveLinkSubjectKey& SubjectData)
			{
				return (SubjectData.SubjectName == SubjectName);
			});

		bValid = FoundPtr != nullptr && LiveLinkClient->DoesSubjectSupportsRole(*FoundPtr, GetRole());
		if (!bValid)
		{
			break;
		}
	}

	return bValid;
}

bool ULiveLinkAnimationVirtualSubject::BuildSubjectSnapshot(TArray<FLiveLinkSubjectFrameData>& OutSnapshot)
{
	OutSnapshot.Reset(Subjects.Num());

	bool bSnapshotDone = true;

	for (const FName SubjectName : Subjects)
	{
		FLiveLinkSubjectFrameData& NextSnapshot = OutSnapshot.AddDefaulted_GetRef();
		if (!LiveLinkClient->EvaluateFrame_AnyThread(SubjectName, GetRole(), NextSnapshot))
		{
			bSnapshotDone = false;
			break;
		}
	}

	return bSnapshotDone;
}

void ULiveLinkAnimationVirtualSubject::BuildSkeleton(const TArray<FLiveLinkSubjectFrameData>& InSubjectSnapshots)
{
	if (DoesSkeletonNeedRebuilding())
	{
		FrameSnapshot.StaticData.InitializeWith(FLiveLinkSkeletonStaticData::StaticStruct(), nullptr);
		FLiveLinkSkeletonStaticData* SkeletonData = FrameSnapshot.StaticData.Cast<FLiveLinkSkeletonStaticData>();

		TArray<FName> BoneNames{ TEXT("Root") };
		TArray<int32> BoneParents{ INDEX_NONE };

		check(InSubjectSnapshots.Num() == Subjects.Num());
		for (int32 i = 0; i < InSubjectSnapshots.Num(); ++i)
		{
			const FLiveLinkSubjectFrameData& SubjectSnapShotData = InSubjectSnapshots[i];
			check(SubjectSnapShotData.StaticData.IsValid());
			const FLiveLinkSkeletonStaticData* SubjectSkeletonData = SubjectSnapShotData.StaticData.Cast<FLiveLinkSkeletonStaticData>();

			LiveLinkAnimationVirtualSubjectUtils::AddToBoneNames(BoneNames, SubjectSkeletonData->GetBoneNames(), Subjects[i]);
			LiveLinkAnimationVirtualSubjectUtils::AddToBoneParents(BoneParents, SubjectSkeletonData->GetBoneParents());
			SkeletonData->PropertyNames.Append(SubjectSkeletonData->PropertyNames);
		}

		SkeletonData->SetBoneNames(BoneNames);
		SkeletonData->SetBoneParents(BoneParents);

		bInvalidate = false;
	}
}

void ULiveLinkAnimationVirtualSubject::BuildFrame(const TArray<FLiveLinkSubjectFrameData>& InSubjectSnapshots)
{
	if (!FrameSnapshot.FrameData.IsValid())
	{
		FrameSnapshot.FrameData.InitializeWith(FLiveLinkAnimationFrameData::StaticStruct(), nullptr);
	}

	FLiveLinkSkeletonStaticData* SnapshotSkeletonData = FrameSnapshot.StaticData.Cast<FLiveLinkSkeletonStaticData>();
	FLiveLinkAnimationFrameData* SnapshotFrameData = FrameSnapshot.FrameData.Cast<FLiveLinkAnimationFrameData>();

	SnapshotFrameData->Transforms.Reset(SnapshotSkeletonData->GetBoneNames().Num());
	SnapshotFrameData->Transforms.Add(FTransform::Identity);
	SnapshotFrameData->MetaData.StringMetaData.Empty();

	//Go over each subject snapshot and take transforms and curves
	check(InSubjectSnapshots.Num() == Subjects.Num());
	for (int32 i = 0; i < InSubjectSnapshots.Num(); ++i)
	{
		const FLiveLinkSubjectFrameData& SubjectSnapShotData = InSubjectSnapshots[i];
		check(SubjectSnapShotData.FrameData.IsValid());
		const FLiveLinkAnimationFrameData* SubjectFrameData = SubjectSnapShotData.FrameData.Cast<FLiveLinkAnimationFrameData>();

		SnapshotFrameData->Transforms.Append(SubjectFrameData->Transforms);
		SnapshotFrameData->PropertyValues.Append(SubjectFrameData->PropertyValues);
		for (const auto& MetaDatum : SubjectFrameData->MetaData.StringMetaData)
		{
			const FName QualifiedKey = FName(*(Subjects[i].ToString() + MetaDatum.Key.ToString()));
			SnapshotFrameData->MetaData.StringMetaData.Emplace(Subjects[i], MetaDatum.Value);
		}
	}
}

bool ULiveLinkAnimationVirtualSubject::DoesSkeletonNeedRebuilding() const
{
	return !FrameSnapshot.StaticData.IsValid() || bInvalidate;
}

#if WITH_EDITOR
void ULiveLinkAnimationVirtualSubject::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	//If our properties have changed, force a skeleton rebuild for next frame
	bInvalidate = true;
}
#endif //WITH_EDITOR
