// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Runtime/Json/Public/Dom/JsonObject.h"
#include "Runtime/Json/Public/Serialization/JsonSerializer.h"
#include "Materials/MaterialInstanceConstant.h"
#include "AssetImportData.h"

class UStaticMesh;



TSharedPtr<FJsonObject>  DeserializeJson(const FString& JsonStringData);
void ShowErrorDialog(const FString& ErrorMessage);
FString GetPluginPath();
FString GetMaterialPresetsPath();
FString GetPreferencesPath();
FString GetMaterial(const FString & MaterialName);
bool CopyMaterialPreset(const FString & MaterialName);
FString GetMSPresetsName();
UObject* LoadAsset(const FString& AssetPath);
void DeleteExtraMesh(const FString& BasePath);
void SaveAsset(const FString& AssetPath);
TArray<FString> ParseLodList( TSharedPtr<FAssetTypeData> AssetImportData);
TArray<TSharedPtr<FAssetLodData>> ParsePlantsLodList(TSharedPtr<FAssetTypeData> AssetImportData);
FString ResolvePath(const FString& AssetPath, TSharedPtr<FAssetTypeData> AssetsImportData, TSharedPtr<FAssetTextureData> TextureData=nullptr);
FString ResolveName(const FString& NamingConvention, TSharedPtr<FAssetTypeData> AssetsImportData, TSharedPtr<FAssetTextureData> TextureData = nullptr);
FString RemoveReservedKeywords(const FString& Name);
FString NormalizeString(FString InputString);
TArray<FString> GetAssetsList(const FString& DirectoryPath);
FString GetRootDestination(const FString& ExportPath);
FString ResolveDestination(const FString& AssetDestination);
bool CopyPresetTextures();

FString GetAssetName(TSharedPtr<FAssetTypeData> AssetImportData);
FString GetUniqueAssetName(const FString& AssetDestination, const FString AssetName, bool FileSearch=false);

FString SanitizeName(const FString& InputName);

namespace AssetUtils {
	//template<typename T>
	TArray<UMaterialInstanceConstant*> GetSelectedAssets(const FString& AssetClass);
	void FocusOnSelected(const FString& Path);
	void AddStaticMaterial(UStaticMesh* SourceMesh, UMaterialInstanceConstant* NewMaterial);
	void SavePackage(UObject* SourceObject);
}

namespace PathUtils
{
	
}

namespace DHI {
	bool GetDHIJsonData(const FString& JsonStringData, TArray<FDHIData>& DHIAssetsData);
	void CopyCharacter(const FDHIData& CharacterSourcePath, bool OverWriteExisting = false);
}
