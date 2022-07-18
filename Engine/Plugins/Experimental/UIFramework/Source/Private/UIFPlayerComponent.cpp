// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIFPlayerComponent.h"

#include "Engine/ActorChannel.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"


/**
 *
 */
void FUIFLayerList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	for (int32 Index : RemovedIndices)
	{
		FUIFLayerEntry& Entry = Entries[Index];
		if (Entry.Layer)
		{
			Entry.Layer->LocalRemoveLayerWidget();
			Entry.bAdded = false;
		}
	}
}


void FUIFLayerList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		FUIFLayerEntry& Entry = Entries[Index];
		if (Entry.Layer)
		{
			Entry.Layer->LocalAddLayerWidget(Entry.ZOrder, Entry.Type);
			Entry.bAdded = true;
		}
	}
}


void FUIFLayerList::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	for (int32 Index : ChangedIndices)
	{
		FUIFLayerEntry& Entry = Entries[Index];
		if (Entry.Layer && !Entry.bAdded)
		{
			Entry.Layer->LocalAddLayerWidget(Entry.ZOrder, Entry.Type);
		}
		else if (!Entry.Layer && Entry.bAdded)
		{
			// it will be done automaticly with UUILayer::BeginDestroy
			//Entry.LocalRemoveLayoutWidget();
		}
		else if (Entry.Layer && Entry.bAdded)
		{
			ensureMsgf(false, TEXT("ZOrder and Type cannot change at runtime."));
		}
	}
}


UUIFLayer* FUIFLayerList::AddEntry(TSubclassOf<UUIFLayer> LayerClass, int32 ZOrder, EUIFLayerType Type)
{
	check(LayerClass.Get());
	APlayerController * LocalOwner = Cast<APlayerController>(OwnerComponent->GetOwner());
	check(LocalOwner->HasAuthority());

	UUIFLayer* Layer = NewObject<UUIFLayer>(LocalOwner, LayerClass.Get());

	FUIFLayerEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Layer = Layer;
	NewEntry.ZOrder = ZOrder;
	NewEntry.Type = Type;
	MarkItemDirty(NewEntry);

	return Layer;
}


void FUIFLayerList::RemoveEntry(UUIFLayer* Layer)
{
	check(Layer);
	int32 Index = Entries.IndexOfByPredicate([Layer](const FUIFLayerEntry& Entry){ return Entry.Layer == Layer; });
	if (Index != INDEX_NONE)
	{
		Entries.RemoveAt(Index);
		MarkArrayDirty();
	}
}


/**
 *
 */
UUIFPlayerComponent::UUIFPlayerComponent()
	: LayerList(this)
{
	SetIsReplicatedByDefault(true);
	bWantsInitializeComponent = true;
}


void UUIFPlayerComponent::UninitializeComponent()
{
	if (!GetOwner()->HasAuthority())
	{
		for (FUIFLayerEntry& Entry : LayerList.Entries)
		{
			if (UUIFLayer* Layer = Entry.Layer)
			{
				Layer->LocalRemoveLayerWidget();
			}
		}
	}
}


void UUIFPlayerComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, LayerList, Params);
}


bool UUIFPlayerComponent::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (FUIFLayerEntry& Entry : LayerList.Entries)
	{
		UUIFLayer* Layer = Entry.Layer;
		if (IsValid(Layer))
		{
			WroteSomething |= Channel->ReplicateSubobject(Layer, *Bunch, *RepFlags);
			WroteSomething |= Layer->ReplicateSubobjects(Channel, Bunch, RepFlags);
		}
	}

	return WroteSomething;
}


UUIFLayer* UUIFPlayerComponent::CreateViewportLayer(TSubclassOf<UUIFLayer> LayerClass, int32 ZOrder)
{
	if (LayerClass.Get() == nullptr || LayerClass->HasAnyClassFlags(EClassFlags::CLASS_Abstract))
	{
		FFrame::KismetExecutionMessage(TEXT("LayerClass cannot be empty or abstract"), ELogVerbosity::Warning, "EmptyOrAbstractLayeClass");
		return nullptr;
	}
	return LayerList.AddEntry(LayerClass, ZOrder, EUIFLayerType::Viewport);
}


UUIFLayer* UUIFPlayerComponent::CreatePlayerScreenLayer(TSubclassOf<UUIFLayer> LayerClass, int32 ZOrder)
{
	if (LayerClass.Get() == nullptr || LayerClass->HasAnyClassFlags(EClassFlags::CLASS_Abstract))
	{
		FFrame::KismetExecutionMessage(TEXT("LayerClass cannot be empty or abstract"), ELogVerbosity::Warning, "EmptyOrAbstractLayeClass");
		return nullptr;
	}
	return LayerList.AddEntry(LayerClass, ZOrder, EUIFLayerType::PlayerScreen);
}


void UUIFPlayerComponent::RemoveLayer(UUIFLayer* Layer)
{
	APlayerController* LocalOwner = Cast<APlayerController>(GetOwner());
	if (LocalOwner->HasAuthority())
	{
		if (Layer)
		{
			LayerList.RemoveEntry(Layer);
		}
	}
}
