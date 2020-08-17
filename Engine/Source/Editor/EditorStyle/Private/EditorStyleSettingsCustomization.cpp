// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorStyleSettingsCustomization.h"
#include "DetailCategoryBuilder.h"
#include "Styling/StyleColors.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"

TSharedRef<IPropertyTypeCustomization> FStyleColorListCustomization::MakeInstance()
{
	return MakeShared<FStyleColorListCustomization>();
}

void FStyleColorListCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	
}

void FStyleColorListCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	TSharedPtr<IPropertyHandle> ColorArrayProperty = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStyleColorList, StyleColors));

	ColorArrayProperty->GetNumChildren(NumChildren);
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		if (ChildIndex < (uint32)EStyleColor::User1)
		{
			ChildBuilder.AddProperty(ColorArrayProperty->GetChildHandle(ChildIndex).ToSharedRef());
		}
		else
		{
			// user colors are added if they have been customized with a display name
			FText DisplayName = UStyleColorTable::Get().GetColorDisplayName((EStyleColor)ChildIndex);
			if (!DisplayName.IsEmpty())
			{
				IDetailPropertyRow& Row = ChildBuilder.AddProperty(ColorArrayProperty->GetChildHandle(ChildIndex).ToSharedRef());
				Row.DisplayName(DisplayName);
			}
		}
	}
}


TSharedRef<IDetailCustomization> FEditorStyleSettingsCustomization::MakeInstance()
{
	return MakeShared<FEditorStyleSettingsCustomization>();
}


void FEditorStyleSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	IDetailCategoryBuilder& ColorCategory = DetailLayout.EditCategory("Colors");

	TArray<UObject*> Objects = { &UStyleColorTable::Get() };

	ColorCategory.AddExternalObjectProperty(Objects, "Colors", EPropertyLocation::Advanced);
}

