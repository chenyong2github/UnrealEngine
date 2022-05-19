// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertFrontendLogFilter_MessageAction.h"

#include "ConcertTransportEvents.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Clients/Logging/Util/MessageActionUtils.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

FConcertLogFilter_MessageAction::FConcertLogFilter_MessageAction()
	: AllowedMessageActionNames(UE::MultiUserServer::MessageActionUtils::GetAllMessageActionNames())
{}

bool FConcertLogFilter_MessageAction::PassesFilter(const FConcertLog& InItem) const
{
	return IsMessageActionAllowed(UE::MultiUserServer::MessageActionUtils::ConvertActionToName(InItem.MessageAction));
}

void FConcertLogFilter_MessageAction::AllowAll()
{
	TSet<FName> Allowed = UE::MultiUserServer::MessageActionUtils::GetAllMessageActionNames();
	if (Allowed.Num() != AllowedMessageActionNames.Num())
	{
		AllowedMessageActionNames = MoveTemp(Allowed);
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_MessageAction::DisallowAll()
{
	if (AllowedMessageActionNames.Num() > 0)
	{
		AllowedMessageActionNames.Reset();
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_MessageAction::ToggleAll(const TSet<FName>& ToToggle)
{
	for (const FName MessageTypeName : ToToggle)
	{
		if (IsMessageActionAllowed(MessageTypeName))
		{
			DisallowMessageAction(MessageTypeName);
		}
		else
		{
			AllowMessageAction(MessageTypeName);
		}
	}

	if (ToToggle.Num() > 0)
	{
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_MessageAction::AllowMessageAction(FName MessageTypeName)
{
	if (!AllowedMessageActionNames.Contains(MessageTypeName))
	{
		AllowedMessageActionNames.Add(MessageTypeName);
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_MessageAction::DisallowMessageAction(FName MessageTypeName)
{
	if (AllowedMessageActionNames.Contains(MessageTypeName))
	{
		AllowedMessageActionNames.Remove(MessageTypeName);
		OnChanged().Broadcast();
	}
}

bool FConcertLogFilter_MessageAction::IsMessageActionAllowed(FName MessageTypeName) const
{
	return AllowedMessageActionNames.Contains(MessageTypeName);
}

bool FConcertLogFilter_MessageAction::AreAllAllowed() const
{
	return AllowedMessageActionNames.Num() == UE::MultiUserServer::MessageActionUtils::GetAllMessageActionNames().Num();
}

uint8 FConcertLogFilter_MessageAction::GetNumSelected() const
{
	return AllowedMessageActionNames.Num();
}

FConcertFrontendLogFilter_MessageAction::FConcertFrontendLogFilter_MessageAction()
{
	ChildSlot = SNew(SHorizontalBox)
		.ToolTipText(LOCTEXT("MessageActionFilter.ToolTipText", "Select a list of allowed message actions\nHint: Type in menu to search"))

		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MessageActionFilter.AllowBefore", "Actions"))
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0, 0, 0)
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.OnGetMenuContent_Raw(this, &FConcertFrontendLogFilter_MessageAction::MakeSelectionMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return Implementation.AreAllAllowed()
						? LOCTEXT("MessageActionFilter.Selection.All", "All")
						: FText::FromString(FString::FromInt(Implementation.GetNumSelected()));
				})
			]
		];
		
}

TSharedRef<SWidget> FConcertFrontendLogFilter_MessageAction::MakeSelectionMenu()
{
	FMenuBuilder MenuBuilder(false, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageActionFilter.SelectAll.", "Select all"),
		LOCTEXT("MessageActionFilter.SelectAll.Tooltip", "Allows all message actions"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.AllowAll(); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageActionFilter.DeselectAll.", "Deselect all"),
		LOCTEXT("MessageActionFilter.DeelectAll.Tooltip", "Disallows all message actions"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.DisallowAll(); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	
	MenuBuilder.AddSeparator();
	
	for (const FName MessageAction : UE::MultiUserServer::MessageActionUtils::GetAllMessageActionNames())
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(UE::MultiUserServer::MessageActionUtils::GetActionDisplayString(MessageAction)),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, MessageAction]()
				{
					if (Implementation.IsMessageActionAllowed(MessageAction))
					{
						Implementation.DisallowMessageAction(MessageAction);
					}
					else
					{
						Implementation.AllowMessageAction(MessageAction);
					}
				}),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this, MessageAction]() { return Implementation.IsMessageActionAllowed(MessageAction); })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
