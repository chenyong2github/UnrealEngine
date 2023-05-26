// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDScene.h"

#include "ChaosVDEditorSettings.h"
#include "ChaosVDGeometryBuilder.h"
#include "ChaosVDParticleActor.h"
#include "Chaos/ImplicitObject.h"
#include "ChaosVDRecording.h"
#include "EditorActorFolders.h"
#include "WorldPersistentFolders.h"
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
}

void FChaosVDScene::UpdateFromRecordedStepData(const int32 SolverID, const FString& SolverName, const FChaosVDStepData& InRecordedStepData, const FChaosVDSolverFrameData& InFrameData)
{
	FChaosVDParticlesByIDMap& SolverParticlesByID = ParticlesBySolverID.FindChecked(SolverID);

	// Go over existing Particle VD Instances and update them or create them if needed 
	for (const FChaosVDParticleDebugData& Particle : InRecordedStepData.RecordedParticles)
	{
		const int32 ParticleVDInstanceID = GetIDForRecordedParticleData(Particle);

		if (InRecordedStepData.ParticlesDestroyedIDs.Contains(ParticleVDInstanceID))
		{
			// Do not process the particle if it was destroyed in the same step
			continue;
		}

		if (AChaosVDParticleActor** ExistingParticleVDInstancePtrPtr = SolverParticlesByID.Find(ParticleVDInstanceID))
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
				SolverParticlesByID.Add(ParticleVDInstanceID, NewParticleVDInstance);
			}
			else
			{
				//TODO: Handle this error
				ensure(false);
			}
		}
	}

	// TODO: This will not work once the recording has what changes between solver steps
	// So it needs to be update to remove particles based on a Particles Removed event
	int32 AmountRemoved = 0;
	for (FChaosVDParticlesByIDMap::TIterator RemoveIterator = SolverParticlesByID.CreateIterator(); RemoveIterator; ++RemoveIterator)
	{
		if (InRecordedStepData.ParticlesDestroyedIDs.Contains(RemoveIterator.Key()))
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

void FChaosVDScene::HandleNewGeometryData(const TSharedPtr<const Chaos::FImplicitObject>& GeometryData, const uint32 GeometryID) const
{
	NewGeometryAvailableDelegate.Broadcast(GeometryData, GeometryID);
}

void FChaosVDScene::HandleEnterNewGameFrame(int32 FrameNumber, const TArray<int32>& AvailableSolversIds)
{
	// Currently the particle actors from all the solvers are in the same level, and we manage them by keeping track
	// of to which solvers they belong using maps.
	// Using Level instead or a Sub ChaosVDScene could be a better solution
	// I'm intentionally not making that change right now until the "level streaming" solution for the tool is defined
	// As that would impose restriction on how levels could be used. For now the map approach is simpler and will be easier to refactor later on.

	TSet<int32> AvailableSolversSet;
	AvailableSolversSet.Reserve(AvailableSolversIds.Num());

	for (int32 SolverID : AvailableSolversIds)
	{
		AvailableSolversSet.Add(SolverID);
		if (!ParticlesBySolverID.Contains(SolverID))
		{
			ParticlesBySolverID.Add(SolverID);
		}
	}

	int32 AmountRemoved = 0;
	for (TMap<int32, FChaosVDParticlesByIDMap>::TIterator RemoveIterator = ParticlesBySolverID.CreateIterator(); RemoveIterator; ++RemoveIterator)
	{
		if (!AvailableSolversSet.Contains(RemoveIterator.Key()))
		{
			for (const TPair<int32, AChaosVDParticleActor*>& ParticleVDInstanceWithID : RemoveIterator.Value())
			{
				PhysicsVDWorld->DestroyActor(ParticleVDInstanceWithID.Value);
			}

			RemoveIterator.RemoveCurrent();

			AmountRemoved++;
		}
	}

	if (AmountRemoved > 0)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
}

void FChaosVDScene::CleanUpScene()
{
	if (PhysicsVDWorld)
	{
		for (const TPair<int32, FChaosVDParticlesByIDMap>& SolverParticleVDInstanceWithID : ParticlesBySolverID)
		{
			for (const TPair<int32, AChaosVDParticleActor*>& ParticleVDInstanceWithID : SolverParticleVDInstanceWithID.Value)
			{
				PhysicsVDWorld->DestroyActor(ParticleVDInstanceWithID.Value);
			}
		}
	}

	ParticlesBySolverID.Reset();
}

const TSharedPtr<const Chaos::FImplicitObject>* FChaosVDScene::GetUpdatedGeometry(int32 GeometryID) const
{
	if (ensure(LoadedRecording.IsValid()))
	{
		if (const TSharedPtr<const Chaos::FImplicitObject>* Geometry = LoadedRecording->GetGeometryDataMap().Find(GeometryID))
		{
			return Geometry;
		}
	}

	return nullptr;
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
			if (const TSharedPtr<const Chaos::FImplicitObject>* Geometry = LoadedRecording->GetGeometryDataMap().Find(InParticleData.ImplicitObjectHash))
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
