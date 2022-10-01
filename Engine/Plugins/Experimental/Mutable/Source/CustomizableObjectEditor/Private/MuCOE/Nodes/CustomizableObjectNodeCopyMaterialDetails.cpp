// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectNodeCopyMaterialDetails.h"

#include "DetailLayoutBuilder.h"

TSharedRef<IDetailCustomization> FCustomizableObjectNodeCopyMaterialDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodeCopyMaterialDetails);
}

void FCustomizableObjectNodeCopyMaterialDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<FName> Names;
	DetailBuilder.GetCategoryNames(Names);

	for (const FName& Name : Names)
	{
		DetailBuilder.HideCategory(Name);
	}
}
