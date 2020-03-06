// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/AABBTree.h"

int32 FAABBTreeCVars::UpdateDirtyElementPayloadData = 1;
FAutoConsoleVariableRef FAABBTreeCVars::CVarUpdateDirtyElementPayloadData(TEXT("p.aabbtree.updatedirtyelementpayloads"), FAABBTreeCVars::UpdateDirtyElementPayloadData, TEXT("Allow AABB tree elements to update internal payload data when they recieve a payload update"));