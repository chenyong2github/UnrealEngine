// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectNodeSkeletalMeshDetails.h"
#include "MuCOE/CustomizableObjectEditorModule.h"
#include "PropertyCustomizationHelpers.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "MuCOE/PinViewer/SPinViewer.h"

#include "MuCOE/SCustomizableObjectNodeSkeletalMeshRTMorphSelector.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectNodeMaterialDetails"


TSharedRef<IDetailCustomization> FCustomizableObjectNodeSkeletalMeshDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodeSkeletalMeshDetails);
}


void FCustomizableObjectNodeSkeletalMeshDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();

    if (DetailsView->GetSelectedObjects().Num())
	{
		Node = Cast<UCustomizableObjectNodeSkeletalMesh>(DetailsView->GetSelectedObjects()[0].Get());
	}

    if (!Node)
    {
        return;
    }

    DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeSkeletalMesh, UsedRealTimeMorphTargetNames));
    DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeSkeletalMesh, bUseAllRealTimeMorphs));

    // Needed to draw the CO information before the Material Layer information
    IDetailCategoryBuilder& CustomizableObject = DetailBuilder.EditCategory("CustomizableObject");

    // Cretaing new categories to show the material layers
    IDetailCategoryBuilder& MorphsCategory = DetailBuilder.EditCategory("RealTimeMorphTargets");

	TSharedPtr<SCustomizableObjectNodeSkeletalMeshRTMorphSelector> MorphSelector;
    MorphsCategory.AddCustomRow(LOCTEXT("MaterialLayerCategory", "RealTimeMorphTargets"))
    [
        SAssignNew(MorphSelector, SCustomizableObjectNodeSkeletalMeshRTMorphSelector).Node(Node)
    ];

	TSharedRef<IPropertyHandle> SkeletalMeshProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeSkeletalMesh, SkeletalMesh));
	SkeletalMeshProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(MorphSelector.Get(), &SCustomizableObjectNodeSkeletalMeshRTMorphSelector::UpdateWidget));

	PinViewerAttachToDetailCustomization(DetailBuilder);
}


#undef LOCTEXT_NAMESPACE
