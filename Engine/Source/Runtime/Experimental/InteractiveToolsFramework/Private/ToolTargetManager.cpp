// Copyright Epic Games, Inc. All Rights Reserved.


#include "ToolTargetManager.h"
#include "InteractiveToolsContext.h"
#include "ToolBuilderUtil.h"

void UToolTargetManager::Initialize()
{
	bIsActive = true;
}

void UToolTargetManager::Shutdown()
{
	Factories.Empty();
	bIsActive = false;
}

void UToolTargetManager::AddTargetFactory(UToolTargetFactory* Factory)
{
	Factories.AddUnique(Factory);
}

bool UToolTargetManager::CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetType) const
{
	for (UToolTargetFactory* Factory : Factories)
	{
		if (Factory->CanBuildTarget(SourceObject, TargetType))
		{
			return true;
		}
	}
	return false;
}

UToolTarget* UToolTargetManager::BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetType)
{
	for (UToolTargetFactory* Factory : Factories)
	{
		if (Factory->CanBuildTarget(SourceObject, TargetType))
		{
			UToolTarget* Result = Factory->BuildTarget(SourceObject, TargetType);
			if (Result != nullptr)
			{
				return Result;
			}
		}
	}
	return nullptr;
}

int32 UToolTargetManager::CountSelectedAndTargetable(const FToolBuilderState& SceneState, const FToolTargetTypeRequirements& TargetType) const
{
	return ToolBuilderUtil::CountComponents(SceneState, [&](UActorComponent* Object)
		{
			return CanBuildTarget(Object, TargetType);
		});
}

UToolTarget* UToolTargetManager::BuildFirstSelectedTargetable(const FToolBuilderState& SceneState, const FToolTargetTypeRequirements& TargetType)
{
	return BuildTarget(
		ToolBuilderUtil::FindFirstComponent(SceneState, [&](UActorComponent* Object)
		{
			return CanBuildTarget(Object, TargetType);
		}),
		TargetType);
}
