#pragma once
#include "CoreMinimal.h"

#include "AssetPreferencesData.h"

class UMaterialInstanceConstant;
class UTexture;

struct TextureData
{
	FString Path;
	UTexture* TextureAsset;
};

struct SurfaceImportParams
{
	
	FString TexturesDestination;
	FString AssetName;
	//FString MasterMaterialPath;
	FString MInstanceDestination;
	FString AssetDestination;
	FString MeshDestination;
	FString FoliageDestination;
	FString MInstanceName;
	bool bExrEnabled;
};


class FImportSurface
{
	typedef TMap<FString, TSharedPtr<FAssetPackedTextures>> ChannelPackedData;


private:
	FImportSurface() = default;
	bool bEnableExrDisplacement;
	TMap<FString, TextureData> ImportTextureMaps(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTypeData> AssetImportData, const FString& TexturesDestination, const TArray<FString>& FilteredTextureTypes);
	FString GetMasterMaterial(const FString & SelectedMaterial);
	static TSharedPtr<FImportSurface> ImportSurfaceInst;
	UAssetImportTask* CreateImportTask(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTextureData> TextureMetaData, const FString& TexturesDestination);

	TextureData ImportTexture(UAssetImportTask * TextureImportTask);
	UMaterialInstanceConstant* CreateInstanceMaterial(const FString& MasterMaterialPath, const FString& InstanceDestination, const FString& AssetName);
	void MInstanceApplyTextures(TMap<FString, TextureData> TextureMaps, UMaterialInstanceConstant* MaterialInstance);
	TArray<FString> GetPackedMapsList(TSharedPtr<FAssetTypeData> AssetImportData);
	TMap<FString, TSharedPtr<FAssetPackedTextures>> ImportPackedMaps(TSharedPtr<FAssetTypeData> AssetImportData, const FString& TexturesDestination);
	FString GetSurfaceType(TSharedPtr<FAssetTypeData> AssetImportData);	

	FString GetMasterMaterialName(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTypeData> AssetImportData);
	TArray<FString> GetFilteredMaps(ChannelPackedData PackedImportData, UMaterialInstanceConstant* MaterialInstance);


	const TArray<FString> NonLinearMaps = { "albedo", "diffuse", "translucency", "specular" };
	const TArray<FString> ColorPackedMaps = { "albedo", "diffuse", "translucency", "specular" , "normal" };
	const TMap<FString, FString> SurfaceTypeMaterials = { {"decal","Decal_MasterMaterial"},
													{"metal","MS_DefaultMaterial"},
													{"imperfection","Imperfection_MasterMaterial"} ,
													{"displacement","Displacement_MasterMaterial"},
													{"brush","BrushDecal_Material"},
													{"atlas","Atlas_Material"} };

	const TArray<FString> AllMapTypes = {
		"opacity",
		  "normal",
		  "bump",
		  "gloss" ,
		  "displacement",
		  "ao",
		  "thickness",
		  "cavity",
		  "specular",
		  "normalbump",
		  "translucency",
		  "albedo",
		  "roughness",
		  "diffuse",
		  "fuzz",
		  "metalness"
	};

	TArray<FString> GetPackedTypes(ChannelPackedData PackedImportData);
	void MInstanceApplyPackedMaps(TMap<FString, TSharedPtr<FAssetPackedTextures>> PackedImportData, UMaterialInstanceConstant* MaterialInstance);
	

	FString  GetMaterialOverride(TSharedPtr<FAssetTypeData> AssetImportData);
	


public:
	static TSharedPtr<FImportSurface> Get();
	
	TSharedPtr<SurfaceImportParams> GetSurfaceImportParams(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTypeData> AssetImportData);
	UMaterialInstanceConstant* ImportSurface(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTypeData> AssetImportData, TSharedPtr<SurfaceImportParams> SImportParams);

};