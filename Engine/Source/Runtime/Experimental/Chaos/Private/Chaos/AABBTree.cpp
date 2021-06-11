// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/AABBTree.h"

int32 FAABBTreeCVars::UpdateDirtyElementPayloadData = 1;
FAutoConsoleVariableRef FAABBTreeCVars::CVarUpdateDirtyElementPayloadData(TEXT("p.aabbtree.updatedirtyelementpayloads"), FAABBTreeCVars::UpdateDirtyElementPayloadData, TEXT("Allow AABB tree elements to update internal payload data when they recieve a payload update"));

int32 FAABBTreeDirtyGridCVars::DirtyElementGridCellSize = 0; //1000; 0 means disabled
FAutoConsoleVariableRef FAABBTreeDirtyGridCVars::CVarDirtyElementGridCellSize(TEXT("p.aabbtree.DirtyElementGridCellSize"), FAABBTreeDirtyGridCVars::DirtyElementGridCellSize, TEXT("DirtyElement Grid acceleration structure cell size in cm. 0 or less will disable the feature"));

int32 FAABBTreeDirtyGridCVars::DirtyElementMaxGridCellQueryCount = 340;
FAutoConsoleVariableRef FAABBTreeDirtyGridCVars::CVarDirtyElementMaxGridCellQueryCount(TEXT("p.aabbtree.DirtyElementMaxGridCellQueryCount"), FAABBTreeDirtyGridCVars::DirtyElementMaxGridCellQueryCount, TEXT("Maximum grid cells to query (per raycast for example) in DirtyElement grid acceleration structure before falling back to brute force"));

int32 FAABBTreeDirtyGridCVars::DirtyElementMaxPhysicalSizeInCells = 16;
FAutoConsoleVariableRef FAABBTreeDirtyGridCVars::CVarDirtyElementMaxPhysicalSizeInCells(TEXT("p.aabbtree.DirtyElementMaxPhysicalSizeInCells"), FAABBTreeDirtyGridCVars::DirtyElementMaxPhysicalSizeInCells, TEXT("If a dirty element stradles more than this number of cells, it will no be added to the grid acceleration structure"));

int32 FAABBTreeDirtyGridCVars::DirtyElementMaxCellCapacity = 32;
FAutoConsoleVariableRef FAABBTreeDirtyGridCVars::CVarDirtyElementMaxCellCapacity(TEXT("p.aabbtree.DirtyElementMaxCellCapacity"), FAABBTreeDirtyGridCVars::DirtyElementMaxCellCapacity, TEXT("The maximum number of dirty elements that can be added to a single grid cell before spilling to slower flat list"));

CSV_DEFINE_CATEGORY(ChaosPhysicsTimers, true);

