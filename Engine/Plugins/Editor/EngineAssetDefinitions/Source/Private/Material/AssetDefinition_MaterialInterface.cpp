// Copyright Epic Games, Inc. All Rights Reserved.

#include "Material/AssetDefinition_MaterialInterface.h"

#include "ContentBrowserMenuContexts.h"
#include "Materials/Material.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "ThumbnailRendering/SceneThumbnailInfoWithPrimitive.h"
#include "Materials/MaterialInstanceConstant.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UThumbnailInfo* UAssetDefinition_MaterialInterface::LoadThumbnailInfo(const FAssetData& InAsset) const
{
	UMaterialInterface* MaterialInterface = CastChecked<UMaterialInterface>(InAsset.GetAsset());
	USceneThumbnailInfoWithPrimitive* ThumbnailInfo = Cast<USceneThumbnailInfoWithPrimitive>(MaterialInterface->ThumbnailInfo);
	if ( ThumbnailInfo == nullptr )
	{
		ThumbnailInfo = NewObject<USceneThumbnailInfoWithPrimitive>(MaterialInterface, NAME_None, RF_Transactional);
		MaterialInterface->ThumbnailInfo = ThumbnailInfo;
	}
	
	const UMaterial* Material = MaterialInterface->GetBaseMaterial();
	if (Material && Material->bUsedWithParticleSprites)
    {
    	ThumbnailInfo->DefaultPrimitiveType = TPT_Plane;
    }

	return ThumbnailInfo;
}

// Menu Extensions
//--------------------------------------------------------------------

namespace MenuExtension_MaterialInterface
{
	static void ExecuteNewMIC(const FToolMenuContext& MenuContext)
	{
		const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(MenuContext);

		IAssetTools::Get().CreateAssetsFrom<UMaterialInterface>(
			CBContext->LoadSelectedObjects<UMaterialInterface>(), UMaterialInstanceConstant::StaticClass(), TEXT("_Inst"), [](UMaterialInterface* SourceObject)
			{
				UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
				Factory->InitialParent = SourceObject;
				return Factory;
			}
		);
	}
	
	static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []{ 
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);
			UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UMaterialInterface::StaticClass());
	        
			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			Section.AddDynamicEntry("GetAssetActions_UMaterialInterface", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				{
					const TAttribute<FText> Label = LOCTEXT("Material_NewMIC", "Create Material Instance");
					const TAttribute<FText> ToolTip = LOCTEXT("Material_NewMICTooltip", "Creates a parameterized material using this material as a base.");
					const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.MaterialInstanceActor");
					const FToolMenuExecuteAction UIAction = FToolMenuExecuteAction::CreateStatic(&ExecuteNewMIC);

					InSection.AddMenuEntry("Material_NewMIC", Label, ToolTip, Icon, UIAction);
				}
			}));
		}));
	});
}


#undef LOCTEXT_NAMESPACE
