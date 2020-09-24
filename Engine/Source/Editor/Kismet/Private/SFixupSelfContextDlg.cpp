// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFixupSelfContextDlg.h"
#include "Widgets/Input/SButton.h"
#include "BlueprintEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "FixupContextDialog"

TSharedRef<SWidget> FFixupSelfContextItem::CreateWidget(TArray<TSharedPtr<FString>>& InFixupOptions)
{
	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		[
			SNew(STextBlock)
			.Text(FText::FromName(FuncName))
		]
		+SHorizontalBox::Slot()
		[
			SAssignNew(ComboBox, STextComboBox)
			.OptionsSource(&InFixupOptions)
			.InitiallySelectedItem(InFixupOptions[0])
		];
}

void SFixupSelfContextDialog::Construct(const FArguments& InArgs, const TArray< UK2Node_CallFunction* >& InNodesToFixup, const FBlueprintEditor* InBlueprintEditorPtr, bool bInOtherPastedNodes)
{
	NodesToFixup = InNodesToFixup;
	BlueprintEditor = InBlueprintEditorPtr;
	bOtherNodes = bInOtherPastedNodes;

	Options.Add(MakeShared<FString>(LOCTEXT("DoNothing", "Do Nothing").ToString()));
	Options.Add(MakeShared<FString>(LOCTEXT("CreateMatchingFunction", "Create Matching Function in Blueprint").ToString()));
	Options.Add(MakeShared<FString>(LOCTEXT("RemoveNodes", "Remove Node(s)").ToString()));

	for (UK2Node_CallFunction* Node : NodesToFixup)
	{
		bool bIsNew = true;
		for (FListViewItem Func : FunctionsToFixup)
		{
			if (Func->FuncName == Node->GetFunctionName())
			{
				bIsNew = false;
				Func->Nodes.Add(Node);
			}
		}

		if (bIsNew)
		{
			FunctionsToFixup.Add(MakeShared<FFixupSelfContextItem>(Node->GetFunctionName()));
			FunctionsToFixup.Last()->Nodes.Add(Node);
		}
	}

	ChildSlot
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FixupDescription", "Some function references could not be resolved in the new context. How would you like to fix them?"))
				.AutoWrapText(true)
			]

			+SVerticalBox::Slot()
			.Padding(5.0f, 5.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(100.0f)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
					[
						SNew(SListView<FListViewItem>)
						.ItemHeight(24.0f)
						.ListItemsSource(&FunctionsToFixup)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SFixupSelfContextDialog::OnGenerateRow)
					]
				]
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(5.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoNodesWarning", "WARNING: Nothing will be pasted!"))
					.ColorAndOpacity(FSlateColor(FLinearColor::Yellow))
					.Visibility(this, &SFixupSelfContextDialog::GetNoneWarningVisibility)
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Bottom)
					.Padding(5.0f, 3.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Cancel", "Cancel"))
						.OnClicked(this, &SFixupSelfContextDialog::CloseWindow, false)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Bottom)
					.Padding(5.0f, 3.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Confirm", "Confirm"))
						.OnClicked(this, &SFixupSelfContextDialog::CloseWindow, true)
					]
				]
			]
		];
}

bool SFixupSelfContextDialog::CreateModal(const TArray<UK2Node_CallFunction*>& NodesToFixup, const FBlueprintEditor* BlueprintEditor, bool bOtherPastedNodes)
{
	TSharedPtr<SWindow> Window;
	TSharedPtr<SFixupSelfContextDialog> Widget;

	Window = SNew(SWindow)
		.Title(LOCTEXT("FixupReferencesTitle", "Fix Self Context Function References"))
		.SizingRule(ESizingRule::UserSized)
		.MinWidth(400.f)
		.MinHeight(300.f)
		.SupportsMaximize(true)
		.SupportsMinimize(false)
		.HasCloseButton(false)
		[
			SNew(SBorder)
			.Padding(4.f)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SAssignNew(Widget, SFixupSelfContextDialog, NodesToFixup, BlueprintEditor, bOtherPastedNodes)
			]
		];

	Widget->MyWindow = Window;

	GEditor->EditorAddModalWindow(Window.ToSharedRef());

	return Widget->bOutConfirmed;
}

TSharedRef<ITableRow> SFixupSelfContextDialog::OnGenerateRow(FListViewItem Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<FListViewItem>, OwnerTable)
		[
			Item->CreateWidget(Options)
		];
}

EVisibility SFixupSelfContextDialog::GetNoneWarningVisibility() const
{
	if (bOtherNodes)
	{
		return EVisibility::Hidden;
	}

	for (FListViewItem Item : FunctionsToFixup)
	{
		if (Item->ComboBox.IsValid())
		{
			int32 Strategy;
			Options.Find(Item->ComboBox->GetSelectedItem(), Strategy);

			if (FFixupSelfContextItem::EFixupStrategy(Strategy) != FFixupSelfContextItem::EFixupStrategy::RemoveNode)
			{
				return EVisibility::Hidden;
			}
		}
	}

	return EVisibility::Visible;
}

FReply SFixupSelfContextDialog::CloseWindow(bool bConfirmed)
{
	if (bConfirmed)
	{
		for (FListViewItem Item : FunctionsToFixup)
		{
			int32 Strategy;
			Options.Find(Item->ComboBox->GetSelectedItem(), Strategy);

			switch (FFixupSelfContextItem::EFixupStrategy(Strategy))
			{
			case FFixupSelfContextItem::EFixupStrategy::DoNothing:
				break;
			case FFixupSelfContextItem::EFixupStrategy::CreateNewFunction:
				if (BlueprintEditor)
				{
					FBlueprintEditorUtils::CreateMatchingFunction(Item->Nodes[0], BlueprintEditor->GetDefaultSchema());
					for (UK2Node_CallFunction* Node : Item->Nodes)
					{
						Node->ReconstructNode();
					}
				}
				break;
			case FFixupSelfContextItem::EFixupStrategy::RemoveNode:
				for (UK2Node_CallFunction* Node : Item->Nodes)
				{
					Node->GetGraph()->RemoveNode(Node);
				}
				break;
			}
		}
	}

	bOutConfirmed = bConfirmed;
	MyWindow->RequestDestroyWindow();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
