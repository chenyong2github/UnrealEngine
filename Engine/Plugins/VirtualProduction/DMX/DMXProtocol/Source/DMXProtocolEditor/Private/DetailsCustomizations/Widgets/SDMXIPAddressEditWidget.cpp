// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXIPAddressEditWidget.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolUtils.h"

#include "EditorStyleSet.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SEditableTextBox.h"


void SDMXIPAddressEditWidget::Construct(const FArguments& InArgs)
{
	OnIPAddressSelectedDelegate = InArgs._OnIPAddressSelected;

	LocalAdapterAddressSource = FDMXProtocolUtils::GetLocalNetworkInterfaceCardIPs();

	ChildSlot
	[
		SAssignNew(LocalAdapterAddressComboBox, SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&LocalAdapterAddressSource)
		.OnGenerateWidget(this, &SDMXIPAddressEditWidget::GenerateLocalAdapterAddressComboBoxEntry)
		.OnSelectionChanged(this, &SDMXIPAddressEditWidget::OnIpAddressSelected)
		.Content()
		[
			SAssignNew(IPAddressEditableTextBlock, SEditableTextBox)
			.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.Text(FText::FromString(InArgs._InitialValue))
			.OnTextCommitted(this, &SDMXIPAddressEditWidget::OnIPAddressEntered)
		]
	];
}

FString SDMXIPAddressEditWidget::GetSelectedIPAddress() const
{
	return IPAddressEditableTextBlock->GetText().ToString();
}

void SDMXIPAddressEditWidget::OnIpAddressSelected(TSharedPtr<FString> InAddress, ESelectInfo::Type InType)
{
	TSharedPtr<FString> SelectedIPAddress = LocalAdapterAddressComboBox->GetSelectedItem();
	if (SelectedIPAddress.IsValid())
	{
		IPAddressEditableTextBlock->SetText(FText::FromString(*SelectedIPAddress));
		OnIPAddressSelectedDelegate.ExecuteIfBound();
	}
}

void SDMXIPAddressEditWidget::OnIPAddressEntered(const FText&, ETextCommit::Type)
{
	OnIPAddressSelectedDelegate.ExecuteIfBound();
}

TSharedRef<SWidget> SDMXIPAddressEditWidget::GenerateLocalAdapterAddressComboBoxEntry(TSharedPtr<FString> InAddress)
{
	return
		SNew(STextBlock)
		.Text(FText::FromString(*InAddress))
		.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}
