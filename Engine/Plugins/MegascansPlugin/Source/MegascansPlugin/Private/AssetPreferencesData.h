#pragma once
#include "CoreMinimal.h"


struct FMaterialPrefs
{
	FString SelectedMaterial;
	TArray<FString> MaterialsList;
	bool bApplyToSelection;
};


struct FLodSettings {
	TArray<int> IncludeLods;
	bool bGenerateExtraLods;
	FString TotalLods;
};


struct FTexturePrefs {
	TArray<FString> TextureList;
	TMap<FString, bool> TextureFilter;

};

struct FRenamePrefs
{
	FString MaterialInstance;
	FString TextureName;
	FString MeshName;	
};

struct FDestinationPrefs
{
	FString TextureDestinationPath;
	FString MaterialDestinationPath;
	FString MeshDestinationPath;
};

struct FMiscOptions3D {
	bool bPlaceInLevel;
	bool bCreateScatterFoliage;
	bool bExcludeHighpoly;
	bool bOverwriteExisting;

};

struct FMiscOptions3dPlant
{
	bool bCreateFoliage;
	bool bApplyWind;

};


struct FPreferences
{
	TSharedPtr<FMaterialPrefs> MaterialPrefs;
	TSharedPtr<FTexturePrefs> TexturePrefs;
	TSharedPtr<FDestinationPrefs> DestinationPrefs;
	TSharedPtr<FRenamePrefs> RenamePrefs;
};

// Data structure to hold the import settings for asset type 3D
struct F3DPreferences : FPreferences
{	
	FLodSettings LodSettings3D;	
	FMiscOptions3D MiscPrefs3D;	
};


// Data structure to hold the import settings for asset type 3d Plants
struct F3dPlantPreferences : FPreferences
{

};

struct FSurfacePreferences : FPreferences
{

};










