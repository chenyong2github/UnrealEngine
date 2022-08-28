// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFActorUtility.h"
#include "Actors/GLTFHotspotActor.h"
#include "LevelSequenceActor.h"

bool FGLTFActorUtility::IsGenericActor(const AActor* Actor)
{
	const FString BlueprintPath = GetBlueprintPath(Actor);
	if (IsSkySphereBlueprint(BlueprintPath)) return false;
	if (IsHDRIBackdropBlueprint(BlueprintPath)) return false;
	if (Actor->IsA<ALevelSequenceActor>()) return false;
	if (Actor->IsA<AGLTFHotspotActor>()) return false;
	if (Actor->IsA<APawn>()) return false;
	return true;
}

FString FGLTFActorUtility::GetBlueprintPath(const AActor* Actor)
{
	UClass* Class = Actor->GetClass();
	if (Class != nullptr && Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		return Class->GetPathName();
	}

	return TEXT("");
}

bool FGLTFActorUtility::IsSkySphereBlueprint(const FString& Path)
{
	// TODO: what if a blueprint inherits BP_Sky_Sphere?
	return Path.Equals(TEXT("/Engine/EngineSky/BP_Sky_Sphere.BP_Sky_Sphere_C"));
}

bool FGLTFActorUtility::IsHDRIBackdropBlueprint(const FString& Path)
{
	// TODO: what if a blueprint inherits HDRIBackdrop?
	return Path.Equals(TEXT("/HDRIBackdrop/Blueprints/HDRIBackdrop.HDRIBackdrop_C"));
}
