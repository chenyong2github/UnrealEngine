// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCBehaviourSetAssetByPath.h"

#include "IDetailTreeNode.h"
#include "SPositiveActionButton.h"
#include "SlateOptMacros.h"
#include "UI/Behaviour/Builtin/RCBehaviourSetAssetByPathModel.h"
#include "UI/RemoteControlPanelStyle.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SRCBehaviourSetAssetByPath"

class SPositiveActionButton;

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourSetAssetByPath::Construct(const FArguments& InArgs, TSharedRef<const FRCSetAssetByPathBehaviourModel> InBehaviourItem)
{
	

	TSharedRef<SHorizontalBox> SelectedClassWidget = SNew(SHorizontalBox);
	URCSetAssetByPathBehaviour* PathBehaviour = Cast<URCSetAssetByPathBehaviour>(InBehaviourItem->GetBehaviour());

	if (!PathBehaviour)
	{
		ChildSlot
		[
			SNullWidget::NullWidget
		];
		return;
	}
		
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
				FSlateColor Color = PathBehaviour->AssetClass == SupportedClass ? FAppStyle::Get().GetSlateColor("Colors.AccentBlue") : FAppStyle::Get().GetSlateColor("Colors.AccentWhite");
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
