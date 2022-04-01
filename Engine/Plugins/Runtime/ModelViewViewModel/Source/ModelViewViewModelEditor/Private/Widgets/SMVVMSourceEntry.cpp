// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "Widgets/SMVVMSourceEntry.h"

#include "Styling/SlateIconFinder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMVVMSource"

static const FSlateBrush* GetSourceIcon(const TOptional<UE::MVVM::FBindingSource>& Source)
{
	if (!Source.IsSet())
	{
		return nullptr;
	}

	return FSlateIconFinder::FindIconBrushForClass(Source.GetValue().Class);
}

static FLinearColor GetSourceColor(const TOptional<UE::MVVM::FBindingSource>& Source)
{
	if (!Source.IsSet() || Source.GetValue().Class == nullptr || Source.GetValue().Class->GetName().IsEmpty())
	{
		return FLinearColor::White;
	}

	uint32 Hash = GetTypeHash(Source.GetValue().Class->GetName());
	FLinearColor Color = FLinearColor::White;
	Color.R = ((Hash * 1 % 96) + 32) / 256.f;
	Color.G = ((Hash * 2 % 96) + 32) / 256.f;
	Color.B = ((Hash * 3 % 96) + 32) / 256.f;
	return Color;
}

static FText GetSourceDisplayName(const TOptional<UE::MVVM::FBindingSource>& Source)
{
	if (!Source.IsSet())
	{
		return LOCTEXT("None", "<None>");
	}

	return Source.GetValue().DisplayName;
}

void SMVVMSourceEntry::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SAssignNew(Image, SImage)
		]
		+ SHorizontalBox::Slot()
		.Padding(4, 0, 0, 0)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SAssignNew(Label, STextBlock)
			.TextStyle(InArgs._TextStyle)
		]
	];

	RefreshSource(InArgs._Source);
}

void SMVVMSourceEntry::RefreshSource(const TOptional<UE::MVVM::FBindingSource>& Source)
{
	Image->SetImage(GetSourceIcon(Source));
	Image->SetColorAndOpacity(GetSourceColor(Source));
	Label->SetText(GetSourceDisplayName(Source));
}

#undef LOCTEXT_NAMESPACE