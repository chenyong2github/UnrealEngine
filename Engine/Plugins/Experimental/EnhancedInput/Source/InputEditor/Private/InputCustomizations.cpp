// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputCustomizations.h"

#include "ActionMappingDetails.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "EnhancedActionKeyMapping.h"
#include "IDetailChildrenBuilder.h"
#include "InputMappingContext.h"
#include "KeyStructCustomization.h"
#include "PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE "InputCustomization"

TSharedRef<IDetailCustomization> FInputContextDetails::MakeInstance()
{
	return MakeShareable(new FInputContextDetails);
}

void FInputContextDetails::CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder)
{
	// Custom Action Mappings
	const TSharedPtr<IPropertyHandle> ActionMappingsPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UInputMappingContext, Mappings));
	ActionMappingsPropertyHandle->MarkHiddenByCustomization();

	IDetailCategoryBuilder& MappingsDetailCategoryBuilder = DetailBuilder.EditCategory(ActionMappingsPropertyHandle->GetDefaultCategoryName());
	const TSharedRef<FActionMappingsNodeBuilder> ActionMappingsBuilder = MakeShareable(new FActionMappingsNodeBuilder(&DetailBuilder, ActionMappingsPropertyHandle));
	MappingsDetailCategoryBuilder.AddCustomBuilder(ActionMappingsBuilder);
}


void FEnhancedActionMappingCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	MappingPropertyHandle = PropertyHandle;

	// Grab the FKey property
	TSharedPtr<IPropertyHandle> KeyHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEnhancedActionKeyMapping, Key));

	TSharedRef<SWidget> RemoveButton = PropertyCustomizationHelpers::MakeDeleteButton(FSimpleDelegate::CreateSP(this, &FEnhancedActionMappingCustomization::RemoveMappingButton_OnClick),
		LOCTEXT("RemoveMappingToolTip", "Remove Mapping"));

	// Create  a new instance of the key customization.
	KeyStructInstance = FKeyStructCustomization::MakeInstance();

	// TODO: Use FDetailArrayBuilder?

	// Pass our header row into the key struct customizeheader method so it populates our row with the key struct header
	StaticCastSharedPtr<FKeyStructCustomization>(KeyStructInstance)->CustomizeHeaderOnlyWithButton(KeyHandle.ToSharedRef(), HeaderRow, CustomizationUtils, RemoveButton);
}

void FEnhancedActionMappingCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> TriggersHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEnhancedActionKeyMapping, Triggers));
	TSharedPtr<IPropertyHandle> ModifiersHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEnhancedActionKeyMapping, Modifiers));

	// TODO: ResetToDefault needs to be disabled for arrays
	ChildBuilder.AddProperty(TriggersHandle.ToSharedRef());
	ChildBuilder.AddProperty(ModifiersHandle.ToSharedRef());
}

void FEnhancedActionMappingCustomization::RemoveMappingButton_OnClick() const
{
	if (MappingPropertyHandle->IsValidHandle())
	{
		const TSharedPtr<IPropertyHandle> ParentHandle = MappingPropertyHandle->GetParentHandle();
		const TSharedPtr<IPropertyHandleArray> ParentArrayHandle = ParentHandle->AsArray();

		ParentArrayHandle->DeleteItem(MappingPropertyHandle->GetIndexInArray());
	}
}

#undef LOCTEXT_NAMESPACE