#include "UI/MSSettings.h"



UMegascansSettings::UMegascansSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer) , bCreateFoliage(true), bEnableLods(true), bBatchImportPrompt(false), bEnableDisplacement(false), bApplyToSelection(false)

{
	
}


UMaterialBlendSettings::UMaterialBlendSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), BlendedMaterialName(TEXT("BlendMaterial"))

{
	BlendedMaterialPath.Path = TEXT("/Game/BlendMaterials");
}

UMaterialAssetSettings::UMaterialAssetSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UMaterialPresetsSettings::UMaterialPresetsSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
}

