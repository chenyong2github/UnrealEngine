// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/AABBTree.h"

int32 FAABBTreeCVars::UpdateDirtyElementPayloadData = 1;
FAutoConsoleVariableRef FAABBTreeCVars::CVarUpdateDirtyElementPayloadData(TEXT("p.aabbtree.updatedirtyelementpayloads"), FAABBTreeCVars::UpdateDirtyElementPayloadData, TEXT("Allow AABB tree elements to update internal payload data when they recieve a payload update"));

int32 FAABBTreeCVars::IgnoreDirtyElements = 0;
FAutoConsoleVariableRef FAABBTreeCVars::CVarIgnoreDirtyElements(TEXT("p.aabbtree.IgnoreDirtyElements"), FAABBTreeCVars::IgnoreDirtyElements, TEXT("Ignore AABB tree dirty elements"));