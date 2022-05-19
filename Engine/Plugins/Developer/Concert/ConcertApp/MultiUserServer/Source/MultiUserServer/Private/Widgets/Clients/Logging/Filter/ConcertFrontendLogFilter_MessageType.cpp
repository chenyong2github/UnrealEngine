// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertFrontendLogFilter_MessageType.h"

#include "ConcertTransportEvents.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Clients/Logging/Util/MessageTypeUtils.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

FConcertLogFilter_MessageType::FConcertLogFilter_MessageType()
	: AllowedMessageTypeNames(UE::MultiUserServer::MessageTypeUtils::GetAllMessageTypeNames())
{}

bool FConcertLogFilter_MessageType::PassesFilter(const FConcertLog& InItem) const
{
	return IsMessageTypeAllowed(InItem.MessageTypeName);
}

void FConcertLogFilter_MessageType::AllowAll()
{
	TSet<FName> Allowed = UE::MultiUserServer::MessageTypeUtils::GetAllMessageTypeNames();
	if (Allowed.Num() != AllowedMessageTypeNames.Num())
	{
		AllowedMessageTypeNames = MoveTemp(Allowed);
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_MessageType::DisallowAll()
{
	if (AllowedMessageTypeNames.Num() > 0)
	{
		AllowedMessageTypeNames.Reset();
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_MessageType::ToggleAll(const TSet<FName>& ToToggle)
{
	for (const FName MessageTypeName : ToToggle)
	{
		if (IsMessageTypeAllowed(MessageTypeName))
		{
			DisallowMessageType(MessageTypeName);
		}
		else
		{
			AllowMessageType(MessageTypeName);
		}
	}

	if (ToToggle.Num() > 0)
	{
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_MessageType::AllowMessageType(FName MessageTypeName)
{
	if (!AllowedMessageTypeNames.Contains(MessageTypeName))
	{
		AllowedMessageTypeNames.Add(MessageTypeName);
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_MessageType::DisallowMessageType(FName MessageTypeName)
{
	if (AllowedMessageTypeNames.Contains(MessageTypeName))
	{
		AllowedMessageTypeNames.Remove(MessageTypeName);
		OnChanged().Broadcast();
	}
}

bool FConcertLogFilter_MessageType::IsMessageTypeAllowed(FName MessageTypeName) const
{
	return AllowedMessageTypeNames.Contains(MessageTypeName);
}

bool FConcertLogFilter_MessageType::AreAllAllowed() const
{
	return AllowedMessageTypeNames.Num() == UE::MultiUserServer::MessageTypeUtils::GetAllMessageTypeNames().Num();
}

uint8 FConcertLogFilter_MessageType::GetNumSelected() const
{
	return AllowedMessageTypeNames.Num();
}

FConcertFrontendLogFilter_MessageType::FConcertFrontendLogFilter_MessageType()
{
	ChildSlot = SNew(SHorizontalBox)
		.ToolTipText(LOCTEXT("MessageTypeFilter.ToolTipText", "Select a list of allowed message types\nHint: Type in menu to search"))

		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MessageTypeFilter.AllowBefore", "Message Types"))
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0, 0, 0)
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.OnGetMenuContent_Raw(this, &FConcertFrontendLogFilter_MessageType::MakeSelectionMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return Implementation.AreAllAllowed()
						? LOCTEXT("MessageTypeFilter.Selection.All", "All")
						: FText::FromString(FString::FromInt(Implementation.GetNumSelected()));
				})
			]
		];
		
}

TSharedRef<SWidget> FConcertFrontendLogFilter_MessageType::MakeSelectionMenu()
{
	FMenuBuilder MenuBuilder(false, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTypeFilter.SelectAll.", "Select all"),
		LOCTEXT("MessageTypeFilter.SelectAll.Tooltip", "Allows all message types"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.AllowAll(); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTypeFilter.DeselectAll.", "Deselect all"),
		LOCTEXT("MessageTypeFilter.DeelectAll.Tooltip", "Disallows all message types"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.DisallowAll(); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	
	MenuBuilder.AddSeparator();
	
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTypeFilter.Events.", "Toggle Events"),
		LOCTEXT("MessageTypeFilter.Events.Tooltip", "Toggles all event data"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.ToggleAll(UE::MultiUserServer::MessageTypeUtils::GetAllMessageTypeNames_EventsOnly()); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTypeFilter.Requests.", "Toggle Requests"),
		LOCTEXT("MessageTypeFilter.Requests.Tooltip", "Toggles all requests"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.ToggleAll(UE::MultiUserServer::MessageTypeUtils::GetAllMessageTypeNames_RequestsOnly()); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTypeFilter.Responses.", "Toggle Responses"),
		LOCTEXT("MessageTypeFilter.Responses.Tooltip", "Toggles all responses"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.ToggleAll(UE::MultiUserServer::MessageTypeUtils::GetAllMessageTypeNames_ResponseOnlyOnly()); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTypeFilter.OnlyAck.", "Toggle ACKs"),
		LOCTEXT("MessageTypeFilter.OnlyAck.Tooltip", "Toggle ACKs"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.ToggleAll(UE::MultiUserServer::MessageTypeUtils::GetAllMessageTypeNames_AcksOnlyOnly()); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	
	MenuBuilder.AddSeparator();
	
	for (const FName MessageType : UE::MultiUserServer::MessageTypeUtils::GetAllMessageTypeNames())
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(UE::MultiUserServer::MessageTypeUtils::SanitizeMessageTypeName(MessageType)),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, MessageType]()
				{
					if (Implementation.IsMessageTypeAllowed(MessageType))
					{
						Implementation.DisallowMessageType(MessageType);
					}
					else
					{
						Implementation.AllowMessageType(MessageType);
					}
				}),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this, MessageType]() { return Implementation.IsMessageTypeAllowed(MessageType); })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
