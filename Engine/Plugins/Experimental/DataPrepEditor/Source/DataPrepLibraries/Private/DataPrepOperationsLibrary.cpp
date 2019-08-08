// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepOperationsLibrary.h"

#include "DataprepCoreUtils.h"

#include "ActorEditorUtils.h"
#include "AssetDeleteModel.h"
#include "AssetDeleteModel.h"
#include "AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Editor.h"
#include "Engine/Light.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "IMeshBuilderModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Layers/ILayers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Math/Vector2D.h"
#include "MeshAttributeArray.h"
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
#include "MeshTypes.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"
#include "TessellationRendering.h"
#include "UObject/SoftObjectPath.h"

DEFINE_LOG_CATEGORY(LogDataprep);

#define LOCTEXT_NAMESPACE "DataprepOperationsLibrary"

extern UNREALED_API UEditorEngine* GEditor;

namespace DataprepOperationsLibraryUtil
{
	TArray<UStaticMesh*> GetSelectedMeshes(const TArray<AActor*>& SelectedActors)
	{
		TSet<UStaticMesh*> SelectedMeshes;

		for (AActor* Actor : SelectedActors)
		{
			if (Actor)
			{
				TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(Actor);
				for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
				{
					SelectedMeshes.Add(StaticMeshComponent->GetStaticMesh());
				}
			}
		}

		return SelectedMeshes.Array();
	}

	TArray<UStaticMesh*> GetSelectedMeshes(const TArray<UObject*>& SelectedObjects)
	{
		TSet<UStaticMesh*> SelectedMeshes;
		TArray<AActor*> SelectedActors;

		// Create LODs but do not commit changes
		for (UObject* Object : SelectedObjects)
		{
			if (Object->IsA(UStaticMesh::StaticClass()))
			{
				SelectedMeshes.Add(Cast<UStaticMesh>(Object));
			}
			else if (Object->IsA(AActor::StaticClass()))
			{
				TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(Cast<AActor>(Object));
				for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
				{
					SelectedMeshes.Add(StaticMeshComponent->GetStaticMesh());
				}
			}
		}

		return SelectedMeshes.Array();
	}

	TArray<UMaterialInterface*> GetUsedMaterials(const TArray<UObject*>& SelectedObjects)
	{
		TSet<UMaterialInterface*> MaterialSet;

		for (UObject* Object : SelectedObjects)
		{
			if (AActor* Actor = Cast< AActor >(Object))
			{
				// Find the materials by iterating over every mesh component.
				TInlineComponentArray<UMeshComponent*> MeshComponents(Actor);
				for (UMeshComponent* MeshComponent : MeshComponents)
				{
					int32 MaterialCount = FMath::Max( MeshComponent->GetNumOverrideMaterials(), MeshComponent->GetNumMaterials() );

					for (int32 Index = 0; Index < MaterialCount; ++Index)
					{
						MaterialSet.Add(MeshComponent->GetMaterial(Index));
					}
				}
			}
			else if (UStaticMesh* StaticMesh = Cast< UStaticMesh >(Object))
			{
				for (int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
				{
					MaterialSet.Add(StaticMesh->GetMaterial(Index));
				}
			}
		}

		return MaterialSet.Array();
	}

	class FScopedStaticMeshEdit final
	{
	public:
		FScopedStaticMeshEdit( UStaticMesh* InStaticMesh )
			: StaticMesh( InStaticMesh )
		{
			BuildSettingsBackup = PreventStaticMeshBuild( StaticMesh );
		}

		FScopedStaticMeshEdit( const FScopedStaticMeshEdit& Other ) = default;
		FScopedStaticMeshEdit( FScopedStaticMeshEdit&& Other ) = default;

		FScopedStaticMeshEdit& operator=( const FScopedStaticMeshEdit& Other ) = default;
		FScopedStaticMeshEdit& operator=( FScopedStaticMeshEdit&& Other ) = default;

		~FScopedStaticMeshEdit()
		{
			RestoreStaticMeshBuild( StaticMesh, MoveTemp( BuildSettingsBackup ) );
		}

	public:
		static TArray< FMeshBuildSettings > PreventStaticMeshBuild( UStaticMesh* StaticMesh )
		{
			if ( !StaticMesh )
			{
				return {};
			}

			TArray< FMeshBuildSettings > BuildSettingsBackup;

			for ( FStaticMeshSourceModel& SourceModel : StaticMesh->GetSourceModels() )
			{
				BuildSettingsBackup.Add( SourceModel.BuildSettings );

				// These were done in the PreBuild step
				SourceModel.BuildSettings.bGenerateLightmapUVs = false;
				SourceModel.BuildSettings.bRecomputeNormals = false;
				SourceModel.BuildSettings.bRecomputeTangents = false;
				SourceModel.BuildSettings.bBuildAdjacencyBuffer = false;
				SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;
			}

			return BuildSettingsBackup;
		}

		static void RestoreStaticMeshBuild( UStaticMesh* StaticMesh, const TArray< FMeshBuildSettings >& BuildSettingsBackup )
		{
			if ( !StaticMesh )
			{
				return;
			}

			// Restore StaticMesh's build settings
			for ( int32 LODIndex = 0; LODIndex < BuildSettingsBackup.Num() ; ++LODIndex )
			{
				// Update only LODs which were cached
				if (StaticMesh->IsSourceModelValid( LODIndex ))
				{
					const FMeshBuildSettings& CachedBuildSettings = BuildSettingsBackup[ LODIndex ];
					FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(LODIndex).BuildSettings;

					// Restore only the properties which were modified
					BuildSettings.bGenerateLightmapUVs = CachedBuildSettings.bGenerateLightmapUVs;
					BuildSettings.bRecomputeNormals = CachedBuildSettings.bRecomputeNormals;
					BuildSettings.bRecomputeTangents = CachedBuildSettings.bRecomputeTangents;
					BuildSettings.bBuildAdjacencyBuffer = CachedBuildSettings.bBuildAdjacencyBuffer;
					BuildSettings.bBuildReversedIndexBuffer = CachedBuildSettings.bBuildReversedIndexBuffer;
				}
			}
		}

	private:
		TArray< FMeshBuildSettings > BuildSettingsBackup;
		UStaticMesh* StaticMesh;
	};

	int32 GetActorDepth(AActor* Actor)
	{
		return Actor ? 1 + GetActorDepth(Actor->GetAttachParentActor()) : 0;
	}

	/** Customized version of UStaticMesh::SetMaterial avoiding the triggering of UStaticMesh::Build and its side-effects */
	void SetMaterial( UStaticMesh* StaticMesh, int32 MaterialIndex, UMaterialInterface* NewMaterial )
	{
		if( StaticMesh->StaticMaterials.IsValidIndex( MaterialIndex ) )
		{
			FStaticMaterial& StaticMaterial = StaticMesh->StaticMaterials[ MaterialIndex ];
			StaticMaterial.MaterialInterface = NewMaterial;
			if( NewMaterial != nullptr )
			{
				if ( StaticMaterial.MaterialSlotName == NAME_None )
				{
					StaticMaterial.MaterialSlotName = NewMaterial->GetFName();
				}

				// Make sure adjacency information fit new material change
				if( RequiresAdjacencyInformation( NewMaterial, nullptr, GWorld->FeatureLevel ) )
				{
					for( FStaticMeshSourceModel& SourceModel : StaticMesh->GetSourceModels() )
					{
						SourceModel.BuildSettings.bBuildAdjacencyBuffer = true;
					}
				}
			}
		}
	}

	// Replacement of UStaticMesh::CacheDerivedData() which performs too much operations for our purpose
	// And displays unwanted progress bar
	// #ueent_todo: Work with Geometry team to find the proper replacement
	void BuildRenderData( UStaticMesh* StaticMesh, IMeshBuilderModule& MeshBuilderModule )
	{
		FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(0);

		FMeshBuildSettings PrevBuildSettings = SourceModel.BuildSettings;
		SourceModel.BuildSettings.bGenerateLightmapUVs = false;
		SourceModel.BuildSettings.bRecomputeNormals = false;
		SourceModel.BuildSettings.bRecomputeTangents = false;
		SourceModel.BuildSettings.bBuildAdjacencyBuffer = false;
		SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;

		FMeshDescription* StaticMeshDescription = SourceModel.MeshDescription.Get();
		check( StaticMeshDescription );

		// Create render data
		StaticMesh->RenderData.Reset( new(FMemory::Malloc(sizeof(FStaticMeshRenderData)))FStaticMeshRenderData() );

		ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
		ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
		check(RunningPlatform);
		const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();

		const FStaticMeshLODGroup& LODGroup = LODSettings.GetLODGroup(StaticMesh->LODGroup);

		if ( !MeshBuilderModule.BuildMesh( *StaticMesh->RenderData, StaticMesh, LODGroup ) )
		{
			UE_LOG(LogDataprep, Error, TEXT("Failed to build static mesh. See previous line(s) for details."));
			return;
		}

		SourceModel.BuildSettings = PrevBuildSettings;
	}
} // ns DataprepOperationsLibraryUtil

void UDataprepOperationsLibrary::SetLods(const TArray<UObject*>& SelectedObjects, const FEditorScriptingMeshReductionOptions& ReductionOptions)
{
	TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

	// Create LODs but do not commit changes
	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			UEditorStaticMeshLibrary::SetLodsWithNotification(StaticMesh, ReductionOptions, false);
		}
	}
}

void UDataprepOperationsLibrary::SetSimpleCollision(const TArray<UObject*>& SelectedObjects, const EScriptingCollisionShapeType ShapeType)
{
	TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

	IMeshBuilderModule& MeshBuilderModule = FModuleManager::LoadModuleChecked< IMeshBuilderModule >( TEXT("MeshBuilder") );

	// Create LODs but do not commit changes
	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			// Remove existing simple collisions
			UEditorStaticMeshLibrary::RemoveCollisionsWithNotification( StaticMesh, false );

			// Check that render data is available if k-DOP type of collision is required
			bool bFreeRenderData = false;
			switch (ShapeType)
			{
				case EScriptingCollisionShapeType::NDOP10_X:
				case EScriptingCollisionShapeType::NDOP10_Y:
				case EScriptingCollisionShapeType::NDOP10_Z:
				case EScriptingCollisionShapeType::NDOP18:
				case EScriptingCollisionShapeType::NDOP26:
				{
					if( StaticMesh->RenderData == nullptr )
					{
						DataprepOperationsLibraryUtil::BuildRenderData( StaticMesh, MeshBuilderModule );
						bFreeRenderData = true;
					}
					break;
				}
				default:
				{
					break;
				}
			}

			UEditorStaticMeshLibrary::AddSimpleCollisionsWithNotification( StaticMesh, ShapeType, false );

			if( bFreeRenderData )
			{
				StaticMesh->RenderData = nullptr;
			}
		}
	}
}

void UDataprepOperationsLibrary::SetConvexDecompositionCollision(const TArray<UObject*>& SelectedObjects, int32 HullCount, int32 MaxHullVerts, int32 HullPrecision)
{
	TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

	// Build complex collision
	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			UEditorStaticMeshLibrary::SetConvexDecompositionCollisionsWithNotification(StaticMesh, HullCount, MaxHullVerts, HullPrecision, false);
		}
	}
}

void UDataprepOperationsLibrary::SetGenerateLightmapUVs( const TArray< UObject* >& Assets, bool bGenerateLightmapUVs )
{
	TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(Assets);

	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			bool bDidChangeSettings = false;

			// 3 is the maximum that lightmass accept
			int32 MinBiggestUVChannel = 3;
			for ( FStaticMeshSourceModel& SourceModel : StaticMesh->GetSourceModels() )
			{
				bDidChangeSettings |= SourceModel.BuildSettings.bGenerateLightmapUVs != bGenerateLightmapUVs;
				SourceModel.BuildSettings.bGenerateLightmapUVs = bGenerateLightmapUVs;
				if( FMeshDescription* MeshDescription = SourceModel.MeshDescription.Get() )
				{
					int32 UVChannelCount = MeshDescription->VertexInstanceAttributes().GetAttributesRef< FVector2D >( MeshAttribute::VertexInstance::TextureCoordinate ).GetNumIndices();
					MinBiggestUVChannel = FMath::Min( MinBiggestUVChannel, UVChannelCount - 1 );
				}
			}

			if ( StaticMesh->LightMapCoordinateIndex > MinBiggestUVChannel && bDidChangeSettings )
			{
				// Correct the coordinate index if it was invalid
				StaticMesh->LightMapCoordinateIndex = MinBiggestUVChannel;
			}
		}
	}
}

void UDataprepOperationsLibrary::SubstituteMaterial(const TArray<UObject*>& SelectedObjects, const FString& MaterialSearch, EEditorScriptingStringMatchType StringMatch, UMaterialInterface* MaterialSubstitute)
{
	TArray<UMaterialInterface*> MaterialsUsed = DataprepOperationsLibraryUtil::GetUsedMaterials(SelectedObjects);

	SubstituteMaterial(SelectedObjects, MaterialSearch, StringMatch, MaterialsUsed, MaterialSubstitute);
}

void UDataprepOperationsLibrary::SubstituteMaterialsByTable(const TArray<UObject*>& SelectedObjects, const UDataTable* DataTable)
{
	if (DataTable == nullptr || DataTable->GetRowStruct() == nullptr || !DataTable->GetRowStruct()->IsChildOf(FMaterialSubstitutionDataTable::StaticStruct()))
	{
		return;
	}

	TArray<UMaterialInterface*> MaterialsUsed = DataprepOperationsLibraryUtil::GetUsedMaterials(SelectedObjects);

	const TMap<FName, uint8*>&  MaterialTableRowMap = DataTable->GetRowMap();
	for (auto& MaterialTableRowEntry : MaterialTableRowMap)
	{
		const FMaterialSubstitutionDataTable* MaterialRow = (const FMaterialSubstitutionDataTable*)MaterialTableRowEntry.Value;
		if (MaterialRow != nullptr && MaterialRow->MaterialReplacement != nullptr)
		{
			SubstituteMaterial(SelectedObjects, MaterialRow->SearchString, MaterialRow->StringMatch, MaterialsUsed, MaterialRow->MaterialReplacement);
		}
	}
}

void UDataprepOperationsLibrary::SubstituteMaterial(const TArray<UObject*>& SelectedObjects, const FString& MaterialSearch, EEditorScriptingStringMatchType StringMatch, const TArray<UMaterialInterface*>& MaterialList, UMaterialInterface* MaterialSubstitute)
{
	TArray<UObject*> MatchingObjects = UEditorFilterLibrary::ByIDName(TArray<UObject*>(MaterialList), MaterialSearch, StringMatch, EEditorScriptingFilterType::Include);

	TArray<UMaterialInterface*> MaterialsToReplace;
	for (UObject* Object : MatchingObjects)
	{
		if (UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Object))
		{
			MaterialsToReplace.Add(MaterialInterface);
		}
	}

	for (UMaterialInterface* MaterialToReplace : MaterialsToReplace)
	{
		for (UObject* Object : SelectedObjects)
		{
			if (AActor* Actor = Cast< AActor >(Object))
			{
				// Find the materials by iterating over every mesh component.
				TInlineComponentArray<UMeshComponent*> MeshComponents(Actor);
				for (UMeshComponent* MeshComponent : MeshComponents)
				{
					int32 MaterialCount = FMath::Max( MeshComponent->GetNumOverrideMaterials(), MeshComponent->GetNumMaterials() );

					for (int32 Index = 0; Index < MaterialCount; ++Index)
					{
						if (MeshComponent->GetMaterial(Index) == MaterialToReplace)
						{
							MeshComponent->SetMaterial(Index, MaterialSubstitute);
						}
					}
				}
			}
			else if (UStaticMesh* StaticMesh = Cast< UStaticMesh >(Object))
			{
				DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

				TArray<FStaticMaterial>& StaticMaterials = StaticMesh->StaticMaterials;
				for (int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
				{
					if (StaticMesh->GetMaterial(Index) == MaterialToReplace)
					{
						DataprepOperationsLibraryUtil::SetMaterial( StaticMesh, Index, MaterialSubstitute );
					}
				}
			}
		}
	}
}

void UDataprepOperationsLibrary::SetMobility( const TArray< UObject* >& SelectedObjects, EComponentMobility::Type MobilityType )
{
	for (UObject* Object : SelectedObjects)
	{
		if (AActor* Actor = Cast< AActor >(Object))
		{
			// Find the materials by iterating over every mesh component.
			TInlineComponentArray<USceneComponent*> SceneComponents(Actor);
			for (USceneComponent* SceneComponent : SceneComponents)
			{
				SceneComponent->SetMobility(MobilityType);
			}
		}
	}
}

void UDataprepOperationsLibrary::SetMaterial( const TArray< UObject* >& SelectedObjects, UMaterialInterface* MaterialSubstitute )
{
	for (UObject* Object : SelectedObjects)
	{
		if (AActor* Actor = Cast< AActor >(Object))
		{
			// Find the materials by iterating over every mesh component.
			TInlineComponentArray<UMeshComponent*> MeshComponents(Actor);
			for (UMeshComponent* MeshComponent : MeshComponents)
			{
				int32 MaterialCount = FMath::Max( MeshComponent->GetNumOverrideMaterials(), MeshComponent->GetNumMaterials() );

				for (int32 Index = 0; Index < MaterialCount; ++Index)
				{
					MeshComponent->SetMaterial(Index, MaterialSubstitute);
				}
			}
		}
		else if (UStaticMesh* StaticMesh = Cast< UStaticMesh >(Object))
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			for (int32 Index = 0; Index < StaticMesh->StaticMaterials.Num(); ++Index)
			{
				DataprepOperationsLibraryUtil::SetMaterial( StaticMesh, Index, MaterialSubstitute );
			}
		}
	}
}

void UDataprepOperationsLibrary::SetLODGroup( const TArray<UObject*>& SelectedObjects, FName& LODGroupName )
{
	TArray<FName> LODGroupNames;
	UStaticMesh::GetLODGroups( LODGroupNames );

	if ( LODGroupNames.Find( LODGroupName ) != INDEX_NONE )
	{
		TArray<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

		// Apply the new LODGroup without rebuilding the static mesh
		for (UStaticMesh* StaticMesh : SelectedMeshes)
		{
			StaticMesh->SetLODGroup( LODGroupName, false);
		}
	}
}

void UDataprepOperationsLibrary::RemoveObjects(const TArray< UObject* >& Objects)
{
	// Implementation based on DatasmithImporterImpl::DeleteActorsMissingFromScene, UEditorLevelLibrary::DestroyActor
	struct FActorAndDepth
	{
		AActor* Actor;
		int32 Depth;
	};

	TArray<FActorAndDepth> ActorsToDelete;
	ActorsToDelete.Reserve(Objects.Num());

	TArray<UObject*> AssetsToDelete;
	AssetsToDelete.Reserve(Objects.Num());

	for (UObject* Object : Objects)
	{
		if ( !ensure(Object) || Object->IsPendingKill() )
		{
			continue;
		}

		if (AActor* Actor = Cast< AActor >( Object ))
		{
			ActorsToDelete.Add(FActorAndDepth{Actor, DataprepOperationsLibraryUtil::GetActorDepth(Actor)});
			// #ueent_todo if rem children option, add them here
		}
		else
		{
			AssetsToDelete.Add(Object);
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
			// skip component with invalid or condemned owner
			AActor* Owner = ChildComponent->GetOwner();
			if ( Owner == nullptr || Owner == Actor || Owner->IsPendingKill() || Objects.Contains(Owner))
			{
				continue;
			}

			ChildComponent->AttachToComponent( AttachParent, FAttachmentTransformRules::KeepWorldTransform );
		}

		// Actual deletion of the actor
		{
			Actor->Rename();
			if (Actor->IsSelected())
			{
				GEditor->SelectActor(Actor, false, false);
				bSelectionAffected = true;
			}

			if (GEditor->Layers)
			{
				GEditor->Layers->DisassociateActorFromLayers(Actor);
			}

			if (UWorld* World = Actor->GetWorld())
			{
				World->EditorDestroyActor(Actor, true);
			}
		}
	}

	if (bSelectionAffected)
	{
		GEditor->NoteSelectionChange();
	}

	FDataprepCoreUtils::PurgeObjects( MoveTemp( AssetsToDelete ) );

}

#undef LOCTEXT_NAMESPACE
