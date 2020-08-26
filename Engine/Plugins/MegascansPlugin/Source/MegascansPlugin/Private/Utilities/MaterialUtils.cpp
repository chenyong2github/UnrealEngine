// Copyright Epic Games, Inc. All Rights Reserved.
#include "Utilities/MaterialUtils.h"
#include "Editor/UnrealEd/Classes/Factories/MaterialInstanceConstantFactoryNew.h"

#include "Runtime/Engine/Classes/Engine/Texture.h"
#include "MaterialEditingLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"


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


