// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "Engine/AssetUserData.h"
#include "Interfaces/Interface_AssetUserData.h"

#include "RigLogic.h"

#include "DNAAsset.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDNAAsset, Log, All);

class IDNAReader;
class IBehaviorReader;
class IGeometryReader;
class FRigLogicMemoryStream;
class UAssetUserData;
enum class EDNADataLayer: uint8;

/* A helper struct to store arrays of arrays of integers. */
USTRUCT()
struct FIntArray
{
	GENERATED_BODY()

	// The values stored within this array.
	UPROPERTY(transient)
	TArray<int32> Values;
};

USTRUCT()
struct RIGLOGICMODULE_API FSharedRigRuntimeContext
{
	GENERATED_BODY()

	/** Part of the .dna file needed for run-time execution of RigLogic;
	 **/
	TSharedPtr<IBehaviorReader> BehaviorReader;

	/** Part of the .dna file used design-time for updating SkeletalMesh geometry
	 **/
	TSharedPtr<IGeometryReader> GeometryReader;

	/** RigLogic itself is stateless, and is designed to be shared between
	  * multiple rig instances based on the same DNA.
	**/
	TSharedPtr<FRigLogic> RigLogic;

	/** Mapping RL indices to UE indices
	  * Note: we use int32 instead of uint32 to allow storing INDEX_NONE for missing elements
	  * if value is valid, it is cast to appropriate uint type
	**/

	/** RL input index to ControlRig's input curve index for each LOD **/
	UPROPERTY(transient)
	TArray<int32> InputCurveIndices;

	/** RL joint index to ControlRig's hierarchy bone index **/
	UPROPERTY(transient)
	TArray<int32> HierarchyBoneIndices;

	/** RL mesh blend shape index to ControlRig's output blendshape curve index for each LOD **/
	UPROPERTY(transient)
	TArray<FIntArray> MorphTargetCurveIndices;

	/** RL mesh+blend shape array index to RL blend shape index for each LOD **/
	UPROPERTY(transient)
	TArray<FIntArray> BlendShapeIndices;

	/** RL animated map index to ControlRig's output anim map curve index for each LOD **/
	UPROPERTY(transient)
	TArray<FIntArray> CurveContainerIndicesForAnimMaps;

	/** RL animated map index to RL anim map curve index for each LOD **/
	UPROPERTY(transient)
	TArray<FIntArray> RigLogicIndicesForAnimMaps;
};


 /** An asset holding the data needed to generate/update/animate a RigLogic character
  * It is imported from character's DNA file as a bit stream, and separated out it into runtime (behavior) and design-time chunks;
  * Currently, the design-time part still loads the geometry, as it is needed for the skeletal mesh update; once SkeletalMeshDNAReader is
  * fully implemented, it will be able to read the geometry directly from the SkeletalMesh and won't load it into this asset 
  **/
UCLASS(NotBlueprintable, hidecategories = (Object))
class RIGLOGICMODULE_API UDNAAsset : public UAssetUserData
{
	GENERATED_BODY()

public:
	UDNAAsset();

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Instanced, Category = ImportSettings)
	class UAssetImportData* AssetImportData;
#endif

	TSharedPtr<IBehaviorReader> GetBehaviorReader()
	{
		return Context->BehaviorReader;
	}

#if WITH_EDITORONLY_DATA
	TSharedPtr<IGeometryReader> GetGeometryReader()
	{
		return Context->GeometryReader;
	}
#endif

	TSharedPtr<FSharedRigRuntimeContext> GetSharedRigRuntimeContext()
	{
		return Context;
	}

	UPROPERTY()
	FString DNAFileName; 

	bool Init(const FString& Filename);
	void Serialize(FArchive& Ar) override;

	/** Used when importing behavior into archetype SkelMesh in the editor,
	  * and when updating SkeletalMesh runtime with GeneSplicer
	**/
	void SetBehaviorReader(TSharedPtr<IDNAReader> SourceDNAReader);
	void SetGeometryReader(TSharedPtr<IDNAReader> SourceDNAReader);

private:
	/** Runtime data necessary for rig computations that is shared between
	  * multiple rig instances based on the same DNA.
	**/
	TSharedPtr<FSharedRigRuntimeContext> Context;

};
