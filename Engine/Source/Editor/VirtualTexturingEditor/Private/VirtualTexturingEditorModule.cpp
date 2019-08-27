// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VirtualTexturingEditorModule.h"

#include "IPlacementModeModule.h"
#include "PropertyEditorModule.h"
#include "RuntimeVirtualTextureAssetTypeActions.h"
#include "RuntimeVirtualTextureBuild.h"
#include "RuntimeVirtualTextureDetailsCustomization.h"
#include "RuntimeVirtualTextureThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureVolume.h"

#define LOCTEXT_NAMESPACE "VirtualTexturingEditorModule"

/** Concrete implementation of the IVirtualTexturingEditorModule interface. */
class FVirtualTexturingEditorModule : public IVirtualTexturingEditorModule
{
public:
	//~ Begin IModuleInterface Interface.
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override;
	//~ End IModuleInterface Interface.

	//~ Begin IVirtualTexturingEditorModule Interface.
	virtual bool HasStreamedMips(URuntimeVirtualTextureComponent* InComponent) const override;
	virtual bool BuildStreamedMips(URuntimeVirtualTextureComponent* InComponent) const override;
	//~ End IVirtualTexturingEditorModule Interface.

private:
	void OnPlacementModeRefresh(FName CategoryName);
};

IMPLEMENT_MODULE(FVirtualTexturingEditorModule, VirtualTexturingEditor);

void FVirtualTexturingEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_RuntimeVirtualTexture));
	
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("RuntimeVirtualTexture", FOnGetDetailCustomizationInstance::CreateStatic(&FRuntimeVirtualTextureDetailsCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("RuntimeVirtualTextureComponent", FOnGetDetailCustomizationInstance::CreateStatic(&FRuntimeVirtualTextureComponentDetailsCustomization::MakeInstance));

	IPlacementModeModule& PlacementModeModule = IPlacementModeModule::Get();
	PlacementModeModule.OnPlacementModeCategoryRefreshed().AddRaw(this, &FVirtualTexturingEditorModule::OnPlacementModeRefresh);

	UThumbnailManager::Get().RegisterCustomRenderer(URuntimeVirtualTexture::StaticClass(), URuntimeVirtualTextureThumbnailRenderer::StaticClass());
}

void FVirtualTexturingEditorModule::ShutdownModule()
{
	if (IPlacementModeModule::IsAvailable())
	{
		IPlacementModeModule::Get().OnPlacementModeCategoryRefreshed().RemoveAll(this);
	}
}

bool FVirtualTexturingEditorModule::SupportsDynamicReloading()
{
	return false;
}

void FVirtualTexturingEditorModule::OnPlacementModeRefresh(FName CategoryName)
{
	static FName VolumeName = FName(TEXT("Volumes"));
	if (CategoryName == VolumeName)
	{
		IPlacementModeModule& PlacementModeModule = IPlacementModeModule::Get();
		PlacementModeModule.RegisterPlaceableItem(CategoryName, MakeShareable(new FPlaceableItem(nullptr, FAssetData(ARuntimeVirtualTextureVolume::StaticClass()))));
	}
}

bool FVirtualTexturingEditorModule::HasStreamedMips(URuntimeVirtualTextureComponent* InComponent) const
{
	return RuntimeVirtualTexture::HasStreamedMips(InComponent);
}

bool FVirtualTexturingEditorModule::BuildStreamedMips(URuntimeVirtualTextureComponent* InComponent) const
{
	return RuntimeVirtualTexture::BuildStreamedMips(InComponent, ERuntimeVirtualTextureDebugType::None);
}

#undef LOCTEXT_NAMESPACE
