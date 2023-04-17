// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeMaterialXPipelineCustomizations.h"

#include "InterchangeMaterialDefinitions.h"
#include "InterchangeMaterialXPipeline.h"
#include "Materials/MaterialFunction.h"

#include "DetailLayoutBuilder.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
//#include "DetailWidgetRow.h"
//#include "IDetailGroup.h"
//#include "InterchangePipelineBase.h"
//#include "Nodes/InterchangeBaseNode.h"
//#include "ScopedTransaction.h"
//#include "Styling/StyleColors.h"
//#include "Widgets/Input/SNumericEntryBox.h"
//#include "Widgets/Layout/SBox.h"

TSharedRef<IDetailCustomization> FInterchangeMaterialXPipelineCustomization::MakeInstance()
{
	return MakeShareable(new FInterchangeMaterialXPipelineCustomization());
}

TSharedRef<IDetailCustomization> FInterchangeMaterialXPipelineSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FInterchangeMaterialXPipelineSettingsCustomization());
}

void FInterchangeMaterialXPipelineCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> EditingObjects;
	DetailBuilder.GetObjectsBeingCustomized(EditingObjects);
	check(EditingObjects.Num() == 1);

	TWeakObjectPtr<UInterchangeMaterialXPipeline> Pipeline = Cast<UInterchangeMaterialXPipeline>(EditingObjects[0].Get());

	if (!ensure(Pipeline.IsValid()))
	{
		return;
	}

	IDetailCategoryBuilder& MaterialXCategory = DetailBuilder.EditCategory("MaterialX");

	MaterialXCategory.SetDisplayName(NSLOCTEXT("InterchangeMaterialXPipelineCustomization", "CategoryDisplayName", "MaterialX Settings"));

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	DetailsView->SetObject(Pipeline->MaterialXSettings);

	MaterialXCategory.AddCustomRow(NSLOCTEXT("InterchangeMaterialXPipelineCustomization", "MaterialXPredefined", "Predefined Material Functions"))
	[
		SNew( SVerticalBox )
		+ SVerticalBox::Slot()
		[
			DetailsView.ToSharedRef()
		]
	];
}

void FInterchangeMaterialXPipelineSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using OnShouldFilterAssetFunc = bool (FInterchangeMaterialXPipelineSettingsCustomization::*)(const FAssetData&);

	TArray<TWeakObjectPtr<UObject>> EditingObjects;
	DetailBuilder.GetObjectsBeingCustomized(EditingObjects);
	check(EditingObjects.Num() == 1);

	MaterialXSettings = Cast<UMaterialXPipelineSettings>(EditingObjects[0].Get());

	if (!ensure(MaterialXSettings.IsValid()))
	{
		return;
	}

	TSharedRef< IPropertyHandle > PairingsHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMaterialXPipelineSettings, PredefinedSurfaceShaders));
	if (!PairingsHandle->IsValidHandle())
	{
		return;
	}

	DetailBuilder.HideProperty(PairingsHandle);

	IDetailCategoryBuilder& MaterialXPredefinedCategory = DetailBuilder.EditCategory("MaterialXPredefined");

	MaterialXPredefinedCategory.SetDisplayName(NSLOCTEXT("InterchangeMaterialXPipelineSettingsCustomization", "CategoryDisplayName", "MaterialX Predefined Surface Shaders"));

	uint32 NumChildren = 0;
	PairingsHandle->GetNumChildren(NumChildren);

	for(uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildPropertyHandle = PairingsHandle->GetChildHandle(i);
		TSharedPtr<IPropertyHandle> KeyPropertyHandle = ChildPropertyHandle->GetKeyHandle();

		OnShouldFilterAssetFunc OnShouldFilterAsset = &FInterchangeMaterialXPipelineSettingsCustomization::OnShouldFilterAsset;

		if(KeyPropertyHandle.IsValid())
		{
			FString Str;
			KeyPropertyHandle->GetValueAsDisplayString(Str);
			if(Str == TEXT("Standard Surface"))
			{
				OnShouldFilterAsset = &FInterchangeMaterialXPipelineSettingsCustomization::OnShouldFilterAssetStandardSurface;
			}
			else if(Str == TEXT("Standard Surface Transmission"))
			{
				OnShouldFilterAsset = &FInterchangeMaterialXPipelineSettingsCustomization::OnShouldFilterAssetStandardSurfaceTransmission;
			}
			else if(Str == TEXT("Surface Unlit"))
			{
				OnShouldFilterAsset = &FInterchangeMaterialXPipelineSettingsCustomization::OnShouldFilterAssetSurfaceUnlit;
			}
			else if(Str == TEXT("Usd Preview Surface"))
			{
				OnShouldFilterAsset = &FInterchangeMaterialXPipelineSettingsCustomization::OnShouldFilterAssetUsdPreviewSurface;
			}
		}

		IDetailPropertyRow& SurfaceShaderTypeRow = DetailBuilder.AddPropertyToCategory(ChildPropertyHandle.ToSharedRef());
		SurfaceShaderTypeRow.ShowPropertyButtons(false);

		FDetailWidgetRow& DetailWidgetRow = SurfaceShaderTypeRow.CustomWidget();
		DetailWidgetRow
		.NameContent()
		[
			SNullWidget::NullWidget
		]
		.ValueContent()
		.MinDesiredWidth(1.0f)
		.MaxDesiredWidth(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UMaterialFunction::StaticClass())
				.PropertyHandle(ChildPropertyHandle)
				.OnShouldFilterAsset(this, OnShouldFilterAsset)
			]
		];
	}

	DetailBuilder.HideCategory(GET_MEMBER_NAME_CHECKED(UMaterialXPipelineSettings, PredefinedSurfaceShaders));
}

bool FInterchangeMaterialXPipelineSettingsCustomization::OnShouldFilterAssetStandardSurface(const FAssetData & InAssetData)
{
	return UMaterialXPipelineSettings::ShouldFilterAssets(Cast<UMaterialFunction>(InAssetData.GetAsset()), UMaterialXPipelineSettings::StandardSurfaceInputs, UMaterialXPipelineSettings::StandardSurfaceOutputs);
}

bool FInterchangeMaterialXPipelineSettingsCustomization::OnShouldFilterAssetStandardSurfaceTransmission(const FAssetData& InAssetData)
{
	return UMaterialXPipelineSettings::ShouldFilterAssets(Cast<UMaterialFunction>(InAssetData.GetAsset()), UMaterialXPipelineSettings::TransmissionSurfaceInputs, UMaterialXPipelineSettings::TransmissionSurfaceOutputs);
}

bool FInterchangeMaterialXPipelineSettingsCustomization::OnShouldFilterAssetSurfaceUnlit(const FAssetData& InAssetData)
{
	return UMaterialXPipelineSettings::ShouldFilterAssets(Cast<UMaterialFunction>(InAssetData.GetAsset()), UMaterialXPipelineSettings::SurfaceUnlitInputs, UMaterialXPipelineSettings::SurfaceUnlitOutputs);
}

bool FInterchangeMaterialXPipelineSettingsCustomization::OnShouldFilterAssetUsdPreviewSurface(const FAssetData& InAssetData)
{
	return UMaterialXPipelineSettings::ShouldFilterAssets(Cast<UMaterialFunction>(InAssetData.GetAsset()), UMaterialXPipelineSettings::UsdPreviewSurfaceInputs, UMaterialXPipelineSettings::UsdPreviewSurfaceOutputs);
}
