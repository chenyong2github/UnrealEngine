// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCProtocolList.h"

#include "Algo/Transform.h"
#include "EditorStyleSet.h"
#include "IRemoteControlProtocolModule.h"
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
				if(!SelectedProtocolName.IsValid())
				{
					Refresh();
				}
            	
            	if(SelectedProtocolName.IsValid())
            	{
            		return FText::FromName(*SelectedProtocolName);
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
		SelectedProtocolName = MakeShared<FName>(*InNewSelection);

		SelectionButton->SetIsOpen(false);

		Refresh();
	}
}

void SRCProtocolList::Refresh()
{
	AvailableProtocolNames.Empty();

	TArray<FName> ProtocolNames = IRemoteControlProtocolModule::Get().GetProtocolNames();
	if(ProtocolNames.Num() > 0)
	{
		if(!SelectedProtocolName.IsValid())
		{
			SelectedProtocolName = MakeShared<FName>(ProtocolNames[0]);
		}

		Algo::Transform(ProtocolNames, AvailableProtocolNames, [](const FName& InName)
		{
			return MakeShared<FName>(InName);
		});

		AvailableProtocolNames.RemoveSingle(SelectedProtocolName);
	}

	if(ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
