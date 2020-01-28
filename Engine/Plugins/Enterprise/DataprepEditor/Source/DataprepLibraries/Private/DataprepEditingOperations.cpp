// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepEditingOperations.h"

#include "DataprepCoreUtils.h"
#include "DataprepOperationsLibrary.h"
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
#include "LevelSequence.h"
#include "Materials/MaterialInstance.h"
#include "MeshMergeModule.h"
#include "ObjectTools.h"
#include "StaticMeshAttributes.h"
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
	void GetActorsToMerge(UWorld*& World, const TArray<UObject*>& InObjects, TArray<AActor*>& ActorsToMerge, TArray<UPrimitiveComponent*>& ComponentsToMerge);

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

void UDataprepDeleteObjectsOperation::OnExecution_Implementation(const FDataprepContext& InContext)
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
	TArray<AActor*> ActorsToMerge;
	TArray<UPrimitiveComponent*> ComponentsToMerge;
	UWorld* CurrentWorld = nullptr;

	DatasmithEditingOperationsUtils::GetActorsToMerge(CurrentWorld, InContext.Objects, ActorsToMerge, ComponentsToMerge);

	// Nothing to do if there is only one component to merge
	if( ComponentsToMerge.Num() < 2)
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

	// Keep the merged actor in the hierarchy, taking the parent of the first component
	// In the future, the merged actor could be attached to the common ancestor instead of the first parent in the list
	MergedActor->GetRootComponent()->AttachToComponent( ComponentsToMerge[0]->GetAttachParent(), FAttachmentTransformRules::KeepWorldTransform );

	// Collect all objects to be deleted
	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Reserve( ComponentsToMerge.Num() + ActorsToMerge.Num() );

	// Simple way to delete the actors: detach the merged components if it's safe to do so
	for (UPrimitiveComponent* Component : ComponentsToMerge)
	{
		if (Component->GetNumChildrenComponents() == 0)
		{
			Component->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		}
		else if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
		{
			// To keep the children in the hierarchy, simply hide the StaticMesh, but the StaticMeshComponent could be replaced by a SceneComponent and
			// then attach the children to it
			StaticMeshComponent->SetStaticMesh(nullptr);
		}
	}

	// Then delete the merged actors that don't have any children component
	for(AActor* Actor : ActorsToMerge)
	{
		UPrimitiveComponent* RootComponent = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		if (RootComponent && RootComponent->GetNumChildrenComponents() == 0)
		{
			ObjectsToDelete.Add(Actor);
		}
	}

	DeleteObjects( ObjectsToDelete );
}

bool UDataprepMergeActorsOperation::MergeStaticMeshActors(UWorld* World, const TArray<UPrimitiveComponent*>& ComponentsToMerge, const FString& RootName, bool bCreateActor)
{
	TSet<UStaticMesh*> StaticMeshes;
	for(UPrimitiveComponent* PrimitiveComponent : ComponentsToMerge)
	{
		if(UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
		{
			if(StaticMeshComponent->GetStaticMesh()->RenderData == nullptr)
			{
				StaticMeshes.Add(StaticMeshComponent->GetStaticMesh());
			}
		}
	}

	DataprepOperationsLibraryUtil::FStaticMeshBuilder StaticMeshBuilder( StaticMeshes );

	//
	// See MeshMergingTool.cpp
	//
	const IMeshMergeUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();

	FMeshMergingSettings MergeSettings;
	MergeSettings.bPivotPointAtZero = bPivotPointAtZero;

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
	MergedMesh = Cast<UStaticMesh>( AddAsset( UtilitiesMergedMesh, NewActorLabel.IsEmpty() ? TEXT("Merged_Mesh") : *NewActorLabel ) );
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

void UDataprepCreateProxyMeshOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	TArray<AActor*> ActorsToMerge;
	TArray<UPrimitiveComponent*> ComponentsToMerge;
	UWorld* CurrentWorld = nullptr;

	DatasmithEditingOperationsUtils::GetActorsToMerge(CurrentWorld, InContext.Objects, ActorsToMerge, ComponentsToMerge);

	// Nothing to do if there is no static mesh components to merge
	if(ComponentsToMerge.Num() == 0)
	{
		UE_LOG(LogDataprep, Log, TEXT("No static mesh to merge"));
		return;
	}

	// Validate render data for static meshes
	TSet<UStaticMesh*> StaticMeshes;
	for(UPrimitiveComponent* PrimitiveComponent : ComponentsToMerge)
	{
		if(UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
		{
			if(StaticMeshComponent->GetStaticMesh()->RenderData == nullptr)
			{
				StaticMeshes.Add(StaticMeshComponent->GetStaticMesh());
			}
		}
	}

	DataprepOperationsLibraryUtil::FStaticMeshBuilder StaticMeshBuilder( StaticMeshes );

	// Update the settings for geometry
	FMeshProxySettings ProxySettings;
	ProxySettings.bOverrideVoxelSize = false;
	
	const float Coefficient = 2.0f * Quality / 100.0f;

	const float MinScreenSize = Coefficient <= 1.0f ? 100.0f : 300.0f;
	const float MaxScreenSize = Coefficient <= 1.0f ? 300.0f : 1200.0f;
	ProxySettings.ScreenSize = FMath::FloorToInt( FMath::Lerp( MinScreenSize, MaxScreenSize, Coefficient <= 1.0f ? Coefficient : Coefficient - 1.0f ) + 0.5f );

	// Determine if incoming lightmap UVs are usable
	ProxySettings.bReuseMeshLightmapUVs = true;
	StaticMeshes.Empty( ComponentsToMerge.Num() );
	for(UPrimitiveComponent* PrimitiveComponent : ComponentsToMerge)
	{
		if(UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
		{
			if(StaticMeshComponent->GetStaticMesh())
			{
				StaticMeshes.Add( StaticMeshComponent->GetStaticMesh() );
			}
		}
	}

	for(UStaticMesh* StaticMesh : StaticMeshes)
	{
		const FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;
		if(!BuildSettings.bGenerateLightmapUVs)
		{
			ProxySettings.bReuseMeshLightmapUVs = false;
			break;
		}
		else if(FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription( 0 ))
		{
			FStaticMeshAttributes Attributes(*MeshDescription);
			bool bHasValidLightmapUVs = Attributes.GetVertexInstanceUVs().IsValid() &&
				Attributes.GetVertexInstanceUVs().GetNumIndices() > BuildSettings.SrcLightmapIndex &&
				Attributes.GetVertexInstanceUVs().GetNumIndices() > BuildSettings.DstLightmapIndex;
			if(!bHasValidLightmapUVs)
			{
				ProxySettings.bReuseMeshLightmapUVs = false;
				break;
			}
		}
	}

	// Update the settings for materials
	ProxySettings.MaterialSettings.bMetallicMap = true;
	ProxySettings.MaterialSettings.bRoughnessMap = true;

	const int32 TextureSize = (Coefficient <= 0.5f) ? 512 : ((Coefficient <= 1.0f) ? 1024 : ((Coefficient <= 1.5f) ? 2048 : 4096 ));
	ProxySettings.MaterialSettings.TextureSize = FIntPoint( TextureSize, TextureSize );

	const TCHAR* ProxyBasePackageName = TEXT("TOREPLACE");

	// Generate proxy mesh and proxy material assets 
	FCreateProxyDelegate ProxyDelegate;
	ProxyDelegate.BindLambda( [&](const FGuid Guid, TArray<UObject*>& AssetsToSync )
	{
		UStaticMesh* ProxyMesh = nullptr;
		if(!AssetsToSync.FindItemByClass(&ProxyMesh))
		{
			UE_LOG(LogDataprep, Error, TEXT("CreateProxyMesh failed. No mesh was created."));
			return;
		}

		// Add asset to set of assets in Dataprep action working set
		MergedMesh = Cast<UStaticMesh>(AddAsset(ProxyMesh, NewActorLabel.IsEmpty() ? TEXT("Proxy_Mesh") : *NewActorLabel));
		if(!MergedMesh)
		{
			UE_LOG(LogDataprep, Error, TEXT("CreateProxyMesh failed. Internal error while creating the merged mesh."));
			return;
		}

		// Place new mesh in the world (on a new actor)
		MergedActor = Cast<AStaticMeshActor>(CreateActor(AStaticMeshActor::StaticClass(), NewActorLabel.IsEmpty() ? TEXT("Proxy_Actor") : *NewActorLabel));
		if(!MergedActor)
		{
			UE_LOG(LogDataprep, Error, TEXT("CreateProxyMesh failed. Internal error while creating the merged actor."));
			return;
		}

		MergedActor->GetStaticMeshComponent()->SetStaticMesh(MergedMesh);
		MergedActor->SetActorLabel(NewActorLabel.IsEmpty() ? TEXT("Proxy_Actor") : *NewActorLabel);
		CurrentWorld->UpdateCullDistanceVolumes(MergedActor, MergedActor->GetStaticMeshComponent());

		// Add the other assets created by the merge, i.e. material, texture, etc, to the context
		TArray< TPair< UObject*, UObject* > > RedirectionMap;
		RedirectionMap.Reserve( AssetsToSync.Num() );

		for(UObject* Object : AssetsToSync)
		{
			if( Cast<UStaticMesh>(Object) != ProxyMesh)
			{
				const FString AssetName = Object->GetName().Replace( ProxyBasePackageName, NewActorLabel.IsEmpty() ? *GetDisplayOperationName().ToString() : *NewActorLabel, ESearchCase::CaseSensitive );
				UObject* AssetFromMerge = AddAsset( Object, *AssetName );

				RedirectionMap.Emplace( AssetFromMerge, Object );
			}
		}

		// Update references accordingly
		for(TPair< UObject*, UObject* >& MapEntry : RedirectionMap)
		{
			TArray<UObject*> ObjectsToReplace(&MapEntry.Value, 1);
			ObjectTools::ForceReplaceReferences( MapEntry.Key, ObjectsToReplace );
		}
	});

	FGuid JobGuid = FGuid::NewGuid();

	const IMeshMergeUtilities& MergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
	MergeUtilities.CreateProxyMesh(ActorsToMerge, ProxySettings, nullptr, GetTransientPackage(), ProxyBasePackageName, JobGuid, ProxyDelegate);

	// Position the merged actor at the right location
	if(MergedActor->GetRootComponent() == nullptr)
	{
		USceneComponent* RootComponent = NewObject< USceneComponent >(MergedActor, USceneComponent::StaticClass(), *MergedActor->GetActorLabel(), RF_Transactional);

		MergedActor->AddInstanceComponent(RootComponent);
		MergedActor->SetRootComponent(RootComponent);
	}

	// Keep the merged actor in the hierarchy, taking the parent of the first component
	// In the future, the merged actor could be attached to the common ancestor instead of the first parent in the list
	MergedActor->GetRootComponent()->AttachToComponent(ComponentsToMerge[0]->GetAttachParent(), FAttachmentTransformRules::KeepWorldTransform);

	// Collect all objects to be deleted
	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Reserve(ComponentsToMerge.Num() + ActorsToMerge.Num());

	// Simple way to delete the actors: detach the merged components if it's safe to do so
	for(UPrimitiveComponent* Component : ComponentsToMerge)
	{
		if(Component->GetNumChildrenComponents() == 0)
		{
			Component->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		}
		else if(UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
		{
			// To keep the children in the hierarchy, simply hide the StaticMesh, but the StaticMeshComponent could be replaced by a SceneComponent and
			// then attach the children to it
			StaticMeshComponent->SetStaticMesh(nullptr);
		}
	}

	// Then delete the merged actors that don't have any children component
	for(AActor* Actor : ActorsToMerge)
	{
		USceneComponent* RootComponent = Cast<USceneComponent>(Actor->GetRootComponent());
		if(RootComponent && RootComponent->GetNumChildrenComponents() == 0)
		{
			ObjectsToDelete.Add(Actor);
		}
	}

	DeleteObjects(ObjectsToDelete);
}

void UDataprepDeleteUnusedAssetsOperation::OnExecution_Implementation(const FDataprepContext& InContext)
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

		TArray<UTexture*> Textures;
		MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::Num, true, ERHIFeatureLevel::Num, true);
		for (UTexture* Texture : Textures)
		{
			UsedAssets.Add(Texture);
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

	for(UObject* Object : InContext.Objects)
	{
		if(FDataprepCoreUtils::IsAsset(Object) && !UsedAssets.Contains(Object))
		{
			ObjectsToDelete.Add(Object);
		}
	}

	DeleteObjects( ObjectsToDelete );
}

void UDataprepCompactSceneGraphOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
#ifdef LOG_TIME
	DataprepEditingOperationTime::FTimeLogger TimeLogger(TEXT("CompactSceneGraph"), [&](FText Text) { this->LogInfo(Text); });
#endif

	TMap<AActor*, bool> VisibilityMap;
	for (UObject* Object : InContext.Objects)
	{
		if (!ensure(Object) || Object->IsPendingKill())
		{
			continue;
		}

		if (AActor* Actor = Cast<AActor>(Object))
		{
			IsActorVisible(Actor, VisibilityMap);
		}
	}

	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Reserve(InContext.Objects.Num());

	for (const TPair<AActor*, bool>& ActorVisiblity : VisibilityMap)
	{
		if (!ActorVisiblity.Value)
		{
			ObjectsToDelete.Add(ActorVisiblity.Key);
		}
	}

	DeleteObjects(ObjectsToDelete);
}

bool UDataprepCompactSceneGraphOperation::IsActorVisible(AActor* Actor, TMap<AActor*, bool>& VisibilityMap)
{
	if (!Actor)
	{
		return false;
	}

	// For scene compaction, actor visibility is defined as the actor having a MeshComponent (PrimitiveComponent could also be used)
	// or an attached child that is visible
	bool* bIsVisible = VisibilityMap.Find(Actor);
	if (bIsVisible)
	{
		return *bIsVisible;
	}

	TArray<UActorComponent*> Components = Actor->GetComponents().Array();
	for (UActorComponent* Component : Components)
	{
		if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(Component))
		{
			VisibilityMap.Add(Actor, true);
			return true;
		}
	}

	TArray<AActor*> AttachedActors;
	Actor->GetAttachedActors(AttachedActors);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (IsActorVisible(AttachedActor, VisibilityMap))
		{
			VisibilityMap.Add(Actor, true);
			return true;
		}
	}

	VisibilityMap.Add(Actor, false);
	return false;
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

	void GetActorsToMerge(UWorld*& World, const TArray<UObject*>& InObjects, TArray<AActor*>& ActorsToMerge, TArray<UPrimitiveComponent*>& ComponentsToMerge)
	{
		World = nullptr;

		for(UObject* Object : InObjects)
		{
			if(AActor* Actor = Cast<AActor>(Object))
			{
				if(!Actor->IsPendingKillOrUnreachable())
				{
					// Set current world to first world encountered
					if(World == nullptr)
					{
						World = Actor->GetWorld();
					}

					if(World != Actor->GetWorld())
					{
						UE_LOG( LogDataprep, Log, TEXT("Actor %s is not part of the Dataprep transient world ..."), *Actor->GetActorLabel() );
						continue;
					}

					TInlineComponentArray<UStaticMeshComponent*> ComponentArray;
					Actor->GetComponents<UStaticMeshComponent>(ComponentArray);

					bool bMeshActorIsValid = false;
					for(UStaticMeshComponent* MeshComponent : ComponentArray)
					{
						// Skip components which are either editor only or for visualization
						if(!MeshComponent->IsEditorOnly() && !MeshComponent->IsVisualizationComponent())
						{
							if(MeshComponent->GetStaticMesh() && MeshComponent->GetStaticMesh()->GetSourceModels().Num() > 0)
							{
								bMeshActorIsValid = true;
								ComponentsToMerge.Add(MeshComponent);
							}
						}
					}

					//Actor needs at least one StaticMeshComponent to be considered valid
					if(bMeshActorIsValid)
					{
						ActorsToMerge.Add(Actor);
					}
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
