// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCActionPanelFooter.h"

#include "Action/RCActionContainer.h"
#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"
#include "Behaviour/RCBehaviour.h"
#include "Controller/RCController.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "RemoteControlPreset.h"
#include "SlateOptMacros.h"
#include "SPositiveActionButton.h"
#include "SRCActionPanel.h"
#include "Styling/CoreStyle.h"
#include "UI/SRemoteControlPanel.h"
#include "UI/Behaviour/RCBehaviourModel.h"
#include "UI/BaseLogicUI/RCLogicHelpers.h"
#include "UI/RemoteControlPanelStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSeparator.h"


#define LOCTEXT_NAMESPACE "SRCActionPanelFooter"

class SPositiveActionButton;

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCActionPanelFooter::Construct(const FArguments& InArgs, TSharedRef<SRCActionPanel> InActionPanel, TSharedRef<FRCBehaviourModel> InBehaviourItem)
{
	SRCLogicPanelHeaderBase::Construct(SRCLogicPanelHeaderBase::FArguments());

	ActionPanelWeakPtr = InActionPanel;
	BehaviourItemWeakPtr = InBehaviourItem;
	
	const TSharedRef<SPositiveActionButton> AddNewMenu = SNew(SPositiveActionButton)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Add Action")))
		.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
		.Text(LOCTEXT("AddActionLabel", "Add Action"))
		.OnGetMenuContent(this, &SRCActionPanelFooter::GetActionMenuContentWidget);
	
	ChildSlot
	[
		SNew(SHorizontalBox)
		// Add new
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			AddNewMenu
		]

		// Add All
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.ContentPadding(2.0f)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.OnClicked(this, &SRCActionPanelFooter::OnAddAllField)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FColor::White)
				.Text(LOCTEXT("AddAllLabel", "Add All"))
			]
		]
		
		// Empty
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.ContentPadding(2.0f)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.OnClicked(this, &SRCActionPanelFooter::OnClickEmptyButton)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FColor::White)
				.Text(LOCTEXT("EmptyActionsButtonLabel", "Empty"))
			]
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

TSharedRef<SWidget> SRCActionPanelFooter::GetActionMenuContentWidget()
{
	constexpr  bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	// List of exposed entities
	if (URemoteControlPreset* Preset = ActionPanelWeakPtr.Pin()->GetPreset())
	{
		const TArray<TWeakPtr<FRemoteControlField>>& RemoteControlFields = Preset->GetExposedEntities<FRemoteControlField>();
		for (const TWeakPtr<FRemoteControlField>& RemoteControlFieldWeakPtr : RemoteControlFields)
		{
			if (const TSharedPtr<FRemoteControlField> RemoteControlField = RemoteControlFieldWeakPtr.Pin())
			{
				if (const URCBehaviour* Behaviour = BehaviourItemWeakPtr.Pin()->GetBehaviour())
				{
					// Skip if we already have an Action created for this exposed entity
					if (Behaviour->ActionContainer->FindActionByFieldId(RemoteControlField->GetId()))
					{
						continue;
					}

					// Create menu entry
					FUIAction Action(FExecuteAction::CreateRaw(this, &SRCActionPanelFooter::OnAddActionClicked, RemoteControlField));
					MenuBuilder.AddMenuEntry(
						FText::Format( LOCTEXT("AddAction", "{0}"), FText::FromName(RemoteControlField->GetLabel())),
						FText::Format( LOCTEXT( "AddActionTooltip", "{0}"), FText::FromName(RemoteControlField->GetLabel())),
						FSlateIcon(),
						MoveTemp(Action));
				}
			}
		}
	}
	
	return MenuBuilder.MakeWidget();
}

void SRCActionPanelFooter::OnAddActionClicked(TSharedPtr<FRemoteControlField> InRemoteControlField)
{
	if (!ensure(BehaviourItemWeakPtr.IsValid()))
	{
		return;
	}

	if (const URCBehaviour* Behaviour = BehaviourItemWeakPtr.Pin()->GetBehaviour())
	{
		URCAction* NewAction = nullptr;
		
		if (InRemoteControlField->FieldType == EExposedFieldType::Property)
		{
			NewAction = Behaviour->ActionContainer->AddAction(StaticCastSharedPtr<FRemoteControlProperty>(InRemoteControlField));
		}
		else if (InRemoteControlField->FieldType == EExposedFieldType::Function)
		{
			NewAction = Behaviour->ActionContainer->AddAction(StaticCastSharedPtr<FRemoteControlFunction>(InRemoteControlField));
		}
		
		if (const TSharedPtr<SRCActionPanel> ActionPanel = ActionPanelWeakPtr.Pin())
		{
			if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ActionPanel->GetRemoteControlPanel())
			{
				RemoteControlPanel->OnActionAdded.Broadcast(NewAction);
			}
		}
	}
}

FReply SRCActionPanelFooter::OnClickEmptyButton()
{
	if (const TSharedPtr<FRCBehaviourModel> BehaviourItem = BehaviourItemWeakPtr.Pin())
	{
		if (const URCBehaviour* Behaviour = BehaviourItem->GetBehaviour())
		{
			Behaviour->ActionContainer->EmptyActions();
		}
	}

	const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ActionPanelWeakPtr.Pin()->GetRemoteControlPanel();
	RemoteControlPanel->OnEmptyActions.Broadcast();
	
	return FReply::Handled();
}

FReply SRCActionPanelFooter::OnAddAllField()
{
	if (!ActionPanelWeakPtr.IsValid() || !BehaviourItemWeakPtr.IsValid())
	{
		return FReply::Handled();
	}

	TSharedPtr<SRCActionPanel> ActionPanel = ActionPanelWeakPtr.Pin();

	if (URemoteControlPreset* Preset = ActionPanel->GetPreset())
	{
		if (const URCBehaviour* Behaviour = BehaviourItemWeakPtr.Pin()->GetBehaviour())
		{
			const TArray<TWeakPtr<FRemoteControlField>>& RemoteControlFields = Preset->GetExposedEntities<FRemoteControlField>();

			// Enumerate the list of Exposed Entities and Functions available in this Preset for our use as Actions
			for (const TWeakPtr<FRemoteControlField>& RemoteControlFieldWeakPtr : RemoteControlFields)
			{
				if (const TSharedPtr<FRemoteControlField> RemoteControlField = RemoteControlFieldWeakPtr.Pin())
				{
					URCAction* NewAction = nullptr;

					// Property Action
					if (RemoteControlField->FieldType == EExposedFieldType::Property)
					{
						NewAction = Behaviour->ActionContainer->AddAction(StaticCastSharedPtr<FRemoteControlProperty>(RemoteControlField));
					}
					// Function Action
					else if (RemoteControlField->FieldType == EExposedFieldType::Function)
					{
						NewAction = Behaviour->ActionContainer->AddAction(StaticCastSharedPtr<FRemoteControlFunction>(RemoteControlField));
					}

					// Broadcast New Action
					if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ActionPanel->GetRemoteControlPanel())
					{
						RemoteControlPanel->OnActionAdded.Broadcast(NewAction);
					}
				}
			}
		}
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE