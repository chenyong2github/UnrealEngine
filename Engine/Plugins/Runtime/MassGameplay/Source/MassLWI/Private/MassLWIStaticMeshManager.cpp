// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLWIStaticMeshManager.h"
#include "MassLWISubsystem.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "MassCommonFragments.h"
#include "MassEntitySubsystem.h"
#include "MassRepresentationSubsystem.h"
#include "MassSpawnerSubsystem.h"
#include "MassSpawnerTypes.h"
#include "MassSpawnLocationProcessor.h"
#include "MassVisualizationTrait.h"
#include "VisualLogger/VisualLogger.h"


namespace UE::Mass::Tweakables
{
	bool bDestroyEntitiesOnEndPlay = false;

	FAutoConsoleVariableRef CLWIVars[] = {
		{TEXT("mass.LWI.DestroyEntitiesOnEndPlay"), bDestroyEntitiesOnEndPlay, TEXT("Whether we should destroy LWI-sources entities when the original LWI manager ends play")},
	};
}

//-----------------------------------------------------------------------------
// AMassLWIStaticMeshManager
//-----------------------------------------------------------------------------
void AMassLWIStaticMeshManager::PostLoad()
{
	Super::PostLoad();

	if (UWorld* World = GetWorld())
	{
		if (UMassLWISubsystem* MassLWI = World->GetSubsystem<UMassLWISubsystem>())
		{
			MassLWI->RegisterLWIManager(*this);
		}
	}
}

void AMassLWIStaticMeshManager::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (World != nullptr && IsRegisteredWithMass() == false)
	{
		if (UMassLWISubsystem* LWIxMass = World->GetSubsystem<UMassLWISubsystem>())
		{
			LWIxMass->RegisterLWIManager(*this);
		}
	}
}

void AMassLWIStaticMeshManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (World && IsRegisteredWithMass())
	{
		if (UE::Mass::Tweakables::bDestroyEntitiesOnEndPlay && Entities.Num())
		{
			if (UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(GetWorld()))
			{
				FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

				TArray<FMassArchetypeEntityCollection> EntityCollectionsToDestroy;
				UE::Mass::Utils::CreateEntityCollections(EntityManager, Entities, FMassArchetypeEntityCollection::FoldDuplicates, EntityCollectionsToDestroy);
				for (FMassArchetypeEntityCollection& Collection : EntityCollectionsToDestroy)
				{
					EntityManager.BatchDestroyEntityChunks(Collection);
				}

				Entities.Reset();
			}
		}

		if (UMassLWISubsystem* LWIxMass = World->GetSubsystem<UMassLWISubsystem>())
		{
			LWIxMass->UnregisterLWIManager(*this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AMassLWIStaticMeshManager::TransferDataToMass(FMassEntityManager& EntityManager)
{
	if (InstanceTransforms.Num() == 0)
	{
		return;
	}

	if (MassTemplateID.IsValid() == false && RepresentedClass)
	{
		CreateMassTemplate(EntityManager);
	}

	if (MassTemplateID.IsValid() && InstancedStaticMeshComponent && FinalizedTemplate)
	{
		const FVector& ManagerLocation = GetActorLocation();

		UMassSpawnerSubsystem* SpawnerSystem = UWorld::GetSubsystem<UMassSpawnerSubsystem>(GetWorld());
		check(SpawnerSystem);
		const int32 NumEntities = InstanceTransforms.Num();
		
		if (ManagerLocation.IsNearlyZero())
		{
			// a tiny bit hacky conversion of InstanceTransforms to FMassTransformsSpawnData. Static assert here should catch if the type doesn't match
			static_assert(sizeof(FMassTransformsSpawnData::FTransformsContainerType) == sizeof(InstanceTransforms)
				// @todo use some "TTypeOf" template 
				, "the InstanceTransforms-to-FMassTransformsSpawnData conversion relies on FMassTransformsSpawnData having only a single property that matches InstanceTransforms' type exactly");
			FConstStructView SpawnLocationDataView(FMassTransformsSpawnData::StaticStruct(), reinterpret_cast<const uint8*>(&InstanceTransforms));

			SpawnerSystem->SpawnEntities(FinalizedTemplate->GetTemplateID(), NumEntities, SpawnLocationDataView, UMassSpawnLocationProcessor::StaticClass(), Entities);
		}
		else
		{
			FMassTransformsSpawnData SpawnLocationData;
			SpawnLocationData.Transforms = InstanceTransforms;
			for (FTransform& EntityTransform : SpawnLocationData.Transforms)
			{
				EntityTransform.AddToTranslation(ManagerLocation);
			}

			FConstStructView SpawnLocationDataView(FMassTransformsSpawnData::StaticStruct(), reinterpret_cast<const uint8*>(&SpawnLocationData));
			SpawnerSystem->SpawnEntities(FinalizedTemplate->GetTemplateID(), NumEntities, SpawnLocationDataView, UMassSpawnLocationProcessor::StaticClass(), Entities);
		}

		// destroy actors that have already been created
		for (const TPair<int32, TObjectPtr<AActor>>& ActorPair : Actors)
		{
			if (ActorPair.Value)
			{
				ActorPair.Value->OnDestroyed.RemoveAll(this);
				ActorPair.Value->Destroy();
			}
		}
		Actors.Reset();

		InstanceTransforms.Reset();
		ValidIndices.Reset();
		FreeIndices.Reset();
		
		InstancedStaticMeshComponent->UnregisterComponent();
		InstancedStaticMeshComponent->DestroyComponent();
	}
}

void AMassLWIStaticMeshManager::CreateMassTemplate(FMassEntityManager& EntityManager)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	UMassSpawnerSubsystem* SpawnerSystem = World->GetSubsystem<UMassSpawnerSubsystem>(World);
	if (!ensure(SpawnerSystem))
	{
		//+complain
		return;
	}

	UMassLWISubsystem* LWIxMass = World->GetSubsystem<UMassLWISubsystem>();
	if (!ensure(LWIxMass))
	{
		return;
	}

	const FMassEntityConfig* ClassConfig = LWIxMass->GetConfigForClass(RepresentedClass);
	if (!ClassConfig || ClassConfig->IsEmpty())
	{
		UE_VLOG_UELOG(this, LogMass, Log, TEXT("%s failed to find a calid entity config for %s class. This LWI manager won't transfer data to Mass.")
			, *UObjectBaseUtility::GetName(), *GetNameSafe(RepresentedClass));
		return;
	}

	const UMassVisualizationTrait* VisTrait = Cast<const UMassVisualizationTrait>(ClassConfig->FindTrait(UMassVisualizationTrait::StaticClass()));
	if (!ensureMsgf(VisTrait, TEXT("The config used doesn't contain a VisualizationTrait, which is required for LWIxMass to function")))
	{
		return;
	}
	if (!ensureMsgf(VisTrait->StaticMeshInstanceDesc.Meshes.Num(), TEXT("The VisualizationTrait.StaticMeshInstanceDesc.Meshes being used needs to have at least one entry")))
	{
		return;
	}
	ensureMsgf(VisTrait->StaticMeshInstanceDesc.Meshes.Num() == 1
		, TEXT("It's recommended for the VisualizationTrait.StaticMeshInstanceDesc.Meshes being used to have exactly one entry, since only the first entry will be used"));

	UMassRepresentationSubsystem* RepresentationSubsystem = nullptr;
	
	const FMassEntityTemplate& SourceTemplate = ClassConfig->GetOrCreateEntityTemplate(*World);
	// what we want to do now is to modify the config to point at the specific static mesh and actor class
	FMassEntityTemplateData NewTemplate(SourceTemplate);

		
	const FMassArchetypeSharedFragmentValues& SharedFragmentValues = SourceTemplate.GetSharedFragmentValues();
	const TArray<FSharedStruct>& SharedFragments = SharedFragmentValues.GetSharedFragments();
	for (FSharedStruct SharedFragment : SharedFragments)
	{
		if (FMassRepresentationSubsystemSharedFragment* AsRepresentationSubsystemSharedFragment = SharedFragment.GetPtr<FMassRepresentationSubsystemSharedFragment>())
		{
			RepresentationSubsystem = AsRepresentationSubsystemSharedFragment->RepresentationSubsystem;
			break;
		}
	}
	
	FStaticMeshInstanceVisualizationDesc StaticMeshInstanceDesc = VisTrait->StaticMeshInstanceDesc;
	// @todo make sure a nullptr here won't break stuff
	FMassStaticMeshInstanceVisualizationMeshDesc& MeshDesc = StaticMeshInstanceDesc.Meshes[0];
	MeshDesc.Mesh = StaticMesh.Get();
#if WITH_EDITOR
	if (!ensure(MeshDesc.Mesh) && GIsEditor)
	{
		MeshDesc.Mesh = StaticMesh.LoadSynchronous();
		if (!ensure(MeshDesc.Mesh))
		{
			return;
		}
	}
#endif // WITH_EDITOR

	if (InstancedStaticMeshComponent)
	{
		MeshDesc.MaterialOverrides = InstancedStaticMeshComponent->OverrideMaterials;
	}

	check(MeshDesc.Mesh);
	NewTemplate.SetTemplateName(MeshDesc.Mesh->GetName());

	if (RepresentationSubsystem)
	{
		FMassRepresentationFragment& RepresentationFragment = NewTemplate.AddFragment_GetRef<FMassRepresentationFragment>();
		RepresentationFragment.StaticMeshDescIndex = RepresentationSubsystem->FindOrAddStaticMeshDesc(StaticMeshInstanceDesc);
		const int32 TemplateActorIndex = RepresentationSubsystem->FindOrAddTemplateActor(RepresentedClass);
		RepresentationFragment.HighResTemplateActorIndex = TemplateActorIndex;
		//RepresentationFragment.LowResTemplateActorIndex = TemplateActorIndex;
	}

	FMassEntityTemplateRegistry& TemplateRegistry = SpawnerSystem->GetMutableTemplateRegistryInstance();
	const uint32 FlavorHash = GetTypeHash(GetNameSafe(RepresentedClass));
	MassTemplateID = FMassEntityTemplateIDFactory::MakeFlavor(SourceTemplate.GetTemplateID(), FlavorHash);

	const TSharedRef<FMassEntityTemplate>& NewFinalizedTemplate = TemplateRegistry.FindOrAddTemplate(MassTemplateID, MoveTemp(NewTemplate));
	MassTemplateID = NewFinalizedTemplate->GetTemplateID();
	FinalizedTemplate = NewFinalizedTemplate;

}

void AMassLWIStaticMeshManager::MarkRegisteredWithMass(const FMassLWIManagerRegistrationHandle RegistrationIndex)
{
	check(MassRegistrationHandle.IsValid() == false && RegistrationIndex.IsValid() == true);
	new (&MassRegistrationHandle) FMassLWIManagerRegistrationHandle(RegistrationIndex);
}

void AMassLWIStaticMeshManager::MarkUnregisteredWithMass()
{
	new (&MassRegistrationHandle) FMassLWIManagerRegistrationHandle();
}
