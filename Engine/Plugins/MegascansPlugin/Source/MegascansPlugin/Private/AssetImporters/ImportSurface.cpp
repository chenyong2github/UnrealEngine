#include "ImportSurface.h"
#include "AssetImportData.h"
#include "Utilities/MiscUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Editor/UnrealEd/Classes/Factories/MaterialInstanceConstantFactoryNew.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstanceConstant.h"
#include "Runtime/Engine/Classes/Engine/Texture.h"
#include "UI/MSSettings.h"
#include "MaterialEditingLibrary.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#include "EditorAssetLibrary.h"

#include "Runtime/Engine/Classes/Engine/Selection.h"
#include "Editor.h"
#include "PackageTools.h"






TSharedPtr<FImportSurface> FImportSurface::ImportSurfaceInst;


TSharedPtr<FImportSurface> FImportSurface::Get()
{
	if (!ImportSurfaceInst.IsValid())
	{
		ImportSurfaceInst = MakeShareable(new FImportSurface);
	}
	return ImportSurfaceInst;
}



UMaterialInstanceConstant* FImportSurface::ImportSurface(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTypeData> AssetImportData, TSharedPtr<SurfaceImportParams> SImportParams)
{	
	
	
	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	const UMaterialPresetsSettings* MatOverrideSettings = GetDefault< UMaterialPresetsSettings>();

	FString MasterMaterialName = TEXT("");
	FString SurfaceType = GetSurfaceType(AssetImportData);
	bool bApplyPackedMaps = true;
	bEnableExrDisplacement = false;	
	FString SelectedMaterialName = GetMasterMaterialName(TypeSurfacePrefs, AssetImportData);		
	
	auto PackedImportData = ImportPackedMaps(AssetImportData, SImportParams->TexturesDestination);
	
	bool bDisplacementEnabled = false;

	if (MegascansSettings->bEnableDisplacement)
	{
		if (SurfaceType != "displacement") {
			MasterMaterialName = TEXT("_Displacement");
			bDisplacementEnabled = true;
		}
		bEnableExrDisplacement = true;
	}

	if (SurfaceType == "displacement") bEnableExrDisplacement = true;

	if (PackedImportData.Num() > 0)
	{
		MasterMaterialName = SelectedMaterialName + MasterMaterialName + TEXT("_CP");
	}
	else {
		MasterMaterialName = SelectedMaterialName + MasterMaterialName;
		bApplyPackedMaps = false;
	}

	// Not supporting channel packing
	//MasterMaterialName = SelectedMaterialName;
	//bApplyPackedMaps = false;
	// End of not supporting channel packing

	
	FString MasterMaterialPath = GetMasterMaterial(MasterMaterialName);
	if (MasterMaterialPath == TEXT(""))
	{
		if ( !bApplyPackedMaps && !bDisplacementEnabled ) {
			 return nullptr;			
		}
		else if (!bApplyPackedMaps && bDisplacementEnabled )
		{
				MasterMaterialPath = GetMasterMaterial(SelectedMaterialName );
				if (MasterMaterialPath == TEXT("")) return nullptr;			
		}
		else if(bApplyPackedMaps && !bDisplacementEnabled)
		{			
			MasterMaterialPath = GetMasterMaterial(SelectedMaterialName);			
			bApplyPackedMaps = false;
			if (MasterMaterialPath == TEXT("")) return nullptr;
		}

		else if (bApplyPackedMaps && bDisplacementEnabled )
		{
			MasterMaterialPath = GetMasterMaterial(SelectedMaterialName + TEXT("_CP"));			
			if (MasterMaterialPath == TEXT(""))
			{
				bApplyPackedMaps = false;
				MasterMaterialPath = GetMasterMaterial(SelectedMaterialName + TEXT("_Displacement"));
				if (MasterMaterialPath == TEXT(""))
				{
					MasterMaterialPath = GetMasterMaterial(SelectedMaterialName);
					if (MasterMaterialPath == TEXT("")) return nullptr;
				}
			}
		}

		else {
			MasterMaterialPath = GetMasterMaterial(SelectedMaterialName);
			if (MasterMaterialPath == TEXT("")) return nullptr;
		}
		
	}

	FString OverrideMatPath = GetMaterialOverride(AssetImportData);
	if(OverrideMatPath != TEXT("None") && OverrideMatPath != TEXT(""))
		if (UEditorAssetLibrary::DoesAssetExist(OverrideMatPath))		
			MasterMaterialPath = OverrideMatPath;
		
	
	
	UMaterialInstanceConstant* MaterialInstance = CreateInstanceMaterial(MasterMaterialPath, SImportParams->MInstanceDestination, SImportParams->MInstanceName);
	
	const TArray<FString> FilteredTextureTypes = GetFilteredMaps(PackedImportData, MaterialInstance);
	TMap<FString, TextureData> TextureMaps = ImportTextureMaps(TypeSurfacePrefs, AssetImportData, SImportParams->TexturesDestination, FilteredTextureTypes);

	if (MaterialInstance == nullptr)
	{
		return nullptr;
	}

	MInstanceApplyTextures(TextureMaps, MaterialInstance);	
	if (bApplyPackedMaps) MInstanceApplyPackedMaps(PackedImportData, MaterialInstance);

	//AssetUtils::SavePackage(MaterialInstance);

	if (MegascansSettings->bApplyToSelection)
	{
		if (AssetImportData->AssetMetaInfo->Type == TEXT("surface") || AssetImportData->AssetMetaInfo->Type == TEXT("atlas") || AssetImportData->AssetMetaInfo->Type == TEXT("brush"))
		{
			// Apply imported surface on selected assets in Content Browser, currently disabled
			/*
			TArray<FAssetData> AssetDatas;
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();
			ContentBrowserSingleton.GetSelectedAssets(AssetDatas);

			for (FAssetData SelectedAsset : AssetDatas)
			{

				//UE_LOG(LogTemp, Error, TEXT("Selected asset type is : %s"), *SelectedAsset.AssetClass.ToString());
				if (SelectedAsset.AssetClass == FName(TEXT("StaticMesh")) || SelectedAsset.AssetClass == FName(TEXT("SkeletalMesh")))
				{
					// Apply to selection in Content Browser
					UStaticMesh* LoadedMesh = CastChecked<UStaticMesh>(LoadAsset(SelectedAsset.ObjectPath.ToString()));			
					LoadedMesh->SetMaterial(0, CastChecked<UMaterialInterface>(MaterialInstance));
				}
			}
			*/

			//Code for selected assets in Editor
			
			USelection* SelectedActors = GEditor->GetSelectedActors();
			TArray<AActor*> Actors;
			TArray<ULevel*> UniqueLevels;
			for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
			{
				AActor* Actor = Cast<AActor>(*Iter);
				TArray<UStaticMeshComponent*> Components;
				Actor->GetComponents<UStaticMeshComponent>(Components);
				for (int32 i = 0; i < Components.Num(); i++)
				{
					UStaticMeshComponent* MeshComponent = Components[i];
					int32 mCnt = MeshComponent->GetNumMaterials();
					for (int j = 0; j < mCnt; j++)						
						MeshComponent->SetMaterial(j, MaterialInstance);

				}
			}
			

		}
	}
	
	AssetUtils::FocusOnSelected(SImportParams->MInstanceDestination);
	return MaterialInstance;
}


TMap<FString, TextureData> FImportSurface::ImportTextureMaps(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTypeData> AssetImportData, const FString& TexturesDestination, const TArray<FString>& FilteredTextureTypes)
{
	TMap<FString, TextureData> TextureMaps;	

	for (TSharedPtr<FAssetTextureData> TextureMetaData : AssetImportData->TextureComponents)
	{
		FString TextureName = ResolveName(TypeSurfacePrefs->RenamePrefs->TextureName, AssetImportData, TextureMetaData);
		FString TextureType = TextureMetaData->Type;

		if (FilteredTextureTypes.Contains(TextureType)) continue;

		

		/*
		if (bEnableExrDisplacement && TextureType == TEXT("displacement"))
		{			
			FString TexturePath = TextureMetaData->Path;
			FString ExrSourcePath = FPaths::GetBaseFilename(TexturePath, false) + TEXT(".exr");
			
			//////////////////////////////////////////////////////////////
			TArray<FString> SupportedResolutions = { "2K", "4K", "8K" };
			SupportedResolutions.Remove(AssetImportData->AssetMetaInfo->Resolution);

			
   			 			
			if (!FPaths::FileExists(ExrSourcePath))
			{
				for (FString& DisplacementResolution : SupportedResolutions)
				{
					FString ExrSourceName;
					ExrSourceName = AssetImportData->AssetMetaInfo->Id + "_" + DisplacementResolution + "_Displacement.exr";
					ExrSourcePath = FPaths::Combine(FPaths::GetPath(TextureMetaData->Path), ExrSourceName);					

					if (FPaths::FileExists(ExrSourcePath))
					{
						TextureMetaData->Path = ExrSourcePath;
						break;
					}
				}


			}
			else TextureMetaData->Path = ExrSourcePath;	

		}
		*/

		UAssetImportTask* TextureImportTask = CreateImportTask(TypeSurfacePrefs, TextureMetaData, TexturesDestination);
		TextureData TextureImportData = ImportTexture(TextureImportTask);

		if (TextureType == TEXT("normal"))
		{
			TextureImportData.TextureAsset->CompressionSettings = TextureCompressionSettings::TC_Normalmap;
			//const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
			//if(MegascansSettings->bFlipNormalGreenChannel)
				TextureImportData.TextureAsset->bFlipGreenChannel = 1;
		}

		if (TextureType == TEXT("opacity"))
			TextureImportData.TextureAsset->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;

		TextureImportData.TextureAsset->VirtualTextureStreaming = 0;

		//UTexture* TextureAsset = Cast<UTexture>(LoadAsset(TextureImportData.Path));
		if (NonLinearMaps.Contains(TextureType))
		{	
			TextureImportData.TextureAsset->SRGB = 1;
			//TextureAsset->SRGB = 1;
		}
		else 
		{
			TextureImportData.TextureAsset->SRGB = 0;
			//TextureAsset->SRGB = 0;
		}

		if (TextureType != TEXT("normal"))
			TextureImportData.TextureAsset->CompressionSettings = TextureCompressionSettings::TC_BC7;
		//TextureAsset->CompressionSettings = TextureCompressionSettings::TC_BC7;
		TextureImportData.TextureAsset->SetFlags(RF_Standalone);
		TextureImportData.TextureAsset->MarkPackageDirty();
		TextureImportData.TextureAsset->PostEditChange();

		//AssetUtils::SavePackage(TextureAsset);
		

		TextureMaps.Add(TextureType, TextureImportData);
	}
	return TextureMaps;
}

FString FImportSurface::GetMasterMaterial(const FString& SelectedMaterial)
{
	CopyPresetTextures();
	FString MaterialPath = GetMaterial(SelectedMaterial);
	return (MaterialPath == TEXT("")) ? TEXT("") : MaterialPath;
}

UAssetImportTask* FImportSurface::CreateImportTask(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTextureData> TextureMetaData, const FString& TexturesDestination)
{
	FString Filename;
	Filename = FPaths::GetBaseFilename(TextureMetaData->NameOverride);
	UAssetImportTask* TextureImportTask = NewObject<UAssetImportTask>();
	TextureImportTask->bAutomated = true;
	TextureImportTask->bSave = false;
	TextureImportTask->Filename = TextureMetaData->Path;
	TextureImportTask->DestinationName = RemoveReservedKeywords(NormalizeString(Filename));
	TextureImportTask->DestinationPath = TexturesDestination;
	TextureImportTask->bReplaceExisting = true;
	return TextureImportTask;
}

TextureData FImportSurface::ImportTexture(UAssetImportTask* TextureImportTask)
{
	TextureData TextureImportData;
	TArray<UAssetImportTask*> ImportTasks;
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	ImportTasks.Add(TextureImportTask);

	AssetTools.ImportAssetTasks(ImportTasks);
	for (UAssetImportTask* ImpTask : ImportTasks)
	{
		if (ImpTask->ImportedObjectPaths.Num() > 0)
		{
			TextureImportData.TextureAsset = CastChecked<UTexture>(UEditorAssetLibrary::LoadAsset(ImpTask->ImportedObjectPaths[0]));
			TextureImportData.Path = ImpTask->ImportedObjectPaths[0];
		}
	}
	return TextureImportData;
}

UMaterialInstanceConstant* FImportSurface::CreateInstanceMaterial(const FString & MasterMaterialPath, const FString& InstanceDestination, const FString& MInstanceName)
{
	if (!UEditorAssetLibrary::DoesAssetExist(MasterMaterialPath)) return nullptr;

	UMaterialInterface* MasterMaterial = CastChecked<UMaterialInterface>(LoadAsset(MasterMaterialPath));
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();

	UObject* MaterialInstAsset = AssetTools.CreateAsset(MInstanceName, InstanceDestination, UMaterialInstanceConstant::StaticClass(), Factory);
	UMaterialInstanceConstant *MaterialInstance = CastChecked<UMaterialInstanceConstant>(MaterialInstAsset);
	
	UMaterialEditingLibrary::SetMaterialInstanceParent(MaterialInstance, MasterMaterial);
	if (MaterialInstance)
	{
		MaterialInstance->SetFlags(RF_Standalone);
		MaterialInstance->MarkPackageDirty();
		MaterialInstance->PostEditChange();

		
	}
	
	return MaterialInstance;
}

void FImportSurface::MInstanceApplyTextures(TMap<FString, TextureData> TextureMaps, UMaterialInstanceConstant* MaterialInstance)
{	

	for (const TPair<FString, TextureData>& TextureData : TextureMaps)
	{

		if (UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(MaterialInstance, FName(*TextureData.Key)))
		{
			//UTexture* TextureAsset = Cast<UTexture>(LoadAsset(TextureData.Value.Path));
			UTexture* TextureAsset = TextureData.Value.TextureAsset;
			if (UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MaterialInstance, FName(*TextureData.Key), TextureAsset))
			{
				MaterialInstance->SetFlags(RF_Standalone);
				MaterialInstance->MarkPackageDirty();
				MaterialInstance->PostEditChange();
				
			}
			
		}
		
	}
	
}

TArray<FString> FImportSurface::GetPackedMapsList(TSharedPtr<FAssetTypeData> AssetImportData)
{
	TArray<FString> PackedMaps;
	return PackedMaps;
}


TMap<FString, TSharedPtr<FAssetPackedTextures>> FImportSurface::ImportPackedMaps(TSharedPtr<FAssetTypeData> AssetImportData, const FString& TexturesDestination)
{
	TMap<FString, TSharedPtr<FAssetPackedTextures>> PackedImportData;
	for (TSharedPtr<FAssetPackedTextures> PackedData : AssetImportData->PackedTextures)
	{
		UAssetImportTask* TextureImportTask = CreateImportTask(nullptr, PackedData->PackedTextureData, TexturesDestination);
		TextureData TextureImportData = ImportTexture(TextureImportTask);

		//UTexture* TextureAsset = Cast<UTexture>(LoadAsset(TextureImportData.Path));
		TextureImportData.TextureAsset->SRGB = 0;

		FString ImportedMap = TextureImportData.Path;
		if (PackedImportData.Contains(ImportedMap))
		{
			PackedImportData.Remove(ImportedMap);
		}
		PackedImportData.Add(ImportedMap, PackedData);
		//AssetUtils::SavePackage(TextureAsset);
	}
	return PackedImportData;
}

TArray<FString> FImportSurface::GetPackedTypes(ChannelPackedData PackedImportData)
{
	TArray<FString> PackedTypes;
	for (auto& PackedData : PackedImportData)
	{
		for (auto& ChData : PackedData.Value->ChannelData)
		{			
				if (ChData.Value[0] == TEXT("gray") || ChData.Value[0] == TEXT("empty") || ChData.Value[0] == TEXT("value")) continue;
				PackedTypes.Add(ChData.Value[0].ToLower());
				
		}

	}
	return PackedTypes;
}

void FImportSurface::MInstanceApplyPackedMaps(TMap<FString, TSharedPtr<FAssetPackedTextures>> PackedImportData, UMaterialInstanceConstant* MaterialInstance)
{
	
	for (auto& PackedMapData : PackedImportData)
	{
		FString ChannelPackedType = TEXT("");
		FString PackedMap = PackedMapData.Key;
		UTexture* PackedAsset = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(PackedMapData.Key));
		if (PackedAsset == nullptr) continue;
		for (auto& ChData : PackedMapData.Value->ChannelData)
		{
			if (ChData.Value[0] == TEXT("gray") || ChData.Value[0] == TEXT("empty") || ChData.Value[0] == TEXT("value")) continue;
			ChannelPackedType = ChannelPackedType + ChData.Value[0].Left(1);
		}

		
		if (UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(MaterialInstance, FName(*ChannelPackedType)))
		{
			UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MaterialInstance, FName(*ChannelPackedType), PackedAsset);
			MaterialInstance->SetFlags(RF_Standalone);
			MaterialInstance->MarkPackageDirty();
			MaterialInstance->PostEditChange();
		}
	}

}



FString FImportSurface::GetMaterialOverride(TSharedPtr<FAssetTypeData> AssetImportData)
{
	TArray<FString> SupportedMatOverrides = { "3d", "3dplant", "surface" };
	FString AssetType = AssetImportData->AssetMetaInfo->Type;
	const UMaterialPresetsSettings* MatOverrideSettings = GetDefault< UMaterialPresetsSettings>();
	if (SupportedMatOverrides.Contains(AssetType))
	{
		if (AssetType == "surface" && MatOverrideSettings->MasterMaterialSurface != nullptr) return MatOverrideSettings->MasterMaterialSurface->GetPathName();
		if (AssetType == "3d" && MatOverrideSettings->MasterMaterial3d != nullptr) return MatOverrideSettings->MasterMaterial3d->GetPathName();
		if (AssetType == "3dplant" && MatOverrideSettings->MasterMaterialPlant != nullptr) return MatOverrideSettings->MasterMaterialPlant->GetPathName();

	}
	return TEXT("");

}


FString FImportSurface::GetSurfaceType(TSharedPtr<FAssetTypeData> AssetImportData)
{
	FString SurfaceType = TEXT("");
	FString AssetType = AssetImportData->AssetMetaInfo->Type;
	auto& TagsList = AssetImportData->AssetMetaInfo->Tags;
	auto& CategoriesList = AssetImportData->AssetMetaInfo->Categories;

	if(TagsList.Contains(TEXT("metal"))) SurfaceType = TEXT("metal");
	else if(TagsList.Contains(TEXT("surface imperfection"))) SurfaceType = TEXT("imperfection");
	else if (TagsList.Contains(TEXT("tileable displacement"))) SurfaceType = TEXT("displacement");
	
	else if (CategoriesList[0] == TEXT("brush")) SurfaceType = TEXT("brush");
	else if (CategoriesList[0] == TEXT("atlas") && TagsList.Contains(TEXT("decal"))) SurfaceType = TEXT("decal");
	else if (CategoriesList[0] == TEXT("atlas") && !TagsList.Contains(TEXT("decal"))) SurfaceType = TEXT("atlas");

	return SurfaceType;
}

FString FImportSurface::GetMasterMaterialName(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTypeData> AssetImportData)
{
	FString SurfaceType = TEXT("");
	FString SelectedMaterialName;
	if (AssetImportData->AssetMetaInfo->Type == TEXT("surface") || AssetImportData->AssetMetaInfo->Type == TEXT("atlas") || AssetImportData->AssetMetaInfo->Type == TEXT("brush"))
	{
		SurfaceType = GetSurfaceType(AssetImportData);
		if (SurfaceTypeMaterials.Contains(SurfaceType))
		{
			SelectedMaterialName = SurfaceTypeMaterials[SurfaceType];
		}
		else {
			SelectedMaterialName = TypeSurfacePrefs->MaterialPrefs->SelectedMaterial;
		}
	}
	else {
		SelectedMaterialName = TypeSurfacePrefs->MaterialPrefs->SelectedMaterial;
	}

	return SelectedMaterialName;
}

TArray<FString> FImportSurface::GetFilteredMaps(ChannelPackedData PackedImportData, UMaterialInstanceConstant* MaterialInstance)
{

	

	TArray<FString> FilteredMaps = GetPackedTypes(PackedImportData);
	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	bool bFalsifyThis = false;
	
	if (MaterialInstance !=nullptr && MegascansSettings->bFilterMasterMaterialMaps)
	{
		
		for (FString MapType : AllMapTypes)
		{
			if (UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(MaterialInstance, FName(*MapType)) == nullptr)
			{
				FilteredMaps.Add(MapType);
			}
		}

	}

	return FilteredMaps;


}


TSharedPtr<SurfaceImportParams> FImportSurface::GetSurfaceImportParams(TSharedPtr<FSurfacePreferences> TypePrefs, TSharedPtr<FAssetTypeData> AssetImportData)
{
	TSharedPtr<SurfaceImportParams> SImportParams = MakeShareable(new SurfaceImportParams);

	FString RootDestination = GetRootDestination(AssetImportData->AssetMetaInfo->ExportPath);	
	FString TexturesDestination = FPaths::Combine(RootDestination, ResolvePath(TypePrefs->DestinationPrefs->TextureDestinationPath, AssetImportData));
	//FString AssetName = RemoveReservedKeywords(NormalizeString(GetUniqueAssetName(TexturesDestination, AssetImportData->AssetMetaInfo->Name)));	
	FString AssetName = GetUniqueAssetName(TexturesDestination, RemoveReservedKeywords(NormalizeString(AssetImportData->AssetMetaInfo->Name)));
	TexturesDestination = FPaths::Combine(TexturesDestination, AssetName);

	FString InstanceDestination = FPaths::Combine(RootDestination, ResolvePath(TypePrefs->DestinationPrefs->MaterialDestinationPath, AssetImportData));
	InstanceDestination = FPaths::Combine(InstanceDestination, AssetName);

	SImportParams->MInstanceName = AssetName + TEXT("_inst");
	SImportParams->TexturesDestination = TexturesDestination;
	SImportParams->AssetName = AssetName;
	SImportParams->MInstanceDestination = InstanceDestination;

	return SImportParams;
}

