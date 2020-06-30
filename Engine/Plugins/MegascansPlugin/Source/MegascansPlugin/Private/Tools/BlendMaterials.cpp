#include "Tools/BlendMaterials.h"
#include "EditorAssetLibrary.h"
#include "Utilities/MiscUtils.h"
#include "Utilities/MaterialUtils.h"
#include "UI/MSSettings.h"

#include "Editor/UnrealEd/Classes/Factories/MaterialInstanceConstantFactoryNew.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstanceConstant.h"
#include "Runtime/Engine/Classes/Engine/Texture.h"
#include "Runtime/CoreUObject/Public/Misc/PackageName.h"
#include "MaterialEditingLibrary.h"

#include "Runtime/Core/Public/Misc/MessageDialog.h"
#include "Runtime/Core/Public/Internationalization/Text.h"


TSharedPtr<FMaterialBlend> FMaterialBlend::MaterialBlendInst;




TSharedPtr<FMaterialBlend> FMaterialBlend::Get()
{
	if (!MaterialBlendInst.IsValid())
	{
		MaterialBlendInst = MakeShareable(new FMaterialBlend);
	}
	return MaterialBlendInst;
}

void FMaterialBlend::BlendSelectedMaterials()
{
	FString FailueReason;
	const UMaterialBlendSettings* BlendSettings = GetDefault<UMaterialBlendSettings>();

	TArray<UMaterialInstanceConstant*> SelectedMaterialInstances = AssetUtils::GetSelectedAssets<UMaterialInstanceConstant>(TEXT("MaterialInstanceConstant"));

	if (SelectedMaterialInstances.Num() < 2)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText(FText::FromString("Select two or more material instances to perform this operation.")));
		return;
	}
	if (SelectedMaterialInstances.Num() > 4)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText(FText::FromString("Current Material Blending setup doesn't support more than 4 material instances.")));
		return;
		// Not supported number
	}

	FString MasterMaterialPath = GetMaterial(DBlendMasterMaterial);

	if (MasterMaterialPath == TEXT(""))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText(FText::FromString("Master Material for blending was not found. Cancelling operation.")));
		return;
		// Master blend material not found
	}
	
	FString BlendInstanceName;
	FString BlendDestinationPath;

	if (BlendSettings->BlendedMaterialName != TEXT(""))
	{
		BlendInstanceName = SanitizeName(BlendSettings->BlendedMaterialName);
	}

	if (BlendSettings->BlendedMaterialPath.Path != "")
	{
		BlendDestinationPath = BlendSettings->BlendedMaterialPath.Path;
	}

	if (!FPackageName::IsValidObjectPath(FPaths::Combine(BlendDestinationPath, BlendInstanceName)))
	{
		BlendInstanceName = DBlendInstanceName;
		BlendDestinationPath = DBlendDestinationPath;
	}

	
	//BlendInstanceName = GetUniqueAssetName(BlendDestinationPath, BlendInstanceName, true);
	if (UEditorAssetLibrary::DoesAssetExist(FPaths::Combine(BlendDestinationPath, BlendInstanceName)))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText(FText::FromString("A Blend material with same name already exists. Please choose a different name.")));
		return;
	}

	UMaterialInstanceConstant* InstancedBlendMaterial = FMaterialUtils::CreateInstanceMaterial(MasterMaterialPath, BlendDestinationPath, BlendInstanceName);

	if (InstancedBlendMaterial == nullptr)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText(FText::FromString("An error ocurred while creating Blend material.")));
		return;
	}

	auto BlendSetIt = BlendSets.CreateConstIterator();
	for (UMaterialInstanceConstant* SelectedInstance : SelectedMaterialInstances)
	{
		
		for (FString MapType : SupportedMapTypes)
		{
			UTexture* PluggedMap = UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(SelectedInstance, FName(*MapType));
			if (PluggedMap)
			{
				FName TargetParameterName = *(*BlendSetIt + TEXT(" ") + MapType);
				UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(InstancedBlendMaterial, TargetParameterName, PluggedMap);
			}
		}

		++BlendSetIt;
	}


}

bool FMaterialBlend::ValidateSelectedAssets(TArray<FString> SelectedMaterials, FString& Failure)
{
	for (FString MaterialPath : SelectedMaterials)
	{

	}
	return false;
}


