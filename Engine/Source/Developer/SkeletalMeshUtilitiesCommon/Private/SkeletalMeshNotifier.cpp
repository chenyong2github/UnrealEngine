// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshNotifier.h"

FSkeletalMeshNotifyDelegate& ISkeletalMeshNotifier::Delegate()
{
	return NotifyDelegate;
}

void ISkeletalMeshNotifier::Notify(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType) const
{
	NotifyDelegate.Broadcast(BoneNames, InNotifyType);
}

