// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/ActorDescContainer.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "PackageTools.h"
#include "EditorWorldUtils.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

#define TEST_NAME_ROOT "System.Engine.WorldPartition"

namespace WorldPartitionTests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldPartitionStreamingGenerationTest, TEST_NAME_ROOT ".StreamingGeneration", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

	bool FWorldPartitionStreamingGenerationTest::RunTest(const FString& Parameters)
	{
#if WITH_EDITOR
		ON_SCOPE_EXIT
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		};

		FScopedEditorWorld ScopedEditorWorld(
			TEXTVIEW("/Engine/WorldPartition/WorldPartitionUnitTest"),
			UWorld::InitializationValues()
				.RequiresHitProxies(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.AllowAudioPlayback(false)
				.CreatePhysicsScene(true)
		);
		
		UWorld* World = ScopedEditorWorld.GetWorld();
		if (!TestTrue(TEXT("Missing World Object"), !!World))
		{
			return false;
		}

		UWorldPartition* WorldPartition = World->GetWorldPartition();
		if (!TestTrue(TEXT("Missing World Partition Object"), !!WorldPartition))
		{
			return false;
		}

		UActorDescContainer* ActorDescMainContainer = WorldPartition->GetActorDescContainer();
		if (!TestTrue(TEXT("Missing World Partition Container"), !!ActorDescMainContainer))
		{
			return false;
		}

		if (!TestTrue(TEXT("World Partition Generate Streaming"), WorldPartition->GenerateContainerStreaming(ActorDescMainContainer)))
		{
			return false;
		}

		FWorldPartitionReference ActorRef(ActorDescMainContainer, FGuid(TEXT("5D9F93BA407A811AFDDDAAB4F1CECC6A")));
		if (!TestTrue(TEXT("Invalid Actor Reference"), ActorRef.IsValid()))
		{
			return false;
		}

		AActor* Actor = ActorRef->GetActor();
		if (!TestTrue(TEXT("Missing Actor"), !!Actor))
		{
			return false;
		}

		FSoftObjectPath ActorRuntimePath;
		if (!TestTrue(TEXT("Actor Path Editor to Runtime Conversion Failed"), FWorldPartitionHelpers::ConvertEditorPathToRuntimePath(ActorRef->GetActorSoftPath(), ActorRuntimePath)))
		{
			return false;
		}

		FSoftObjectPath ActorEditorPath;
		if (!TestTrue(TEXT("Actor Path Runtime to Editor Conversion Failed"), FWorldPartitionHelpers::ConvertRuntimePathToEditorPath(ActorRuntimePath, ActorEditorPath)))
		{
			return false;
		}

		if (!TestTrue(TEXT("Actor Path Editor to Runtime to Editor Conversion Failed"), ActorEditorPath == ActorRef->GetActorSoftPath()))
		{
			return false;
		}
#endif
		return true;
	}
}

#undef TEST_NAME_ROOT

#endif 