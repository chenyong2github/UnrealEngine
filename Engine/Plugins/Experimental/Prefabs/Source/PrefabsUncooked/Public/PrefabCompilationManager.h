// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "CoreMinimal.h"
#include "CoreTypes.h"

class UPrefabUncooked;

struct FPrefabCompilationManager
{
	static void Initialize();
	static void NotifyPrefabEdited(const UPrefabUncooked* EditorPrefab);
};
