// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCControllerPanel.h"

#include "Controller/RCController.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "RCControllerModel.h"
#include "RCVirtualPropertyContainer.h"
#include "RemoteControlLogicConfig.h"
#include "RemoteControlPreset.h"
#include "SlateOptMacros.h"
#include "SRCControllerPanelList.h"
#include "Styling/RemoteControlStyles.h"
#include "UI/BaseLogicUI/RCLogicModeBase.h"
#include "UI/Panels/SRCDockPanel.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "SRCControllerPanel"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCControllerPanel::Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlPanel>& InPanel)
{
	SRCLogicPanelBase::Construct(SRCLogicPanelBase::FArguments(), InPanel);
	
	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	// Controller Dock Panel
	TSharedPtr<SRCMinorPanel> ControllerDockPanel = SNew(SRCMinorPanel)
		.HeaderLabel(LOCTEXT("ControllersLabel", "Controller"))
		[
			SAssignNew(ControllerPanelList, SRCControllerPanelList, SharedThis(this))
		];

	// Add New Controller Button
	const TSharedRef<SWidget> AddNewControllerButton = SNew(SComboButton)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Add Controller")))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
		.ForegroundColor(FSlateColor::UseForeground())
		.CollapseMenuOnParentFocus(true)
		.HasDownArrow(false)
		.ContentPadding(FMargin(4.f, 2.f))
		.ButtonContent()
		[
			SNew(SBox)
			.WidthOverride(RCPanelStyle->IconSize.X)
			.HeightOverride(RCPanelStyle->IconSize.Y)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
			]
		]
		.MenuContent()
		[
			GetControllerMenuContentWidget()
		];

	// Empty All Button
	TSharedRef<SWidget> EmptyAllButton = SNew(SButton)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Empty Controllers")))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ForegroundColor(FSlateColor::UseForeground())
		.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
		.ToolTipText(LOCTEXT("EmptyAllToolTip", "Deletes all the controllers."))
		.OnClicked(this, &SRCControllerPanel::RequestDeleteAllItems)
		.Visibility_Lambda([this]() { return ControllerPanelList.IsValid() && !ControllerPanelList->IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBox)
			.WidthOverride(RCPanelStyle->IconSize.X)
			.HeightOverride(RCPanelStyle->IconSize.Y)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::GetBrush("Icons.Delete"))
			]
		];

	ControllerDockPanel->AddHeaderToolbarItem(EToolbar::Left, AddNewControllerButton);
	ControllerDockPanel->AddHeaderToolbarItem(EToolbar::Right, EmptyAllButton);

	ChildSlot
		.Padding(RCPanelStyle->PanelPadding)
		[
			ControllerDockPanel.ToSharedRef()
		];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

static UObject* GetBaseStructForType(FName StructType)
{
	if (StructType == NAME_Vector)
	{
		return TBaseStructure<FVector>::Get();
	}
	else if (StructType == NAME_Color)
	{
		return TBaseStructure<FColor>::Get();
	}
	else if (StructType == NAME_Rotator)
	{
		return TBaseStructure<FRotator>::Get();
	}

	ensureMsgf(false, TEXT("Found unsupported struct type %s in config."), *StructType.ToString());

	return nullptr;
}

bool SRCControllerPanel::IsListFocused() const
{
	return ControllerPanelList->IsListFocused();
}

void SRCControllerPanel::DeleteSelectedPanelItem()
{
	if (ControllerPanelList)
	{
		ControllerPanelList->DeleteSelectedPanelItem();
	}
}

FReply SRCControllerPanel::RequestDeleteAllItems()
{
	if (!ControllerPanelList.IsValid())
	{
		return FReply::Unhandled();
	}

	const FText WarningMessage = FText::Format(LOCTEXT("DeleteAllWarning", "You are about to delete '{0}' controllers. This action might not be undone.\nAre you sure you want to proceed?"), ControllerPanelList->Num());

	EAppReturnType::Type UserResponse = FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage);

	if (UserResponse == EAppReturnType::Yes)
	{
		return OnClickEmptyButton();
	}

	return FReply::Handled();
}

TSharedRef<SWidget> SRCControllerPanel::GetControllerMenuContentWidget() const
{
	constexpr  bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	TArray<TPair<EPropertyBagPropertyType, UObject*>> VirtualPropertyFieldClassNames;

	// See Config file: BaseRemoteControl.ini
	const URemoteControlLogicConfig* RCLogicConfig = GetDefault<URemoteControlLogicConfig>();
	for (const EPropertyBagPropertyType ControllerType : RCLogicConfig->SupportedControllerTypes)
	{
		if (ControllerType == EPropertyBagPropertyType::Struct)
		{
			for (const FName StructType : RCLogicConfig->SupportedControllerStructTypes)
			{
				UObject* ValueTypeObject = GetBaseStructForType(StructType);
				if (ensure(ValueTypeObject))
				{
					VirtualPropertyFieldClassNames.Add({ ControllerType, ValueTypeObject });
				}
			}
		}
		else
		{
			VirtualPropertyFieldClassNames.Add({ ControllerType, nullptr });
		}
	}

	// Generate a menu from the list of supported Controllers
	for (const TPair<EPropertyBagPropertyType, UObject*>& Pair : VirtualPropertyFieldClassNames)
	{
		// Display Name
		const UEnum* Enum = FindObjectChecked<UEnum>(nullptr, TEXT("/Script/StructUtils.EPropertyBagPropertyType"));
		FString PropertyTypeString = Enum->GetValueAsName(Pair.Key).ToString();
		PropertyTypeString.RemoveFromStart("EPropertyBagPropertyType::");

		if (PropertyTypeString.StartsWith(TEXT("Bool")))
		{
			PropertyTypeString = "Boolean";
		}
		
		if (PropertyTypeString.StartsWith(TEXT("Int")))
		{
			PropertyTypeString = PropertyTypeString.Replace(TEXT("Int"), TEXT("Integer"));
		}

		const FName DefaultName = Pair.Value ? Pair.Value->GetFName() : *PropertyTypeString;

		// Menu Item
		FUIAction Action(FExecuteAction::CreateSP(this, &SRCControllerPanel::OnAddControllerClicked, Pair.Key, Pair.Value));
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("AddController", "{0}"), FText::FromName(DefaultName)),
			FText::Format(LOCTEXT("AddControllerTooltip", "Add a controller of type {0}."), FText::FromName(DefaultName)),
			FSlateIcon(),
			Action);
	}

	return MenuBuilder.MakeWidget();
}

void SRCControllerPanel::OnAddControllerClicked(const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject) const
{
	// Add to the asset
	if (URemoteControlPreset* Preset = GetPreset())
	{
		URCVirtualPropertyInContainer* NewVirtualProperty = Preset->AddVirtualProperty(URCController::StaticClass(), InValueType, InValueTypeObject);

		// Refresh list
		const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = GetRemoteControlPanel();
		check(RemoteControlPanel);
		RemoteControlPanel->OnControllerAdded.Broadcast(NewVirtualProperty->PropertyName);
	}
}

void SRCControllerPanel::EnterRenameMode()
{
	if (ControllerPanelList)
	{
		ControllerPanelList->EnterRenameMode();
	}
}

FReply SRCControllerPanel::OnClickEmptyButton()
{
	if (URemoteControlPreset* Preset = GetPreset())
	{
		Preset->ResetVirtualProperties();
	}

	if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = GetRemoteControlPanel())
	{
		RemoteControlPanel->OnEmptyControllers.Broadcast();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
