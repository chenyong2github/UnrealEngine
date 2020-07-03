#pragma once
#include "CoreMinimal.h"
#include "Templates/Casts.h"
#include "Runtime/Json/Public/Dom/JsonObject.h"
#include "Runtime/Json/Public/Serialization/JsonSerializer.h"

class UStaticMesh;
class UMaterialInstanceConstant;
struct FAssetTextureData;
struct FAssetTypeData;

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

	TArray<UObject*> GetSelectedAssets(const FString& AssetClass);

	template<typename T>
	TArray<T*> GetSelectedAssets(const FString& AssetClass)
	{
		TArray<UObject*> SelectedAssets = GetSelectedAssets(AssetClass);

		TArray<T*> ObjectArray;
		ObjectArray.Reserve(SelectedAssets.Num());
		for (UObject* SelectedAsset : SelectedAssets)
		{
			ObjectArray.Add(CastChecked<T>(SelectedAsset));
		}
		return ObjectArray;
	}
	void FocusOnSelected(const FString& Path);
	void AddStaticMaterial(UStaticMesh* SourceMesh, UMaterialInstanceConstant* NewMaterial);
	void SavePackage(UObject* SourceObject);
}

namespace PathUtils
{
	
}
