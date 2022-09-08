// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/UIFButton.h"
#include "UIFPlayerComponent.h"

#include "Components/Button.h"
#include "Components/ButtonSlot.h"

#include "Engine/ActorChannel.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"


/**
 * 
 */
UUIFrameworkButton::UUIFrameworkButton()
{
	WidgetClass = UButton::StaticClass();
}

void UUIFrameworkButton::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, Slot, Params);
}

void UUIFrameworkButton::SetContent(FUIFrameworkSimpleSlot InEntry)
{
	bool bWidgetIsDifferent = Slot.GetWidget() != InEntry.GetWidget();
	if (bWidgetIsDifferent)
	{
		if (Slot.GetWidget())
		{
			Slot.GetWidget()->AuthoritySetParent(nullptr, FUIFrameworkParentWidget());
		}

		if (InEntry.GetWidget())
		{
			UUIFrameworkPlayerComponent* PreviousOwner = InEntry.GetWidget()->GetPlayerComponent();
			if (PreviousOwner != nullptr && PreviousOwner != GetPlayerComponent())
			{
				Slot.SetWidget(nullptr);
				FFrame::KismetExecutionMessage(TEXT("The widget was created for another player. It can't be added."), ELogVerbosity::Warning, "InvalidPlayerParent");
			}
		}
	}

	Slot = InEntry;
	Slot.SetWidget(InEntry.GetWidget()); // to make sure the id is set

	if (bWidgetIsDifferent && Slot.GetWidget())
	{
		Slot.GetWidget()->AuthoritySetParent(GetPlayerComponent(), FUIFrameworkParentWidget(this));
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, Slot, this);
}


void UUIFrameworkButton::AuthorityForEachChildren(const TFunctionRef<void(UUIFrameworkWidget*)>& Func)
{
	Super::AuthorityForEachChildren(Func);
	Func(Slot.GetWidget());
}

void UUIFrameworkButton::AuthorityRemoveChild(UUIFrameworkWidget* Widget)
{
	Super::AuthorityRemoveChild(Widget);
	ensure(Widget == Slot.GetWidget());

	Slot.SetWidget(nullptr);;
	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, Slot, this);
}

void UUIFrameworkButton::LocalOnUMGWidgetCreated()
{
	Super::LocalOnUMGWidgetCreated();
	UButton* Button = CastChecked<UButton>(LocalGetUMGWidget());
	Button->OnClicked.AddUniqueDynamic(this, &ThisClass::ServerClick);
}


void UUIFrameworkButton::LocalAddChild(UUIFrameworkWidget* Child)
{
	Super::LocalAddChild(Child);
	if (ensure(Child && Child->GetWidgetId() == Slot.GetWidgetId()))
	{
		UWidget* ChildWidget = Child->LocalGetUMGWidget();
		if (ensure(ChildWidget))
		{
			UButton* Button = CastChecked<UButton>(LocalGetUMGWidget());
			Button->AddChild(ChildWidget);
			Slot.LocalPreviousWidgetId = Slot.GetWidgetId();
		}
	}
}

void UUIFrameworkButton::ServerClick_Implementation()
{
	if (GetPlayerComponent())
	{
		FUIFrameworkClickEventArgument Argument;
		Argument.PlayerController = GetPlayerComponent()->GetPlayerController();
		Argument.Sender = this;
		OnClick.Broadcast(Argument);
	}
}

void UUIFrameworkButton::OnRep_Slot()
{
	if (LocalGetUMGWidget() && Slot.GetWidget() && Slot.GetWidgetId() == Slot.LocalPreviousWidgetId)
	{
		UButtonSlot* ButtonSlot = CastChecked<UButtonSlot>(LocalGetUMGWidget()->Slot);
		ButtonSlot->SetPadding(Slot.Padding);
		ButtonSlot->SetHorizontalAlignment(Slot.HorizontalAlignment);
		ButtonSlot->SetVerticalAlignment(Slot.VerticalAlignment);
	}
	// else do not do anything, the slot was not added yet or it was modified but was not applied yet by the PlayerComponent
}
