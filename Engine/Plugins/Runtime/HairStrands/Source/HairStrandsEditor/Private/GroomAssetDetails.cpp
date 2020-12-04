// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomAssetDetails.h"
#include "Widgets/Input/SCheckBox.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "IDetailGroup.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/SToolTip.h"
#include "IDocumentation.h"
#include "GroomComponent.h"
#include "SlateOptMacros.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorDirectories.h"
#include "UnrealEdGlobals.h"
#include "IDetailsView.h"
#include "MaterialList.h"
#include "PropertyCustomizationHelpers.h"
#include "Interfaces/IMainFrameModule.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Rendering/SkeletalMeshModel.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Logging/LogMacros.h"

#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "MeshAttributeArray.h"

#include "Widgets/Input/STextComboBox.h"

#include "Widgets/Input/SNumericEntryBox.h"
#include "IDocumentation.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "ComponentReregisterContext.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "SKismetInspector.h"
#include "PropertyEditorDelegates.h"
#include "PropertyCustomizationHelpers.h"
#include "GroomCustomAssetEditorToolkit.h"
#include "IPropertyUtilities.h"

static FLinearColor HairGroupColor(1.0f, 0.5f, 0.0f);
static FLinearColor HairLODColor(1.0f, 0.5f, 0.0f);

#define LOCTEXT_NAMESPACE "GroomRenderingDetails"


///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Array panel for hair strands infos
TSharedRef<SUniformGridPanel> MakeHairStrandsInfoGrid(const FSlateFontInfo& DetailFontInfo, FHairGroupInfoWithVisibility& CurrentAsset)
{
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(2.0f);

	// Header
	Grid->AddSlot(1, 0) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(LOCTEXT("HairInfo_Curves", "Curves"))
	];

	Grid->AddSlot(2, 0) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(LOCTEXT("HairInfo_Vertices", "Vertices"))
	];

	// Strands
	Grid->AddSlot(0, 1) // x, y
	.HAlign(HAlign_Left)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(LOCTEXT("HairInfo_Strands", "Strands"))
	];
	Grid->AddSlot(1, 1) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(FText::AsNumber(CurrentAsset.NumCurves))
	];
	Grid->AddSlot(2, 1) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(FText::AsNumber(CurrentAsset.NumCurveVertices))
	];

	// Guides
	Grid->AddSlot(0, 2) // x, y
	.HAlign(HAlign_Left)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(LOCTEXT("HairInfo_Guides", "Guides"))
	];
	Grid->AddSlot(1, 2) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(FText::AsNumber(CurrentAsset.NumGuides))
	];
	Grid->AddSlot(2, 2) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(FText::AsNumber(CurrentAsset.NumGuideVertices))
	];

	return Grid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Array panel for hair cards infos

TSharedRef<SUniformGridPanel> MakeHairCardsInfoGrid(const FSlateFontInfo& DetailFontInfo, FHairGroupCardsInfo& CardsInfo)
{
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(2.0f);

	// Header
	Grid->AddSlot(0, 0) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(LOCTEXT("HairCardsInfo_Curves", "Cards"))
	];

	Grid->AddSlot(1, 0) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(LOCTEXT("HairCardsInfo_Vertices", "Vertices"))
	];

	// Strands
	Grid->AddSlot(0, 1) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(FText::AsNumber(CardsInfo.NumCards))
	];
	Grid->AddSlot(1, 1) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(FText::AsNumber(CardsInfo.NumCardVertices))
	];

	return Grid;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Details view

FGroomRenderingDetails::FGroomRenderingDetails(IGroomCustomAssetEditorToolkit* InToolkit, EMaterialPanelType Type)
{
	if (InToolkit)
	{
		GroomAsset = InToolkit->GetCustomAsset();
	}
	bDeleteWarningConsumed = false;
	PanelType = Type;
}

FGroomRenderingDetails::~FGroomRenderingDetails()
{

}

TSharedRef<IDetailCustomization> FGroomRenderingDetails::MakeInstance(IGroomCustomAssetEditorToolkit* InToolkit, EMaterialPanelType Type)
{
	return MakeShareable(new FGroomRenderingDetails(InToolkit, Type));
}

FName GetCategoryName(EMaterialPanelType Type)
{
	switch (Type) 
	{
	case EMaterialPanelType::Strands:		return FName(TEXT("Strands"));
	case EMaterialPanelType::Cards:			return FName(TEXT("Cards"));
	case EMaterialPanelType::Meshes:		return FName(TEXT("Meshes"));
	case EMaterialPanelType::Interpolation: return FName(TEXT("Interpolation"));
	case EMaterialPanelType::LODs:			return FName(TEXT("LODs"));
	case EMaterialPanelType::Physics:		return FName(TEXT("Physics"));
	}
	return FName(TEXT("Unknown"));
}

void FGroomRenderingDetails::ApplyChanges()
{
	GroomDetailLayout->ForceRefreshDetails();
}

void FGroomRenderingDetails::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = DetailLayout.GetSelectedObjects();
	check(SelectedObjects.Num() <= 1); // The OnGenerateCustomWidgets delegate will not be useful if we try to process more than one object.

	GroomAsset = SelectedObjects.Num() > 0 ? Cast<UGroomAsset>(SelectedObjects[0].Get()) : nullptr;
	GroomDetailLayout = &DetailLayout;

	FName CategoryName = GetCategoryName(PanelType);
	IDetailCategoryBuilder& HairGroupCategory = DetailLayout.EditCategory(CategoryName, FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	CustomizeStrandsGroupProperties(DetailLayout, HairGroupCategory);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Custom widget for material slot for hair rendering

void FGroomRenderingDetails::AddNewGroupButton(IDetailCategoryBuilder& FilesCategory, FProperty* Property)
{
	// Add a button for adding element to the hair groups array
	FilesCategory.AddCustomRow(FText::FromString(TEXT("AddGroup")))
	.ValueContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.OnClicked(this, &FGroomRenderingDetails::OnAddGroup, Property)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Plus"))
			]
		]
	];
}

void FGroomRenderingDetails::CustomizeStrandsGroupProperties(IDetailLayoutBuilder& DetailLayout, IDetailCategoryBuilder& FilesCategory)
{
	switch (PanelType)
	{
	case EMaterialPanelType::Cards:
	{
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsRendering), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsPhysics), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsCards), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMeshes), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMaterials), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInfo), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, EnableGlobalInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairInterpolationType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, MinLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, DisableBelowMinLodStripping), UGroomAsset::StaticClass()));
	}
	break;
	case EMaterialPanelType::Meshes:
	{
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsRendering), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsPhysics), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsCards), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMeshes), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMaterials), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInfo), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, EnableGlobalInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairInterpolationType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, MinLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, DisableBelowMinLodStripping), UGroomAsset::StaticClass()));
	}
	break;
	case EMaterialPanelType::Strands:
	{
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInterpolation), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsRendering), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsPhysics), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsCards), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMeshes), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMaterials), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInfo), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, EnableGlobalInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairInterpolationType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, MinLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, DisableBelowMinLodStripping), UGroomAsset::StaticClass()));
	}
	break;
	case EMaterialPanelType::Physics:
	{
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsRendering), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsPhysics), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsCards), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMeshes), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMaterials), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInfo), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, EnableGlobalInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairInterpolationType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, MinLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, DisableBelowMinLodStripping), UGroomAsset::StaticClass()));
	}
	break;
	case EMaterialPanelType::Interpolation:
	{
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsRendering), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsPhysics), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsCards), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMeshes), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMaterials), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInfo), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, EnableGlobalInterpolation), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairInterpolationType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, MinLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, DisableBelowMinLodStripping), UGroomAsset::StaticClass()));
	}
	break;
	case EMaterialPanelType::LODs:
	{
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsRendering), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsPhysics), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsCards), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMeshes), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMaterials), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsLOD), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInfo), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, EnableGlobalInterpolation), UGroomAsset::StaticClass()));
		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairInterpolationType), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, LODSelectionType), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, MinLOD), UGroomAsset::StaticClass()));
//		DetailLayout.HideProperty(DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, DisableBelowMinLodStripping), UGroomAsset::StaticClass()));
	}
	break;
	}

	switch (PanelType)
	{
		case EMaterialPanelType::Cards:		
		{
			TSharedRef<IPropertyHandle> Property = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsCards), UGroomAsset::StaticClass());
			AddNewGroupButton(FilesCategory, Property->GetProperty());
			if (Property->IsValidHandle())
			{
				TSharedRef<FDetailArrayBuilder> PropertyBuilder = MakeShareable(new FDetailArrayBuilder(Property, false, false, false));
				PropertyBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FGroomRenderingDetails::OnGenerateElementForHairGroup, &DetailLayout));
				PropertyBuilder->SetDisplayName(FText::FromString(TEXT("Cards assets")));
				FilesCategory.AddCustomBuilder(PropertyBuilder, false);
			}
		}
		break;
		case EMaterialPanelType::Meshes:	
		{
			TSharedRef<IPropertyHandle> Property = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsMeshes), UGroomAsset::StaticClass());
			AddNewGroupButton(FilesCategory, Property->GetProperty());
			if (Property->IsValidHandle())
			{
				TSharedRef<FDetailArrayBuilder> PropertyBuilder = MakeShareable(new FDetailArrayBuilder(Property, false, false, false));
				PropertyBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FGroomRenderingDetails::OnGenerateElementForHairGroup, &DetailLayout));
				PropertyBuilder->SetDisplayName(FText::FromString(TEXT("Meshes assets")));
				FilesCategory.AddCustomBuilder(PropertyBuilder, false);
			}
		}
		break;
		case EMaterialPanelType::Strands:	
		{
			TSharedRef<IPropertyHandle> Property = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsRendering), UGroomAsset::StaticClass());
			if (Property->IsValidHandle())
			{
				TSharedRef<FDetailArrayBuilder> PropertyBuilder = MakeShareable(new FDetailArrayBuilder(Property, false, false, false));
				PropertyBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FGroomRenderingDetails::OnGenerateElementForHairGroup, &DetailLayout));
				PropertyBuilder->SetDisplayName(FText::FromString(TEXT("Strands Groups")));
				FilesCategory.AddCustomBuilder(PropertyBuilder, false);
			}
		}
		break;
		case EMaterialPanelType::Physics:
		{
			TSharedRef<IPropertyHandle> Property = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsPhysics), UGroomAsset::StaticClass());
			if (Property->IsValidHandle())
			{
				TSharedRef<FDetailArrayBuilder> PropertyBuilder = MakeShareable(new FDetailArrayBuilder(Property, false, false, false));
				PropertyBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FGroomRenderingDetails::OnGenerateElementForHairGroup, &DetailLayout));
				PropertyBuilder->SetDisplayName(FText::FromString(TEXT("Physics Groups")));
				FilesCategory.AddCustomBuilder(PropertyBuilder, false);
			}
		}
		break;
		case EMaterialPanelType::Interpolation:
		{
			TSharedRef<IPropertyHandle> Property = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsInterpolation), UGroomAsset::StaticClass());
			if (Property->IsValidHandle())
			{
				TSharedRef<FDetailArrayBuilder> PropertyBuilder = MakeShareable(new FDetailArrayBuilder(Property, false, false, false));
				PropertyBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FGroomRenderingDetails::OnGenerateElementForHairGroup, &DetailLayout));
				PropertyBuilder->SetDisplayName(FText::FromString(TEXT("Interpolation Groups")));
				FilesCategory.AddCustomBuilder(PropertyBuilder, false);
			}
		}
		break;
		case EMaterialPanelType::LODs:
		{
			TSharedRef<IPropertyHandle> Property = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomAsset, HairGroupsLOD), UGroomAsset::StaticClass());
			if (Property->IsValidHandle())
			{
				TSharedRef<FDetailArrayBuilder> PropertyBuilder = MakeShareable(new FDetailArrayBuilder(Property, false, false, false));
				PropertyBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FGroomRenderingDetails::OnGenerateElementForHairGroup, &DetailLayout));
				PropertyBuilder->SetDisplayName(FText::FromString(TEXT("LOD Groups")));
				FilesCategory.AddCustomBuilder(PropertyBuilder, false);
			}
		}
		break;
	}
}

FReply FGroomRenderingDetails::OnAddGroup(FProperty* Property)
{
	check(GroomAsset);
	switch (PanelType)
	{
	case EMaterialPanelType::Cards:
	{
		FScopedTransaction Transaction(FText::FromString(TEXT("AddCardsGroup")));
		GroomAsset->HairGroupsCards.AddDefaulted();

		FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ArrayAdd);
		GroomAsset->PostEditChangeProperty(PropertyChangedEvent);
	}
	break;
	case EMaterialPanelType::Meshes:
	{
		FScopedTransaction Transaction(FText::FromString(TEXT("AddMeshesGroup")));
		GroomAsset->HairGroupsMeshes.AddDefaulted();

		FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ArrayAdd);
		GroomAsset->PostEditChangeProperty(PropertyChangedEvent);
	}
	break;
	}

	return FReply::Handled();
}

FName& FGroomRenderingDetails::GetMaterialSlotName(int32 GroupIndex)
{
	check(GroomAsset);
	switch (PanelType)
	{
		case EMaterialPanelType::Cards:		return GroomAsset->HairGroupsCards[GroupIndex].MaterialSlotName;
		case EMaterialPanelType::Meshes:	return GroomAsset->HairGroupsMeshes[GroupIndex].MaterialSlotName;
		case EMaterialPanelType::Strands:	return GroomAsset->HairGroupsRendering[GroupIndex].MaterialSlotName;
	}

	static FName Default;
	return Default;
}

const FName& FGroomRenderingDetails::GetMaterialSlotName(int32 GroupIndex) const
{
	static const FName Default(TEXT("Invalid"));
	check(GroomAsset);
	switch (PanelType)
	{
	case EMaterialPanelType::Cards:		return GroupIndex < GroomAsset->HairGroupsCards.Num() ? GroomAsset->HairGroupsCards[GroupIndex].MaterialSlotName : Default;
	case EMaterialPanelType::Meshes:	return GroupIndex < GroomAsset->HairGroupsMeshes.Num() ? GroomAsset->HairGroupsMeshes[GroupIndex].MaterialSlotName : Default;
	case EMaterialPanelType::Strands:	return GroupIndex < GroomAsset->HairGroupsRendering.Num() ? GroomAsset->HairGroupsRendering[GroupIndex].MaterialSlotName : Default;
	}

	return Default;
}

int32 FGroomRenderingDetails::GetGroupCount() const
{
	check(GroomAsset);
	switch (PanelType)
	{
	case EMaterialPanelType::Cards:			return GroomAsset->HairGroupsCards.Num();
	case EMaterialPanelType::Meshes:		return GroomAsset->HairGroupsMeshes.Num();
	case EMaterialPanelType::Strands:		return GroomAsset->HairGroupsRendering.Num();
	case EMaterialPanelType::Physics:		return GroomAsset->HairGroupsPhysics.Num();
	case EMaterialPanelType::Interpolation:	return GroomAsset->HairGroupsInterpolation.Num();
	case EMaterialPanelType::LODs:			return GroomAsset->HairGroupsLOD.Num();
	}

	return 0;
}

FReply FGroomRenderingDetails::OnRemoveGroupClicked(int32 GroupIndex, FProperty* Property)
{
	check(GroomAsset);
	switch (PanelType)
	{
	case EMaterialPanelType::Cards:		
	{
		if (GroupIndex < GroomAsset->HairGroupsCards.Num())
		{
			FScopedTransaction Transaction(FText::FromString(TEXT("RemoveCardsGroup")));
			GroomAsset->HairGroupsCards.RemoveAt(GroupIndex);

			FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ArrayRemove);
			GroomAsset->PostEditChangeProperty(PropertyChangedEvent);
		}
	}
	break;
	case EMaterialPanelType::Meshes:
	{
		if (GroupIndex < GroomAsset->HairGroupsMeshes.Num())
		{
			FScopedTransaction Transaction(FText::FromString(TEXT("RemoveMeshesGroup")));
			GroomAsset->HairGroupsMeshes.RemoveAt(GroupIndex);

			FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ArrayRemove);
			GroomAsset->PostEditChangeProperty(PropertyChangedEvent);
		}
	}
	break;
	}

	return FReply::Handled();
}

void FGroomRenderingDetails::SetMaterialSlot(int32 GroupIndex, int32 MaterialIndex)
{
	if (!GroomAsset || GroupIndex < 0)
	{
		return;
	}

	int32 GroupCount = GetGroupCount();
	if (MaterialIndex == INDEX_NONE)
	{
		GetMaterialSlotName(GroupIndex) = NAME_None;
	}
	else if (GroupIndex < GroupCount && MaterialIndex >= 0 && MaterialIndex < GroomAsset->HairGroupsMaterials.Num())
	{
		GetMaterialSlotName(GroupIndex) = GroomAsset->HairGroupsMaterials[MaterialIndex].SlotName;
	}

	GroomAsset->MarkMaterialsHasChanged();
}

TSharedRef<SWidget> FGroomRenderingDetails::OnGenerateStrandsMaterialMenuPicker(int32 GroupIndex)
{
	if (GroomAsset == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	const int32 MaterialCount = GroomAsset->HairGroupsMaterials.Num();
	if(MaterialCount == 0)
	{
		return SNullWidget::NullWidget;
	}
	FMenuBuilder MenuBuilder(true, NULL);

	// Default material
	{
		int32 MaterialIt = INDEX_NONE;
		FText DefaultString = FText::FromString(TEXT("Default"));
		FUIAction Action(FExecuteAction::CreateSP(this, &FGroomRenderingDetails::SetMaterialSlot, GroupIndex, MaterialIt));
		MenuBuilder.AddMenuEntry(DefaultString, FText::GetEmpty(), FSlateIcon(), Action);
	}

	// Add a menu item for material
	for (int32 MaterialIt = 0; MaterialIt < MaterialCount; ++MaterialIt)
	{
		FText MaterialString = FText::FromString(FString::FromInt(MaterialIt) + TEXT(" - ") + GroomAsset->HairGroupsMaterials[MaterialIt].SlotName.ToString());
		FUIAction Action(FExecuteAction::CreateSP(this, &FGroomRenderingDetails::SetMaterialSlot, GroupIndex, MaterialIt));
		MenuBuilder.AddMenuEntry(MaterialString, FText::GetEmpty(), FSlateIcon(), Action);
	}

	return MenuBuilder.MakeWidget();
}

FText FGroomRenderingDetails::GetStrandsMaterialName(int32 GroupIndex) const
{
	const FName& MaterialSlotName = GetMaterialSlotName(GroupIndex);
	const int32 MaterialIndex = GroomAsset->GetMaterialIndex(MaterialSlotName);
	FText MaterialString = FText::FromString(TEXT("Default"));
	if (MaterialIndex != INDEX_NONE)
	{
		MaterialString = FText::FromString(FString::FromInt(MaterialIndex) + TEXT(" - ") + GroomAsset->HairGroupsMaterials[MaterialIndex].SlotName.ToString());
	}
	return MaterialString;
}

TSharedRef<SWidget> FGroomRenderingDetails::OnGenerateStrandsMaterialPicker(int32 GroupIndex, IDetailLayoutBuilder* DetailLayoutBuilder)
{
	return CreateMaterialSwatch(DetailLayoutBuilder->GetThumbnailPool(), GroupIndex);
}

bool FGroomRenderingDetails::IsStrandsMaterialPickerEnabled(int32 GroupIndex) const
{
	return true;
}

static void ExpandStruct(TSharedPtr<IPropertyHandle>& PropertyHandle, IDetailChildrenBuilder& ChildrenBuilder)
{
	uint32 ChildrenCount = 0;
	PropertyHandle->GetNumChildren(ChildrenCount);
	for (uint32 ChildIt = 0; ChildIt < ChildrenCount; ++ChildIt)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(ChildIt);
		ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
	}
}

static void ExpandStruct(TSharedRef<IPropertyHandle>& PropertyHandle, IDetailChildrenBuilder& ChildrenBuilder)
{
	uint32 ChildrenCount = 0;
	PropertyHandle->GetNumChildren(ChildrenCount);
	for (uint32 ChildIt = 0; ChildIt < ChildrenCount; ++ChildIt)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(ChildIt);
		ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
	}
}

FReply FGroomRenderingDetails::OnRemoveLODClicked(int32 GroupIndex, int32 LODIndex, FProperty* Property)
{
	check(GroomAsset);
	if (GroupIndex < GroomAsset->HairGroupsLOD.Num() && LODIndex >= 0 && LODIndex < GroomAsset->HairGroupsLOD[GroupIndex].LODs.Num() && GroomAsset->HairGroupsLOD[GroupIndex].LODs.Num() > 1)
	{
		FScopedTransaction Transaction(FText::FromString(TEXT("RemoveLOD")));

		GroomAsset->HairGroupsLOD[GroupIndex].LODs.RemoveAt(LODIndex);

		FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ArrayRemove);
		GroomAsset->PostEditChangeProperty(PropertyChangedEvent);
	}
	return FReply::Handled();
}

FReply FGroomRenderingDetails::OnAddLODClicked(int32 GroupIndex, FProperty* Property)
{
	const int32 MaxLODCount = 8; 
	if (GroupIndex < GroomAsset->HairGroupsLOD.Num() && GroomAsset->HairGroupsLOD[GroupIndex].LODs.Num() < MaxLODCount)
	{
		FScopedTransaction Transaction(FText::FromString(TEXT("AddLOD")));

		GroomAsset->HairGroupsLOD[GroupIndex].LODs.AddDefaulted();
		const int32 LODCount = GroomAsset->HairGroupsLOD[GroupIndex].LODs.Num();
		if (LODCount > 1)
		{
			const FHairLODSettings& PrevLODSettings = GroomAsset->HairGroupsLOD[GroupIndex].LODs[LODCount-2];
			FHairLODSettings& LODSettings = GroomAsset->HairGroupsLOD[GroupIndex].LODs[LODCount-1];

			// Prefill the LOD setting with basic preset
			LODSettings.VertexDecimation = PrevLODSettings.VertexDecimation * 0.5f;
			LODSettings.AngularThreshold = PrevLODSettings.AngularThreshold * 2.f;
			LODSettings.CurveDecimation = PrevLODSettings.CurveDecimation * 0.5f;
			LODSettings.ScreenSize = PrevLODSettings.ScreenSize * 0.5f;
			LODSettings.GeometryType = PrevLODSettings.GeometryType;
		}

		FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ArrayAdd);
		GroomAsset->PostEditChangeProperty(PropertyChangedEvent);
	}
	return FReply::Handled();
}

FReply FGroomRenderingDetails::OnRefreshCards(int32 GroupIndex, FProperty* Property)
{
	if (GroupIndex < GroomAsset->HairGroupsCards.Num())
	{
		FScopedTransaction Transaction(FText::FromString(TEXT("RefreshCards")));
		
		FPropertyChangedEvent PropertyChangedEvent(Property);
		GroomAsset->HairGroupsCards[GroupIndex].ProceduralSettings.Version++;
		GroomAsset->PostEditChangeProperty(PropertyChangedEvent);
	}
	return FReply::Handled();
}

FReply FGroomRenderingDetails::OnSaveCards(int32 GroupIndex, FProperty* Property)
{
	if (GroupIndex < GroomAsset->HairGroupsCards.Num())
	{
		GroomAsset->SaveProceduralCards(GroupIndex);
	}
	return FReply::Handled();
}

void FGroomRenderingDetails::AddLODSlot(TSharedRef<IPropertyHandle>& LODHandle, IDetailChildrenBuilder& ChildrenBuilder, int32 GroupIndex, int32 LODIndex)
{	
	ExpandStruct(LODHandle, ChildrenBuilder);
}

void FGroomRenderingDetails::OnGenerateElementForLODs(TSharedRef<IPropertyHandle> StructProperty, int32 LODIndex, IDetailChildrenBuilder& ChildrenBuilder, IDetailLayoutBuilder* DetailLayout, int32 GroupIndex)
{
	const FSlateFontInfo DetailFontInfo = IDetailLayoutBuilder::GetDetailFont();
	FProperty* Property = StructProperty->GetProperty();

	ChildrenBuilder.AddCustomRow(LOCTEXT("HairInfo_Separator", "Separator"))
	.ValueContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SSeparator)
			.Thickness(2)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(HairLODColor)
			.Text(FText::Format(LOCTEXT("LOD", "LOD {0}"), FText::AsNumber(LODIndex)))
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SSeparator)
			.Thickness(2)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.OnClicked(this, &FGroomRenderingDetails::OnRemoveLODClicked, GroupIndex, LODIndex, Property)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Cross"))
			]
		]
	];

	// Rename the array entry name by its group name and adds all its existing properties
	StructProperty->SetPropertyDisplayName(LOCTEXT("LODProperties", "LOD Properties"));
	ExpandStruct(StructProperty, ChildrenBuilder);
}

TSharedRef<SWidget> FGroomRenderingDetails::MakeGroupNameButtonCustomization(int32 GroupIndex, FProperty* Property)
{
	switch (PanelType)
	{
	case EMaterialPanelType::LODs:
	{
		return SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.OnClicked(this, &FGroomRenderingDetails::OnAddLODClicked, GroupIndex, Property)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Plus"))
			];
	}
	break;
	case EMaterialPanelType::Cards:
	{
		return SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.OnClicked(this, &FGroomRenderingDetails::OnRemoveGroupClicked, GroupIndex, Property)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Cross"))
			];
	}
	break;
	case EMaterialPanelType::Meshes:
	{
		return SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.OnClicked(this, &FGroomRenderingDetails::OnRemoveGroupClicked, GroupIndex, Property)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Cross"))
			];
	}
	break;
	}

	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> FGroomRenderingDetails::MakeGroupNameCustomization(int32 GroupIndex)
{
	switch (PanelType)
	{
	case EMaterialPanelType::Cards:
	{
		return SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(HairGroupColor)
			.Text(FText::Format(LOCTEXT("Cards", "Cards {0}"), FText::AsNumber(GroupIndex)));
	}
	break;
	case EMaterialPanelType::Meshes:
	{
		return SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(HairGroupColor)
			.Text(FText::Format(LOCTEXT("Meshes", "Meshes {0}"), FText::AsNumber(GroupIndex)));
	}
	break;
	default:
	{
		return SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(HairGroupColor)
			.Text(FText::Format(LOCTEXT("Group", "Group ID {0}"), FText::AsNumber(GroupIndex)));
	}
	break;
	}

	return SNullWidget::NullWidget;
}

// Hair group custom display
void FGroomRenderingDetails::OnGenerateElementForHairGroup(TSharedRef<IPropertyHandle> StructProperty, int32 GroupIndex, IDetailChildrenBuilder& ChildrenBuilder, IDetailLayoutBuilder* DetailLayout)
{
	const FSlateFontInfo DetailFontInfo = IDetailLayoutBuilder::GetDetailFont();

	FProperty* Property = StructProperty->GetProperty();

	ChildrenBuilder.AddCustomRow(LOCTEXT("HairInfo_Separator", "Separator"))
	.NameContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SSeparator)
			.Thickness(2)
			.ColorAndOpacity(HairGroupColor)
		]
	]
	.ValueContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SSeparator)
			.Thickness(2)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		[
			MakeGroupNameCustomization(GroupIndex)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeGroupNameButtonCustomization(GroupIndex, Property)
		]
	];

	if (GroomAsset != nullptr && GroupIndex>=0 && GroupIndex < GroomAsset->HairGroupsInfo.Num() && (PanelType == EMaterialPanelType::Strands || PanelType == EMaterialPanelType::Interpolation))
	{
		ChildrenBuilder.AddCustomRow(LOCTEXT("HairStrandsInfo_Array", "HairStrandsInfo"))
		.ValueContent()
		.HAlign(HAlign_Fill)
		[
			MakeHairStrandsInfoGrid(DetailFontInfo, GroomAsset->HairGroupsInfo[GroupIndex])
		];
	}

	if (GroomAsset != nullptr && GroupIndex >= 0 && GroupIndex < GroomAsset->HairGroupsCards.Num() && (PanelType == EMaterialPanelType::Cards))
	{
		ChildrenBuilder.AddCustomRow(LOCTEXT("HairCardsInfo_Array", "HairCardsInfo"))
		.ValueContent()
		.HAlign(HAlign_Fill)
		[
			MakeHairCardsInfoGrid(DetailFontInfo, GroomAsset->HairGroupsCards[GroupIndex].CardsInfo)
		];
	}

	// Display a material picker for strands/cards/meshes panel. This material picker allows to select material among the one valid for this current asset, i.e., 
	// materials which have been added by the user within the material panel
	if (PanelType == EMaterialPanelType::Strands || PanelType == EMaterialPanelType::Cards || PanelType == EMaterialPanelType::Meshes)
	{
		ChildrenBuilder.AddCustomRow(LOCTEXT("HairGroup_Material", "Material"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HairRenderingGroup_Label_Material", "Material"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.IsEnabled(this, &FGroomRenderingDetails::IsStrandsMaterialPickerEnabled, GroupIndex)
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		.MaxDesiredWidth(0.0f) // no maximum
		[
			OnGenerateStrandsMaterialPicker(GroupIndex, DetailLayout)
		];
	}

	// Rename the array entry name by its group name and adds all its existing properties
	StructProperty->SetPropertyDisplayName(LOCTEXT("GroupProperties", "Properties"));

	uint32 ChildrenCount = 0;
	StructProperty->GetNumChildren(ChildrenCount);
	for (uint32 ChildIt = 0; ChildIt < ChildrenCount; ++ChildIt)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = StructProperty->GetChildHandle(ChildIt);
		FName PropertyName = ChildHandle->GetProperty()->GetFName();

		switch (PanelType)
		{
		case EMaterialPanelType::Strands:
		{
			if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupsRendering, GeometrySettings) ||
				PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupsRendering, ShadowSettings) ||
				PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupsRendering, AdvancedSettings))
			{
				ExpandStruct(ChildHandle, ChildrenBuilder);
			}
			else
			{
				ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
			}
		}
		break;
		case EMaterialPanelType::Cards:
		{
			if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupsCardsSourceDescription, SourceType))
			{
				// Add the Source type selection and just below add a save button
				ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
				if (GroomAsset != nullptr && GroupIndex >= 0 && GroupIndex < GroomAsset->HairGroupsCards.Num() && (PanelType == EMaterialPanelType::Cards))
				{
					ChildrenBuilder.AddCustomRow(LOCTEXT("HairCardsButtons", "HairCardsButtons"))
					.ValueContent()
					.HAlign(HAlign_Fill)
					[

						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.VAlign(VAlign_Center)
							.HAlign(HAlign_Center)
							.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
							.OnClicked(this, &FGroomRenderingDetails::OnRefreshCards, GroupIndex, Property)
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("Icons.Refresh"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.VAlign(VAlign_Center)
							.HAlign(HAlign_Center)
							.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
							.OnClicked(this, &FGroomRenderingDetails::OnSaveCards, GroupIndex, Property)
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("AssetEditor.SaveAsset.Greyscale"))
							]
						]
					];
				}
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupsCardsSourceDescription, CardsInfo))
			{
				// Not node display
			}
			else
			{
				ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
			}
		}
		break;
		case EMaterialPanelType::Meshes:
		{
			ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
		break;
		case EMaterialPanelType::Interpolation:
		{
			if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupsInterpolation, DecimationSettings) ||
				PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupsInterpolation, InterpolationSettings))
			{
				ExpandStruct(ChildHandle, ChildrenBuilder);
			}
			else
			{
				ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
			}
		}
		break;
		case EMaterialPanelType::LODs:
		{
			if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupsLOD, LODs))
			{
				// Add a custom builder for each LOD arrays within each group. This way we can customize this 'nested/inner' array
				TSharedRef<FDetailArrayBuilder> PropertyBuilder = MakeShareable(new FDetailArrayBuilder(ChildHandle.ToSharedRef(), false, false, false));
				PropertyBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FGroomRenderingDetails::OnGenerateElementForLODs, DetailLayout, GroupIndex));
				PropertyBuilder->SetDisplayName(FText::FromString(TEXT("LODs")));
				ChildrenBuilder.AddCustomBuilder(PropertyBuilder);
			}
			else
			{
				ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
			}
		}
		break;
		case EMaterialPanelType::Physics:
		{
			ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
		break;
		default:
		{
			ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
		break;
		}

	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FGroomRenderingDetails::OnSetObject(const FAssetData& AssetData)
{

}

FString FGroomRenderingDetails::OnGetObjectPath(int32 GroupIndex) const
{
	if (!GroomAsset || GroupIndex < 0)
		return FString();

	int32 MaterialIndex = INDEX_NONE;
	check(GroomAsset);
	switch (PanelType)
	{
	case EMaterialPanelType::Cards:
	{
		if (GroupIndex < GroomAsset->HairGroupsCards.Num())
		{
			MaterialIndex = GroomAsset->GetMaterialIndex(GroomAsset->HairGroupsCards[GroupIndex].MaterialSlotName);
		}
	}
	break;
	case EMaterialPanelType::Meshes:
	{
		if (GroupIndex < GroomAsset->HairGroupsMeshes.Num())
		{
			MaterialIndex = GroomAsset->GetMaterialIndex(GroomAsset->HairGroupsMeshes[GroupIndex].MaterialSlotName);
		}
	}
	break;
	case EMaterialPanelType::Strands:
	{
		if (GroupIndex < GroomAsset->HairGroupsRendering.Num())
		{
			MaterialIndex = GroomAsset->GetMaterialIndex(GroomAsset->HairGroupsRendering[GroupIndex].MaterialSlotName);
		}
	}
	break;
	}

	if (MaterialIndex == INDEX_NONE)
		return FString();
	
	return GroomAsset->HairGroupsMaterials[MaterialIndex].Material->GetPathName();
}

/**
 * Called to get the visibility of the replace button
 */
bool FGroomRenderingDetails::GetReplaceVisibility(TSharedPtr<IPropertyHandle> PropertyHandle) const
{
	return false;
}

/**
 * Called when reset to base is clicked
 */
void FGroomRenderingDetails::OnResetToBaseClicked(TSharedPtr<IPropertyHandle> PropertyHandle)
{

}

TSharedRef<SWidget> FGroomRenderingDetails::CreateMaterialSwatch( const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool/*, const TArray<FAssetData>& OwnerAssetDataArray*/, int32 GroupIndex)
{
	FIntPoint ThumbnailSize(64, 64);

	FResetToDefaultOverride ResetToDefaultOverride = FResetToDefaultOverride::Create(
		FIsResetToDefaultVisible::CreateSP(this, &FGroomRenderingDetails::GetReplaceVisibility),
		FResetToDefaultHandler::CreateSP(this, &FGroomRenderingDetails::OnResetToBaseClicked)
	);

	const bool bDisplayCompactSize = false;
	return
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding( 0.0f )
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew( SObjectPropertyEntryBox )
					.EnableContentPicker(false)
					.DisplayUseSelected(false)
					.ObjectPath(this, &FGroomRenderingDetails::OnGetObjectPath, GroupIndex)
					.AllowedClass(UMaterialInterface::StaticClass())
					.OnObjectChanged(this, &FGroomRenderingDetails::OnSetObject)
					.ThumbnailPool(ThumbnailPool)
					.DisplayCompactSize(bDisplayCompactSize)
					.CustomResetToDefault(ResetToDefaultOverride)
					//.OwnerAssetDataArray(OwnerAssetDataArray)
					.CustomContentSlot()
					[
						SNew(SComboButton)
						.IsEnabled(this, &FGroomRenderingDetails::IsStrandsMaterialPickerEnabled, GroupIndex)
						.OnGetMenuContent(this, &FGroomRenderingDetails::OnGenerateStrandsMaterialMenuPicker, GroupIndex)
						.VAlign(VAlign_Center)
						.ContentPadding(2)
						.ButtonContent()
						[
							SNew(STextBlock)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.Text(this, &FGroomRenderingDetails::GetStrandsMaterialName, GroupIndex)
						]
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE