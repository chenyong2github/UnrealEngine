// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDScene.h"

#include "ChaosVDEditorSettings.h"
#include "ChaosVDGeometryBuilder.h"
#include "ChaosVDParticleActor.h"
#include "Chaos/ImplicitObject.h"
#include "ChaosVDRecording.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"

#include "UObject/Package.h"

FChaosVDScene::FChaosVDScene() = default;

FChaosVDScene::~FChaosVDScene() = default;

void FChaosVDScene::Initialize()
{
	if (!ensure(!bIsInitialized))
	{
		return;
	}

	PhysicsVDWorld = CreatePhysicsVDWorld();

	GeometryGenerator = MakeShared<FChaosVDGeometryBuilder>();

	bIsInitialized = true;
}

void FChaosVDScene::DeInitialize()
{
	if (!ensure(bIsInitialized))
	{
		return;
	}

	GeometryGenerator.Reset();

	CleanUpScene();

	if (PhysicsVDWorld)
	{
		PhysicsVDWorld->DestroyWorld(true);
		GEngine->DestroyWorldContext(PhysicsVDWorld);

		PhysicsVDWorld->MarkAsGarbage();
		PhysicsVDWorld = nullptr;
	}
	
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	bIsInitialized = false;
}

void FChaosVDScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PhysicsVDWorld);
	Collector.AddStableReferenceMap(ParticleVDInstancesByID);
}

void FChaosVDScene::UpdateFromRecordedStepData(const int32 SolverID, const FString& SolverName, const FChaosVDStepData& InRecordedStepData, const FChaosVDSolverFrameData& InFrameData)
{
	TSet<int32> ParticlesIDsInRecordedStepData;
	ParticlesIDsInRecordedStepData.Reserve(InRecordedStepData.RecordedParticles.Num());

	// Go over existing Particle VD Instances and update them or create them if needed 
	for (const FChaosVDParticleDebugData& Particle : InRecordedStepData.RecordedParticles)
	{
		const int32 ParticleVDInstanceID = GetIDForRecordedParticleData(Particle);
		ParticlesIDsInRecordedStepData.Add(ParticleVDInstanceID);

		if (AChaosVDParticleActor** ExistingParticleVDInstancePtrPtr = ParticleVDInstancesByID.Find(ParticleVDInstanceID))
		{
			if (AChaosVDParticleActor* ExistingParticleVDInstancePtr = *ExistingParticleVDInstancePtrPtr)
			{
				ExistingParticleVDInstancePtr->UpdateFromRecordedData(Particle, InFrameData.SimulationTransform);
			}
			else
			{
				//TODO: Handle this error
				ensure(false);
			}
		}
		else
		{
			if (AChaosVDParticleActor* NewParticleVDInstance = SpawnParticleFromRecordedData(Particle, InFrameData))
			{
				FStringFormatOrderedArguments Args {SolverName, FString::FromInt(SolverID)};
				const FName FolderPath = *FPaths::Combine(FString::Format(TEXT("Solver {0} | ID {1}"), Args), UEnum::GetDisplayValueAsText(Particle.ParticleType).ToString());

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
	for (TMap<int32, AChaosVDParticleActor*>::TIterator RemoveIterator = ParticleVDInstancesByID.CreateIterator(); RemoveIterator; ++RemoveIterator)
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

void FChaosVDScene::HandleNewGeometryData(const TSharedPtr<const Chaos::FImplicitObject>& GeometryData, const int32 GeometryID) const
{
	NewGeometryAvailableDelegate.Broadcast(GeometryData, GeometryID);
}

void FChaosVDScene::CleanUpScene()
{
	if (PhysicsVDWorld)
	{
		for (const TPair<int32, AChaosVDParticleActor*>& ParticleVDInstanceWithID : ParticleVDInstancesByID)
		{
			PhysicsVDWorld->DestroyActor(ParticleVDInstanceWithID.Value);
		}
	}

	ParticleVDInstancesByID.Reset();
}

AChaosVDParticleActor* FChaosVDScene::SpawnParticleFromRecordedData(const FChaosVDParticleDebugData& InParticleData, const FChaosVDSolverFrameData& InFrameData)
{
	using namespace Chaos;

	FActorSpawnParameters Params;
	Params.Name = *InParticleData.DebugName;
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	if (AChaosVDParticleActor* NewActor = PhysicsVDWorld->SpawnActor<AChaosVDParticleActor>(Params))
	{
		NewActor->UpdateFromRecordedData(InParticleData, InFrameData.SimulationTransform);

		if (!InParticleData.DebugName.IsEmpty())
		{
			NewActor->SetActorLabel(InParticleData.DebugName);
		}

		NewActor->SetScene(AsShared());
		
		if (ensure(LoadedRecording.IsValid()))
		{
			if (const TSharedPtr<const Chaos::FImplicitObject>* Geometry = LoadedRecording->GetGeometryDataMap().Find(InParticleData.ImplicitObjectID))
			{
				NewActor->UpdateGeometryData(*Geometry);
			}
		}

		return NewActor;
	}

	return nullptr;
}

int32 FChaosVDScene::GetIDForRecordedParticleData(const FChaosVDParticleDebugData& InParticleData) const
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
