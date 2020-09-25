// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeModule.h"
#include "Serialization/CustomVersion.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "LandscapeComponent.h"
#include "LandscapeVersion.h"
#include "Materials/MaterialInstance.h"

#include "LandscapeProxy.h"
#include "Landscape.h"
#include "Engine/Texture2D.h"

// Register the custom version with core
FCustomVersionRegistration GRegisterLandscapeCustomVersion(FLandscapeCustomVersion::GUID, FLandscapeCustomVersion::LatestVersion, TEXT("Landscape"));

class FLandscapeModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	void StartupModule() override;
	void ShutdownModule() override;
};

#if WITH_EDITOR
/**
 * Gets landscape-specific material's static parameters values.
 *
 * @param OutStaticParameterSet A set that should be updated with found parameters values.
 * @param Material Material instance to look for parameters.
 */
void LandscapeMaterialsParameterValuesGetter(FStaticParameterSet &OutStaticParameterSet, UMaterialInstance* Material);

/**
 * Updates landscape-specific material parameters.
 *
 * @param OutStaticParameterSet A set of parameters.
 * @param Material A material to update.
 */
bool LandscapeMaterialsParameterSetUpdater(FStaticParameterSet &OutStaticParameterSet, UMaterial* Material);
#endif // WITH_EDITOR

#if WITH_EDITOR
/**
 * Gets array of Landscape-specific textures and materials connected with given
 * level.
 *
 * @param Level Level to search textures and materials in.
 * @param OutTexturesAndMaterials (Output parameter) Array to fill.
 */
void GetLandscapeTexturesAndMaterials(ULevel* Level, TArray<UObject*>& OutTexturesAndMaterials)
{
	TArray<UObject*> ObjectsInLevel;
	const bool bIncludeNestedObjects = true;
	GetObjectsWithOuter(Level, ObjectsInLevel, bIncludeNestedObjects);
	for (auto* ObjInLevel : ObjectsInLevel)
	{
		ULandscapeComponent* LandscapeComponent = Cast<ULandscapeComponent>(ObjInLevel);
		if (LandscapeComponent)
		{
			LandscapeComponent->GetGeneratedTexturesAndMaterialInstances(OutTexturesAndMaterials);
		}		
	}
}

/**
 * A function that fires everytime a world is renamed.
 *
 * @param World A world that was renamed.
 * @param InName New world name.
 * @param NewOuter New outer of the world after rename.
 * @param Flags Rename flags.
 * @param bShouldFailRename (Output parameter) If you set it to true, then the renaming process should fail.
 */
void WorldRenameEventFunction(UWorld* World, const TCHAR* InName, UObject* NewOuter, ERenameFlags Flags, bool& bShouldFailRename)
{
	// Also rename all textures and materials used by landscape components
	TArray<UObject*> LandscapeTexturesAndMaterials;
	GetLandscapeTexturesAndMaterials(World->PersistentLevel, LandscapeTexturesAndMaterials);
	UPackage* PersistentLevelPackage = World->PersistentLevel->GetOutermost();
	for (auto* OldTexOrMat : LandscapeTexturesAndMaterials)
	{
		// Now that landscape textures and materials are properly parented, this should not be necessary anymore
		if (OldTexOrMat && OldTexOrMat->GetOuter() == PersistentLevelPackage)
		{
			// The names for these objects are not important, just generate a new name to avoid collisions
			if (!OldTexOrMat->Rename(nullptr, NewOuter, Flags))
			{
				bShouldFailRename = true;
			}
		}
	}
}
#endif

void FLandscapeModule::StartupModule()
{
#if WITH_EDITOR
	// This code will execute after your module is loaded into memory (but after global variables are initialized, of course.)
	UMaterialInstance::CustomStaticParametersGetters.AddStatic(
		&LandscapeMaterialsParameterValuesGetter
	);

	UMaterialInstance::CustomParameterSetUpdaters.Add(
		UMaterialInstance::FCustomParameterSetUpdaterDelegate::CreateStatic(
			&LandscapeMaterialsParameterSetUpdater
		)
	);
#endif // WITH_EDITOR

#if WITH_EDITOR
	FWorldDelegates::OnPreWorldRename.AddStatic(
		&WorldRenameEventFunction
	);
#endif // WITH_EDITOR
}

void FLandscapeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

IMPLEMENT_MODULE(FLandscapeModule, Landscape);
