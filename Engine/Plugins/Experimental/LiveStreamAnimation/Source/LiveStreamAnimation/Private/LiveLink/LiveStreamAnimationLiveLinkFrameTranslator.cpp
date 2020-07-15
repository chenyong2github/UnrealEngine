// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLink/LiveStreamAnimationLiveLinkFrameTranslator.h"
#include "LiveLink/LiveStreamAnimationLiveLinkRole.h"
#include "LiveLink/LiveStreamAnimationLiveLinkFrameData.h"
#include "LiveStreamAnimationLog.h"
#include "UObject/UnrealType.h"
#include "Animation/Skeleton.h"
#include "Misc/ScopeExit.h"
#include "Roles/LiveLinkAnimationRole.h"

class FLiveStreamAnimationLiveLinkFrameTranslator : public ILiveLinkFrameTranslatorWorker
{
public:

	FLiveStreamAnimationLiveLinkFrameTranslator(TMap<FLiveStreamAnimationHandle, FLiveStreamAnimationLiveLinkTranslationProfile>&& InTranslationProfiles)
		: TranslationProfiles(MoveTemp(InTranslationProfiles))
	{
	}

	virtual ~FLiveStreamAnimationLiveLinkFrameTranslator()
	{
	}

	virtual TSubclassOf<ULiveLinkRole> GetFromRole() const override
	{
		return ULiveStreamAnimationLiveLinkRole::StaticClass();
	}

	virtual TSubclassOf<ULiveLinkRole> GetToRole() const override
	{
		return ULiveLinkAnimationRole::StaticClass();
	}

	virtual bool Translate(const FLiveLinkStaticDataStruct& InStaticData, const FLiveLinkFrameDataStruct& InFrameData, FLiveLinkSubjectFrameData& OutTranslatedFrame) const override
	{
		if (const FLiveStreamAnimationLiveLinkFrameData* FrameData = InFrameData.Cast<const FLiveStreamAnimationLiveLinkFrameData>())
		{
			const FLiveStreamAnimationLiveLinkSourceOptions Options = FrameData->Options;
			const FLiveStreamAnimationHandle TranslationProfileHandle = FrameData->TranslationProfileHandle;

			FLiveLinkAnimationFrameData* AnimFrameData = new FLiveLinkAnimationFrameData;
			*AnimFrameData = *FrameData;

			ON_SCOPE_EXIT
			{
				OutTranslatedFrame.FrameData.InitializeWith(AnimFrameData);
				OutTranslatedFrame.StaticData.InitializeWith(InStaticData);
			};

			// We don't have any transforms, so we don't need to translate.
			if (!Options.WithTransforms())
			{
				return true;
			}

			// Handle was invalid, so we no translation is necessary.
			if (!TranslationProfileHandle.IsValid())
			{
				return true;
			}

			const FLiveStreamAnimationLiveLinkTranslationProfile* TranslationProfile = TranslationProfiles.Find(TranslationProfileHandle);
			const FLiveLinkSkeletonStaticData* StaticData = InStaticData.Cast<const FLiveLinkSkeletonStaticData>();

			// Skeleton was invalid, or we don't need to do any translation, so we're done.
			if (!TranslationProfile || !StaticData)
			{
				return true;
			}

			// If we only have a partial transforms, then we will need to get their ref poses.
			// This is also where we could do quantization, etc.
			if (!Options.bWithTransformTranslation ||
				!Options.bWithTransformRotation ||
				!Options.bWithTransformScale)
			{
				auto UpdateTransform = [Options](FTransform& OutTransform, const FTransform& DefaultTransform)
				{
					const FVector Translation = Options.bWithTransformTranslation ? OutTransform.GetTranslation() : DefaultTransform.GetTranslation();
					const FQuat Rotation = Options.bWithTransformRotation ? OutTransform.GetRotation() : DefaultTransform.GetRotation();
					const FVector Scale = Options.bWithTransformScale ? OutTransform.GetScale3D() : DefaultTransform.GetScale3D();

					OutTransform.SetComponents(Rotation, Translation, Scale);
				};

				// TODO: Could probably add some Test Sanity Checks to ensure cached bone lookup matches Live Link Skeleton.
				//			Bonus points, have some status information on whether or not we already ran the validation,
				//			and whether or not it succeeded, so we can do this in shipping (requires locking, etc.)

				const TArray<FTransform>& BoneTransformsByIndex = TranslationProfile->GetBoneTransformsByIndex();
				if (AnimFrameData->Transforms.Num() == BoneTransformsByIndex.Num())
				{
					for (int32 i = 0; i < AnimFrameData->Transforms.Num(); ++i)
					{
						UpdateTransform(AnimFrameData->Transforms[i], BoneTransformsByIndex[i]);
					}
				}
				else
				{
					const TMap<FName, FTransform>& PoseTransforms = TranslationProfile->GetBoneTransformsByName();
					for (int32 i = 0; i < StaticData->BoneNames.Num(); ++i)
					{
						const FName BoneName = StaticData->BoneNames[i];
						const FTransform& DefaultBoneTransform = PoseTransforms.FindChecked(BoneName);
						FTransform& OutPoseTransform = AnimFrameData->Transforms[i];

						UpdateTransform(OutPoseTransform, DefaultBoneTransform);
					}
				}
			}
		}

		return true;
	}

private:

	TMap<FLiveStreamAnimationHandle, FLiveStreamAnimationLiveLinkTranslationProfile> TranslationProfiles;
};

bool FLiveStreamAnimationLiveLinkTranslationProfile::UpdateTransformMappings()
{
	BoneTransformsByName.Empty();
	BoneTransformsByIndex.Empty();

	if (USkeleton* LocalSkeleton = Skeleton.Get())
	{
		const FReferenceSkeleton& ReferenceSkeleton = LocalSkeleton->GetReferenceSkeleton();
		const TArray<FMeshBoneInfo>& RefBoneInfo = ReferenceSkeleton.GetRawRefBoneInfo();
		const TArray<FTransform>& RefBonePose = ReferenceSkeleton.GetRefBonePose();

		BoneTransformsByName.Reserve(RefBoneInfo.Num());

		// RefBoneInfo and RefBonePose should necessarily have the same number of entries,
		// and each entry at the same index in each should necessarily reference the same
		// bone information.
		for (int32 i = 0; i < RefBoneInfo.Num() && i < RefBonePose.Num(); ++i)
		{
			const FName SkelBoneName = RefBoneInfo[i].Name;
			const FName* FoundRemappedBone = BoneRemappings.Find(SkelBoneName);
			const FName UseBoneName = FoundRemappedBone ? *FoundRemappedBone : SkelBoneName;

			if (BoneTransformsByName.Contains(UseBoneName))
			{
				UE_LOG(LogLiveStreamAnimation, Warning,
					TEXT("FLiveStreamAnimationLiveLinkTranslationProfile::UpdateTransformMappings: Duplicate bone name found when creating BoneMappings. This may cause broken animation. Bone=%s"));
			}

			BoneTransformsByName.Add(UseBoneName, RefBonePose[i]);
		}

		if (BonesToUse.Num() > 0)
		{
			TSet<FName> FoundBones;
			FoundBones.Reserve(BonesToUse.Num());
			BoneTransformsByIndex.Reserve(BonesToUse.Num());
			
			for (const FName& BoneToUse : BonesToUse)
			{
				if (FoundBones.Contains(BoneToUse))
				{
					UE_LOG(LogLiveStreamAnimation, Warning,
						TEXT("FLiveStreamAnimationLiveLinkTranslationProfile::UpdateTransformMappings: Duplicate bone name, cannot use cached mappings. Bone=%s"),
						*BoneToUse.ToString());

					BoneTransformsByIndex.Empty();
					break;
				}

				if (FTransform* FoundTransform = BoneTransformsByName.Find(BoneToUse))
				{
					BoneTransformsByIndex.Add(*FoundTransform);
				}
				else
				{
					UE_LOG(LogLiveStreamAnimation, Warning,
						TEXT("FLiveStreamAnimationLiveLinkTranslationProfile::UpdateTransformMappings: Invalid bone name, cannot use cached mappings. Bone=%s"),
						*BoneToUse.ToString());

					BoneTransformsByIndex.Empty();
					break;
				}
			}
		}

		return true;
	}

	return false;
}

TSubclassOf<ULiveLinkRole> ULiveStreamAnimationLiveLinkFrameTranslator::GetFromRole() const
{
	return ULiveStreamAnimationLiveLinkRole::StaticClass();
}

TSubclassOf<ULiveLinkRole> ULiveStreamAnimationLiveLinkFrameTranslator::GetToRole() const
{
	return ULiveLinkAnimationRole::StaticClass();
}

ULiveStreamAnimationLiveLinkFrameTranslator::FWorkerSharedPtr ULiveStreamAnimationLiveLinkFrameTranslator::FetchWorker()
{
	// TODO: This won't be needed in live scenarios, but for testing purposes
	//			it would *probably* be smart to hook into USkeleton's Bone Hierarchy Update
	//			and invalidate our old worker.
	if (!Worker.IsValid())
	{
		TMap<FLiveStreamAnimationHandle, FLiveStreamAnimationLiveLinkTranslationProfile> LocalTranslationProfiles;
		LocalTranslationProfiles.Reserve(TranslationProfiles.Num());

		for (auto It = TranslationProfiles.CreateIterator(); It; ++It)
		{
			const FLiveStreamAnimationHandle Handle(It.Key());
			if (!Handle.IsValid())
			{
				UE_LOG(LogLiveStreamAnimation, Warning,
					TEXT("ULiveStreamAnimationLiveLinkFrameTranslator::FetchWorker: %s is not a registered LiveStreamAnimationHandle! Skipping translation profile. Class=%s"),
					*It.Key().ToString(), *GetClass()->GetName());

				continue;
			}

			FLiveStreamAnimationLiveLinkTranslationProfile LocalTranslationProfile = It.Value();
			if (!LocalTranslationProfile.UpdateTransformMappings())
			{
				UE_LOG(LogLiveStreamAnimation, Warning,
					TEXT("ULiveStreamAnimationLiveLinkFrameTranslator::FetchWorker: %s failed to update bone mappings for Skeleton %s! Skipping translation profile. Class=%s"),
					*It.Key().ToString(), *LocalTranslationProfile.Skeleton.ToString(), *GetClass()->GetName());

				continue;
			}

			LocalTranslationProfiles.Emplace(Handle, MoveTemp(LocalTranslationProfile));
		}

		Worker = MakeShared<FLiveStreamAnimationLiveLinkFrameTranslator, ESPMode::ThreadSafe>(MoveTemp(LocalTranslationProfiles));
	}

	return Worker;
}

#if WITH_EDITOR
void ULiveStreamAnimationLiveLinkFrameTranslator::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULiveStreamAnimationLiveLinkFrameTranslator, TranslationProfiles))
	{
		Worker.Reset();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULiveStreamAnimationLiveLinkFrameTranslator::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULiveStreamAnimationLiveLinkFrameTranslator, TranslationProfiles))
	{
		Worker.Reset();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif