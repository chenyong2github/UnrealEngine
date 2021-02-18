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

struct FSharedRigRuntimeContext
{
	template <typename T>
	struct TNestedArray
	{
		TArray<T> Values;
	};

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

	/** Cached joint indices that need to be updated for each LOD **/
	TArray<TNestedArray<uint16>> VariableJointIndices;
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
		return Context.BehaviorReader;
	}

#if WITH_EDITORONLY_DATA
	TSharedPtr<IGeometryReader> GetGeometryReader()
	{
		return Context.GeometryReader;
	}
#endif

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
	friend struct FRigUnit_RigLogic;
	FSharedRigRuntimeContext Context;

};
