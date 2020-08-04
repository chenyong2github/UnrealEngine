// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LSALiveLinkSkelMeshSource.h"
#include "LSALiveLinkFrameTranslator.h"
#include "LSALiveLinkDataHandler.h"
#include "LSALiveLinkSettings.h"
#include "LiveStreamAnimationSubsystem.h"

#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "ILiveLinkClient.h"
#include "LiveLinkPresetTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "ReferenceSkeleton.h"
#include "Engine/SkeletalMesh.h"

ULiveLinkTestSkelMeshTrackerComponent::ULiveLinkTestSkelMeshTrackerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULiveLinkTestSkelMeshTrackerComponent::StartTrackingSkelMesh(FName InLiveLinkSubjectName)
{
	using namespace LiveStreamAnimation;

	StopTrackingSkelMesh();

	TSharedPtr<const FLSALiveLinkSkelMeshSource> PinnedSource = Source.Pin();
	if (!PinnedSource.IsValid())
	{
		UWorld* World = GetWorld();

		if (ULiveStreamAnimationSubsystem* Subsystem = UGameInstance::GetSubsystem<ULiveStreamAnimationSubsystem>(World ? World->GetGameInstance() : nullptr))
		{
			if (ULSALiveLinkDataHandler* DataHandler = Subsystem->GetDataHandler<ULSALiveLinkDataHandler>())
			{
				PinnedSource = DataHandler->GetOrCreateLiveLinkSkelMeshSource();
				Source = PinnedSource;
			}
		}

		if (!PinnedSource.IsValid())
		{
			return;
		}
	}

	if (InLiveLinkSubjectName != NAME_None)
	{
		if (USkeletalMeshComponent* LocalSkelMeshComp = GetSkelMeshComp())
		{
			if (const FReferenceSkeleton* RefSkel = LocalSkelMeshComp->SkeletalMesh ? &LocalSkelMeshComp->SkeletalMesh->RefSkeleton : nullptr)
			{
				if (ILiveLinkClient* LiveLinkClient = PinnedSource->GetLiveLinkClient())
				{
					FLiveLinkSubjectPreset SubjectPresets;
					SubjectPresets.Key = FLiveLinkSubjectKey(PinnedSource->GetGuid(), InLiveLinkSubjectName);
					SubjectPresets.Role = ULiveLinkAnimationRole::StaticClass();
					SubjectPresets.bEnabled = true;

					const bool bCreatedSubjectSuccessfully = LiveLinkClient->CreateSubject(SubjectPresets);
					if (!bCreatedSubjectSuccessfully)
					{
						return;
					}

					SubjectName = InLiveLinkSubjectName;
	
					TArray<TTuple<int32, int32>> BoneAndParentIndices;
					const TArray<FMeshBoneInfo>& RawRefBoneInfo = RefSkel->GetRefBoneInfo();
					if (BonesToTrack.Num() > 0)
					{
						TSet<int32> TempBonesToUse;

						for (const FBoneReference& BoneReference : BonesToTrack)
						{
							const int32 BoneIndex = RefSkel->FindRawBoneIndex(BoneReference.BoneName);
							if (INDEX_NONE != BoneIndex)
							{
								TempBonesToUse.Add(BoneIndex);
							}
						}

						if (TempBonesToUse.Num() > 0)
						{
							// Forcibly add our root bone so we don't get into a weird state.
							// TODO: We should probably check to see if this was already added.
							for (int32 i = 0; i < RawRefBoneInfo.Num(); ++i)
							{
								if (INDEX_NONE == RawRefBoneInfo[i].ParentIndex)
								{
									TempBonesToUse.Add(i);
									break;
								}
							}

							// Go ahead and generate our simple skelton.
							// This is done by scanning backwards and associating the bones with their parents.
							// If we aren't tracking a parent, we will use that parent's parent (recursively until we hit the root).
							for (int32 Bone : TempBonesToUse)
							{
								int32 Parent = Bone;
								
								do
								{
									Parent = RawRefBoneInfo[Parent].ParentIndex;
								} while (INDEX_NONE != Parent && !TempBonesToUse.Contains(Parent));

								BoneAndParentIndices.Add(TTuple<int32, int32>(Bone, Parent));
							}
						}
					}

					if (BoneAndParentIndices.Num() == 0)
					{
						for (int32 i = 0; i < RawRefBoneInfo.Num(); ++i)
						{
							BoneAndParentIndices.Add(TTuple<int32, int32>(i, RawRefBoneInfo[i].ParentIndex));
						}
					}

					FLiveLinkSkeletonStaticData SkeletonData;
					SkeletonData.BoneNames.Reserve(BoneAndParentIndices.Num());
					SkeletonData.BoneParents.Reserve(BoneAndParentIndices.Num());
					UsingBones.Empty(BoneAndParentIndices.Num());

					for (const TTuple<int32, int32>& BoneAndParent : BoneAndParentIndices)
					{
						SkeletonData.BoneNames.Add(RawRefBoneInfo[BoneAndParent.Get<0>()].Name);
						SkeletonData.BoneParents.Add(BoneAndParent.Get<1>());
						UsingBones.Add(BoneAndParent.Get<0>());
					}

					UsingSkelMeshComp = LocalSkelMeshComp;

					FLiveLinkStaticDataStruct StaticData;
					StaticData.InitializeWith(&SkeletonData);

					LiveLinkClient->PushSubjectStaticData_AnyThread(GetSubjectKey(), ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));
					PrimaryComponentTick.AddPrerequisite(LocalSkelMeshComp, LocalSkelMeshComp->PrimaryComponentTick);
				}
			}
		}
	}
}

void ULiveLinkTestSkelMeshTrackerComponent::StopTrackingSkelMesh()
{
	using namespace LiveStreamAnimation;

	if (USkeletalMeshComponent* LocalSkelMeshComp = UsingSkelMeshComp.Get())
	{
		PrimaryComponentTick.RemovePrerequisite(LocalSkelMeshComp, LocalSkelMeshComp->PrimaryComponentTick);
	}

	TSharedPtr<const FLSALiveLinkSkelMeshSource> PinnedSource = Source.Pin();
	if (PinnedSource.IsValid())
	{
		if (ILiveLinkClient * LiveLinkClient = PinnedSource->GetLiveLinkClient())
		{
			LiveLinkClient->RemoveSubject_AnyThread(GetSubjectKey());
		}
	}

	UsingSkelMeshComp = nullptr;
	UsingBones.Empty();
}

void ULiveLinkTestSkelMeshTrackerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	using namespace LiveStreamAnimation;

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (USkeletalMeshComponent* LocalSkelMeshComp = UsingSkelMeshComp.Get())
	{
		TSharedPtr<const FLSALiveLinkSkelMeshSource> PinnedSource = Source.Pin();
		if (ILiveLinkClient * LiveLinkClient = PinnedSource->GetLiveLinkClient())
		{
			FLiveLinkAnimationFrameData Frames;

			TArray<FTransform> Transforms = LocalSkelMeshComp->GetBoneSpaceTransforms();
			if (UsingBones.Num() == Transforms.Num())
			{
				Frames.Transforms = MoveTemp(Transforms);
			}
			else
			{
				Frames.Transforms.Reserve(UsingBones.Num());
				for (const int32 BoneIndex : UsingBones)
				{
					Frames.Transforms.Add(Transforms[BoneIndex]);
				}
			}

			FLiveLinkFrameDataStruct FrameData;
			FrameData.InitializeWith(&Frames);

			LiveLinkClient->PushSubjectFrameData_AnyThread(GetSubjectKey(), MoveTemp(FrameData));
		}
	}
}

void ULiveLinkTestSkelMeshTrackerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopTrackingSkelMesh();

	Super::EndPlay(EndPlayReason);
}

class USkeleton* ULiveLinkTestSkelMeshTrackerComponent::GetSkeleton(bool& bInvalidSkeletonIsError)
{
	bInvalidSkeletonIsError = false;

	auto GetSkelFromSkelMeshComp = [](USkeletalMeshComponent* Comp)
	{
		return (Comp && Comp->SkeletalMesh) ? Comp->SkeletalMesh->Skeleton : nullptr;
	};

	USkeleton* Skeleton = GetSkelFromSkelMeshComp(GetSkelMeshComp());

	// If this happens, it's likely because we're in a Blueprint.
	if (Skeleton == nullptr)
	{
		if (const ULSALiveLinkFrameTranslator* LocalTranslator = ULSALiveLinkSettings::GetFrameTranslator())
		{
			if (const FLSALiveLinkTranslationProfile* Profile = LocalTranslator->GetTranslationProfile(TranslationProfile))
			{
				Skeleton = Profile->Skeleton.LoadSynchronous();
			}
		}
	}

	if (Skeleton == nullptr)
	{
		if (UClass* Class = Cast<UClass>(GetOuter()))
		{
			if (SkelMeshComp.ComponentProperty != NAME_None)
			{
				if (FObjectPropertyBase* ObjProp = FindFProperty<FObjectPropertyBase>(Class, SkelMeshComp.ComponentProperty))
				{
					if (UObject* CDO = Class->GetDefaultObject())
					{
						Skeleton = GetSkelFromSkelMeshComp(Cast<USkeletalMeshComponent>(ObjProp->GetObjectPropertyValue_InContainer(CDO)));
					}
				}
			}
		}
	}

	return Skeleton;
}

ILiveLinkClient* ULiveLinkTestSkelMeshTrackerComponent::GetLiveLinkClient() const
{
	using namespace LiveStreamAnimation;

	TSharedPtr<const FLSALiveLinkSkelMeshSource> PinnedSource = Source.Pin();
	return PinnedSource.IsValid() ? PinnedSource->GetLiveLinkClient() : nullptr;
}

FLiveLinkSubjectKey ULiveLinkTestSkelMeshTrackerComponent::GetSubjectKey() const
{
	using namespace LiveStreamAnimation;

	TSharedPtr<const FLSALiveLinkSkelMeshSource> PinnedSource = Source.Pin();
	return FLiveLinkSubjectKey(PinnedSource->GetGuid(), SubjectName);
}

USkeletalMeshComponent* ULiveLinkTestSkelMeshTrackerComponent::GetSkelMeshComp() const
{
	if (USkeletalMeshComponent* LocalSkelMeshComp = WeakSkelMeshComp.Get())
	{
		return LocalSkelMeshComp;
	}

	return Cast<USkeletalMeshComponent>(SkelMeshComp.GetComponent(GetOwner()));
}