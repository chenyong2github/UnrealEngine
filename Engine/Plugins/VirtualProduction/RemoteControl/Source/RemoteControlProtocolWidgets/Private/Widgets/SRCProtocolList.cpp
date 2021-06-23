// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCProtocolList.h"

#include "EditorStyleSet.h"
#include "IRemoteControlProtocolModule.h"
#include "RemoteControlProtocolWidgetsSettings.h"
#include "Algo/Transform.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

#define LOCTEXT_NAMESPACE "RemoteControlProtocolWidgets"

void SRCProtocolList::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(SelectionButton, SComboButton)
		.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
        .ForegroundColor(FSlateColor::UseForeground())
        .ButtonContent()
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
			.Text_Lambda([this]()
			{
				if (!SelectedProtocolName.IsValid())
				{
					SelectedProtocolName = MakeShared<FName>(GetMutableDefault<URemoteControlProtocolWidgetsSettings>()->PreferredProtocol);
					Refresh();
				}

				const TSharedPtr<FName> CurrentSelectedProtocolName = GetSelectedProtocolNameInternal();
				if (CurrentSelectedProtocolName.IsValid())
				{
					return FText::FromName(*CurrentSelectedProtocolName);
				}

				return FText::GetEmpty();
			})
		]
		.MenuContent()
		[
			SAssignNew(ListView, SListView<TSharedPtr<FName>>)
            .ListItemsSource(&AvailableProtocolNames)
            .OnGenerateRow(this, &SRCProtocolList::ConstructListItem)
            .OnSelectionChanged(this, &SRCProtocolList::OnSelectionChanged)
		]
	];
}

TSharedRef<ITableRow> SRCProtocolList::ConstructListItem(TSharedPtr<FName> InItem, const TSharedRef<STableViewBase>& InOwnerTable) const
{
	FText DisplayText;
	if (InItem.IsValid() && !InItem->IsNone())
	{
		DisplayText = FText::FromName(*InItem);
	}

	return SNew(STableRow<TSharedPtr<FText>>, InOwnerTable)
	[
		SNew(STextBlock)
		.Text(DisplayText)
	];
}

void SRCProtocolList::OnSelectionChanged(TSharedPtr<FName> InNewSelection, ESelectInfo::Type InSelectInfo)
{
	if (InNewSelection.IsValid())
	{
		SetSelectedProtocolName(InNewSelection);
		SelectionButton->SetIsOpen(false);

		Refresh();
	}
}

void SRCProtocolList::Refresh()
{
	AvailableProtocolNames.Empty();

	TArray<FName> ProtocolNames = IRemoteControlProtocolModule::Get().GetProtocolNames();
	if (ProtocolNames.Num() > 0)
	{
		ProtocolNames.Sort(FNameLexicalLess());

		Algo::Transform(ProtocolNames, AvailableProtocolNames, [](const FName& InName)
		{
			return MakeShared<FName>(InName);
		});
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedPtr<FName>& SRCProtocolList::GetSelectedProtocolNameInternal()
{
	TArray<FName> ProtocolNames = IRemoteControlProtocolModule::Get().GetProtocolNames();

	// No protocols available, return none
	if (ProtocolNames.Num() == 0)
	{
		SetSelectedProtocolName(nullptr);
		return SelectedProtocolName;
	}

	// None selected, select and return first available
	if (!SelectedProtocolName.IsValid() || SelectedProtocolName->IsNone())
	{
		SetSelectedProtocolName(MakeShared<FName>(ProtocolNames[0]));
		return SelectedProtocolName;
	}

	// Otherwise a protocol is selected, make sure its available
	if (!ProtocolNames.Contains(*SelectedProtocolName.Get()))
	{
		SetSelectedProtocolName(MakeShared<FName>(ProtocolNames[0]));
		return SelectedProtocolName;
	}

	return SelectedProtocolName;
}

void SRCProtocolList::SetSelectedProtocolName(const TSharedPtr<FName> InProtocolName)
{
	if(InProtocolName == SelectedProtocolName)
	{
		return;
	}
	
	SelectedProtocolName = InProtocolName;
	URemoteControlProtocolWidgetsSettings* Settings = GetMutableDefault<URemoteControlProtocolWidgetsSettings>();
	Settings->PreferredProtocol = InProtocolName.IsValid() ? *SelectedProtocolName : NAME_None;
	Settings->PostEditChange();
	Settings->SaveConfig();
}

#undef LOCTEXT_NAMESPACE
