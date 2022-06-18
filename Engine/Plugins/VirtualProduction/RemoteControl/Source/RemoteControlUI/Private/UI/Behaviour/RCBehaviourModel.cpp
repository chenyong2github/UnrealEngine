// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCBehaviourModel.h"

#include "Behaviour/RCBehaviourNode.h"
#include "Engine/Blueprint.h"
#include "UI/BaseLogicUI/RCLogicHelpers.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FRCBehaviourModel"

FRCBehaviourModel::FRCBehaviourModel(URCBehaviour* InBehaviour)
	: BehaviourWeakPtr(InBehaviour)
{
	const FText BehaviorDisplayName = BehaviourWeakPtr->GetDisplayName().ToUpper();

	SAssignNew(BehaviourTitleText, STextBlock)
		.Text(BehaviorDisplayName)
		.Font(FRemoteControlPanelStyle::Get()->GetFontStyle("RemoteControlPanel.Behaviours.Title"));
}

TSharedRef<SWidget> FRCBehaviourModel::GetWidget() const
{
	if (!ensure(BehaviourWeakPtr.IsValid()))
	{
		return SNullWidget::NullWidget;
	}

	return SNew(SHorizontalBox)
		.Clipping(EWidgetClipping::OnDemand)
		// Behaviour name
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(FMargin(8.f))
		[
			BehaviourTitleText.ToSharedRef()
		];
}

TSharedRef<SWidget> FRCBehaviourModel::GetBehaviourDetailsWidget() const
{
	return SNullWidget::NullWidget;
}

void FRCBehaviourModel::OnOverrideBlueprint() const
{
	if (URCBehaviour* Behaviour = BehaviourWeakPtr.Get())
	{
		UBlueprint* Blueprint = Behaviour->GetBlueprint();
		if (!Blueprint)
		{
			Blueprint = UE::RCLogicHelpers::CreateBlueprintWithDialog(Behaviour->BehaviourNodeClass, Behaviour->GetPackage(), UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			Behaviour->SetOverrideBehaviourBlueprintClass(Blueprint);
		}

		UE::RCLogicHelpers::OpenBlueprintEditor(Blueprint);
	}
}

bool FRCBehaviourModel::IsBehaviourEnabled() const
{
	if (URCBehaviour* Behaviour = BehaviourWeakPtr.Get())
	{
		return Behaviour->bIsEnabled;
	}

	return false;
}

void FRCBehaviourModel::SetIsBehaviourEnabled(const bool bIsEnabled)
{
	if (URCBehaviour* Behaviour = BehaviourWeakPtr.Get())
	{
		Behaviour->bIsEnabled = bIsEnabled;

		BehaviourTitleText->SetEnabled(bIsEnabled);
	}
}

URCBehaviour* FRCBehaviourModel::GetBehaviour() const
{
	return BehaviourWeakPtr.Get();
}

#undef LOCTEXT_NAMESPACE