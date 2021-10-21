// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneQueries/SceneSnappingManager.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ContextObjectStore.h"

USceneSnappingManager* USceneSnappingManager::Find(UInteractiveToolManager* ToolManager)
{
	if (ensure(ToolManager))
	{
		USceneSnappingManager* Found = ToolManager->GetContextObjectStore()->FindContext<USceneSnappingManager>();
		if (Found != nullptr)
		{
			return Found;
		}
	}
	return nullptr;
}


USceneSnappingManager* USceneSnappingManager::Find(UInteractiveGizmoManager* GizmoManager)
{
	if (ensure(GizmoManager))
	{
		USceneSnappingManager* Found = GizmoManager->GetContextObjectStore()->FindContext<USceneSnappingManager>();
		if (Found != nullptr)
		{
			return Found;
		}
	}
	return nullptr;
}
