// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCControllerPanelHeader.h"

#include "Controller/RCController.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "RCVirtualPropertyContainer.h"
#include "RemoteControlLogicConfig.h"
#include "RemoteControlPreset.h"
#include "SlateOptMacros.h"
#include "SPositiveActionButton.h"
#include "SRCControllerPanel.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SRCControllerPanelHeader"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCControllerPanelHeader::Construct(const FArguments& InArgs, const TSharedRef<SRCControllerPanel> InControllerPanel)
{
	SRCLogicPanelHeaderBase::Construct(SRCLogicPanelHeaderBase::FArguments());
	
	ControllerPanelWeakPtr = InControllerPanel;

	const TSharedRef<SPositiveActionButton> AddNewMenu = SNew(SPositiveActionButton)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Add Controller")))
		.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
		.Text(LOCTEXT("AddControllerLabel", "Add Controller"))
		.OnGetMenuContent(this, &SRCControllerPanelHeader::GetControllerMenuContentWidget);
	
	ChildSlot
	[
		SNew(SHorizontalBox)
		// [Add]
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
			.OnClicked(this, &SRCControllerPanelHeader::OnClickEmptyButton)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FColor::White)
				.Text(LOCTEXT("EmptyAllControllersButtonLabel", "Empty"))
			]
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

static UObject* GetBaseStructForType(FName StructType)
{
	if (StructType == "FVector")
	{
		return TBaseStructure<FVector>::Get();
	}
	else if (StructType == "FColor")
	{
		return TBaseStructure<FColor>::Get();
	}
	else if (StructType == "FRotator")
	{
		return TBaseStructure<FRotator>::Get();
	}

	ensureMsgf(false, TEXT("Found unsupported struct type %s in config."), *StructType.ToString());

	return nullptr;
}

TSharedRef<SWidget> SRCControllerPanelHeader::GetControllerMenuContentWidget() const
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

		const FName DefaultName = Pair.Value ? Pair.Value->GetFName() : *PropertyTypeString;
		
		// Menu Item
		FUIAction Action(FExecuteAction::CreateRaw(this, &SRCControllerPanelHeader::OnAddControllerClicked, Pair.Key, Pair.Value));
		MenuBuilder.AddMenuEntry(
			FText::Format( LOCTEXT("AddController", "{0}"), FText::FromName(DefaultName)),
			FText::Format( LOCTEXT("AddControllerTooltip", "{0}"), FText::FromName(DefaultName)),
			FSlateIcon(),
			Action);
	}

	return MenuBuilder.MakeWidget();
}

void SRCControllerPanelHeader::OnAddControllerClicked(const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject) const
{
	// Add to the asset
	if (URemoteControlPreset* Preset = ControllerPanelWeakPtr.Pin()->GetPreset())
	{
		URCVirtualPropertyInContainer* NewVirtualProperty = Preset->AddVirtualProperty(URCController::StaticClass(), InValueType, InValueTypeObject);
		
		// Refresh list
		const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ControllerPanelWeakPtr.Pin()->GetRemoteControlPanel();
		check(RemoteControlPanel)
		RemoteControlPanel->OnControllerAdded.Broadcast(NewVirtualProperty->PropertyName);
	}
}

FReply SRCControllerPanelHeader::OnClickEmptyButton()
{
	if (TSharedPtr<SRCControllerPanel> ControllerPanel = ControllerPanelWeakPtr.Pin())
	{
		if (URemoteControlPreset* Preset = ControllerPanel->GetPreset())
		{
			Preset->ResetVirtualProperties();
		}

		const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = ControllerPanelWeakPtr.Pin()->GetRemoteControlPanel();
		RemoteControlPanel->OnEmptyControllers.Broadcast();
	}
	
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE