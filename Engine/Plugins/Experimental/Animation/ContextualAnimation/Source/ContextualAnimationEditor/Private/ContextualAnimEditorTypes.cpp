// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimEditorTypes.h"
#include "ContextualAnimSceneAsset.h"
#include "Animation/AnimMontage.h"

// UContextualAnimNewIKTargetParams
////////////////////////////////////////////////////////////////////////////////////////////////

void UContextualAnimNewIKTargetParams::Reset(const FName& InSourceRole, const UContextualAnimSceneAsset& InSceneAsset)
{
	CachedRoles = InSceneAsset.GetRoles();
	check(CachedRoles.Contains(InSourceRole));

	SceneAssetPtr = &InSceneAsset;

	SourceRole = InSourceRole;
	GoalName = NAME_None;
	TargetBone = FBoneReference();
	SourceBone = FBoneReference();

	if (CachedRoles.Num() > 1)
	{
		TargetRole = *CachedRoles.FindByPredicate([this](const FName& Role) { return Role != SourceRole; });
	}
}

bool UContextualAnimNewIKTargetParams::HasValidData() const
{
	return GoalName != NAME_None &&
		TargetBone.BoneName != NAME_None &&
		SourceBone.BoneName != NAME_None &&
		CachedRoles.Contains(TargetRole);
}

const UContextualAnimSceneAsset& UContextualAnimNewIKTargetParams::GetSceneAsset() const
{
	check(SceneAssetPtr.IsValid());
	return *SceneAssetPtr.Get();
}

USkeleton* UContextualAnimNewIKTargetParams::GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle)
{
	bInvalidSkeletonIsError = false;

	const FContextualAnimTrack* AnimTrack = nullptr;
	if (PropertyHandle)
	{
		if (PropertyHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(UContextualAnimNewIKTargetParams, SourceBone))
		{
			AnimTrack = GetSceneAsset().GetAnimTrack(SourceRole, 0);
		}
		else if (PropertyHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(UContextualAnimNewIKTargetParams, TargetBone))
		{
			AnimTrack = GetSceneAsset().GetAnimTrack(TargetRole, 0);
		}
	}

	return (AnimTrack && AnimTrack->Animation) ? AnimTrack->Animation->GetSkeleton() : nullptr;
}

TArray<FString> UContextualAnimNewIKTargetParams::GetTargetRoleOptions() const
{
	TArray<FString> Options;

	for (const FName& Role : CachedRoles)
	{
		if (Role != SourceRole)
		{
			Options.Add(Role.ToString());
		}
	}

	return Options;
}