// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layers/UIFCanvasLayer.h"
#include "Layers/UIFCanvasLayerUserWidget.h"

#include "UIFPlayerComponent.h"
#include "UIFWidget.h"

#include "Engine/ActorChannel.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"


 /**
 *
 */
void FUIFCanvasWidgetList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	for (int32 Index : RemovedIndices)
	{
		FUIFCanvasWidgetEntry& Entry = Entries[Index];
		if (Entry.Widget && Entry.bAdded)
		{
			OwnerLayer->LocalRemoveWidget(Entry.Widget);
			Entry.bAdded = false;
		}
	}
}


void FUIFCanvasWidgetList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		FUIFCanvasWidgetEntry& Entry = Entries[Index];
		if (Entry.Widget != nullptr)
		{
			OwnerLayer->LocalAddWidget(Entry.Widget);
			Entry.bAdded = true;
		}
	}
}


void FUIFCanvasWidgetList::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	for (int32 Index : ChangedIndices)
	{
		FUIFCanvasWidgetEntry& Entry = Entries[Index];
		if (Entry.Widget && !Entry.bAdded)
		{
			OwnerLayer->LocalAddWidget(Entry.Widget);
			Entry.bAdded = true;
		}
		else if (!Entry.Widget && Entry.bAdded)
		{
			OwnerLayer->LocalRemoveEmptySlot();
			Entry.bAdded = false;
		}
		else if (Entry.Widget && Entry.bAdded)
		{
			OwnerLayer->LocalSetSlot(Entry.Widget, Entry.Slot);
		}
	}
}


void FUIFCanvasWidgetList::AddEntry(UUIFWidget* InWidget, const FUIFCanvasLayerSlot& InSlot)
{
	check(InWidget);
	FUIFCanvasWidgetEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Widget = InWidget;
	NewEntry.Slot = InSlot;
	MarkItemDirty(NewEntry);
}


void FUIFCanvasWidgetList::RemoveEntry(UUIFWidget* InWidget)
{
	check(InWidget);
	int32 Index = Entries.IndexOfByPredicate([InWidget](const FUIFCanvasWidgetEntry& Entry) { return Entry.Widget == InWidget; });
	if (Index != INDEX_NONE)
	{
		Entries.RemoveAt(Index);
		MarkArrayDirty();
	}
}


void FUIFCanvasWidgetList::UpdateEntry(UUIFWidget* InWidget, const FUIFCanvasLayerSlot& InSlot)
{
	check(InWidget);
	FUIFCanvasWidgetEntry* Entry = Entries.FindByPredicate([InWidget](const FUIFCanvasWidgetEntry& Entry) { return Entry.Widget == InWidget; });
	if (Entry)
	{
		Entry->Slot = InSlot;
		MarkItemDirty(*Entry);
	}
}


/**
 *
 */
UUIFCanvasLayer::UUIFCanvasLayer()
	: WidgetList(this)
{
	LayerWidgetClass = FSoftObjectPath(TEXT("/UISystem/Layout/WBP_CanvasLayer.WBP_CanvasLayer_C"));
}


void UUIFCanvasLayer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, WidgetList, Params);
}


bool UUIFCanvasLayer::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (FUIFCanvasWidgetEntry& Entry : WidgetList.Entries)
	{
		UUIFWidget* Widget = Entry.Widget;
		if (IsValid(Widget))
		{
			WroteSomething = Channel->ReplicateSubobject(Widget, *Bunch, *RepFlags) || WroteSomething;
		}
	}

	return WroteSomething;
}


UUIFWidget* UUIFCanvasLayer::CreateWidget(TSubclassOf<UUIFWidget> WidgetClass, FUIFCanvasLayerSlot Slot)
{
	UUIFWidget* Result = nullptr;
	if (WidgetClass.Get() == nullptr || !GetOuterAPlayerController()->HasAuthority())
	{
		return Result;
	}

	Result = NewObject<UUIFWidget>(GetOuterAPlayerController(), WidgetClass.Get());
	WidgetList.AddEntry(Result, Slot);

	return Result;
}


void UUIFCanvasLayer::RemoveWidget(UUIFWidget* Widget)
{
	APlayerController* LocalOwner = Cast<APlayerController>(GetOuter());
	check(LocalOwner->HasAuthority());

	if (Widget)
	{
		WidgetList.RemoveEntry(Widget);
	}
}


void UUIFCanvasLayer::SetSlot(UUIFWidget* Widget, FUIFCanvasLayerSlot Slot)
{
	APlayerController* LocalOwner = Cast<APlayerController>(GetOuter());
	check(LocalOwner->HasAuthority());

	if (Widget)
	{
		WidgetList.UpdateEntry(Widget, Slot);
	}
}


void UUIFCanvasLayer::LocalAddWidget(UUIFWidget* InUIWidget)
{
	TWeakObjectPtr<UUIFCanvasLayer> WeakSelf = this;
	TWeakObjectPtr<UUIFWidget> WeakUIWidget = InUIWidget;
	InUIWidget->LocalCreateWidgetAsync([WeakSelf, WeakUIWidget]()
		{
			UUIFCanvasLayer* StrongSelf = WeakSelf.Get();
			UUIFWidget* StrongUIWidget = WeakUIWidget.Get();
			if (StrongSelf && StrongUIWidget && StrongUIWidget->GetWidget())
			{
				if (UUIFCanvasLayerUserWidget* LocalLayerWidget = Cast<UUIFCanvasLayerUserWidget>(StrongSelf->GetLayerWidget()))
				{
					if (const FUIFCanvasWidgetEntry* Entry = StrongSelf->WidgetList.GetEntries().FindByPredicate([StrongUIWidget](const FUIFCanvasWidgetEntry& Other){ return Other.Widget == StrongUIWidget; }))
					{
						LocalLayerWidget->AddWidget(StrongUIWidget->GetWidget(), Entry->Slot);
					}
				}
			}
		});
}


void UUIFCanvasLayer::LocalRemoveWidget(UUIFWidget* InUIWidget)
{
	UWidget* Widget = InUIWidget->GetWidget();
	UUIFCanvasLayerUserWidget* LocalLayerWidget = Cast<UUIFCanvasLayerUserWidget>(GetLayerWidget());
	if (LocalLayerWidget && Widget)
	{
		LocalLayerWidget->RemoveWidget(Widget);
	}
}


void UUIFCanvasLayer::LocalSetSlot(UUIFWidget* InUIWidget, const FUIFCanvasLayerSlot& InSlot)
{
	UWidget* Widget = InUIWidget->GetWidget();
	UUIFCanvasLayerUserWidget* LocalLayerWidget = Cast<UUIFCanvasLayerUserWidget>(GetLayerWidget());
	if (LocalLayerWidget && Widget)
	{
		LocalLayerWidget->UpdateWidget(Widget, InSlot);
	}
}


void UUIFCanvasLayer::LocalRemoveEmptySlot()
{
	//todo
}


void UUIFCanvasLayer::OnLocalLayerWidgetAdded()
{
	UUIFCanvasLayerUserWidget* LocalLayerWidget = CastChecked<UUIFCanvasLayerUserWidget>(GetLayerWidget());
	for (const FUIFCanvasWidgetEntry& Entry : WidgetList.GetEntries())
	{
		if (Entry.Widget && Entry.Widget->GetWidget())
		{
			LocalLayerWidget->AddWidget(Entry.Widget->GetWidget(), Entry.Slot);
		}
	}
}