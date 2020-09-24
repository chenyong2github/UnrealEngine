// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigHierarchyCache.h"
#include "Rigs/RigHierarchyContainer.h"

bool FCachedRigElement::UpdateCache(const FRigHierarchyContainer* InContainer)
{
	if(InContainer)
	{
		if(!IsValid() || InContainer->Version != ContainerVersion)
		{
			return UpdateCache(GetKey(), InContainer);
		}
		return IsValid();
	}
	return false;
}

bool FCachedRigElement::UpdateCache(const FRigElementKey& InKey, const FRigHierarchyContainer* InContainer)
{
	if(InContainer)
	{
		if(!IsValid() || !IsIdentical(InKey, InContainer))
		{
			Reset();

			int32 Idx = InContainer->GetIndex(InKey);
			if(Idx != INDEX_NONE)
			{
				Key = InKey;
				Index = (uint16)Idx;
			}

			ContainerVersion = InContainer->Version;
		}
		return IsValid();
	}
	return false;
}

bool FCachedRigElement::UpdateCache(const FName& InName, const FRigBoneHierarchy* InHierarchy)
{
	if (InHierarchy && InHierarchy->Container)
	{
		return UpdateCache(FRigElementKey(InName, ERigElementType::Bone), InHierarchy->Container);
	}
	return false;
}

bool FCachedRigElement::UpdateCache(const FName& InName, const FRigSpaceHierarchy* InHierarchy)
{
	if (InHierarchy && InHierarchy->Container)
	{
		return UpdateCache(FRigElementKey(InName, ERigElementType::Space), InHierarchy->Container);
	}
	return false;
}

bool FCachedRigElement::UpdateCache(const FName& InName, const FRigControlHierarchy* InHierarchy)
{
	if (InHierarchy && InHierarchy->Container)
	{
		return UpdateCache(FRigElementKey(InName, ERigElementType::Control), InHierarchy->Container);
	}
	return false;
}

bool FCachedRigElement::UpdateCache(const FName& InName, const FRigCurveContainer* InHierarchy)
{
	if (InHierarchy && InHierarchy->Container)
	{
		return UpdateCache(FRigElementKey(InName, ERigElementType::Curve), InHierarchy->Container);
	}
	return false;
}

bool FCachedRigElement::IsIdentical(const FRigElementKey& InKey, const FRigHierarchyContainer* InContainer)
{
	return InKey == Key && InContainer->Version == ContainerVersion;
}
