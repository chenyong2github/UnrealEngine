// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeSkeletalMeshFactory.generated.h"

class UInterchangeSkeletalMeshFactoryNode;
class USkeletalMesh;
class USkeleton;

UCLASS(BlueprintType, Experimental)
class INTERCHANGEIMPORT_API UInterchangeSkeletalMeshFactory : public UInterchangeFactoryBase
{
	GENERATED_BODY()
public:
	struct FImportAssetObjectLODData
	{
		int32 LodIndex = INDEX_NONE;
		TArray<FName> ExistingOriginalPerSectionMaterialImportName;
#if WITH_EDITOR
		TArray<SkeletalMeshImportData::FMaterial> ImportedMaterials;
#endif
	};
	struct FImportAssetObjectData
	{
		bool bIsReImport = false;
		USkeleton* SkeletonReference = nullptr;
		bool bApplySkinningOnly = false;
		TArray<FImportAssetObjectLODData> LodDatas;
		bool IsValid() const;
	};
	//////////////////////////////////////////////////////////////////////////
	// Interchange factory base interface begin

	virtual UClass* GetFactoryClass() const override;
	virtual EInterchangeFactoryAssetType GetFactoryAssetType() override { return EInterchangeFactoryAssetType::Meshes; }
	virtual UObject* BeginImportAssetObject_GameThread(const FImportAssetObjectParams& Arguments) override;
	virtual UObject* ImportAssetObject_Async(const FImportAssetObjectParams& Arguments) override;
	virtual UObject* EndImportAssetObject_GameThread(const FImportAssetObjectParams& Arguments) override;
	virtual void Cancel() override;
	virtual void SetupObject_GameThread(const FSetupObjectParams& Arguments) override;
	virtual bool GetSourceFilenames(const UObject* Object, TArray<FString>& OutSourceFilenames) const override;
	virtual bool SetSourceFilename(const UObject* Object, const FString& SourceFilename, int32 SourceIndex) const override;
	virtual bool SetReimportSourceIndex(const UObject* Object, int32 SourceIndex) const override;

	// Interchange factory base interface end
	//////////////////////////////////////////////////////////////////////////
private:
	FEvent* SkeletalMeshLockPropertiesEvent = nullptr;

	FImportAssetObjectData ImportAssetObjectData;
};


