#include "AssetPreferencesHandler.h"
#include "Runtime/Core/Public/HAL/PlatformFilemanager.h"
#include "Runtime/Core/Public/Misc/MessageDialog.h"
#include "Runtime/Core/Public/Misc/FileHelper.h"
#include "Runtime/Json/Public/Dom/JsonObject.h"
//#include "JsonObjectConverter.h"
#include "Runtime/Json/Public/Serialization/JsonSerializer.h"
#include "Utilities/MiscUtils.h"



TSharedPtr<FAssetPreferencesHandler> FAssetPreferencesHandler::PrefDataHandlerInst;

FAssetPreferencesHandler::FAssetPreferencesHandler()	
{
	
	ParsePreferencesFile();
}


void FAssetPreferencesHandler::ParsePreferencesFile()
{
	
	FString PrefJsonFilePath = GetPreferencesPath();

	FString PrefsData;
	LoadJsonFromFile(PrefJsonFilePath, PrefsData);
	PrefJsonObject = DeserializeJson(PrefsData);
	PrefJsonObject = PrefJsonObject->GetObjectField(TEXT("msAsset"));

}


TSharedPtr<FAssetPreferencesHandler> FAssetPreferencesHandler::Get()
{
	if (!PrefDataHandlerInst.IsValid())
	{
		PrefDataHandlerInst = MakeShareable(new FAssetPreferencesHandler);
	}
	return PrefDataHandlerInst;
}

TSharedPtr<FSurfacePreferences> FAssetPreferencesHandler::GetSurfacePreferences()
{
	//UE_LOG(MegascansLog, Log, TEXT("Reading Surface preferences data."));
	TSharedPtr<FJsonObject> PrefSurfaceObject = PrefJsonObject->GetObjectField(TEXT("surface"));
	TSharedPtr<FSurfacePreferences> AssetSurfacePrefs = MakeShareable(new FSurfacePreferences);

	AssetSurfacePrefs->MaterialPrefs = GetMaterialPrefs(PrefSurfaceObject->GetObjectField(TEXT("materialPrefs")));
	AssetSurfacePrefs->TexturePrefs = GetTexturePrefs(PrefSurfaceObject->GetObjectField(TEXT("texturePrefs")));
	AssetSurfacePrefs->DestinationPrefs = GetDestinationPrefs(PrefSurfaceObject->GetObjectField(TEXT("destinationPrefs")));
	AssetSurfacePrefs->RenamePrefs = GetRenamePrefs(PrefSurfaceObject->GetObjectField(TEXT("renamePrefs")));

	return AssetSurfacePrefs;
}

TSharedPtr<F3dPlantPreferences> FAssetPreferencesHandler::Get3dPlantPreferences()
{
	//UE_LOG(MegascansLog, Log, TEXT("Reading 3d Plants preferences data."));
	TSharedPtr<FJsonObject> PrefPlantObject = PrefJsonObject->GetObjectField(TEXT("3dplant"));
	TSharedPtr<F3dPlantPreferences> AssetPlantPrefs = MakeShareable(new F3dPlantPreferences);

	AssetPlantPrefs->MaterialPrefs = GetMaterialPrefs(PrefPlantObject->GetObjectField(TEXT("materialPrefs")));
	AssetPlantPrefs->TexturePrefs = GetTexturePrefs(PrefPlantObject->GetObjectField(TEXT("texturePrefs")));
	AssetPlantPrefs->DestinationPrefs = GetDestinationPrefs(PrefPlantObject->GetObjectField(TEXT("destinationPrefs")));
	AssetPlantPrefs->RenamePrefs = GetRenamePrefs(PrefPlantObject->GetObjectField(TEXT("renamePrefs")));


	return AssetPlantPrefs;
}



TSharedPtr<F3DPreferences> FAssetPreferencesHandler::Get3dPreferences()
{
	//UE_LOG(MegascansLog, Log, TEXT("Reading 3d asset preferences data."));
	TSharedPtr<FJsonObject> Pref3dObject = PrefJsonObject->GetObjectField(TEXT("3d"));
	TSharedPtr<F3DPreferences> Asset3dPrefs = MakeShareable(new F3DPreferences);
	
	Asset3dPrefs->LodSettings3D = GetLodPrefs(Pref3dObject->GetObjectField(TEXT("lodSettings")));
	Asset3dPrefs->MiscPrefs3D = Get3dMiscPrefs(Pref3dObject->GetObjectField(TEXT("miscOptions")));

	Asset3dPrefs->MaterialPrefs = GetMaterialPrefs(Pref3dObject->GetObjectField(TEXT("materialPrefs")));
	Asset3dPrefs->TexturePrefs = GetTexturePrefs(Pref3dObject->GetObjectField(TEXT("texturePrefs")));
	Asset3dPrefs->DestinationPrefs = GetDestinationPrefs(Pref3dObject->GetObjectField(TEXT("destinationPrefs")));
	Asset3dPrefs->RenamePrefs = GetRenamePrefs(Pref3dObject->GetObjectField(TEXT("renamePrefs")));

	
	return Asset3dPrefs;	
}


// Read, parse and return the Texture preferences
TSharedPtr<FTexturePrefs> FAssetPreferencesHandler::GetTexturePrefs(TSharedPtr<FJsonObject>TexturePrefObject)
{
	//UE_LOG(MegascansLog, Log, TEXT("Reading texture preferences data."));
	TSharedPtr<FTexturePrefs> AssetTexturePrefs= MakeShareable(new FTexturePrefs);
	TSharedPtr<FJsonObject> TextureFilterObject = TexturePrefObject->GetObjectField(TEXT("textureFilter"));
	for (auto currJsonValue = TextureFilterObject->Values.CreateConstIterator(); currJsonValue; ++currJsonValue)
	{		
		const FString TextureName = (*currJsonValue).Key;		
		TSharedPtr< FJsonValue > Value = (*currJsonValue).Value;
		bool TextureStatus = Value->AsBool();
		AssetTexturePrefs->TextureFilter.Add(TextureName, TextureStatus);
		AssetTexturePrefs->TextureList.Add(TextureName);
		//TSharedPtr<FJsonObject> JsonObjectIn = Value->AsObject();
	}


	return AssetTexturePrefs;
}

TSharedPtr<FDestinationPrefs> FAssetPreferencesHandler::GetDestinationPrefs(TSharedPtr<FJsonObject> DestinationPrefObject)
{
	TSharedPtr<FDestinationPrefs> DestinationPrefs = MakeShareable(new FDestinationPrefs);
	DestinationPrefs->TextureDestinationPath = DestinationPrefObject->GetStringField(TEXT("texturesDestination"));
	DestinationPrefs->MaterialDestinationPath = DestinationPrefObject->GetStringField(TEXT("materialDestination"));
	
	if (DestinationPrefObject->HasField(TEXT("meshDestination")))
	{
		DestinationPrefs->MeshDestinationPath = DestinationPrefObject->GetStringField(TEXT("meshDestination"));
	}
	return DestinationPrefs;
}

TSharedPtr<FRenamePrefs> FAssetPreferencesHandler::GetRenamePrefs(TSharedPtr<FJsonObject> RenamePrefObject)
{
	TSharedPtr<FRenamePrefs> RenamePrefs = MakeShareable(new FRenamePrefs);
	RenamePrefs->TextureName = RenamePrefObject->GetStringField(TEXT("texture"));
	RenamePrefs->MaterialInstance = RenamePrefObject->GetStringField(TEXT("materialInst"));

	if (RenamePrefObject->HasField(TEXT("mesh")))
	{
		RenamePrefs->MeshName = RenamePrefObject->GetStringField(TEXT("mesh"));
	}
	return RenamePrefs;
	
}

// Read, parse and return the Material preferences
TSharedPtr<FMaterialPrefs> FAssetPreferencesHandler::GetMaterialPrefs(TSharedPtr<FJsonObject> MatPrefObject)
{
	TSharedPtr<FMaterialPrefs> AssetMatPrefs= MakeShareable( new FMaterialPrefs) ;
	AssetMatPrefs->SelectedMaterial = MatPrefObject->GetStringField(TEXT("selectedMaterial"));
	//UE_LOG(LogTemp, Error, TEXT("Selected Material : %s"), *AssetMatPrefs.SelectedMaterial);
	AssetMatPrefs->bApplyToSelection = MatPrefObject->GetBoolField(TEXT("applySelection"));
	TArray<TSharedPtr<FJsonValue>> MaterialArray = MatPrefObject->GetArrayField(TEXT("materialList"));

	for (TSharedPtr<FJsonValue> MaterialElement : MaterialArray)
	{
		AssetMatPrefs->MaterialsList.Add(MaterialElement->AsString());
	}
	return AssetMatPrefs;
}

// Read, parse and return the Lod preferences
FLodSettings FAssetPreferencesHandler::GetLodPrefs(TSharedPtr<FJsonObject> LodPrefObject)
{
	FLodSettings AssetLodPrefs;
	AssetLodPrefs.bGenerateExtraLods = LodPrefObject->GetBoolField(TEXT("generateExtraLods"));
	AssetLodPrefs.TotalLods = LodPrefObject->GetStringField(TEXT("numberOfLods"));

	TArray<TSharedPtr<FJsonValue> > IncludeLodArray = LodPrefObject->GetArrayField(TEXT("includeLods"));
	for (TSharedPtr<FJsonValue> LodElement : IncludeLodArray)
	{
		AssetLodPrefs.IncludeLods.Add((int32)LodElement->AsNumber());
	}

	return AssetLodPrefs;
}

// Read, parse and return Misc options for 3d types.
FMiscOptions3D FAssetPreferencesHandler::Get3dMiscPrefs(TSharedPtr<FJsonObject> MiscOptions3dObject) 
{
	FMiscOptions3D Asset3dMiscPrefs;
	Asset3dMiscPrefs.bCreateScatterFoliage = MiscOptions3dObject->GetBoolField(TEXT("scatterCreateFoliage"));
	Asset3dMiscPrefs.bPlaceInLevel = MiscOptions3dObject->GetBoolField(TEXT("placeInLevel"));
	Asset3dMiscPrefs.bExcludeHighpoly = MiscOptions3dObject->GetBoolField(TEXT("excludeHighpoly"));
	//Asset3dMiscPrefs.bOverwriteExisting = MiscOptions3dObject->GetBoolField(TEXT("overwriteExisting"));
	return Asset3dMiscPrefs;
}

// Utility function to read data from a Json file as string
bool FAssetPreferencesHandler::LoadJsonFromFile(const FString& JsonFilePath, FString& JsonStringData)
{
	bool bIsJsonReadSuccessful = false;
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*JsonFilePath))
	{
		FString LoadErrorMessage = TEXT("File doesnt exist ");
		FText DialogErrorMessage = FText::FromString(LoadErrorMessage);
		EAppReturnType::Type ObjectLoadAction = FMessageDialog::Open(EAppMsgType::Ok, DialogErrorMessage);
	}
	else {
		bIsJsonReadSuccessful = FFileHelper::LoadFileToString(JsonStringData, *JsonFilePath);
	}
	return bIsJsonReadSuccessful;

}

// Read json data from a string and convert to a json object, jsonobject can be parsed and stored in data structures
bool FAssetPreferencesHandler::DeserializeJson1(FString JsonSourceData)
{
	bool bIsDeseriliazeSuccessful = false;
	TSharedPtr<FJsonObject> PrefDataJsonObject = MakeShareable(new FJsonObject);
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonSourceData);

	bIsDeseriliazeSuccessful = FJsonSerializer::Deserialize(JsonReader, PrefDataJsonObject);
	
	return bIsDeseriliazeSuccessful;

}

void FAssetPreferencesHandler::ShowErrorDialog(const FString& ErrorMessage)
{
	FText DialogErrorMessage = FText::FromString(ErrorMessage);
	EAppReturnType::Type ObjectLoadAction = FMessageDialog::Open(EAppMsgType::Ok, DialogErrorMessage);

}

