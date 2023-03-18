// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVD/Public/ChaosVDScene.h"

#include "ChaosVDEditorSettings.h"
#include "ChaosVDModule.h"
#include "ChaosVDParticleActor.h"
#include "ChaosVDRecording.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "UObject/Package.h"

void FChaosVDScene::Initialize()
{
	if (!ensure(!bIsInitialized))
	{
		return;
	}

	PhysicsVDWorld = CreatePhysicsVDWorld();

	bIsInitialized = true;
}

void FChaosVDScene::DeInitialize()
{
	if (!ensure(bIsInitialized))
	{
		return;
	}

	if (PhysicsVDWorld)
	{
		PhysicsVDWorld->DestroyWorld(true);
		PhysicsVDWorld->MarkAsGarbage();
	}
	
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	bIsInitialized = false;
}

void FChaosVDScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PhysicsVDWorld);
	Collector.AddStableReferenceMap(ParticleVDInstancesByID);
}

void FChaosVDScene::UpdateFromRecordedStepData(EChaosVDSolverType SolverID, const FChaosVDStepData& InRecordedStepData)
{
	TSet<uint32> ParticlesIDsInRecordedStepData;
	ParticlesIDsInRecordedStepData.Reserve(InRecordedStepData.RecordedParticles.Num());

	// Go over existing Particle VD Instances and update them or create them if needed 
	for (const FChaosVDParticleDebugData& Particle : InRecordedStepData.RecordedParticles)
	{
		const uint32 ParticleVDInstanceID = GetIDForRecordedParticleData(Particle);
		ParticlesIDsInRecordedStepData.Add(ParticleVDInstanceID);

		if (AChaosVDParticleActor** ExistingParticleVDInstancePtrPtr = ParticleVDInstancesByID.Find(ParticleVDInstanceID))
		{
			if (AChaosVDParticleActor* ExistingParticleVDInstancePtr = *ExistingParticleVDInstancePtrPtr)
			{
				ExistingParticleVDInstancePtr->UpdateFromRecordedData(Particle);
			}
			else
			{
				//TODO: Handle this error
				ensure(false);
			}
		}
		else
		{
			if (AChaosVDParticleActor* NewParticleVDInstance = SpawnParticleFromRecordedData(Particle))
			{
				FName FolderPath = *FPaths::Combine(TEXT("Solver ID : ") + UEnum::GetDisplayValueAsText(SolverID).ToString(), UEnum::GetDisplayValueAsText(Particle.ParticleType).ToString());
				NewParticleVDInstance->SetFolderPath(FolderPath);

				// TODO: Precalculate the max num of entries we would see in the loaded file, and use that number to pre-allocate this map
				ParticleVDInstancesByID.Add(ParticleVDInstanceID, NewParticleVDInstance);
			}
			else
			{
				//TODO: Handle this error
				ensure(false);
			}
		}
	}

	int32 AmountRemoved = 0;
	for (TMap<uint32, AChaosVDParticleActor*>::TIterator RemoveIterator = ParticleVDInstancesByID.CreateIterator(); RemoveIterator; ++RemoveIterator)
	{
		if (!ParticlesIDsInRecordedStepData.Contains(RemoveIterator.Key()))
		{
			if (AChaosVDParticleActor* ActorToRemove = RemoveIterator.Value())
			{
				PhysicsVDWorld->DestroyActor(ActorToRemove);
			}

			RemoveIterator.RemoveCurrent();

			AmountRemoved++;
		}
	}
	
	OnSceneUpdated().Broadcast();
}

void FChaosVDScene::CleanUpScene()
{
	for (const TPair<uint32, AChaosVDParticleActor*>& ParticleVDInstanceWithID : ParticleVDInstancesByID)
	{
		PhysicsVDWorld->DestroyActor(ParticleVDInstanceWithID.Value);
	}

	ParticleVDInstancesByID.Reset();

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
}

AChaosVDParticleActor* FChaosVDScene::SpawnParticleFromRecordedData(const FChaosVDParticleDebugData& InParticleData)
{
	// Temp code until we have geometry data
	const UChaosVDEditorSettings* Settings = GetDefault<UChaosVDEditorSettings>();
	UStaticMesh* DebugMesh = Settings->DebugMesh.LoadSynchronous();

	FActorSpawnParameters Params;
	Params.Name = *InParticleData.DebugName;
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	if (AChaosVDParticleActor* NewActor = PhysicsVDWorld->SpawnActor<AChaosVDParticleActor>(Params))
	{
		NewActor->UpdateFromRecordedData(InParticleData);
		NewActor->SetActorLabel(InParticleData.DebugName);
		NewActor->SetStaticMesh(DebugMesh);

		return NewActor;
	}

	return nullptr;
}

uint32 FChaosVDScene::GetIDForRecordedParticleData(const FChaosVDParticleDebugData& InParticleData) const
{
	return InParticleData.ParticleIndex;
}

UWorld* FChaosVDScene::CreatePhysicsVDWorld() const
{
	const FName UniqueWorldName = FName(FGuid::NewGuid().ToString());
	UWorld* NewWorld = NewWorld = NewObject<UWorld>( GetTransientPackage(), UniqueWorldName );
	
	NewWorld->WorldType = EWorldType::EditorPreview;

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext( NewWorld->WorldType );
	WorldContext.SetCurrentWorld(NewWorld);

	NewWorld->InitializeNewWorld( UWorld::InitializationValues()
										  .AllowAudioPlayback( false )
										  .CreatePhysicsScene( false )
										  .RequiresHitProxies( false )
										  .CreateNavigation( false )
										  .CreateAISystem( false )
										  .ShouldSimulatePhysics( false )
										  .SetTransactional( false )
	);

	// Add the base content as a sublevel
	const UChaosVDEditorSettings* Settings = GetDefault<UChaosVDEditorSettings>();

	ULevelStreamingDynamic* StreamedInLevel = NewObject<ULevelStreamingDynamic>(NewWorld);
	StreamedInLevel->SetWorldAssetByPackageName(FName(Settings->BasePhysicsVDWorld.GetLongPackageName()));

	StreamedInLevel->PackageNameToLoad = FName(Settings->BasePhysicsVDWorld.GetLongPackageName());

	StreamedInLevel->SetShouldBeLoaded(true);
	StreamedInLevel->bShouldBlockOnLoad = true;
	StreamedInLevel->bInitiallyLoaded = true;

	StreamedInLevel->SetShouldBeVisible(true);
	StreamedInLevel->bInitiallyVisible = true;
	StreamedInLevel->bLocked = true;

	NewWorld->AddStreamingLevel(StreamedInLevel);

	NewWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);

	return NewWorld;
}


