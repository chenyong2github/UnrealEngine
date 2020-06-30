#pragma once
#include "CoreMinimal.h"
#include "Runtime/Json/Public/Dom/JsonObject.h"
#include "Runtime/Json/Public/Serialization/JsonSerializer.h"

class UStaticMesh;
class UMaterialInstanceConstant;


static TSharedPtr<FJsonObject>  DeserializeJson(const FString& JsonStringData);
static void ShowErrorDialog(const FString& ErrorMessage);
static FString GetPluginPath();
static FString GetMaterialPresetsPath();
static FString GetPreferencesPath();
FString GetMaterial(const FString & MaterialName);
static bool CopyMaterialPreset(const FString & MaterialName);
static FString GetMSPresetsName();
UObject* LoadAsset(const FString& AssetPath);
void DeleteExtraMesh(const FString& BasePath);
void SaveAsset(const FString& AssetPath);
static TArray<FString> ParseLodList( TSharedPtr<FAssetTypeData> AssetImportData);
FString ResolvePath(const FString& AssetPath, TSharedPtr<FAssetTypeData> AssetsImportData, TSharedPtr<FAssetTextureData> TextureData=nullptr);
FString ResolveName(const FString& NamingConvention, TSharedPtr<FAssetTypeData> AssetsImportData, TSharedPtr<FAssetTextureData> TextureData = nullptr);
FString RemoveReservedKeywords(const FString& Name);
FString NormalizeString(FString InputString);
TArray<FString> GetAssetsList(const FString& DirectoryPath);
static FString GetRootDestination(const FString& ExportPath);
FString ResolveDestination(const FString& AssetDestination);
bool CopyPresetTextures();

static FString GetAssetName(TSharedPtr<FAssetTypeData> AssetImportData);
static FString GetUniqueAssetName(const FString& AssetDestination, const FString AssetName, bool FileSearch=false);

static FString SanitizeName(const FString& InputName);

namespace AssetUtils {
	template<typename T>
	static TArray<T*> GetSelectedAssets(const FString& AssetClass);
	static void FocusOnSelected(const FString& Path);
	static void AddStaticMaterial(UStaticMesh* SourceMesh, UMaterialInstanceConstant* NewMaterial);
	static void SavePackage(UObject* SourceObject);
}

namespace PathUtils
{
	
}
