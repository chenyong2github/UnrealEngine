#include "Utilities/MiscUtils.h"
#include "AssetImportData.h"
#include "Runtime/Core/Public/Misc/Paths.h"
#include "Runtime/Core/Public/HAL/PlatformFilemanager.h"
#include "Runtime/Core/Public/GenericPlatform/GenericPlatformFile.h"
#include "Runtime/Core/Public/HAL/FileManager.h"

#include "EditorAssetLibrary.h"
#include "Runtime/AssetRegistry/Public/AssetRegistryModule.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Runtime/Engine/Classes/Engine/Selection.h"

#include "Editor/UnrealEd/Classes/Factories/MaterialInstanceConstantFactoryNew.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstanceConstant.h"
#include "Runtime/Engine/Classes/Engine/StaticMesh.h"

#include "PackageTools.h"

#include <regex>
#include <ostream>
#include <sstream>




TSharedPtr<FJsonObject> DeserializeJson(const FString& JsonStringData)
{
	TSharedPtr<FJsonObject> JsonDataObject;
	bool bIsDeseriliazeSuccessful = false;	
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonStringData);
	bIsDeseriliazeSuccessful = FJsonSerializer::Deserialize(JsonReader, JsonDataObject);	
	return JsonDataObject;
}

void ShowErrorDialog(const FString& ErrorMessage)
{

}

FString GetPluginPath()
{
	FString PluginName = TEXT("MegascansPlugin");
	FString PluginsPath = FPaths::EnginePluginsDir();

	FString MSPluginPath = FPaths::Combine(PluginsPath, PluginName);
	return MSPluginPath;
}

FString GetSourceMSPresetsPath()
{
	FString MaterialPresetsPath = FPaths::Combine(GetPluginPath(), TEXT("Content"), GetMSPresetsName());
	return MaterialPresetsPath;
}

FString GetPreferencesPath()
{
	FString PrefFileName = TEXT("unreal_action_settings.json");
	return FPaths::Combine(GetPluginPath(), TEXT("Content"), TEXT("Python"), PrefFileName);
}

FString GetMSPresetsName()
{
	return TEXT("MSPresets");
}

FString GetMaterial(const FString & MaterialName)
{	

	FString MaterialPath = FPaths::Combine(TEXT("/Game"), GetMSPresetsName(), MaterialName, MaterialName);
	
	if (!UEditorAssetLibrary::DoesAssetExist(MaterialPath))
	{
		//UE_LOG(LogTemp, Error, TEXT("Master Material doesnt exist."));
		if (CopyMaterialPreset(MaterialName))
		{
			if (UEditorAssetLibrary::DoesAssetExist(MaterialPath))
			{	
				
				
				return MaterialPath;
			}			
			
		}
	}
	else {
		return MaterialPath;
	}
	return TEXT("");
}

bool CopyMaterialPreset(const FString & MaterialName)
{
	
	FString MaterialSourceFolderPath = FPaths::Combine(GetSourceMSPresetsPath(), MaterialName);
	MaterialSourceFolderPath = FPaths::ConvertRelativePathToFull(MaterialSourceFolderPath);
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	
	if (!PlatformFile.DirectoryExists(*MaterialSourceFolderPath))
	{
		return false;
	}

	FString MaterialDestinationPath = FPaths::ProjectContentDir();
	MaterialDestinationPath = FPaths::Combine(MaterialDestinationPath, GetMSPresetsName());
	MaterialDestinationPath = FPaths::Combine(MaterialDestinationPath, MaterialName);
	
	if (!PlatformFile.DirectoryExists(*MaterialDestinationPath))
	{
		
		if (!PlatformFile.CreateDirectoryTree(*MaterialDestinationPath))
		{
			
			return false;
		}

		if (!PlatformFile.CopyDirectoryTree(*MaterialDestinationPath, *MaterialSourceFolderPath, true))
		{
			
			return false;
		}

	}

	
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FString> MaterialBasePath;
	FString BasePath = FPaths::Combine(FPaths::ProjectContentDir(), GetMSPresetsName());
	MaterialBasePath.Add("/Game/MSPresets");
	MaterialBasePath.Add(FPaths::Combine(TEXT("/Game/MSPresets"), MaterialName));
	AssetRegistryModule.Get().ScanPathsSynchronous(MaterialBasePath, true);
	return true;
	
}



bool CopyPresetTextures()
{
	FString TexturesDestinationPath = FPaths::ProjectContentDir();
	
	TexturesDestinationPath = FPaths::Combine(TexturesDestinationPath, GetMSPresetsName(), TEXT("MSTextures"));	


	FString TexturesSourceFolderPath = FPaths::Combine(GetSourceMSPresetsPath(), TEXT("MSTextures"));	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*TexturesDestinationPath))
	{
		if (!PlatformFile.CreateDirectoryTree(*TexturesDestinationPath))
		{
			
			return false;
		}

		if (!PlatformFile.CopyDirectoryTree(*TexturesDestinationPath, *TexturesSourceFolderPath, true))
		{
			
			return false;
		}
	}

	return true;
}


UObject* LoadAsset(const FString& AssetPath)
{
	return UEditorAssetLibrary::LoadAsset(AssetPath);
}

TArray<FString> GetAssetsList(const FString& DirectoryPath)
{
	return UEditorAssetLibrary::ListAssets(DirectoryPath, false);

}
void SaveAsset(const FString& AssetPath) 
{
	UEditorAssetLibrary::SaveAsset(AssetPath);
}


void DeleteExtraMesh(const FString& BasePath)
{
	TArray<FString> AssetBasePath;
	AssetBasePath.Add(BasePath);
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().ScanPathsSynchronous(AssetBasePath, true);
	FString ExtraMeshName = TEXT("StaticMesh_0");
	FString AssetPath = FPaths::Combine(BasePath, ExtraMeshName);
	
	AssetRegistryModule.Get().ScanPathsSynchronous(AssetBasePath, true);
}



FString ResolvePath(const FString& PathConvention, TSharedPtr<FAssetTypeData> AssetsImportData, TSharedPtr<FAssetTextureData> TextureData)
{
	TMap<FString, FString> TypeNames = {
		{TEXT("3d"),TEXT("3D_Assets")},
		{TEXT("3dplant"),TEXT("3D_Plants")},
		{TEXT("surface"),TEXT("Surfaces")},
		{TEXT("atlas"),TEXT("Atlases")},
		{TEXT("brush"),TEXT("Brushes")},
		{TEXT("3datlas"),TEXT("3D_Atlases")}
	};

	
	FString ResolvedPath = TEXT("");
	FString ResolvedConvention = TEXT("");
	
	FString PlaceholderConventionTexture = "Megascans/{AssetType}/{AssetName}/";

	TArray<FString> NamingConventions;
	PathConvention.ParseIntoArray(NamingConventions, TEXT("/"));

	for (FString NamingConvention : NamingConventions)
	{
		if (NamingConvention.Contains(TEXT("{")) || NamingConvention.Contains(TEXT("}")))
		{
			
			ResolvedConvention = TEXT("");
			NamingConvention = NamingConvention.Replace(TEXT("{"), TEXT(""));
			NamingConvention = NamingConvention.Replace(TEXT("}"), TEXT(""));

			if (NamingConvention == TEXT("AssetName"))
			{
				ResolvedConvention = AssetsImportData->AssetMetaInfo->Name;
			}
			else if (NamingConvention == TEXT("AssetType"))
			{
				if (TypeNames.Contains(AssetsImportData->AssetMetaInfo->Type))
				{
					ResolvedConvention = TypeNames[AssetsImportData->AssetMetaInfo->Type];
				}
				else {
					ResolvedConvention = TEXT("Misc");
				}
			}
			else if (NamingConvention == TEXT("TextureType"))
			{
				if (TextureData)
				{
					ResolvedConvention = TextureData->Type;
				}
				else {
					ResolvedConvention = TEXT("TextureType");
				}
			}
			else if (NamingConvention == TEXT("AssetId"))
			{
				ResolvedConvention = AssetsImportData->AssetMetaInfo->Id;
			}
			else if (NamingConvention == TEXT("TextureResolution"))
			{
				if (TextureData)
				{
					ResolvedConvention = TextureData->Resolution;
				}
				else {
					ResolvedConvention = TEXT("Resolution");
				}
			}
			else if (NamingConvention == TEXT("TextureName"))
			{
				if (TextureData)
				{
					ResolvedConvention = TextureData->Name;
				}
				else {
					ResolvedConvention = TEXT("Name");
				}
			}

			ResolvedConvention = NormalizeString(ResolvedConvention);
			ResolvedPath = FPaths::Combine(ResolvedPath, ResolvedConvention);
		}
		else 
		{
			ResolvedPath = FPaths::Combine(ResolvedPath, NamingConvention);
		}

	}

	return ResolvedPath;
}

FString ResolveName(const FString& NamingConvention, TSharedPtr<FAssetTypeData> AssetsImportData, TSharedPtr<FAssetTextureData> TextureData)
{
	
	FString ResolvedName = TEXT("");
	FString DummyNameConvention = TEXT("prefix_{AssetName}{AssetType}_{TextureType}_{TextureName}_postfix");
	return ResolvedName;
}


FString NormalizeString(FString InputString)
{	
	const std::string SInputString = std::string(TCHAR_TO_UTF8(*InputString));	
	const std::regex SpecialCharacters("[^a-zA-Z0-9]+");
	std::stringstream Result;
	std::regex_replace(std::ostream_iterator<char>(Result), SInputString.begin(), SInputString.end(), SpecialCharacters, "_");
	FString NormalizedString = FString(Result.str().c_str());

	return NormalizedString;
}

FString RemoveReservedKeywords(const FString& Name)
{
	FString ResolvedName = Name;
	TArray<FString> ReservedKeywrods = { "CON","PRN", "AUX", "CLOCK$", "NUL", "NONE", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
	TArray<FString> NameArray;
	Name.ParseIntoArray(NameArray, TEXT("_"));
	for (FString NameTag : NameArray)
	{
		if (ReservedKeywrods.Contains(NameTag))
		{			
			ResolvedName.Replace(*NameTag, TEXT(""));
		}
	}
	return ResolvedName;
}


TArray<FString> ParseLodList(TSharedPtr<FAssetTypeData> AssetImportData)
{
	
	FString FileExtension;
	FString FilePath;
	AssetImportData->MeshList[0]->Path.Split(TEXT("."), &FilePath, &FileExtension);
	FileExtension = FPaths::GetExtension(AssetImportData->MeshList[0]->Path);

	TArray<FString> LodPathList;	
	int32 CurrentLod = FCString::Atoi(*AssetImportData->AssetMetaInfo->ActiveLOD.Replace(TEXT("lod"), TEXT("")));

	if (AssetImportData->AssetMetaInfo->ActiveLOD == TEXT("high")) CurrentLod = -1;

	

	for (TSharedPtr<FAssetLodData> LodData : AssetImportData->LodList)
	{		
		if (LodData->Lod == TEXT("high")) continue;

		int32 LodNumber = FCString::Atoi(*LodData->Lod.Replace(TEXT("lod"), TEXT("")));
		if (LodNumber > CurrentLod)
		{	
			FString LodExtension, LodName;
			LodData->Path.Split(TEXT("."), &LodName, &LodExtension);
			LodExtension = FPaths::GetExtension(LodData->Path);
			if (LodExtension == FileExtension)
			{
				
				LodPathList.Add(LodData->Path);
			}
		}
	}
	return LodPathList;
}



FString GetRootDestination(const FString& ExportPath)
{
	TArray<FString> PathTokens;	
	FString BasePath;
	FString DestinationPath;
	ExportPath.Split(TEXT("Content"), &BasePath, &DestinationPath);
	FString RootDestination = TEXT("/Game/");

#if PLATFORM_WINDOWS
	DestinationPath.ParseIntoArray(PathTokens, TEXT("\\"));
#elif PLATFORM_MAC
	DestinationPath.ParseIntoArray(PathTokens, TEXT("/"));
#elif PLATFORM_LINUX
	DestinationPath.ParseIntoArray(PathTokens, TEXT("/"));
#endif	

	for (FString Token : PathTokens)
	{
		FString NormalizedToken = RemoveReservedKeywords( NormalizeString(Token));		
		RootDestination = FPaths::Combine(RootDestination, NormalizedToken);
	}	
	if (RootDestination == TEXT("/Game/"))
	{
		RootDestination = FPaths::Combine(RootDestination, TEXT("Megascans"));
	}
	return RootDestination;
}




FString ResolveDestination(const FString& AssetDestination)
{
	return TEXT("Resolved destination path");
}

FString GetAssetName(TSharedPtr<FAssetTypeData> AssetImportData)
{

	return FString();
}

FString GetUniqueAssetName(const FString& AssetDestination, const FString AssetName, bool FileSearch)
{

	FString ResolvedAssetName = RemoveReservedKeywords(NormalizeString(AssetName));	
	TArray<FString> AssetDirectories;	
	auto& FileManager = IFileManager::Get();
	FileManager.FindFiles(AssetDirectories, *AssetDestination, FileSearch, !FileSearch);

	
	for (int i = 0; i < 200; i++)
	{
		FString ResolvedAssetDirectory = TEXT("");
		if (i < 10) {
			ResolvedAssetDirectory = ResolvedAssetName + TEXT("_0") + FString::FromInt(i);
		}
		else
		{
			ResolvedAssetDirectory = ResolvedAssetName + TEXT("_") + FString::FromInt(i);
		}

		if (!UEditorAssetLibrary::DoesDirectoryExist(FPaths::Combine(AssetDestination, ResolvedAssetDirectory)))
		{
			
			return ResolvedAssetDirectory;
		}

	}

	return TEXT("");
}

FString SanitizeName(const FString& InputName)
{
	return RemoveReservedKeywords(NormalizeString(InputName));
}




	template<typename T>
	TArray<T*> AssetUtils::GetSelectedAssets(const FString& AssetClass)
	{
		TArray<T*> ObjectArray;

		TArray<FAssetData> AssetDatas;
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();
		ContentBrowserSingleton.GetSelectedAssets(AssetDatas);

		for (FAssetData SelectedAsset : AssetDatas)
		{
			
			if (SelectedAsset.AssetClass == FName(*AssetClass))
			{
				ObjectArray.Add(CastChecked<T>(UEditorAssetLibrary::LoadAsset(SelectedAsset.ObjectPath.ToString())));
			}
		}
		return ObjectArray;
	}

	void AssetUtils::FocusOnSelected(const FString& Path)
	{
		TArray<FString> Folders;
		Folders.Add(Path);
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();
		ContentBrowserSingleton.SyncBrowserToFolders(Folders);
	}

	
	void AssetUtils::AddStaticMaterial(UStaticMesh* SourceMesh, UMaterialInstanceConstant* NewMaterial)
	{
		if (NewMaterial == nullptr) return;

		UMaterialInterface* NewMatInterface = CastChecked<UMaterialInterface>(NewMaterial);
		FStaticMaterial MaterialSlot = FStaticMaterial(NewMatInterface);
		SourceMesh->StaticMaterials.Add(MaterialSlot);
	}

	void AssetUtils::SavePackage(UObject* SourceObject)
	{
		TArray<UObject*> InputObjects;
		InputObjects.Add(SourceObject);
		UPackageTools::SavePackagesForObjects(InputObjects);

	}



