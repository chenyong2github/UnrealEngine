// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXIPAddressEditWidget.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"

#include "EditorStyleSet.h"
#include "IPAddress.h"
#include "SocketSubsystem.h" 
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"


void SDMXIPAddressEditWidget::Construct(const FArguments& InArgs)
{
	Mode = InArgs._Mode;
	OnIPAddressSelected = InArgs._OnIPAddressSelected;

	ChildSlot
	[
		SAssignNew(WidgetSwitcher, SWidgetSwitcher)
		
		+ SWidgetSwitcher::Slot()
		[
			SAssignNew(LocalAdapterAddressComboBox, SDMXLocalAdapterAddressComboBox)
			.OnLocalAdapterAddressSelected(this, &SDMXIPAddressEditWidget::OnIpAddressSelectedInChild)
		]

		+ SWidgetSwitcher::Slot()
		[
			SAssignNew(IPAddressEditableTextBox, SEditableTextBox)
			.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.IsReadOnly(false)
			.OnTextCommitted(this, &SDMXIPAddressEditWidget::OnIPAddressCommitted)
		]
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

	// Set the initial value to the editable text block widget
	IPAddressEditableTextBox->SetText(FText::FromString(InitialValue));

	SetEditModeInternal(Mode);
}

TSharedPtr<FString> SDMXIPAddressEditWidget::GetSelectedIPAddress() const
{
	check(LocalAdapterAddressComboBox.IsValid());
	check(IPAddressEditableTextBox.IsValid());

	if (Mode == EDMXIPEditWidgetMode::LocalAdapterAddresses)
	{
		return LocalAdapterAddressComboBox->GetSelectedLocalAdapterAddress();
	}
	else if (Mode == EDMXIPEditWidgetMode::EditableTextBox)
	{
		return MakeShared<FString>(EditableTextBoxString);
	}
	else
	{
		// Unhandled mode
		checkNoEntry();
	}

	return MakeShared<FString>();
}

void SDMXIPAddressEditWidget::SetEditMode(EDMXIPEditWidgetMode NewMode)
{
	if (Mode == NewMode)
	{
		return;
	}

	SetEditModeInternal(NewMode);

	OnIpAddressSelectedInChild();
}

void SDMXIPAddressEditWidget::SetEditModeInternal(EDMXIPEditWidgetMode NewMode)
{
	check(WidgetSwitcher.IsValid());
	check(LocalAdapterAddressComboBox.IsValid());

	Mode = NewMode;
	if (Mode == EDMXIPEditWidgetMode::LocalAdapterAddresses)
	{
		WidgetSwitcher->SetActiveWidget(LocalAdapterAddressComboBox.ToSharedRef());
	}
	else if (Mode == EDMXIPEditWidgetMode::EditableTextBox)
	{
		WidgetSwitcher->SetActiveWidget(IPAddressEditableTextBox.ToSharedRef());
	}
	else
	{
		// Unhandled mode
		checkNoEntry();
	}
}

void SDMXIPAddressEditWidget::OnIPAddressCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
{
	check(IPAddressEditableTextBox.IsValid());

	EditableTextBoxString = InNewText.ToString();

	// Convert back to text to reflect lossy conversion
	IPAddressEditableTextBox->SetText(FText::FromString(EditableTextBoxString));

	OnIpAddressSelectedInChild();
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
#if PLATFORM_WINDOWS
	// Add the default route IP Address, only for windows
	const FString DefaultRouteLocalAdapterAddress = TEXT("0.0.0.0");
	LocalAdapterAddressSource.Add(MakeShared<FString>(DefaultRouteLocalAdapterAddress));
#endif 
	// Add the local host IP address
	const FString LocalHostIpAddress = TEXT("127.0.0.1");
	LocalAdapterAddressSource.Add(MakeShared<FString>(LocalHostIpAddress));

	TArray<TSharedPtr<FInternetAddr>> Addresses;
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalAdapterAddresses(Addresses);
	bool bFoundLocalHost = false;
	for (TSharedPtr<FInternetAddr> Address : Addresses)
	{
		// Add unique, so in ase the OS call returns with the local host or default route IP, we don't add it twice
		LocalAdapterAddressSource.AddUnique(MakeShared<FString>(Address->ToString(false)));
	}
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
