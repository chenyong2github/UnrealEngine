// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter/IKRetargeter.h"

#include "IKRigObjectVersion.h"

#if WITH_EDITOR
const FName UIKRetargeter::GetSourceIKRigPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, SourceIKRigAsset); };
const FName UIKRetargeter::GetTargetIKRigPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, TargetIKRigAsset); };
const FName UIKRetargeter::GetSourcePreviewMeshPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, SourcePreviewMesh); };
const FName UIKRetargeter::GetTargetPreviewMeshPropertyName() { return GET_MEMBER_NAME_STRING_CHECKED(UIKRetargeter, TargetPreviewMesh); }

void UIKRetargeter::GetSpeedCurveNames(TArray<FName>& OutSpeedCurveNames) const
{
	for (const URetargetChainSettings* ChainSetting : ChainSettings)
	{
		if (ChainSetting->SpeedCurveName != NAME_None)
		{
			OutSpeedCurveNames.Add(ChainSetting->SpeedCurveName);
		}
	}
}

#endif

UIKRetargeter::UIKRetargeter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootSettings = CreateDefaultSubobject<URetargetRootSettings>(TEXT("RootSettings"));
	RootSettings->SetFlags(RF_Transactional);

	CleanAndInitialize();
}

void UIKRetargeter::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FIKRigObjectVersion::GUID);
};

void UIKRetargeter::PostLoad()
{
	Super::PostLoad();

	// load deprecated chain mapping (pre UStruct to UObject refactor)
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!ChainMapping_DEPRECATED.IsEmpty())
	{
		for (const FRetargetChainMap& OldChainMap : ChainMapping_DEPRECATED)
		{
			if (OldChainMap.TargetChain == NAME_None)
			{
				continue;
			}
			
			TObjectPtr<URetargetChainSettings>* MatchingChain = ChainSettings.FindByPredicate([&](const URetargetChainSettings* Chain)
			{
				return Chain ? Chain->TargetChain == OldChainMap.TargetChain : false;
			});
			
			if (MatchingChain)
			{
				(*MatchingChain)->SourceChain = OldChainMap.SourceChain;
			}
			else
			{
				TObjectPtr<URetargetChainSettings> NewChainMap = NewObject<URetargetChainSettings>(this, URetargetChainSettings::StaticClass(), NAME_None, RF_Transactional);
				NewChainMap->TargetChain = OldChainMap.TargetChain;
				NewChainMap->SourceChain = OldChainMap.SourceChain;
				ChainSettings.Add(NewChainMap);
			}
		}
	}

	#if WITH_EDITORONLY_DATA
		// load deprecated target actor offset
		if (!FMath::IsNearlyZero(TargetActorOffset_DEPRECATED))
		{
			TargetMeshOffset.X = TargetActorOffset_DEPRECATED;
		}

		// load deprecated target actor scale
		if (!FMath::IsNearlyZero(TargetActorScale_DEPRECATED))
		{
			TargetMeshScale = TargetActorScale_DEPRECATED;
		}
	#endif


	// load deprecated retarget poses (pre adding retarget poses for source)
	if (!RetargetPoses_DEPRECATED.IsEmpty())
	{
		TargetRetargetPoses = RetargetPoses_DEPRECATED;
		RetargetPoses_DEPRECATED.Empty();
	}

	// load deprecated current retarget pose (pre adding retarget poses for source)
	if (CurrentRetargetPose_DEPRECATED != NAME_None)
	{
		CurrentTargetRetargetPose = CurrentRetargetPose_DEPRECATED;
		CurrentRetargetPose_DEPRECATED = NAME_None;
	}
	
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	CleanAndInitialize();
}

void UIKRetargeter::CleanAndInitialize()
{
	// remove null settings
	ChainSettings.Remove(nullptr);

	// use default pose as current pose unless set to something else
	if (CurrentSourceRetargetPose == NAME_None)
	{
		CurrentSourceRetargetPose = GetDefaultPoseName();
	}
	if (CurrentTargetRetargetPose == NAME_None)
	{
		CurrentTargetRetargetPose = GetDefaultPoseName();
	}

	// enforce the existence of a default pose
	if (!SourceRetargetPoses.Contains(GetDefaultPoseName()))
	{
		SourceRetargetPoses.Emplace(GetDefaultPoseName());
	}
	if (!TargetRetargetPoses.Contains(GetDefaultPoseName()))
	{
		TargetRetargetPoses.Emplace(GetDefaultPoseName());
	}

	// ensure current pose exists, otherwise set it to the default pose
	if (!SourceRetargetPoses.Contains(CurrentSourceRetargetPose))
	{
		CurrentSourceRetargetPose = GetDefaultPoseName();
	}
	if (!TargetRetargetPoses.Contains(CurrentTargetRetargetPose))
	{
		CurrentTargetRetargetPose = GetDefaultPoseName();
	}
};

FQuat FIKRetargetPose::GetDeltaRotationForBone(const FName BoneName) const
{
	const FQuat* BoneRotationOffset = BoneRotationOffsets.Find(BoneName);
	return BoneRotationOffset != nullptr ? *BoneRotationOffset : FQuat::Identity;
}

void FIKRetargetPose::SetDeltaRotationForBone(FName BoneName, FQuat RotationDelta)
{
	FQuat* RotOffset = BoneRotationOffsets.Find(BoneName);
	if (RotOffset == nullptr)
	{
		// first time this bone has been modified in this pose
		BoneRotationOffsets.Emplace(BoneName, RotationDelta);
		return;
	}

	*RotOffset = RotationDelta;
}

FVector FIKRetargetPose::GetRootTranslationDelta() const
{
	return RootTranslationOffset;
}

void FIKRetargetPose::SetRootTranslationDelta(const FVector& TranslationDelta)
{
	RootTranslationOffset = TranslationDelta;
}

void FIKRetargetPose::AddToRootTranslationDelta(const FVector& TranslateDelta)
{
	RootTranslationOffset += TranslateDelta;
}

void FIKRetargetPose::SortHierarchically(const FIKRigSkeleton& Skeleton)
{
	// sort offsets hierarchically so that they are applied in leaf to root order
	// when generating the component space retarget pose in the processor
	BoneRotationOffsets.KeySort([Skeleton](FName A, FName B)
	{
		return Skeleton.GetBoneIndexFromName(A) > Skeleton.GetBoneIndexFromName(B);
	});
}

const FIKRetargetPose* UIKRetargeter::GetCurrentRetargetPose(const ERetargetSourceOrTarget& SourceOrTarget) const
{
	return SourceOrTarget == ERetargetSourceOrTarget::Source ? &SourceRetargetPoses[CurrentSourceRetargetPose] : &TargetRetargetPoses[CurrentTargetRetargetPose];
}

const FName UIKRetargeter::GetDefaultPoseName()
{
	static const FName DefaultPoseName = "Default Pose";
	return DefaultPoseName;
}
