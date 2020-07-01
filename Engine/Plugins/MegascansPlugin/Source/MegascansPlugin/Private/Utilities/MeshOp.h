#pragma once
#include "CoreMinimal.h"
#include "AssetPreferencesData.h"

class UFbxImportUI;
class UStaticMesh;
class UAbcImportSettings;


class FMeshOps
{
private:
	FMeshOps() = default;
	static TSharedPtr<FMeshOps> MeshOpsInst;
	UFbxImportUI* GetFbxOptions();
	

	template<class T> FString ImportFile(T* ImportOptions, const FString& Destination, const FString& AssetName, const FString& Source);
	UAbcImportSettings* GetAbcSettings();

public:
	static TSharedPtr<FMeshOps> Get();
	FString ImportMesh(const FString& MeshPath, const FString& Destination, const FString& AssetName = TEXT(""));
	void ApplyLods(const TArray<FString>& LodList, UStaticMesh* SourceMesh);
	TArray<FString> ImportLodsAsStaticMesh(const TArray<FString>& LodList, const FString& AssetDestination);
	void ApplyAbcLods(UStaticMesh* SourceMesh, const TArray<FString>& LodPathList, const FString& AssetDestination);
	void CreateFoliageAsset(const FString& FoliagePath, UStaticMesh* SourceAsset, const FString& FoliageAssetName);
	void RemoveExtraMaterialSlot(UStaticMesh* SourceMesh);
};