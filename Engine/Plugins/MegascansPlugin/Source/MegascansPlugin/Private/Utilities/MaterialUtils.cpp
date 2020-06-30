#include "Utilities/MaterialUtils.h"
#include "Editor/UnrealEd/Classes/Factories/MaterialInstanceConstantFactoryNew.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstanceConstant.h"
#include "Runtime/Engine/Classes/Engine/Texture.h"
#include "MaterialEditingLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "AssetPreferencesData.h"
#include "AssetImportData.h"
#include "UI/MSSettings.h"


UMaterialInstanceConstant* FMaterialUtils::CreateInstanceMaterial(const FString& MasterMaterialPath, const FString& InstanceDestination, const FString& AssetName)
{	
	UMaterialInterface* MasterMaterial = CastChecked<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MasterMaterialPath));
	if (MasterMaterial == nullptr) return nullptr;

	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	UMaterialInstanceConstant* MaterialInstance = CastChecked<UMaterialInstanceConstant>(AssetTools.CreateAsset(AssetName, InstanceDestination, UMaterialInstanceConstant::StaticClass(), Factory));
	UMaterialEditingLibrary::SetMaterialInstanceParent(MaterialInstance, MasterMaterial);
	if (MaterialInstance)
	{
		MaterialInstance->SetFlags(RF_Standalone);
		MaterialInstance->MarkPackageDirty();
		MaterialInstance->PostEditChange();
	}
	return MaterialInstance;
}

FString FMaterialUtils::GetMasterMaterial(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTypeData> AssetImportData, const FString& MasterMaterialOverride)
{

	//Decouple from Surface implementation along with other options.
	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	FString MaterialVariants = TEXT("");
	FString MasterMaterialName = (MasterMaterialOverride == TEXT("")) ? TypeSurfacePrefs->MaterialPrefs->SelectedMaterial : MasterMaterialOverride;

	return FString();
}
