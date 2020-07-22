// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "SceneOutlinerPublicTypes.h"

#include "SceneOutlinerModule.h"

namespace SceneOutliner
{

void FSharedOutlinerData::UseDefaultColumns()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	// Create an instance of every default column type
	for (auto& DefaultColumn : SceneOutlinerModule.DefaultColumnMap)
	{
		ColumnMap.Add(DefaultColumn.Key, DefaultColumn.Value);
	}
}

}	// namespace SceneOutliner 