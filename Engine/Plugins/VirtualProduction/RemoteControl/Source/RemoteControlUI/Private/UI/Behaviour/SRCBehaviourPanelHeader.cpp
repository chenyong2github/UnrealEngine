// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCBehaviourPanelHeader.h"

#include "Behaviour/RCBehaviourBlueprintNode.h"
#include "Behaviour/RCBehaviourNode.h"
#include "Controller/RCController.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "SlateOptMacros.h"
#include "SPositiveActionButton.h"
#include "SRCBehaviourPanel.h"
#include "UI/SRemoteControlPanel.h"
#include "UI/Controller/RCControllerModel.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SRCControllerPanelHeader"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourPanelHeader::Construct(const FArguments& InArgs, TSharedRef<SRCBehaviourPanel> InBehaviourPanel, TSharedPtr<FRCControllerModel> InControllerItem)
{
	SRCLogicPanelHeaderBase::Construct(SRCLogicPanelHeaderBase::FArguments());
	
	BehaviourPanelWeakPtr = InBehaviourPanel;
	ControllerItemWeakPtr = InControllerItem;

	// Add New Button
	const TSharedRef<SPositiveActionButton> AddNewMenu = SNew(SPositiveActionButton)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Add Behaviour")))
		.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
		.Text(LOCTEXT("AddBehaviourLabel", "Add Behaviour"))
		.OnGetMenuContent(this, &SRCBehaviourPanelHeader::GetBehaviourMenuContentWidget);
	
	ChildSlot
	[
		SNew(SHorizontalBox)
		// [Add New]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			AddNewMenu
		]
		// [Empty]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SButton)
			.ContentPadding(2.0f)
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.OnClicked(this, &SRCBehaviourPanelHeader::OnClickEmptyButton)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FColor::White)
				.Text(LOCTEXT("EmptyBehavioursButtonLabel", "Empty"))
			]
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

TSharedRef<SWidget> SRCBehaviourPanelHeader::GetBehaviourMenuContentWidget()
{
	constexpr bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	if (const TSharedPtr<FRCControllerModel> ControllerItem = ControllerItemWeakPtr.Pin())
	{
		if (URCController* Controller = Cast<URCController>(ControllerItem->GetVirtualProperty()))
		{
			for( TObjectIterator<UClass> It ; It ; ++It )
			{
				UClass* Class = *It;
				
				if( Class->IsChildOf(URCBehaviourNode::StaticClass()) )
				{
					const bool bIsClassInstantiatable = Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Abstract) || FKismetEditorUtilities::IsClassABlueprintSkeleton(Class);
					if (bIsClassInstantiatable)
					{
						continue;
					}
					
					if (Class->IsInBlueprint())
					{
						if (Class->GetSuperClass() != URCBehaviourBlueprintNode::StaticClass())
						{
							continue;
						}
					}
					
					URCBehaviour* Behaviour = Controller->CreateBehaviour(Class);
					if (!Behaviour)
					{
						continue;
					}
			
					FUIAction Action(FExecuteAction::CreateRaw(this, &SRCBehaviourPanelHeader::OnAddBehaviourClicked, Class));
					MenuBuilder.AddMenuEntry(
						FText::Format( LOCTEXT("AddBehaviourNode", "{0}"), FText::FromName(Class->GetFName())),
						FText::Format( LOCTEXT("AddBehaviourNodeTooltip", "{0}"), FText::FromName(Class->GetFName())),
						FSlateIcon(),
						Action);
				}
			}		
		}
	}
	
	return MenuBuilder.MakeWidget();
}

void SRCBehaviourPanelHeader::OnAddBehaviourClicked(UClass* InClass)
{
	if (const TSharedPtr<FRCControllerModel> ControllerItem = ControllerItemWeakPtr.Pin())
	{
		if (URCController* Controller = Cast<URCController>(ControllerItem->GetVirtualProperty()))
		{
			URCBehaviour* NewBehaviour = Controller->AddBehaviour(InClass);

			if (const TSharedPtr<SRCBehaviourPanel> BehaviourPanel = BehaviourPanelWeakPtr.Pin())
			{
				if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = BehaviourPanel->GetRemoteControlPanel())
				{
					RemoteControlPanel->OnBehaviourAdded.Broadcast(NewBehaviour);
				}
			}
		}
	}
}

FReply SRCBehaviourPanelHeader::OnClickEmptyButton()
{
	if (const TSharedPtr<FRCControllerModel> ControllerItem = ControllerItemWeakPtr.Pin())
	{
		if (URCController* Controller = Cast<URCController>(ControllerItem->GetVirtualProperty()))
		{
			Controller->EmptyBehaviours();
		}
	}

	const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = BehaviourPanelWeakPtr.Pin()->GetRemoteControlPanel();
	RemoteControlPanel->OnEmptyBehaviours.Broadcast();
	
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE