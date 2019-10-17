// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepEditingOperations.h"

#include "DataprepCoreUtils.h"
#include "DataPrepOperationsLibrary.h"
#include "IDataprepProgressReporter.h"

#include "ActorEditorUtils.h"
#include "AssetRegistryModule.h"
#include "Async/ParallelFor.h"
#include "Editor.h"
#include "EditorLevelLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "GenericPlatform/GenericPlatformTime.h"
#include "IMeshBuilderModule.h"
#include "IMeshMergeUtilities.h"
#include "Layers/ILayers.h"
#include "LevelSequence.h"
#include "Materials/MaterialInstance.h"
#include "MeshMergeModule.h"
#include "StaticMeshResources.h"

#define LOCTEXT_NAMESPACE "DatasmithEditingOperations"

#ifdef LOG_TIME
namespace DataprepEditingOperationTime
{
	typedef TFunction<void(FText)> FLogFunc;

	class FTimeLogger
	{
	public:
		FTimeLogger(const FString& InText, FLogFunc&& InLogFunc)
			: StartTime( FPlatformTime::Cycles64() )
			, Text( InText )
			, LogFunc(MoveTemp(InLogFunc))
		{
			UE_LOG( LogDataprep, Log, TEXT("%s ..."), *Text );
		}

		~FTimeLogger()
		{
			// Log time spent to import incoming file in minutes and seconds
			double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

			int ElapsedMin = int(ElapsedSeconds / 60.0);
			ElapsedSeconds -= 60.0 * (double)ElapsedMin;
			FText Msg = FText::Format( LOCTEXT("DataprepOperation_LogTime", "{0} took {1} min {2} s."), FText::FromString( Text ), ElapsedMin, FText::FromString( FString::Printf( TEXT("%.3f"), ElapsedSeconds ) ) );
			LogFunc( Msg );
		}

	private:
		uint64 StartTime;
		FString Text;
		FLogFunc LogFunc;
	};
}
#endif

namespace DatasmithEditingOperationsUtils
{
	void FindActorsToMerge(const TArray<AActor*>& ChildrenActors, TArray<AActor*>& ActorsToMerge);
	void FindActorsToCollapseOrDelete(const TArray<AActor*>& ActorsToVisit, TArray<AActor*>& ActorsToCollapse, TArray<UObject*>& ActorsToDelete );
	void GetRootActors(UWorld* World, TArray<AActor*>& RootActors);

	int32 GetActorDepth(AActor* Actor)
	{
		return Actor ? 1 + GetActorDepth(Actor->GetAttachParentActor()) : 0;
	}

	class FMergingData
	{
	public:
		FMergingData(const TArray<UPrimitiveComponent*>& PrimitiveComponents);

		bool Equals(const FMergingData& Other);

		TMap< FString, TArray< FTransform > > Data;
	};
}

void UDataprepRemoveObjectsOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
#ifdef LOG_TIME
	DataprepEditingOperationTime::FTimeLogger TimeLogger( TEXT("RemoveObjects"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	// Implementation based on DatasmithImporterImpl::DeleteActorsMissingFromScene, UEditorLevelLibrary::DestroyActor
	struct FActorAndDepth
	{
		AActor* Actor;
		int32 Depth;
	};

	TArray<FActorAndDepth> ActorsToDelete;
	ActorsToDelete.Reserve(InContext.Objects.Num());

	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Reserve(InContext.Objects.Num());

	for (UObject* Object : InContext.Objects)
	{
		if ( !ensure(Object) || Object->IsPendingKill() )
		{
			continue;
		}

		if (AActor* Actor = Cast< AActor >( Object ))
		{
			ActorsToDelete.Add(FActorAndDepth{Actor, DatasmithEditingOperationsUtils::GetActorDepth(Actor)});
			// #ueent_todo if rem children option, add them here
		}
		else if(FDataprepCoreUtils::IsAsset(Object))
		{
			ObjectsToDelete.Add(Object);
		}
	}

	// Sort actors by decreasing depth (in order to delete children first)
	ActorsToDelete.Sort([](const FActorAndDepth& Lhs, const FActorAndDepth& Rhs){ return Lhs.Depth > Rhs.Depth; });

	bool bSelectionAffected = false;
	for (const FActorAndDepth& ActorInfo : ActorsToDelete)
	{
		AActor* Actor = ActorInfo.Actor;

		// Reattach our children to our parent
		TArray< USceneComponent* > AttachChildren = Actor->GetRootComponent()->GetAttachChildren(); // Make a copy because the array in RootComponent will get modified during the process
		USceneComponent* AttachParent = Actor->GetRootComponent()->GetAttachParent();

		for ( USceneComponent* ChildComponent : AttachChildren )
		{
			if(ChildComponent)
			{
				// skip component with invalid or condemned owner
				AActor* Owner = ChildComponent->GetOwner();
				if ( Owner == nullptr || Owner == Actor || Owner->IsPendingKill() || InContext.Objects.Contains(Owner) /* Slow!!! */)
				{
					continue;
				}

				ChildComponent->AttachToComponent( AttachParent, FAttachmentTransformRules::KeepWorldTransform );
			}
		}

		ObjectsToDelete.Add( Actor );
	}

	DeleteObjects( ObjectsToDelete );
}

void UDataprepMergeActorsOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	TArray<AStaticMeshActor*> ActorsToMerge;
	TArray<UPrimitiveComponent*> ComponentsToMerge;
	UWorld* CurrentWorld = nullptr;

	for(UObject* Object : InContext.Objects)
	{
		if( AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Object) )
		{
			if(!MeshActor->IsPendingKillOrUnreachable())
			{
				if(CurrentWorld == nullptr)
				{
					CurrentWorld = MeshActor->GetWorld();
				}

				if(CurrentWorld != MeshActor->GetWorld())
				{
					// #ueent_todo: Warn that incompatible actor found and discarded
					continue;
				}

				TInlineComponentArray<UStaticMeshComponent*> ComponentArray;
				MeshActor->GetComponents<UStaticMeshComponent>(ComponentArray);

				bool bMeshActorIsValid = false;
				for (UStaticMeshComponent* MeshComponent : ComponentArray)
				{
					if (MeshComponent->GetStaticMesh()/* && MeshComponent->GetStaticMesh()->RenderData.IsValid()*/)
					{
						bMeshActorIsValid = true;
						ComponentsToMerge.Add(MeshComponent);
					}
				}

				//Actor needs at least one StaticMeshComponent to be considered valid
				if (bMeshActorIsValid)
				{
					ActorsToMerge.Add( MeshActor );
				}

			}
		}
	}

	// Nothing to do if there is none or only one static mesh actor
	if( ActorsToMerge.Num() < 2 && ComponentsToMerge.Num() < 2)
	{
		UE_LOG( LogDataprep, Log, TEXT("No static mesh actors to merge") );
		return;
	}

#ifdef LOG_TIME
	DataprepEditingOperationTime::FTimeLogger TimeLogger( TEXT("MergeActors"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	if(!MergeStaticMeshActors(CurrentWorld, ComponentsToMerge, NewActorLabel.IsEmpty() ? TEXT("Merged") : *NewActorLabel ))
	{
		return;
	}

	// Position the merged actor at the right location
	if(MergedActor->GetRootComponent() == nullptr)
	{
		USceneComponent* RootComponent = NewObject< USceneComponent >( MergedActor, USceneComponent::StaticClass(), *MergedActor->GetActorLabel(), RF_Transactional );

		MergedActor->AddInstanceComponent( RootComponent );
		MergedActor->SetRootComponent( RootComponent );
	}

	MergedActor->GetRootComponent()->SetWorldLocation( MergedMeshWorldLocation );

	// Collect all objects to be deleted
	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Reserve( ComponentsToMerge.Num() + ActorsToMerge.Num() );

	if(bDeleteMergedMeshes)
	{
		TSet<UObject*> StaticMeshes;
		StaticMeshes.Reserve( ComponentsToMerge.Num() );

		for(UPrimitiveComponent* PrimitiveComponent : ComponentsToMerge)
		{
			if(UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(PrimitiveComponent)->GetStaticMesh())
			{
				StaticMeshes.Add(StaticMesh);
				ObjectsToDelete.Add( StaticMesh );
				Cast<UStaticMeshComponent>(PrimitiveComponent)->SetStaticMesh( nullptr );
			}
		}
	}

	if(bDeleteMergedActors)
	{
		for(AStaticMeshActor* MeshActor : ActorsToMerge)
		{
			ObjectsToDelete.Add( MeshActor );
		}
	}

	DeleteObjects( ObjectsToDelete );
}

bool UDataprepMergeActorsOperation::MergeStaticMeshActors(UWorld* World, const TArray<UPrimitiveComponent*>& ComponentsToMerge, const FString& RootName, bool bCreateActor)
{
	//
	// See MeshMergingTool.cpp
	//
	const IMeshMergeUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();

	TArray<UObject*> CreatedAssets;
	const float ScreenAreaSize = TNumericLimits<float>::Max();
	MeshUtilities.MergeComponentsToStaticMesh( ComponentsToMerge, World, MergeSettings, nullptr, GetTransientPackage(), FString(), CreatedAssets, MergedMeshWorldLocation, ScreenAreaSize, true);

	UStaticMesh* UtilitiesMergedMesh = nullptr;
	if (!CreatedAssets.FindItemByClass(&UtilitiesMergedMesh))
	{
		UE_LOG(LogDataprep, Error, TEXT("MergeStaticMeshActors failed. No mesh was created."));
		return false;
	}

	// Add asset to set of assets in Dataprep action working set
	MergedMesh = Cast<UStaticMesh>( AddAsset( UtilitiesMergedMesh, UStaticMesh::StaticClass(), NewActorLabel.IsEmpty() ? TEXT("Merged_Mesh") : *NewActorLabel ) );
	if (!MergedMesh)
	{
		UE_LOG(LogDataprep, Error, TEXT("MergeStaticMeshActors failed. Internal error while creating the merged mesh."));
		return false;
	}

	if(bCreateActor == true)
	{
		// Place new mesh in the world
		MergedActor = Cast<AStaticMeshActor>( CreateActor( AStaticMeshActor::StaticClass(), NewActorLabel.IsEmpty() ? TEXT("Merged_Actor") : *NewActorLabel ) );
		if (!MergedActor)
		{
			UE_LOG(LogDataprep, Error, TEXT("MergeStaticMeshActors failed. Internal error while creating the merged actor."));
			return false;
		}

		MergedActor->GetStaticMeshComponent()->SetStaticMesh(MergedMesh);
		MergedActor->SetActorLabel(NewActorLabel);
		World->UpdateCullDistanceVolumes(MergedActor, MergedActor->GetStaticMeshComponent());
	}

	return true;
}

void UDataprepMergeActorsOperation::PrepareStaticMeshes(TSet<UStaticMesh*> StaticMeshes, IMeshBuilderModule& MeshBuilderModule)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UDataprepMergeActorsOperation::PrepareStaticMeshes);

	if( StaticMeshes.Num() > 1 )
	{
		TArray<UStaticMesh*> StaticMeshesToBuild = StaticMeshes.Array();
		ParallelFor( StaticMeshesToBuild.Num(), [&]( int32 Index ) {
			DataprepOperationsLibraryUtil::BuildRenderData( StaticMeshesToBuild[Index], MeshBuilderModule );
		});
	}
	else
	{
		for(UStaticMesh* StaticMesh : StaticMeshes)
		{
			DataprepOperationsLibraryUtil::BuildRenderData( StaticMesh, MeshBuilderModule );
		}
	}
}

void UDataprepSmartMergeOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	UWorld* World = nullptr;

	for(UObject* Object : InContext.Objects)
	{
		if(AActor* Actor = Cast<AActor>(Object))
		{
			World = Actor->GetWorld();
			break;
		}
	}

	if(World == nullptr/* || World == GWorld*/)
	{
		return;
	}

	// Get root actors
	TArray<AActor*> RootActors;
	DatasmithEditingOperationsUtils::GetRootActors(World, RootActors);

	TArray<AActor*> Actors;
	DatasmithEditingOperationsUtils::FindActorsToMerge(RootActors, Actors);

#ifdef LOG_TIME
	DataprepEditingOperationTime::FTimeLogger TimeLogger( TEXT("SmartMerge"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	SmartMerge( World, Actors );
}

void UDataprepSmartMergeOperation::SmartMerge(UWorld* World, const TArray<AActor*>& Actors)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UDataprepSmartMergeOperation::SmartMerge);

	TSharedPtr<FDataprepWorkReporter> Task = CreateTask( NSLOCTEXT( "SmartMergeOperation", "RunMerge", "Executing operation ..." ), 100.f, 1.0f );

	Task->ReportNextStep( NSLOCTEXT( "SmartMergeOperation", "FindingActors", "Analyzing scene \"{0}\" ..."), 10.f );

	// Group actor to merge by number of valid components
	typedef TPair< AActor*, TArray<UPrimitiveComponent*> > FMergeableActor;
	TMap< int32, TArray< FMergeableActor > > MergeableActorsMap;
	for(AActor* Actor : Actors)
	{
		TArray<AActor*> ChildActors;
		Actor->GetAttachedActors( ChildActors );

		TArray< UPrimitiveComponent* > MeshComponents;
		for(AActor* ChildActor : ChildActors)
		{
			TArray<UPrimitiveComponent*> Components;
			ChildActor->GetComponents<UPrimitiveComponent>( Components );

			for(UPrimitiveComponent* Component : Components)
			{
				// #ueent_todo: Support all primitive components
				if(UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
				{
					if(StaticMeshComponent->GetStaticMesh() && StaticMeshComponent->GetStaticMesh()->GetNumSourceModels())
					{
						MeshComponents.Add( StaticMeshComponent );
					}
				}
			}
		}

		TArray< FMergeableActor >& MergeableActors = MergeableActorsMap.FindOrAdd( MeshComponents.Num() );

		MergeableActors.Emplace( Actor, MeshComponents );
	}

	Task->ReportNextStep( NSLOCTEXT( "SmartMergeOperation", "GroupingActors", "Grouping actors ..."), 10.f );

	TArray< TArray< FMergeableActor > > MergeableActorsSet;
	for(auto& MergeableActorsEntry : MergeableActorsMap)
	{
		TArray< FMergeableActor >& GroupedMergeableActors = MergeableActorsEntry.Value;
		int32 ProcessedCount = 0;
		TArray<bool> ProcessedEntries;
		ProcessedEntries.AddZeroed( GroupedMergeableActors.Num() );

		while(ProcessedCount < GroupedMergeableActors.Num())
		{
			for(int32 Index = 0; Index < ProcessedEntries.Num(); ++Index)
			{
				if(ProcessedEntries[Index] == false)
				{
					ProcessedEntries[Index] = true;
					DatasmithEditingOperationsUtils::FMergingData ReferenceMergingData( GroupedMergeableActors[Index].Value );
					ProcessedCount++;

					TArray< FMergeableActor > MergeableActors;
					MergeableActors.Add( GroupedMergeableActors[Index] );

					for(int32 SubIndex = 0; SubIndex < ProcessedEntries.Num(); ++SubIndex)
					{
						if(ProcessedEntries[SubIndex] == false)
						{
							if( ReferenceMergingData.Equals( DatasmithEditingOperationsUtils::FMergingData( GroupedMergeableActors[SubIndex].Value ) ) )
							{
								MergeableActors.Add( GroupedMergeableActors[SubIndex] );
								ProcessedEntries[SubIndex] = true;
								ProcessedCount++;
							}
						}
					}

					MergeableActorsSet.Add( MergeableActors );
				}
			}
		}
	}

	TArray< UObject* > ObjectsToDelete;

	// Build render data for static meshes to be merged
	IMeshBuilderModule& MeshBuilderModule = FModuleManager::LoadModuleChecked< IMeshBuilderModule >( TEXT("MeshBuilder") );
	TSet<UStaticMesh*> StaticMeshes;

	Task->ReportNextStep( NSLOCTEXT( "SmartMergeOperation", "BuildingMeshes", "Analyzing meshes ..."), 20.f );

	for( auto& MergeableActors : MergeableActorsSet )
	{
		TArray<UPrimitiveComponent*>& PrimitiveComponents = MergeableActors[0].Value;

		if( PrimitiveComponents.Num() > 1 )
		{
			for(UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if( UStaticMeshComponent* StaticMeshComponent = Cast< UStaticMeshComponent >( PrimitiveComponent ) )
				{
					if( StaticMeshComponent->GetStaticMesh()->RenderData == nullptr )
					{
						StaticMeshes.Add( StaticMeshComponent->GetStaticMesh() );
					}
					ObjectsToDelete.Add( StaticMeshComponent->GetStaticMesh() );
				}
			}
		}
	}

	PrepareStaticMeshes( StaticMeshes, MeshBuilderModule );

	Task->ReportNextStep( NSLOCTEXT( "SmartMergeOperation", "MergingActors", "Merging actors ..."), 60.f );
	{
		TSharedPtr<FDataprepWorkReporter> SubTask = CreateTask( NSLOCTEXT( "SmartMergeOperation", "MergingActors", "Merging actors ..."), MergeableActorsSet.Num(), 1.0f );

		for( auto& MergeableActors : MergeableActorsSet )
		{
			SubTask->ReportNextStep( NSLOCTEXT( "SmartMergeOperation", "MergingActor", "Merging actor ...") );

			TArray<UPrimitiveComponent*>& PrimitiveComponents = MergeableActors[0].Value;
			if( PrimitiveComponents.Num() > 1 )
			{
				MergeStaticMeshActors( World, PrimitiveComponents, TEXT("SmartMerge"), false );

				if(MergedMesh == nullptr)
				{
					continue;
				}

				// IMeshUtilities::MergeComponentsToStaticMesh seems to bake the rotation (maybe scaling) of the parent actor in the merged mesh.
				// To compensate for this, the inverse rotation and the inverse scaling are computed and will be applied to the world transform of
				// the mesh actors which will be created
				// #ueent_todo: Investigate the reason why this must be done
				FTransform BaseComponentToWorld = MergeableActors[0].Key->GetRootComponent()->GetComponentToWorld();
				FQuat InvBaseRotation = BaseComponentToWorld.GetRotation().Inverse();
				FVector InvBaseScale = FVector::OneVector / BaseComponentToWorld.GetScale3D();

				for( FMergeableActor& MergeableActor : MergeableActors )
				{
					AActor* Actor = MergeableActor.Key;

					FActorSpawnParameters Params;
					Params.OverrideLevel = Actor->GetLevel();

					// Place new mesh in the world
					MergedActor = Cast<AStaticMeshActor>( CreateActor( AStaticMeshActor::StaticClass(), TEXT("SmartMergeActor") ) );
					if(MergedActor != nullptr)
					{
						USceneComponent* ParentComponent = Actor->GetRootComponent()->GetAttachParent();
						FString ActorName = Actor->GetName();
						FName NewName = MakeUniqueObjectName( Actor->GetOuter(), Actor->GetClass() );
						FDataprepCoreUtils::RenameObject( Actor, *NewName.ToString() );

						MergedActor->GetStaticMeshComponent()->SetStaticMesh( MergedMesh );
						FDataprepCoreUtils::RenameObject( MergedActor, *ActorName );
						MergedActor->SetActorLabel( Actor->GetActorLabel() );

						FTransform ComponentToWorld = Actor->GetRootComponent()->GetComponentToWorld();
						ComponentToWorld.SetTranslation( ComponentToWorld.GetTranslation() /*+ MergedActorLocation*/ );
						ComponentToWorld.SetRotation( ComponentToWorld.GetRotation() * InvBaseRotation );
						ComponentToWorld.SetScale3D( ComponentToWorld.GetScale3D() * InvBaseScale );
						MergedActor->GetRootComponent()->SetComponentToWorld( ComponentToWorld );

						World->UpdateCullDistanceVolumes( MergedActor, MergedActor->GetStaticMeshComponent() );

						if(ParentComponent)
						{
							MergedActor->GetRootComponent()->AttachToComponent( ParentComponent, FAttachmentTransformRules::KeepWorldTransform );
						}

						TArray<AActor*> ActorsToDelete;
						Actor->GetAttachedActors( ActorsToDelete );
						ActorsToDelete.Add( Actor );

						ObjectsToDelete.Append( ActorsToDelete );
					}
					else
					{
						UE_LOG(LogDataprep, Error, TEXT("MergeStaticMeshActors failed. Internal error while creating the merged actor."));
					}

					TArray<AActor*> ActorsToDelete;
					Actor->GetAttachedActors( ActorsToDelete );
					ActorsToDelete.Add( Actor );

					ObjectsToDelete.Append( ActorsToDelete );
				}
			}
		}
	}

	// Release render data of built static meshes
	for(UStaticMesh* StaticMesh : StaticMeshes)
	{
		if(StaticMesh)
		{
			StaticMesh->RenderData.Reset();
		}
	}

	DeleteObjects(ObjectsToDelete);
}

void UDataprepCleanWorldOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	UWorld* World = nullptr;

	TSet<UObject*> UsedAssets;
	UsedAssets.Reserve(InContext.Objects.Num());

	auto CollectAssets = [&UsedAssets](UMaterialInterface* MaterialInterface )
	{
		UsedAssets.Add(MaterialInterface);
		if(UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface))
		{
			if(MaterialInstance->Parent)
			{
				UsedAssets.Add(MaterialInstance->Parent);
			}
		}

		for(const FMaterialTextureInfo& TextureInfo : MaterialInterface->GetTextureStreamingData())
		{
			if(UObject* Texture = TextureInfo.TextureReference.ResolveObject())
			{
				UsedAssets.Add(Texture);
			}
		}
	};

#ifdef LOG_TIME
	DataprepEditingOperationTime::FTimeLogger TimeLogger( TEXT("CleanWorld"), [&]( FText Text) { this->LogInfo( Text ); });
#endif

	for (UObject* Object : InContext.Objects)
	{
		if ( !ensure(Object) || Object->IsPendingKill() )
		{
			continue;
		}

		if (AActor* Actor = Cast< AActor >( Object ))
		{
			World = Actor->GetWorld();

			TArray<UActorComponent*> Components = Actor->GetComponents().Array();
			Components.Append( Actor->GetInstanceComponents() );

			for(UActorComponent* Component : Components)
			{
				if(UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component))
				{
					if(UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh())
					{
						UsedAssets.Add(StaticMesh);

						for(FStaticMaterial& StaticMaterial : StaticMesh->StaticMaterials)
						{
							if(UMaterialInterface* MaterialInterface = StaticMaterial.MaterialInterface)
							{
								CollectAssets(MaterialInterface);
							}
						}
					}

					for(UMaterialInterface* MaterialInterface : MeshComponent->OverrideMaterials)
					{
						if(MaterialInterface)
						{
							CollectAssets(MaterialInterface);
						}
					}
				}
			}
		}
		else if(ULevelSequence* LevelSequence = Cast<ULevelSequence>(Object))
		{
			UsedAssets.Add(LevelSequence);
		}
	}

	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Reserve(InContext.Objects.Num());

	if(World != nullptr)
	{
		// Get root actors
		TArray<AActor*> RootActors;
		DatasmithEditingOperationsUtils::GetRootActors(World, RootActors);

		TArray<AActor*> ActorsToCollapse;
		DatasmithEditingOperationsUtils::FindActorsToCollapseOrDelete(RootActors, ActorsToCollapse, ObjectsToDelete);

		for(AActor* Actor : ActorsToCollapse)
		{
			USceneComponent* RootComponent = Actor->GetRootComponent();

			TArray< USceneComponent* > AttachChildren = RootComponent->GetAttachChildren(); // Make a copy because the array in RootComponent will get modified during the process
			USceneComponent* AttachParent = RootComponent->GetAttachParent();

			for ( USceneComponent* ChildComponent : AttachChildren )
			{
				if(ChildComponent)
				{
					// skip component with invalid or condemned owner
					AActor* Owner = ChildComponent->GetOwner();
					if ( Owner == nullptr || Owner == Actor || Owner->IsPendingKill() || InContext.Objects.Contains(Owner) /* Slow!!! */)
					{
						continue;
					}

					ChildComponent->AttachToComponent( AttachParent, FAttachmentTransformRules::KeepWorldTransform );
				}
			}

			// Remove actor from world and add to array for deletion
			{
				World->RemoveActor(Actor, true);
				FDataprepCoreUtils::MoveToTransientPackage( Actor );

				ObjectsToDelete.Add( Actor );
			}
		}
	}

	for(UObject* Object : InContext.Objects)
	{
		if(FDataprepCoreUtils::IsAsset(Object) && !UsedAssets.Contains(Object))
		{
			ObjectsToDelete.Add(Object);
		}
	}

	DeleteObjects( ObjectsToDelete );
}

namespace DatasmithEditingOperationsUtils
{
	void FindActorsToMerge(const TArray<AActor*>& ChildrenActors, TArray<AActor*>& ActorsToMerge)
	{
		for(AActor* ChildActor : ChildrenActors)
		{
			TArray<AActor*> ActorsToVisit;
			ChildActor->GetAttachedActors(ActorsToVisit);

			bool bCouldBeMerged = ActorsToVisit.Num() > 0;
			for(AActor* ActorToVisit : ActorsToVisit)
			{
				TArray<AActor*> Children;
				ActorToVisit->GetAttachedActors(Children);

				if(Children.Num() > 0)
				{
					bCouldBeMerged = false;
					break;
				}

				// Check if we can find a static mesh component
				UStaticMeshComponent* Component = ActorToVisit->FindComponentByClass<UStaticMeshComponent>();
				if(Component == nullptr)
				{
					bCouldBeMerged = false;
					break;
				}
			}

			if(bCouldBeMerged)
			{
				ActorsToMerge.Add(ChildActor);
				continue;
			}

			FindActorsToMerge(ActorsToVisit, ActorsToMerge);
		}
	}

	void FindActorsToCollapseOrDelete(const TArray<AActor*>& ActorsToVisit, TArray<AActor*>& ActorsToCollapse, TArray<UObject*>& ActorsToDelete )
	{
		for(AActor* Actor : ActorsToVisit)
		{
			if(Actor->GetClass() == AActor::StaticClass())
			{
				TArray<AActor*> AttachedActors;
				Actor->GetAttachedActors(AttachedActors);

				if(AttachedActors.Num() == 0)
				{
					ActorsToDelete.Add( Actor );
					continue;
				}
				else if(AttachedActors.Num() == 1)
				{
					AActor* ChildActor = AttachedActors[0];

					TArray<AActor*> AttachedChildActors;
					ChildActor->GetAttachedActors(AttachedChildActors);

					if(AttachedChildActors.Num() == 0)
					{
						ActorsToCollapse.Add(Actor);
						continue;
					}
				}

				FindActorsToCollapseOrDelete(AttachedActors, ActorsToCollapse, ActorsToDelete);
			}
			else
			{
				TArray<AActor*> AttachedActors;
				Actor->GetAttachedActors(AttachedActors);
				FindActorsToCollapseOrDelete(AttachedActors, ActorsToCollapse, ActorsToDelete);
			}
		}
	}

	void GetRootActors(UWorld * World, TArray<AActor*>& OutRootActors)
	{
		for(ULevel* Level : World->GetLevels())
		{
			for(AActor* Actor : Level->Actors)
			{
				const bool bIsValidRootActor = Actor &&
					!Actor->IsPendingKill() &&
					Actor->IsEditable() &&
					!Actor->IsTemplate() &&
					!FActorEditorUtils::IsABuilderBrush(Actor) &&
					!Actor->IsA(AWorldSettings::StaticClass()) &&
					Actor->GetParentActor() == nullptr &&
					Actor->GetRootComponent() != nullptr &&
					Actor->GetRootComponent()->GetAttachParent() == nullptr;

				if(bIsValidRootActor )
				{
					OutRootActors.Add( Actor );
				}
			}
		}
	}

	FMergingData::FMergingData(const TArray<UPrimitiveComponent*>& PrimitiveComponents)
	{
		Data.Reserve(PrimitiveComponents.Num());

		for(UPrimitiveComponent* PrimitveComponent : PrimitiveComponents)
		{
			if(UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitveComponent))
			{
				FSoftObjectPath SoftObjectPath(StaticMeshComponent->GetStaticMesh());

				TArray< FTransform >& Transforms = Data.FindOrAdd(SoftObjectPath.ToString());
				Transforms.Add(PrimitveComponent->GetRelativeTransform());
			}
		}
	}

	bool FMergingData::Equals(const FMergingData& Other)
	{
		for(auto& OtherEntry : Other.Data)
		{
			TArray< FTransform >* Transforms = Data.Find(OtherEntry.Key);
			if(Transforms == nullptr)
			{
				return false;
			}

			TArray<bool> TransformMatched;
			TransformMatched.AddZeroed(Transforms->Num());
			for(const FTransform& OtherTransform : OtherEntry.Value)
			{
				int32 FoundTransformIndex = -1;
				for(int32 Index = 0; Index < Transforms->Num(); ++Index)
				{
					if(TransformMatched[Index] == false && (*Transforms)[Index].Equals(OtherTransform))
					{
						TransformMatched[Index] = true;
						FoundTransformIndex = Index;
						break;
					}
				}

				if(FoundTransformIndex == -1)
				{
					return false;
				}
			}
		}

		return true;
	}
}
#undef LOCTEXT_NAMESPACE
