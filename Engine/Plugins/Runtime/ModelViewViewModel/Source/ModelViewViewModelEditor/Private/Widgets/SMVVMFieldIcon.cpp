// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMFieldIcon.h"

#include "Bindings/MVVMBindingHelper.h"
#include "BlueprintEditor.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SLayeredImage.h"

#define LOCTEXT_NAMESPACE "SMVVMFieldIcon"

void SMVVMFieldIcon::Construct(const FArguments& Args)
{
	ChildSlot
	[
		SAssignNew(LayeredImage, SLayeredImage)
	];

	RefreshBinding(Args._Field);
}

void SMVVMFieldIcon::RefreshBinding(const UE::MVVM::FMVVMConstFieldVariant& Field)
{
	LayeredImage->SetImage(nullptr);
	LayeredImage->RemoveAllLayers();

	if (Field.IsEmpty())
	{
		return;
	}

	const FProperty* IconProperty = nullptr;

	if (Field.IsProperty())
	{
		IconProperty = Field.GetProperty();
	}
	else if (Field.IsFunction())
	{
		const UFunction* Function = Field.GetFunction();
		const FProperty* ReturnProperty = UE::MVVM::BindingHelper::GetReturnProperty(Function);
		if (ReturnProperty != nullptr)
		{
			IconProperty = ReturnProperty;
		}
		else
		{
			IconProperty = UE::MVVM::BindingHelper::GetFirstArgumentProperty(Function);
		}
	}

	if (IconProperty != nullptr)
	{
		FSlateColor PrimaryColor, SecondaryColor;
		const FSlateBrush* SecondaryBrush = nullptr;
		const FSlateBrush* PrimaryBrush = FBlueprintEditor::GetVarIconAndColorFromProperty(IconProperty, PrimaryColor, SecondaryBrush, SecondaryColor);

		LayeredImage->SetImage(PrimaryBrush);
		LayeredImage->SetColorAndOpacity(PrimaryColor);
		LayeredImage->AddLayer(SecondaryBrush, SecondaryColor);
	}
}

#undef LOCTEXT_NAMESPACE
