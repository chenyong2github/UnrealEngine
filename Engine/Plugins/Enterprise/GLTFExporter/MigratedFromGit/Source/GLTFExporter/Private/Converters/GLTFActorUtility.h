// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"

struct FGLTFActorUtility
{
	static bool IsRootActor(const AActor* Actor, bool bSelectedOnly);

	static UBlueprint* GetBlueprintFromActor(const AActor* Actor);

	static bool IsSkySphereBlueprint(const UBlueprint* Blueprint);

	static bool IsHDRIBackdropBlueprint(const UBlueprint* Blueprint);
};
