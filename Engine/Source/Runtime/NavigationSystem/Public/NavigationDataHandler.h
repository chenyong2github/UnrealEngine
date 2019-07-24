// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NavigationOctreeController.h"
#include "NavigationDirtyAreasController.h"


struct NAVIGATIONSYSTEM_API FNavigationDataHandler
{
	FNavigationOctreeController& OctreeController;
	FNavigationDirtyAreasController& DirtyAreasController;

	FNavigationDataHandler(FNavigationOctreeController& InOctreeController, FNavigationDirtyAreasController& InDirtyAreasController);

	void RemoveNavOctreeElementId(const FOctreeElementId& ElementId, int32 UpdateFlags);
	FSetElementId RegisterNavOctreeElement(UObject& ElementOwner, INavRelevantInterface& ElementInterface, int32 UpdateFlags);
	void AddElementToNavOctree(const FNavigationDirtyElement& DirtyElement);
	void UnregisterNavOctreeElement(UObject& ElementOwner, INavRelevantInterface& ElementInterface, int32 UpdateFlags);
	void UpdateNavOctreeElement(UObject& ElementOwner, INavRelevantInterface& ElementInterface, int32 UpdateFlags);
	void UpdateNavOctreeParentChain(UObject& ElementOwner, bool bSkipElementOwnerUpdate = false);
	bool UpdateNavOctreeElementBounds(UActorComponent& Comp, const FBox& NewBounds, const FBox& DirtyArea);	
	void FindElementsInNavOctree(const FBox& QueryBox, const FNavigationOctreeFilter& Filter, TArray<FNavigationOctreeElement>& Elements);
	bool ReplaceAreaInOctreeData(const UObject& Object, TSubclassOf<UNavArea> OldArea, TSubclassOf<UNavArea> NewArea, bool bReplaceChildClasses);
	void AddLevelCollisionToOctree(ULevel& Level);
	void RemoveLevelCollisionFromOctree(ULevel& Level);
	void UpdateActorAndComponentsInNavOctree(AActor& Actor);
	void ProcessPendingOctreeUpdates();
};
