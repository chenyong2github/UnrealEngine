// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCBehaviourSetAssetByPath.h"

#include "IDetailTreeNode.h"
#include "RemoteControlPreset.h"
#include "SPositiveActionButton.h"
#include "SlateOptMacros.h"
#include "Styling/StyleColors.h"
#include "UI/Behaviour/Builtin/Path/RCBehaviourSetAssetByPathModel.h"
#include "UI/RemoteControlPanelStyle.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SRCBehaviourSetAssetByPath"

class SPositiveActionButton;

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourSetAssetByPath::Construct(const FArguments& InArgs, TSharedRef<FRCSetAssetByPathBehaviourModel> InBehaviourItem)
{
	URCSetAssetByPathBehaviour* PathBehaviour = Cast<URCSetAssetByPathBehaviour>(InBehaviourItem->GetBehaviour());
	TSharedRef<SHorizontalBox> InternalExternalSwitchWidget = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> SelectedClassWidget = SNew(SHorizontalBox)
		.Visibility_Lambda([PathBehaviour]()
		{
			return PathBehaviour->bInternal ? EVisibility::Visible : EVisibility::Collapsed;
		});

	if (!PathBehaviour)
	{
		ChildSlot
		[
			SNullWidget::NullWidget
		];
		return;
	}
		
	InternalExternalSwitchWidget->AddSlot()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Center)
	.Padding(FMargin(0.0f,0.0f))
	.FillWidth(1.0f)
	.AutoWidth()
	[
		SNew(SButton)
		.Text(FText::FromString(FString("Internal")))
		.ButtonColorAndOpacity_Lambda([PathBehaviour]()
		{
			FSlateColor Color = PathBehaviour->bInternal ? FAppStyle::Get().GetSlateColor("Colors.Highlight") : FAppStyle::Get().GetSlateColor("Colors.AccentWhite");
			return Color;
		})
		.OnPressed_Lambda([PathBehaviour]()
		{
			PathBehaviour->bInternal = true;
		})
	];
	
	InternalExternalSwitchWidget->AddSlot()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Center)
	.Padding(FMargin(0.0f,0.0f))
	.FillWidth(1.0f)
	.AutoWidth()
	[
		SNew(SButton)
		.Text(FText::FromString(FString("External")))
		.ButtonColorAndOpacity_Lambda([PathBehaviour]()
		{
			FSlateColor Color = PathBehaviour->bInternal ? FAppStyle::Get().GetSlateColor("Colors.AccentWhite") : FAppStyle::Get().GetSlateColor("Colors.Highlight");
			return Color;
		})
		.OnPressed_Lambda([PathBehaviour]()
		{
			PathBehaviour->bInternal = false;
		})
	];
	
	for (UClass* SupportedClass : PathBehaviour->GetSupportedClasses())
	{
		SelectedClassWidget->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.0f,0.0f))
		.FillWidth(1.0f)
		.AutoWidth()
		[
			SNew(SButton)
			.Text(FText::FromString(SupportedClass->GetName()))
			.ButtonColorAndOpacity_Lambda([PathBehaviour, SupportedClass]()
			{
				FSlateColor Color = PathBehaviour->AssetClass == SupportedClass ? FAppStyle::Get().GetSlateColor("Colors.Select") : FAppStyle::Get().GetSlateColor("Colors.White");
				return Color;
			})
			.OnPressed_Lambda([PathBehaviour, SupportedClass]()
			{
				PathBehaviour->AssetClass = SupportedClass;
			})
		];
	}
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		[
			InternalExternalSwitchWidget
		]
		+ SVerticalBox::Slot()
		.Padding(0.f,12.f)
		[
			SelectedClassWidget
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			InBehaviourItem->GetPropertyWidget()
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
