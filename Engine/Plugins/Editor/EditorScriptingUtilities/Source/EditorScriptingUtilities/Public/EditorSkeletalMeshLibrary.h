// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineTypes.h"

#include "EditorSkeletalMeshLibrary.generated.h"

class UPhysicsAsset;
class USkeletalMesh;

/**
* Utility class to altering and analyzing a SkeletalMesh and use the common functionalities of the SkeletalMesh Editor.
* The editor should not be in play in editor mode.
 */
UCLASS()
class EDITORSCRIPTINGUTILITIES_API UEditorSkeletalMeshLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Regenerate LODs of the mesh
	 *
	 * @param SkeletalMesh	The mesh that will regenerate LOD
	 * @param NewLODCount	Set valid value (>0) if you want to change LOD count.
	 *						Otherwise, it will use the current LOD and regenerate
	 * @param bRegenerateEvenIfImported	If this is true, it only regenerate even if this LOD was imported before
	 *									If false, it will regenerate for only previously auto generated ones
	 * @param bGenerateBaseLOD If this is true and there is some reduction data, the base LOD will be reduce according to the settings
	 * @return	true if succeed. If mesh reduction is not available this will return false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh", meta = (ScriptMethod))
	static bool RegenerateLOD(USkeletalMesh* SkeletalMesh, int32 NewLODCount = 0, bool bRegenerateEvenIfImported = false, bool bGenerateBaseLOD = false);

	/** Get number of mesh vertices for an LOD of a Skeletal Mesh
	 *
	 * @param SkeletalMesh		Mesh to get number of vertices from.
	 * @param LODIndex			Index of the mesh LOD.
	 * @return Number of vertices. Returns 0 if invalid mesh or LOD index.
	 */
	UFUNCTION(BlueprintPure, Category = "Editor Scripting | SkeletalMesh")
	static int32 GetNumVerts(USkeletalMesh* SkeletalMesh, int32 LODIndex);

	/** Get number of sections for a LOD of a Skeletal Mesh
	 *
	 * @param SkeletalMesh		Mesh to get number of vertices from.
	 * @param LODIndex			Index of the mesh LOD.
	 * @return Number of sections. Returns INDEX_NONE if invalid mesh or LOD index.
	 */
	UFUNCTION(BlueprintPure, Category = "Editor Scripting | SkeletalMesh")
	static int32 GetNumSections(USkeletalMesh* SkeletalMesh, int32 LODIndex);

	/**
	 * Gets the material slot used for a specific LOD section.
	 * @param	SkeletalMesh		SkeletalMesh to get the material index from.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @param	SectionIndex		Index of the StaticMesh Section.
	 * @return  MaterialSlotIndex	Index of the material slot used by the section or INDEX_NONE in case of error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | StaticMesh")
	static int32 GetLODMaterialSlot(USkeletalMesh* SkeletalMesh, int32 LODIndex, int32 SectionIndex);

	/** Rename a socket within a skeleton
	 * @param SkeletalMesh	The mesh inside which we are renaming a socket
	 * @param OldName       The old name of the socket
	 * @param NewName		The new name of the socket
	 * @return true if the renaming succeeded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh", meta = (ScriptMethod))
	static bool RenameSocket(USkeletalMesh* SkeletalMesh, FName OldName, FName NewName);

	/**
	 * Retrieve the number of LOD contain in the specified skeletal mesh.
	 *
	 * @param SkeletalMesh: The static mesh we import or re-import a LOD.
	 *
	 * @return The LOD number.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh")
	static int32 GetLODCount(USkeletalMesh* SkeletalMesh);

	/**
	 * Import or re-import a LOD into the specified base mesh. If the LOD do not exist it will import it and add it to the base static mesh. If the LOD already exist it will re-import the specified LOD.
	 *
	 * @param BaseSkeletalMesh: The static mesh we import or re-import a LOD.
	 * @param LODIndex: The index of the LOD to import or re-import. Valid value should be between 0 and the base skeletal mesh LOD number. Invalid value will return INDEX_NONE
	 * @param SourceFilename: The fbx source filename. If we are re-importing an existing LOD, it can be empty in this case it will use the last import file. Otherwise it must be an existing fbx file.
	 *
	 * @return The index of the LOD that was imported or re-imported. Will return INDEX_NONE if anything goes bad.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh")
	static int32 ImportLOD(USkeletalMesh* BaseMesh, const int32 LODIndex, const FString& SourceFilename);

	/**
	 * Re-import the specified skeletal mesh and all the custom LODs.
	 *
	 * @param SkeletalMesh: is the skeletal mesh we import or re-import a LOD.
	 *
	 * @return true if re-import works, false otherwise see log for explanation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh")
	static bool ReimportAllCustomLODs(USkeletalMesh* SkeletalMesh);

	/**
	 * Copy the build options with the specified LOD build settings.
	 * @param SkeletalMesh - Mesh to process.
	 * @param LodIndex - The LOD we get the reduction settings.
	 * @param OutBuildOptions - The build settings where we copy the build options.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh")
	static void GetLodBuildSettings(const USkeletalMesh* SkeletalMesh, const int32 LodIndex, FSkeletalMeshBuildSettings& OutBuildOptions);

	/**
	 * Set the LOD build options for the specified LOD index.
	 * @param SkeletalMesh - Mesh to process.
	 * @param LodIndex - The LOD we will apply the build settings.
	 * @param BuildOptions - The build settings we want to apply to the LOD.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh")
	static void SetLodBuildSettings(USkeletalMesh* SkeletalMesh, const int32 LodIndex, const FSkeletalMeshBuildSettings& BuildOptions);

	/** Remove all the specified LODs. This function will remove all the valid LODs in the list.
	 * Valid LOD is any LOD greater then 0 that exist in the skeletalmesh. We cannot remove the base LOD 0.
	 *
	 * @param SkeletalMesh	The mesh inside which we are renaming a socket
	 * @param ToRemoveLODs	The LODs we need to remove
	 * @return true if the successfully remove all the LODs. False otherwise, but evedn if it return false it
	 * will have removed all valid LODs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh", meta = (ScriptMethod))
	static bool RemoveLODs(USkeletalMesh* SkeletalMesh, TArray<int32> ToRemoveLODs);

	/**
	 * This function will strip all triangle in the specified LOD that don't have any UV area pointing on a black pixel in the TextureMask.
	 * We use the UVChannel 0 to find the pixels in the texture.
	 *
	 * @Param SkeletalMesh: The skeletalmesh we want to optimize
	 * @Param LODIndex: The LOD we want to optimize
	 * @Param TextureMask: The texture containing the stripping mask. non black pixel strip triangle, black pixel keep them.
	 * @Param Threshold: The threshold we want when comparing the texture value with zero.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh", meta = (ScriptMethod))
	static bool StripLODGeometry(USkeletalMesh* SkeletalMesh, const int32 LODIndex, UTexture2D* TextureMask, const float Threshold);

	/**
	 * This function creates a PhysicsAsset for the given SkeletalMesh with the same settings as if it were created through FBX import
	 *
	 * @Param SkeletalMesh: The SkeletalMesh we want to create the PhysicsAsset for
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | SkeletalMesh")
	static UPhysicsAsset* CreatePhysicsAsset(USkeletalMesh* SkeletalMesh);
};

