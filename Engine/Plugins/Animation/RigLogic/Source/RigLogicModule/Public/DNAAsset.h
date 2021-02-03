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

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Instanced, Category = ImportSettings)
	class UAssetImportData* AssetImportData;
#endif

	TSharedPtr<IBehaviorReader> GetBehaviorReader()
	{
		return BehaviorStreamReader;
	}

#if WITH_EDITORONLY_DATA
	TSharedPtr<IGeometryReader> GetGeometryReader()
	{
		return GeometryStreamReader;
	}
#endif

	UPROPERTY()
	FString DNAFileName; 

	bool Init(const FString Filename);
	void Serialize(FArchive& Ar) override;

	/** Used when importing behavior into archetype SkelMesh in the editor, 
	  * and when updating SkeletalMesh runtime with GeneSplicer; it will split
	  * GeneSplicerDNAReader will overwrite the behavior-only reader
	  * allowing the RigLogic RigUnit to read behavior part directly from it
	**/
	void SetBehaviorReader(const TSharedPtr<IDNAReader> SourceDNAReader);
	void SetGeometryReader(const TSharedPtr<IDNAReader> SourceDNAReader);

private:
	/** Part of the .dna file needed for run-time execution of RigLogic;
	 **/
    TSharedPtr<IBehaviorReader> BehaviorStreamReader = nullptr;

	/** Part of the .dna file used design-time for updating SkeletalMesh geometry
	 **/
	TSharedPtr<IGeometryReader> GeometryStreamReader = nullptr;

};
