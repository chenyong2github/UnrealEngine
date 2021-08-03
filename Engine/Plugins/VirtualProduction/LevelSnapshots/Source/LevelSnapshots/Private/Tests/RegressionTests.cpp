// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotTestRunner.h"
#include "SnapshotTestActor.h"
#include "Types/ActorWithReferencesInCDO.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// Bug fixes should generally be tested. Put tests for bug fixes here.

/**
 * FTakeClassDefaultObjectSnapshotArchive used to crash when a class CDO contained a collection of object references. Make sure it does not crash and restores.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContainersWithObjectReferencesInCDO, "VirtualProduction.LevelSnapshots.Snapshot.Regression.ContainersWithObjectReferencesInCDO", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FContainersWithObjectReferencesInCDO::RunTest(const FString& Parameters)
{
	AActorWithReferencesInCDO* Actor = nullptr;

	FSnapshotTestRunner()
		.ModifyWorld([&](UWorld* World)
		{
			Actor = World->SpawnActor<AActorWithReferencesInCDO>();
		})
		.TakeSnapshot()
		.ModifyWorld([&](UWorld* World)
		{
			Actor->SetAllPropertiesTo(Actor->CylinderMesh);
		})
		.ApplySnapshot()
		.RunTest([&]()
		{
			TestTrue(TEXT("Object properties restored correctly"), Actor->DoAllPropertiesPointTo(Actor->CubeMesh));
		});
	
	return true;
}

