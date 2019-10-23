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
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Math/Vector2D.h"
#include "MeshDescriptionOperations.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"
#include "StaticMeshAttributes.h"
#include "TessellationRendering.h"
#include "UObject/SoftObjectPath.h"

DEFINE_LOG_CATEGORY(LogDataprep);

#define LOCTEXT_NAMESPACE "DataprepOperationsLibrary"

extern UNREALED_API UEditorEngine* GEditor;

namespace DataprepOperationsLibraryUtil
{
	TSet<UStaticMesh*> GetSelectedMeshes(const TArray<AActor*>& SelectedActors)
	{
		TSet<UStaticMesh*> SelectedMeshes;

		for (AActor* Actor : SelectedActors)
		{
			if (Actor)
			{
				TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(Actor);
				for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
				{
					if(UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
					{
						SelectedMeshes.Add( StaticMesh );
					}
				}
			}
		}

		return SelectedMeshes;
	}

	TSet<UStaticMesh*> GetSelectedMeshes(const TArray<UObject*>& SelectedObjects)
	{
		TSet<UStaticMesh*> SelectedMeshes;
		TArray<AActor*> SelectedActors;

		for (UObject* Object : SelectedObjects)
		{
			if ( UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object) )
			{
				SelectedMeshes.Add( StaticMesh );
			}
			else if ( Object->IsA(UStaticMeshComponent::StaticClass()) )
			{
				if ((StaticMesh = Cast<UStaticMeshComponent>(Object)->GetStaticMesh()) != nullptr )
				{
					SelectedMeshes.Add(StaticMesh);
				}
			}
			else if (AActor* Actor = Cast<AActor>(Object) )
			{
				TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents( Actor );
				for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
				{
					if((StaticMesh = StaticMeshComponent->GetStaticMesh()) != nullptr)
					{
						SelectedMeshes.Add( StaticMesh );
					}
				}
			}
		}

		return SelectedMeshes;
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

	TArray<UStaticMesh*> GetUsedMeshes(const TArray<UObject*>& SelectedObjects)
	{
		TSet<UStaticMesh*> MeshesSet;

		for (UObject* Object : SelectedObjects)
		{
			if (AActor* Actor = Cast< AActor >(Object))
			{
				// Find the meshes by iterating over every mesh component.
				TInlineComponentArray<UStaticMeshComponent*> MeshComponents(Actor);
				for (UStaticMeshComponent* MeshComponent : MeshComponents)
				{
					if(MeshComponent && MeshComponent->GetStaticMesh())
					{
						MeshesSet.Add( MeshComponent->GetStaticMesh() );
					}
				}
			}
		}

		return MeshesSet.Array();
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
				SourceModel.BuildSettings.bComputeWeightedNormals = false;
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
					BuildSettings.bComputeWeightedNormals = CachedBuildSettings.bComputeWeightedNormals;
				}
			}
		}

	private:
		TArray< FMeshBuildSettings > BuildSettingsBackup;
		UStaticMesh* StaticMesh;
	};

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

	FStaticMeshBuilder::FStaticMeshBuilder(const TSet<UStaticMesh *>& InStaticMeshes)
	{
		StaticMeshes = BuildStaticMeshes( InStaticMeshes );
	}

	FStaticMeshBuilder::~FStaticMeshBuilder()
	{
		// Release render data of built static meshes
		for(UStaticMesh* StaticMesh : StaticMeshes)
		{
			if(StaticMesh)
			{
				StaticMesh->RenderData.Reset();
			}
		}
	}

	TArray<UStaticMesh*> BuildStaticMeshes(const TSet<UStaticMesh*>& StaticMeshes, bool bForceBuild)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DataprepOperationsLibraryUtil::BuildStaticMeshes);

		TArray<UStaticMesh*> BuiltMeshes;
		BuiltMeshes.Reserve( StaticMeshes.Num() );

		if(bForceBuild)
		{
			BuiltMeshes.Append( StaticMeshes.Array() );
		}
		else
		{
			for(UStaticMesh* StaticMesh : StaticMeshes)
			{
				if(StaticMesh && (!StaticMesh->RenderData.IsValid() || !StaticMesh->RenderData->IsInitialized()))
				{
					BuiltMeshes.Add( StaticMesh );
				}
			}
		}

		if(BuiltMeshes.Num() > 0)
		{
			// Start with the biggest mesh first to help balancing tasks on threads
			BuiltMeshes.Sort(
				[](const UStaticMesh& Lhs, const UStaticMesh& Rhs) 
			{ 
				int32 LhsVerticesNum = Lhs.IsMeshDescriptionValid(0) ? Lhs.GetMeshDescription(0)->Vertices().Num() : 0;
				int32 RhsVerticesNum = Rhs.IsMeshDescriptionValid(0) ? Rhs.GetMeshDescription(0)->Vertices().Num() : 0;

				return LhsVerticesNum > RhsVerticesNum;
			}
			);

			//Cache the BuildSettings and update them before building the meshes.
			TArray< TArray<FMeshBuildSettings> > StaticMeshesSettings;
			StaticMeshesSettings.Reserve( BuiltMeshes.Num() );

			for (UStaticMesh* StaticMesh : BuiltMeshes)
			{
				TArray<FStaticMeshSourceModel>& SourceModels = StaticMesh->GetSourceModels();
				TArray<FMeshBuildSettings> BuildSettings;
				BuildSettings.Reserve(SourceModels.Num());

				for(int32 Index = 0; Index < SourceModels.Num(); ++Index)
				{
					FStaticMeshSourceModel& SourceModel = SourceModels[Index];

					BuildSettings.Add( SourceModel.BuildSettings );

					if(FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(Index))
					{
						FStaticMeshAttributes Attributes(*MeshDescription);
						if(SourceModel.BuildSettings.DstLightmapIndex != -1)
						{
							TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
							SourceModel.BuildSettings.bGenerateLightmapUVs = VertexInstanceUVs.IsValid() && VertexInstanceUVs.GetNumIndices() > SourceModel.BuildSettings.DstLightmapIndex;
						}
						else
						{
							SourceModel.BuildSettings.bGenerateLightmapUVs = false;
						}

						SourceModel.BuildSettings.bRecomputeNormals = !(Attributes.GetVertexInstanceNormals().IsValid() && Attributes.GetVertexInstanceNormals().GetNumIndices() > 0);
						SourceModel.BuildSettings.bRecomputeTangents = false;
						//SourceModel.BuildSettings.bBuildAdjacencyBuffer = false;
						//SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;
					}
				}

				StaticMeshesSettings.Add(MoveTemp(BuildSettings));				
			}

			// Disable warnings from LogStaticMesh. Not useful
			ELogVerbosity::Type PrevLogStaticMeshVerbosity = LogStaticMesh.GetVerbosity();
			LogStaticMesh.SetVerbosity( ELogVerbosity::Error );

			UStaticMesh::BatchBuild(BuiltMeshes, true );

			// Restore LogStaticMesh verbosity
			LogStaticMesh.SetVerbosity( PrevLogStaticMeshVerbosity );

			for(int32 Index = 0; Index < BuiltMeshes.Num(); ++Index)
			{
				UStaticMesh* StaticMesh = BuiltMeshes[Index];
				TArray<FMeshBuildSettings>& PrevBuildSettings = StaticMeshesSettings[Index];

				TArray<FStaticMeshSourceModel>& SourceModels = StaticMesh->GetSourceModels();

				for(int32 SourceModelIndex = 0; SourceModelIndex < SourceModels.Num(); ++SourceModelIndex)
				{
					SourceModels[SourceModelIndex].BuildSettings = PrevBuildSettings[SourceModelIndex];
				}

				for ( FStaticMeshLODResources& LODResources : StaticMesh->RenderData->LODResources )
				{
					LODResources.bHasColorVertexData = true;
				}
			}
		}

		return BuiltMeshes;
	}
} // ns DataprepOperationsLibraryUtil

void UDataprepOperationsLibrary::SetLods(const TArray<UObject*>& SelectedObjects, const FEditorScriptingMeshReductionOptions& ReductionOptions, TArray<UObject*>& ModifiedObjects)
{
	TSet<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

	// Create LODs but do not commit changes
	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			UEditorStaticMeshLibrary::SetLodsWithNotification(StaticMesh, ReductionOptions, false);

			ModifiedObjects.Add( StaticMesh );
		}
	}
}

void UDataprepOperationsLibrary::SetSimpleCollision(const TArray<UObject*>& SelectedObjects, const EScriptingCollisionShapeType ShapeType, TArray<UObject*>& ModifiedObjects)
{
	TSet<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

	// Make sure all static meshes to be processed have render data for NDOP types
	bool bNeedRenderData = false;
	switch (ShapeType)
	{
		case EScriptingCollisionShapeType::NDOP10_X:
		case EScriptingCollisionShapeType::NDOP10_Y:
		case EScriptingCollisionShapeType::NDOP10_Z:
		case EScriptingCollisionShapeType::NDOP18:
		case EScriptingCollisionShapeType::NDOP26:
		{
			bNeedRenderData = true;
			break;
		}
		default:
		{
			break;
		}
	}

	DataprepOperationsLibraryUtil::FStaticMeshBuilder StaticMeshBuilder( bNeedRenderData ? SelectedMeshes : TSet<UStaticMesh*>() );

	// Create LODs but do not commit changes
	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			// Remove existing simple collisions
			UEditorStaticMeshLibrary::RemoveCollisionsWithNotification( StaticMesh, false );

			UEditorStaticMeshLibrary::AddSimpleCollisionsWithNotification( StaticMesh, ShapeType, false );

			ModifiedObjects.Add( StaticMesh );
		}
	}
}

void UDataprepOperationsLibrary::SetConvexDecompositionCollision(const TArray<UObject*>& SelectedObjects, int32 HullCount, int32 MaxHullVerts, int32 HullPrecision, TArray<UObject*>& ModifiedObjects)
{
	TSet<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

	// Make sure all static meshes to be processed have render data
	DataprepOperationsLibraryUtil::FStaticMeshBuilder StaticMeshBuilder(SelectedMeshes);

	// Build complex collision
	for (UStaticMesh* StaticMesh : SelectedMeshes)
	{
		if (StaticMesh)
		{
			DataprepOperationsLibraryUtil::FScopedStaticMeshEdit StaticMeshEdit( StaticMesh );

			UEditorStaticMeshLibrary::SetConvexDecompositionCollisionsWithNotification(StaticMesh, HullCount, MaxHullVerts, HullPrecision, false);

			ModifiedObjects.Add( StaticMesh );
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

void UDataprepOperationsLibrary::SetLODGroup( const TArray<UObject*>& SelectedObjects, FName& LODGroupName, TArray<UObject*>& ModifiedObjects )
{
	TArray<FName> LODGroupNames;
	UStaticMesh::GetLODGroups( LODGroupNames );

	if ( LODGroupNames.Find( LODGroupName ) != INDEX_NONE )
	{
		TSet<UStaticMesh*> SelectedMeshes = DataprepOperationsLibraryUtil::GetSelectedMeshes(SelectedObjects);

		// Apply the new LODGroup without rebuilding the static mesh
		for (UStaticMesh* StaticMesh : SelectedMeshes)
		{
			if(StaticMesh)
			{
				StaticMesh->SetLODGroup( LODGroupName, false);
				ModifiedObjects.Add( StaticMesh );
			}
		}
	}
}

void UDataprepOperationsLibrary::SetMesh(const TArray<UObject*>& SelectedObjects, UStaticMesh* MeshSubstitute)
{
	for (UObject* Object : SelectedObjects)
	{
		if (AActor* Actor = Cast< AActor >(Object))
		{
			// Find the meshes by iterating over every mesh component.
			TInlineComponentArray<UStaticMeshComponent*> MeshComponents(Actor);
			for (UStaticMeshComponent* MeshComponent : MeshComponents)
			{
				if(MeshComponent)
				{
					MeshComponent->SetStaticMesh( MeshSubstitute );
				}
			}
		}
	}
}

void UDataprepOperationsLibrary::SubstituteMesh(const TArray<UObject*>& SelectedObjects, const FString& MeshSearch, EEditorScriptingStringMatchType StringMatch, UStaticMesh* MeshSubstitute)
{
	TArray<UStaticMesh*> MeshesUsed = DataprepOperationsLibraryUtil::GetUsedMeshes(SelectedObjects);

	SubstituteMesh( SelectedObjects, MeshSearch, StringMatch, MeshesUsed, MeshSubstitute );
}

void UDataprepOperationsLibrary::SubstituteMeshesByTable(const TArray<UObject*>& SelectedObjects, const UDataTable * DataTable)
{
	if (DataTable == nullptr || DataTable->GetRowStruct() == nullptr || !DataTable->GetRowStruct()->IsChildOf(FMeshSubstitutionDataTable::StaticStruct()))
	{
		return;
	}

	TArray<UStaticMesh*> MeshesUsed = DataprepOperationsLibraryUtil::GetUsedMeshes(SelectedObjects);

	const TMap<FName, uint8*>&  MeshTableRowMap = DataTable->GetRowMap();
	for (auto& MeshTableRowEntry : MeshTableRowMap)
	{
		const FMeshSubstitutionDataTable* MeshRow = (const FMeshSubstitutionDataTable*)MeshTableRowEntry.Value;
		if (MeshRow != nullptr && MeshRow->MeshReplacement != nullptr)
		{
			SubstituteMesh( SelectedObjects, MeshRow->SearchString, MeshRow->StringMatch, MeshesUsed, MeshRow->MeshReplacement );
		}
	}
}

void UDataprepOperationsLibrary::SubstituteMesh(const TArray<UObject*>& SelectedObjects, const FString& MeshSearch, EEditorScriptingStringMatchType StringMatch, const TArray<UStaticMesh*>& MeshList, UStaticMesh* MeshSubstitute)
{
	TArray<UObject*> MatchingObjects = UEditorFilterLibrary::ByIDName(TArray<UObject*>(MeshList), MeshSearch, StringMatch, EEditorScriptingFilterType::Include);

	TSet<UStaticMesh*> MeshesToReplace;
	for (UObject* Object : MatchingObjects)
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object))
		{
			MeshesToReplace.Add(StaticMesh);
		}
	}

	for (UObject* Object : SelectedObjects)
	{
		if (AActor* Actor = Cast< AActor >(Object))
		{
			// Find the meshes by iterating over every mesh component.
			TInlineComponentArray<UStaticMeshComponent*> MeshComponents(Actor);
			for (UStaticMeshComponent* MeshComponent : MeshComponents)
			{
				if( MeshesToReplace.Contains( MeshComponent->GetStaticMesh() ) )
				{
					MeshComponent->SetStaticMesh( MeshSubstitute );
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
