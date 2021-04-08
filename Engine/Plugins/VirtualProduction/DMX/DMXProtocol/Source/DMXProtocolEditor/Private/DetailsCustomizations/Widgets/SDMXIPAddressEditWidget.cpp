// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXIPAddressEditWidget.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolUtils.h"

#include "EditorStyleSet.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"


void SDMXIPAddressEditWidget::Construct(const FArguments& InArgs)
{
	OnIPAddressSelected = InArgs._OnIPAddressSelected;

	ChildSlot
	[
		SAssignNew(LocalAdapterAddressComboBox, SDMXLocalAdapterAddressComboBox)
		.OnLocalAdapterAddressSelected(this, &SDMXIPAddressEditWidget::OnIpAddressSelectedInChild)
	];

	const FString& InitialValue = InArgs._InitialValue;

	// Set the initial value to the local adapter address combo box if it exists
	const TArray<TSharedPtr<FString>>& LocalAdapterAddresses = LocalAdapterAddressComboBox->GetLocalAdapterAddresses();
	const int32 InitialValueIndex = LocalAdapterAddresses.IndexOfByPredicate([&InitialValue](const TSharedPtr<FString>& LocalAdapterAddress) {
		return *LocalAdapterAddress == InitialValue;
		});

	if (InitialValueIndex != INDEX_NONE)
	{
		LocalAdapterAddressComboBox->Select(LocalAdapterAddresses[InitialValueIndex]);
	}
	else if (LocalAdapterAddresses.Num() > 0)
	{
		LocalAdapterAddressComboBox->Select(LocalAdapterAddresses[0]);
	}
}

TSharedPtr<FString> SDMXIPAddressEditWidget::GetSelectedIPAddress() const
{
	check(LocalAdapterAddressComboBox.IsValid());

	return LocalAdapterAddressComboBox->GetSelectedLocalAdapterAddress();
}

void SDMXIPAddressEditWidget::OnIpAddressSelectedInChild()
{
	OnIPAddressSelected.ExecuteIfBound();
}


//////////////////////////////////////
// SDMXLocalAdapterAddressComboBox

void SDMXLocalAdapterAddressComboBox::Construct(const FArguments& InArgs)
{
	OnLocalAdapterAddressSelected = InArgs._OnLocalAdapterAddressSelected;

	InitializeLocalAdapterAddresses();

	const TSharedPtr<FString>* InitiallySelectedItemPtr = LocalAdapterAddressSource.FindByPredicate([&InArgs](const TSharedPtr<FString>& Item) {
			return *Item == InArgs._InitialSelection;
		});

	const TSharedPtr<FString> InitiallySelectedItem = InitiallySelectedItemPtr ? *InitiallySelectedItemPtr : nullptr;
	const FString InitiallySelectedString = InitiallySelectedItem.IsValid() ? *InitiallySelectedItem : TEXT("Network adapters changed");

	ChildSlot
	[
		SAssignNew(LocalAdapterAddressComboBox, SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&LocalAdapterAddressSource)
		.OnGenerateWidget(this, &SDMXLocalAdapterAddressComboBox::GenerateLocalAdapterAddressComboBoxEntry)
		.InitiallySelectedItem(InitiallySelectedItem)
		.OnSelectionChanged(this, &SDMXLocalAdapterAddressComboBox::HandleLocalAdapterAddressSelectionChanged)
		.Content()
		[
			SAssignNew(LocalAdapterAddressTextBlock, STextBlock)
			.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.Text(FText::FromString(InitiallySelectedString))
		]
	];
}

void SDMXLocalAdapterAddressComboBox::Select(const TSharedPtr<FString>& LocalAdapterAddress)
{
	check(LocalAdapterAddress.IsValid());
	check(LocalAdapterAddressSource.Contains(LocalAdapterAddress));

	LocalAdapterAddressComboBox->SetSelectedItem(LocalAdapterAddress);
}

TSharedRef<FString> SDMXLocalAdapterAddressComboBox::GetSelectedLocalAdapterAddress() const
{
	check(LocalAdapterAddressComboBox.IsValid());
	TSharedPtr<FString> SelectedItem = LocalAdapterAddressComboBox->GetSelectedItem();
	if (SelectedItem.IsValid())
	{
		return SelectedItem.ToSharedRef();
	}

	return MakeShared<FString>(TEXT("No network adapter avaialable"));
}

void SDMXLocalAdapterAddressComboBox::InitializeLocalAdapterAddresses()
{
	LocalAdapterAddressSource = FDMXProtocolUtils::GetLocalNetworkInterfaceCardIPs();
}
	
TSharedRef<SWidget> SDMXLocalAdapterAddressComboBox::GenerateLocalAdapterAddressComboBoxEntry(TSharedPtr<FString> InAddress)
{
	return
		SNew(STextBlock)
		.Text(FText::FromString(*InAddress))
		.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}

void SDMXLocalAdapterAddressComboBox::HandleLocalAdapterAddressSelectionChanged(TSharedPtr<FString> InAddress, ESelectInfo::Type InType)
{
	check(LocalAdapterAddressTextBlock.IsValid());
	LocalAdapterAddressTextBlock->SetText(FText::FromString(*InAddress));

	OnLocalAdapterAddressSelected.ExecuteIfBound();
}
