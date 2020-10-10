// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomComponentDetailsCustomization.h"
#include "Layout/Margin.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Commands/UICommandList.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "EditorStyleSet.h"
#include "EditorModeManager.h"
#include "PropertyCustomizationHelpers.h"
#include "IDetailChildrenBuilder.h"

#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"

#include "IDetailsView.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"

#include "ScopedTransaction.h"
#include "IPropertyUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "GroomComponent"

//////////////////////////////////////////////////////////////////////////
// FGroomComponentDetailsCustomization

TSharedRef<IDetailCustomization> FGroomComponentDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FGroomComponentDetailsCustomization);
}

void FGroomComponentDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	const TArray< TWeakObjectPtr<UObject> >& SelectedObjects = DetailLayout.GetSelectedObjects();
	MyDetailLayout = nullptr;
	
	FNotifyHook* NotifyHook = DetailLayout.GetPropertyUtilities()->GetNotifyHook();

	bool bEditingActor = false;

	UGroomComponent* GroomComponent = nullptr;
	for (int32 ObjectIndex = 0; ObjectIndex < SelectedObjects.Num(); ++ObjectIndex)
	{
		UObject* TestObject = SelectedObjects[ObjectIndex].Get();
		if (AActor* CurrentActor = Cast<AActor>(TestObject))
		{
			if (UGroomComponent* CurrentComponent = CurrentActor->FindComponentByClass<UGroomComponent>())
			{
				bEditingActor = true;
				GroomComponent = CurrentComponent;
				break;
			}
		}
		else if (UGroomComponent* TestComponent = Cast<UGroomComponent>(TestObject))
		{
			GroomComponent = TestComponent;
			break;
		}
	}
	GroomComponentPtr = GroomComponent;

	IDetailCategoryBuilder& HairGroupCategory = DetailLayout.EditCategory("GroomGroupsDesc", FText::GetEmpty(), ECategoryPriority::TypeSpecific);
	CustomizeDescGroupProperties(DetailLayout, HairGroupCategory);
}

void FGroomComponentDetailsCustomization::CustomizeDescGroupProperties(IDetailLayoutBuilder& DetailLayout, IDetailCategoryBuilder& StrandsGroupFilesCategory)
{
	TSharedRef<IPropertyHandle> GroupDescAssetsProperty = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UGroomComponent, GroomGroupsDesc), UGroomComponent::StaticClass());
	if (GroupDescAssetsProperty->IsValidHandle())
	{
		TSharedRef<FDetailArrayBuilder> GroupDescPropertyBuilder = MakeShareable(new FDetailArrayBuilder(GroupDescAssetsProperty, false, false, false));
		GroupDescPropertyBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FGroomComponentDetailsCustomization::OnGenerateElementForHairGroup, &DetailLayout));
		GroupDescPropertyBuilder->SetDisplayName(FText::FromString(TEXT("Hair Groups")));
		StrandsGroupFilesCategory.AddCustomBuilder(GroupDescPropertyBuilder, false);
	}
}

void FGroomComponentDetailsCustomization::SetOverride(int32 GroupIndex, TSharedPtr<IPropertyHandle> ChildHandle, bool bValue)
{
	FName PropertyName = ChildHandle->GetProperty()->GetFName();

	// Reset the override flag so that the groom component will fallback onto the groom asset value
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairWidth))
	{
		GroomComponentPtr->GroomGroupsDesc[GroupIndex].HairWidth_Override = bValue;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairRootScale))
	{
		GroomComponentPtr->GroomGroupsDesc[GroupIndex].HairRootScale_Override = bValue;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairTipScale))
	{
		GroomComponentPtr->GroomGroupsDesc[GroupIndex].HairTipScale_Override = bValue;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairClipLength))
	{
		GroomComponentPtr->GroomGroupsDesc[GroupIndex].HairClipLength_Override = bValue;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairShadowDensity))
	{
		GroomComponentPtr->GroomGroupsDesc[GroupIndex].HairShadowDensity_Override = bValue;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairRaytracingRadiusScale))
	{
		GroomComponentPtr->GroomGroupsDesc[GroupIndex].HairRaytracingRadiusScale_Override = bValue;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, bUseHairRaytracingGeometry))
	{
		GroomComponentPtr->GroomGroupsDesc[GroupIndex].bUseHairRaytracingGeometry_Override = bValue;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, bUseStableRasterization))
	{
		GroomComponentPtr->GroomGroupsDesc[GroupIndex].bUseStableRasterization_Override = bValue;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, bScatterSceneLighting))
	{
		GroomComponentPtr->GroomGroupsDesc[GroupIndex].bScatterSceneLighting_Override = bValue;
	}
}

void FGroomComponentDetailsCustomization::OnResetToDefault(int32 GroupIndex, TSharedPtr<IPropertyHandle> ChildHandle)
{
	if (ChildHandle == nullptr || GroomComponentPtr == nullptr || GroupIndex < 0 || GroupIndex >= GroomComponentPtr->GroomGroupsDesc.Num())
	{
		return;
	}
	
	FScopedTransaction ScopedTransaction(NSLOCTEXT("UnrealEd", "PropertyWindowResetToDefault", "Reset to Default"));
	//GroomComponentPtr->Modify();
	SetOverride(GroupIndex, ChildHandle, false);
	GroomComponentPtr->UpdateHairGroupsDescAndInvalidateRenderState();
}

void FGroomComponentDetailsCustomization::OnValueChanged(int32 GroupIndex, TSharedPtr<IPropertyHandle> ChildHandle)
{
	if (ChildHandle == nullptr || GroomComponentPtr == nullptr || GroupIndex < 0 || GroupIndex >= GroomComponentPtr->GroomGroupsDesc.Num())
	{
		return;
	}

	FScopedTransaction ScopedTransaction(NSLOCTEXT("UnrealEd", "PropertyWindowPreValueChanged", "PreValue Changed"));
	//GroomComponentPtr->Modify();
	SetOverride(GroupIndex, ChildHandle, true);
	GroomComponentPtr->UpdateHairGroupsDescAndInvalidateRenderState();
}


static TSharedRef<SUniformGridPanel> MakeHairInfoGrid(const FSlateFontInfo& DetailFontInfo, FHairGroupDesc& GroupDesc)
{
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(2.0f);

	// Header
	Grid->AddSlot(0, 0) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(LOCTEXT("HairInfo_Curves", "Curves"))
	];

	Grid->AddSlot(1, 0) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(LOCTEXT("HairInfo_Guides", "Guides"))
	];

	Grid->AddSlot(2, 0) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(LOCTEXT("HairInfo_Length", "Max. Length"))
	];

	// Value
	Grid->AddSlot(0, 1) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(FText::AsNumber(GroupDesc.HairCount))
	];
	Grid->AddSlot(1, 1) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(FText::AsNumber(GroupDesc.GuideCount))
	];
	Grid->AddSlot(2, 1) // x, y
	.HAlign(HAlign_Right)
	[
		SNew(STextBlock)
		.Font(DetailFontInfo)
		.Text(FText::AsNumber(GroupDesc.HairLength))
	];

	return Grid;
}

// Hair group custom display
void FGroomComponentDetailsCustomization::OnGenerateElementForHairGroup(TSharedRef<IPropertyHandle> StructProperty, int32 GroupIndex, IDetailChildrenBuilder& ChildrenBuilder, IDetailLayoutBuilder* DetailLayout)
{
	const FSlateFontInfo DetailFontInfo = IDetailLayoutBuilder::GetDetailFont();

	ChildrenBuilder.AddCustomRow(LOCTEXT("HairInfo_Separator", "Separator"))
	.NameContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SSeparator)
			.Thickness(2)
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
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(FText::Format(LOCTEXT("Group", "Group ID {0}"), FText::AsNumber(GroupIndex)))
		]
	];

	if (GroomComponentPtr != nullptr && GroupIndex>=0 && GroupIndex < GroomComponentPtr->GroomGroupsDesc.Num())
	{
		ChildrenBuilder.AddCustomRow(LOCTEXT("HairInfo_Separator", "Separator"))
		.ValueContent()
		.HAlign(HAlign_Fill)
		[
			MakeHairInfoGrid(DetailFontInfo, GroomComponentPtr->GroomGroupsDesc[GroupIndex])
		];
	}

	uint32 ChildrenCount = 0;
	StructProperty->GetNumChildren(ChildrenCount);
	for (uint32 ChildIt = 0; ChildIt < ChildrenCount; ++ChildIt)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = StructProperty->GetChildHandle(ChildIt);

		// These properties are not display with regular widget but with a dedicated array
		FName PropertyName = ChildHandle->GetProperty()->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairCount) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairLength) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, GuideCount)) 
		{
			continue;
		}

		ChildHandle->SetOnPropertyResetToDefault(FSimpleDelegate::CreateSP(this, &FGroomComponentDetailsCustomization::OnResetToDefault, GroupIndex, ChildHandle));
		ChildHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateSP(this, &FGroomComponentDetailsCustomization::OnValueChanged, GroupIndex, ChildHandle));
		ChildrenBuilder.AddProperty(ChildHandle.ToSharedRef());
	}
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
