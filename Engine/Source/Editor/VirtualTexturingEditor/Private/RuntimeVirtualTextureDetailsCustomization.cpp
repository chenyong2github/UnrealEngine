// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright Epic Games, Inc. All Rights Reserved.

#include "RuntimeVirtualTextureDetailsCustomization.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "RuntimeVirtualTextureBuild.h"
#include "ScopedTransaction.h"
#include "SResetToDefaultMenu.h"
#include "VT/RuntimeVirtualTexture.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "VirtualTexturingEditorModule"

FRuntimeVirtualTextureDetailsCustomization::FRuntimeVirtualTextureDetailsCustomization()
	: VirtualTexture(nullptr)
{
}

TSharedRef<IDetailCustomization> FRuntimeVirtualTextureDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FRuntimeVirtualTextureDetailsCustomization);
}

namespace
{
	// Helper for adding text containing real values to the properties that are edited as power (or multiple) of 2
	void AddTextToProperty(IDetailLayoutBuilder& DetailBuilder, IDetailCategoryBuilder& CategoryBuilder, FName const& PropertyName, TSharedPtr<STextBlock>& TextBlock)
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = DetailBuilder.GetProperty(PropertyName);
		DetailBuilder.HideProperty(PropertyHandle);

		TSharedPtr<SResetToDefaultMenu> ResetToDefaultMenu;

		CategoryBuilder.AddCustomRow(PropertyHandle->GetPropertyDisplayName())
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(200.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.Padding(4.0f)
			[
				SNew(SWrapBox)
				.UseAllottedSize(true)

				+ SWrapBox::Slot()
				.Padding(FMargin(0.0f, 2.0f, 2.0f, 0.0f))
				[
					SAssignNew(TextBlock, STextBlock)
				]
			]

			+ SHorizontalBox::Slot()
			[
				PropertyHandle->CreatePropertyValueWidget()
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f)
			[
				// Would be better to use SResetToDefaultPropertyEditor here but that is private in the PropertyEditor lib
				SAssignNew(ResetToDefaultMenu, SResetToDefaultMenu)
			]
		];

		ResetToDefaultMenu->AddProperty(PropertyHandle.ToSharedRef());
	}
}

void FRuntimeVirtualTextureDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Get and store the linked URuntimeVirtualTexture
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() > 1)
	{
		return;
	}
	VirtualTexture = Cast<URuntimeVirtualTexture>(ObjectsBeingCustomized[0].Get());
	if (VirtualTexture == nullptr)
	{
		return;
	}

	// Add size helpers
	IDetailCategoryBuilder& SizeCategory = DetailBuilder.EditCategory("Size", FText::GetEmpty());
	AddTextToProperty(DetailBuilder, SizeCategory, "TileCount", TileCountText);
	AddTextToProperty(DetailBuilder, SizeCategory, "TileSize", TileSizeText);
	AddTextToProperty(DetailBuilder, SizeCategory, "TileBorderSize", TileBorderSizeText);

	// Add details block
	IDetailCategoryBuilder& DetailsCategory = DetailBuilder.EditCategory("Details", FText::GetEmpty(), ECategoryPriority::Important);
	static const FText RowText = LOCTEXT("Category_Details", "Details");
	DetailsCategory.AddCustomRow(RowText)
	.WholeRowContent()
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		.Padding(4.0f)
		[
			SAssignNew(SizeText, STextBlock)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		.Padding(4.0f)
		[
			SAssignNew(PageTableTextureMemoryText, STextBlock)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		.Padding(4.0f)
		[
			SAssignNew(PhysicalTextureMemoryText, STextBlock)
		]
	];

	// Add refresh callback for all properties 
	DetailBuilder.GetProperty(FName(TEXT("TileCount")))->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FRuntimeVirtualTextureDetailsCustomization::RefreshDetails));
	DetailBuilder.GetProperty(FName(TEXT("TileSize")))->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FRuntimeVirtualTextureDetailsCustomization::RefreshDetails));
	DetailBuilder.GetProperty(FName(TEXT("TileBorderSize")))->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FRuntimeVirtualTextureDetailsCustomization::RefreshDetails));
	DetailBuilder.GetProperty(FName(TEXT("MaterialType")))->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FRuntimeVirtualTextureDetailsCustomization::RefreshDetails));
	DetailBuilder.GetProperty(FName(TEXT("bCompressTextures")))->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FRuntimeVirtualTextureDetailsCustomization::RefreshDetails));
	DetailBuilder.GetProperty(FName(TEXT("RemoveLowMips")))->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FRuntimeVirtualTextureDetailsCustomization::RefreshDetails));

	// Initialize text blocks
	RefreshDetails();
}

void FRuntimeVirtualTextureDetailsCustomization::RefreshDetails()
{
	FNumberFormattingOptions SizeOptions;
	SizeOptions.UseGrouping = false;
	SizeOptions.MaximumFractionalDigits = 0;

 	TileCountText->SetText(FText::Format(LOCTEXT("Details_Number", "{0}"), FText::AsNumber(VirtualTexture->GetTileCount(), &SizeOptions)));
 	TileSizeText->SetText(FText::Format(LOCTEXT("Details_Number", "{0}"), FText::AsNumber(VirtualTexture->GetTileSize(), &SizeOptions)));
 	TileBorderSizeText->SetText(FText::Format(LOCTEXT("Details_Number", "{0}"), FText::AsNumber(VirtualTexture->GetTileBorderSize(), &SizeOptions)));

	SizeText->SetText(FText::Format(LOCTEXT("Details_Size", "Virtual Texture Size: {0}"), FText::AsNumber(VirtualTexture->GetSize(), &SizeOptions)));
	PageTableTextureMemoryText->SetText(FText::Format(LOCTEXT("Details_PageTableMemory", "Page Table Texture Memory (estimated): {0} KiB"), FText::AsNumber(VirtualTexture->GetEstimatedPageTableTextureMemoryKb(), &SizeOptions)));
	PhysicalTextureMemoryText->SetText(FText::Format(LOCTEXT("Details_PhysicalMemory", "Physical Texture Memory (estimated): {0} KiB"), FText::AsNumber(VirtualTexture->GetEstimatedPhysicalTextureMemoryKb(), &SizeOptions)));
}


FRuntimeVirtualTextureComponentDetailsCustomization::FRuntimeVirtualTextureComponentDetailsCustomization()
{
}

TSharedRef<IDetailCustomization> FRuntimeVirtualTextureComponentDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FRuntimeVirtualTextureComponentDetailsCustomization);
}

void FRuntimeVirtualTextureComponentDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Get and store the linked URuntimeVirtualTextureComponent
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() > 1)
	{
		return;
	}
	RuntimeVirtualTextureComponent = Cast<URuntimeVirtualTextureComponent>(ObjectsBeingCustomized[0].Get());
	if (RuntimeVirtualTextureComponent == nullptr)
	{
		return;
	}

	IDetailCategoryBuilder& VirtualTextureCategory = DetailBuilder.EditCategory("VirtualTextureBuild", FText::GetEmpty());
	
	VirtualTextureCategory
	.AddCustomRow(LOCTEXT("Button_BuildStreamingMips", "Build Streaming Mips"), true)
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("Button_BuildStreamingMips", "Build Streaming Mips"))
	]
	.ValueContent()
	.MaxDesiredWidth(125.f)
	[
		SNew(SButton)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.ContentPadding(2)
		.Text(LOCTEXT("Button_Build", "Build"))
		.ToolTipText(LOCTEXT("Button_Build_Tooltip", "Build the low mips as streaming virtual texture data"))
		.OnClicked(this, &FRuntimeVirtualTextureComponentDetailsCustomization::BuildStreamedMips)
	];

	VirtualTextureCategory
	.AddCustomRow(LOCTEXT("Button_BuildDebugStreamingMips", "Build Debug Streaming Mips"), true)
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("Button_BuildDebugStreamingMips", "Build Debug Streaming Mips"))
	]
	.ValueContent()
	.MaxDesiredWidth(125.f)
	[
		SNew(SButton)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.ContentPadding(2)
		.Text(LOCTEXT("Button_Build", "Build"))
		.ToolTipText(LOCTEXT("Button_BuildDebug_Tooltip", "Build the low mips with debug data"))
		.OnClicked(this, &FRuntimeVirtualTextureComponentDetailsCustomization::BuildLowMipsDebug)
	];
}

FReply FRuntimeVirtualTextureComponentDetailsCustomization::BuildStreamedMips()
{
	bool bOK = RuntimeVirtualTexture::BuildStreamedMips(RuntimeVirtualTextureComponent, ERuntimeVirtualTextureDebugType::None);
	return bOK ? FReply::Handled() : FReply::Unhandled();
}

FReply FRuntimeVirtualTextureComponentDetailsCustomization::BuildLowMipsDebug()
{
	bool bOK = RuntimeVirtualTexture::BuildStreamedMips(RuntimeVirtualTextureComponent, ERuntimeVirtualTextureDebugType::Debug);
	return bOK ? FReply::Handled() : FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
