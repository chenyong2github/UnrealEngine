// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorStyleSettingsCustomization.h"
#include "DetailCategoryBuilder.h"
#include "Styling/StyleColors.h"
#include "DetailLayoutBuilder.h"


TSharedRef<IDetailCustomization> FEditorStyleSettingsCustomization::MakeInstance()
{
	return MakeShared<FEditorStyleSettingsCustomization>();
}


void FEditorStyleSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	IDetailCategoryBuilder& ColorCategory = DetailLayout.EditCategory("Color");

	TArray<UObject*> Objects = { &UStyleColorTable::Get() };

	ColorCategory.AddExternalObjectProperty(Objects, "Colors");
}