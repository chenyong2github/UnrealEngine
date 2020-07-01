#pragma once
#include "CoreMinimal.h"

#include "AssetPreferencesData.h"

class FJsonObject;

// Class to read asset preferences from a json file, and serialize the json values to Asset preferences data
class FAssetPreferencesHandler {

private:
	FAssetPreferencesHandler();
	static TSharedPtr<FAssetPreferencesHandler> PrefDataHandlerInst;
	bool LoadJsonFromFile(const FString& JsonFilePath, FString& JsonStringData);
	bool DeserializeJson1(FString JsonStringData);
	TSharedPtr<FMaterialPrefs> GetMaterialPrefs(TSharedPtr<FJsonObject> MatPrefObject);
	TSharedPtr<FTexturePrefs> GetTexturePrefs(TSharedPtr<FJsonObject> TexturePrefObject);
	TSharedPtr<FDestinationPrefs> GetDestinationPrefs(TSharedPtr<FJsonObject> DestinationPrefObject);
	TSharedPtr<FRenamePrefs> GetRenamePrefs(TSharedPtr<FJsonObject> RenamePrefObject);
	FLodSettings GetLodPrefs(TSharedPtr<FJsonObject> LodPrefObject);
	FMiscOptions3D Get3dMiscPrefs(TSharedPtr<FJsonObject> MiscOptions3dObject);
	void ShowErrorDialog(const FString& ErrorMessage);

	void ParsePreferencesFile();
	TSharedPtr<FJsonObject> PrefJsonObject;




public:	
	static TSharedPtr<FAssetPreferencesHandler> Get();
	TSharedPtr<FSurfacePreferences> GetSurfacePreferences();
	TSharedPtr<F3DPreferences> Get3dPreferences();
	TSharedPtr<F3dPlantPreferences> Get3dPlantPreferences();


};