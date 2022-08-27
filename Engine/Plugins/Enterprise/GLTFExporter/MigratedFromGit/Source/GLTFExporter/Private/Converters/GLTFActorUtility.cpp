// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFActorUtility.h"
#include "Engine/Blueprint.h"

bool FGLTFActorUtility::IsRootActor(const AActor* Actor, bool bSelectedOnly)
{
	const AActor* ParentActor = Actor->GetAttachParentActor();
	return ParentActor == nullptr || (bSelectedOnly && !ParentActor->IsSelected());
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
