// Copyright Epic Games, Inc. All Rights Reserved.

#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Editor/UnrealEd/Public/PackageTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "IPlacementModeModule.h"
#include "PCGVolume.h"
#include "PCGComponent.h"
#include "Tests/PCGTestsCommon.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGComponentInNewPCGVolumes, FPCGTestBaseClass, "Editor.Plugins.Tools.PCG.PCGComponentInNewPCGVolumes", PCGTestsCommon::TestFlags)
bool FPCGComponentInNewPCGVolumes::RunTest(const FString& Parameters)
{
	// Get current world
	UWorld* World = GEditor->GetEditorWorldContext().World();
	UTEST_NOT_NULL(TEXT("Failed to get editor world context!"), World);

	// Condition for static analysis.
	if (!World)
	{
		return false;
	}

	// We need to get the PCGVolume actor factory
	TSubclassOf<AActor> PCGVolumeClass = APCGVolume::StaticClass();
	UActorFactory* PCGVolumeFactory = GEditor->FindActorFactoryForActorClass(PCGVolumeClass);
	UTEST_NOT_NULL(TEXT("Failed to find PCGVolume actor factory."), PCGVolumeFactory);

	// Get the asset data for the PCGVolume class
	FAssetData PCGVolumeAssetData = FAssetData(PCGVolumeClass);

	// Add a new PCGVolume actor to the level
	AActor* VolumeActor = nullptr;
	// To clean created assets, there appears window save content
	if (GCurrentLevelEditingViewportClient)
	{
		FTransform ActorTransform;
		VolumeActor = GEditor->UseActorFactory(PCGVolumeFactory, PCGVolumeAssetData, &ActorTransform);
	}

	UTEST_NOT_NULL(TEXT("Failed to add PCGVolume actor."), VolumeActor);

	// Condition for static analysis.
	if (VolumeActor)
	{
		UPCGComponent* PCGComponent = VolumeActor->FindComponentByClass<UPCGComponent>();
		TestNotNull("PCGVolume actor does not contain a PCGComponent!", PCGComponent);

		// Destroy the PCGVolume actor after test
		World->EditorDestroyActor(VolumeActor, false);
	}

	return true;
}
#endif
