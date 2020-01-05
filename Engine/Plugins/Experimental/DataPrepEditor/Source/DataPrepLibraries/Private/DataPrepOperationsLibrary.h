// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorFilterLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/EngineTypes.h"
#include "EditorStaticMeshLibrary.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "DataPrepOperationsLibrary.generated.h"

class AActor;
class IMeshBuilderModule;
class UMaterialInterface;
class UStaticMesh;

DECLARE_LOG_CATEGORY_EXTERN( LogDataprep, Log, All );

namespace DataprepOperationsLibraryUtil
{
	class FStaticMeshBuilder
	{
	public:
		FStaticMeshBuilder(const TSet<UStaticMesh*>& InStaticMeshes);
		~FStaticMeshBuilder();
	private:
		TArray<UStaticMesh*> StaticMeshes;
	};

	/*
	 * Builds render data of a set of static meshes.
	 * @param StaticMeshes	Set of static meshes to build if render data is missing or a forced build is required
	 * @param bForceBuild	Indicates if all static meshes should be built or only the incomplete ones
	 * @returns the array of static meshes which have actually been built
	 */
	TArray<UStaticMesh*> BuildStaticMeshes( const TSet<UStaticMesh*>& StaticMeshes, bool bForceBuild = false );

	/*
	 * Find the set of static meshes in or referenced by a given array of objects.
	 * @param SelectedObjects	Array of UObjects to go through
	 * @returns a set of static meshes
	 */
	TSet<UStaticMesh*> GetSelectedMeshes(const TArray<UObject*>& SelectedObjects);
}

/*
 * Simple struct for the table row used for UDataprepOperationsLibrary::SubstituteMaterials
 */
USTRUCT(BlueprintType)
struct FMaterialSubstitutionDataTable : public FTableRowBase
{
	GENERATED_BODY()

	/** Name of the material(s) to search for. Wildcard is supported */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaterialSubstitutionTable")
	FString SearchString;

	/** Type of matching to perform with SearchString string */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaterialSubstitutionTable")
	EEditorScriptingStringMatchType StringMatch;

	/** Material to use for the substitution */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaterialSubstitutionTable")
	UMaterialInterface* MaterialReplacement;
};

/*
* Simple struct for the table row used for UDataprepOperationsLibrary::SubstituteMaterials
*/
USTRUCT(BlueprintType)
struct FMeshSubstitutionDataTable : public FTableRowBase
{
	GENERATED_BODY()

	/** Name of the mesh(es) to search for. Wildcard is supported */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshSubstitutionTable")
	FString SearchString;

	/** Type of matching to perform with SearchString string */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshSubstitutionTable")
	EEditorScriptingStringMatchType StringMatch;

	/** Mesh to use for the substitution */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshSubstitutionTable")
	UStaticMesh* MeshReplacement;
};

/*
 * Simple struct to set up LODGroup name on static meshes
 * This is for internal purpose only to allow users to select the name of the LODGroup
 * to apply on static meshes in UDataprepOperationsLibrary::SetLODGroup
 */
USTRUCT(BlueprintInternalUseOnly)
struct DATAPREPLIBRARIES_API FLODGroupName
{
	GENERATED_BODY()

	/** Value of the name of LODGroup not the display name */
	UPROPERTY()
	FString Value;
};

/*
 * Simple struct to set up LODGroup name on static meshes
 * This is for internal purpose only to allow users to select the name of the LODGroup
 * to apply on static meshes in UDataprepOperationsLibrary::SetLODGroup
 */
USTRUCT(BlueprintInternalUseOnly)
struct DATAPREPLIBRARIES_API FMeshReductionOptions
{
	GENERATED_BODY()

	/** Value of the name of LODGroup not the display name */
	UPROPERTY()
	float ReductionPercent;

	/** Value of the name of LODGroup not the display name */
	UPROPERTY()
	float ScreenSize;
};

/*
 * Simple struct to set up LODGroup name on static meshes
 * This is for internal purpose only to allow users to select the name of the LODGroup
 * to apply on static meshes in UDataprepOperationsLibrary::SetLODGroup
 */
USTRUCT(BlueprintInternalUseOnly)
struct DATAPREPLIBRARIES_API FMeshReductionArray
{
	GENERATED_BODY()

	/** Value of the name of LODGroup not the display name */
	UPROPERTY()
	TArray<FMeshReductionOptions> Array;
};

UCLASS()
class DATAPREPLIBRARIES_API UDataprepOperationsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Generate LODs on the static meshes contained in the input array
	 * by the actors contained in the input array
	 * @param	SelectedObjects			Array of UObjects to process.
	 * @param	ReductionOptions		Options on how to generate LODs on the mesh.
	 * @remark: Static meshes are not re-built after the new LODs are set
	 * Generates an array of unique static meshes from the input array either by a cast if
	 * the UObject is a UStaticMesh or collecting the static meshes referred to if the UObject
	 * is a AActor
	 * Calls UEditorStaticMeshLibrary::SetLods on each static mesh of the resulting array.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SetLods(const TArray<UObject*>& SelectedObjects, const FEditorScriptingMeshReductionOptions& ReductionOptions, TArray<UObject*>& ModifiedObjects);

	/**
	 * Set one simple collision of the given shape type on the static meshes contained in the
	 * input array or referred to by the actors contained in the input array
	 * @param	SelectedActors			Array of actors to process.
	 * @param	ShapeType				Options on which simple collision to add to the mesh.
	 * @remark: Static meshes are not re-built after the new collision settings are set
	 * Generates an array of unique static meshes from the input array either by a cast if
	 * the UObject is a UStaticMesh or collecting the static meshes referred to if the UObject
	 * is a AActor
	 * Calls UEditorStaticMeshLibrary::RemoveCollisions to remove any existing collisions
	 * on each static mesh of the resulting array
	 * Calls UEditorStaticMeshLibrary::AddSimpleCollisions on each static mesh of the resulting array.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SetSimpleCollision(const TArray<UObject*>& SelectedObjects, const EScriptingCollisionShapeType ShapeType, TArray<UObject*>& ModifiedObjects);

	/**
	 * Add complex collision on the static meshes contained in the input array
	 * by the actors contained in the input array
	 * @param	SelectedActors			Array of actors to process.
	 * @param	HullCount				Maximum number of convex pieces that will be created. Must be positive.
	 * @param	MaxHullVerts			Maximum number of vertices allowed for any generated convex hull.
	 * @param	HullPrecision			Number of voxels to use when generating collision. Must be positive.
	 * @remark: Static meshes are not re-built after the new collision settings are set
	 * Generates an array of unique static meshes from the input array either by a cast if
	 * the UObject is a UStaticMesh or collecting the static meshes referred to if the UObject
	 * is a AActor
	 * Calls UEditorStaticMeshLibrary::SetConvexDecompositionCollisions on each static mesh of the resulting array.
	 * Note that any simple collisions on each static mesh of the resulting array will be removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SetConvexDecompositionCollision(const TArray<UObject*>& SelectedObjects, int32 HullCount, int32 MaxHullVerts, int32 HullPrecision, TArray<UObject*>& ModifiedObjects);

	/**
	 * Replaces designated materials in all or specific content folders with specific ones
	 * @param SelectedObjects: Objects to consider for the substitution
	 * @param MaterialSearch: Name of the material(s) to search for. Wildcard is supported
	 * @param StringMatch: Type of matching to perform with MaterialSearch string
	 * @param MaterialSubstitute: Material to use for the substitution
	 * @remark: A material override will be added to static mesh components if their attached
	 *			static mesh uses the searched material but not themselves
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SubstituteMaterial(const TArray<UObject*>& SelectedObjects, const FString& MaterialSearch, EEditorScriptingStringMatchType StringMatch, UMaterialInterface* MaterialSubstitute);

	/**
	 * Replaces designated materials in all or specific content folders with requested ones
	 * @param SelectedObjects: Objects to consider for the substitution
	 * @param DataTable: Data table to use for the substitution
	 * @remark: SubstituteMaterial is called for each entry of the input data table
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SubstituteMaterialsByTable(const TArray<UObject*>& SelectedObjects, const UDataTable* DataTable);

	/**
	 * Remove inputs content
	 * @param Objects Objects to remove
	 * @remark: Static meshes are not re-built after the new LOD groups are set
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SetLODGroup( const TArray< UObject* >& SelectedObjects, FName& LODGroupName, TArray<UObject*>& ModifiedObjects );

	/**
	 * Set the material to all elements of a set of Static Meshes or Static Mesh Actors
	 * @param SelectedObjects	Objects to set the input material on
	 * @param MaterialInterface Material to use
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SetMaterial( const TArray< UObject* >& SelectedObjects, UMaterialInterface* MaterialSubstitute );

	/**
	 * Set mobility on a set of static mesh actors
	 * @param SelectedObjects Objects to set mobility on
	 * @param MobilityType Type of mobility to set on selected mesh actors
	 * @remark: Only objects of class AStaticMeshActor will be considered
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SetMobility( const TArray< UObject* >& SelectedObjects, EComponentMobility::Type MobilityType );

	/**
	 * Set the mesh to all elements of a set of Actors containing StaticMeshComponents
	 * @param SelectedObjects	Objects to set the input mesh on
	 * @param MeshSubstitute	Mesh to use
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SetMesh( const TArray< UObject* >& SelectedObjects, UStaticMesh* MeshSubstitute );

	/**
	 * Replaces designated meshes in all or specific content folders with specific ones
	 * @param SelectedObjects:	Objects to consider for the substitution
	 * @param MeshSearch:		Name of the mesh(es) to search for. Wildcard is supported
	 * @param StringMatch:		Type of matching to perform with MeshSearch string
	 * @param MeshSubstitute:	Mesh to use for the substitution
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SubstituteMesh(const TArray<UObject*>& SelectedObjects, const FString& MeshSearch, EEditorScriptingStringMatchType StringMatch, UStaticMesh* MeshSubstitute);

	/**
	 * Replaces designated meshes in all or specific content folders with requested ones
	 * @param SelectedObjects:	Objects to consider for the substitution
	 * @param DataTable:		Data table to use for the substitution
	 * @remark: SubstituteMesh is called for each entry of the input data table
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Operation")
	static void SubstituteMeshesByTable(const TArray<UObject*>& SelectedObjects, const UDataTable* DataTable);

private:
	static void SubstituteMaterial(const TArray<UObject*>& SelectedObjects, const FString& MaterialSearch, EEditorScriptingStringMatchType StringMatch, const TArray<UMaterialInterface*>& MaterialList, UMaterialInterface* MaterialSubstitute);
	static void SubstituteMesh(const TArray<UObject*>& SelectedObjects, const FString& MeshSearch, EEditorScriptingStringMatchType StringMatch, const TArray<UStaticMesh*>& MeshList, UStaticMesh* MeshSubstitute);
};
