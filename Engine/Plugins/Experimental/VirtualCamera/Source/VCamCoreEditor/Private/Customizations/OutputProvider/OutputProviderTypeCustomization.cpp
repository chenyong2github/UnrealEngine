// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutputProviderTypeCustomization.h"

#include "DetailCategoryBuilder.h"
#include "Output/VCamOutputProviderBase.h"
#include "UI/VCamWidget.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE "FOutputProviderCustomization"

namespace UE::VCamCoreEditor::Private
{
	TSharedRef<IPropertyTypeCustomization> FOutputProviderTypeCustomization::MakeInstance()
	{
		return MakeShared<FOutputProviderTypeCustomization>();
	}

	void FOutputProviderTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{}

	void FOutputProviderTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		UObject* CustomizedObject;
		const bool bSuccessGettingValue = PropertyHandle->GetValue(CustomizedObject) == FPropertyAccess::Success;
		UVCamOutputProviderBase* CustomizedOutputProvider = bSuccessGettingValue
			? Cast<UVCamOutputProviderBase>(CustomizedObject)
			: nullptr;
		if (!CustomizedOutputProvider)
		{
			return;
		}
		
		uint32 NumChildren;
		if (PropertyHandle->GetNumChildren(NumChildren) != FPropertyAccess::Success
			|| NumChildren != 1)
		{
			return;
		}
		
		IDetailPropertyRow* DetailRow = ChildBuilder.AddExternalObjects({ CustomizedOutputProvider },
			FAddPropertyParams()
				.CreateCategoryNodes(false)		// Avoid creating intermediate group expansion
				.AllowChildren(true)			// Child properties should be shown
				.HideRootObjectNode(false)		// Needed so we can use NameContent() & ValueContent() below
				);
		DetailRow->CustomWidget(true)
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				PropertyHandle->CreatePropertyValueWidget()
			];
	}
}

#undef LOCTEXT_NAMESPACE