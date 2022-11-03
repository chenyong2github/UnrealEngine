// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/UIFWidgetTree.h"
#include "UIFLog.h"
#include "UIFPlayerComponent.h"
#include "UIFWidget.h"

#include "Engine/ActorChannel.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

#if UE_UIFRAMEWORK_WITH_DEBUG
namespace UE::UIFramework::Private
{
	TArray<FUIFrameworkWidgetTree*> GTrees;
	void TestWidgetTree()
	{
		for (FUIFrameworkWidgetTree* Tree : GTrees)
		{
			Tree->AuthorityTest();
		}
	}

	static FAutoConsoleCommand CCmdTestWidgetTree(
		TEXT("UIFramework.TestWidgetTree"),
		TEXT("Test if all containers are properly setup"),
		FConsoleCommandDelegate::CreateStatic(TestWidgetTree),
		ECVF_Cheat);
}
#endif

/**
 *
 */
FUIFrameworkWidgetTreeEntry::FUIFrameworkWidgetTreeEntry(UUIFrameworkWidget* InParent, UUIFrameworkWidget* InChild)
	: Parent(InParent)
	, Child(InChild)
	, ParentId(InParent ? InParent->GetWidgetId() : FUIFrameworkWidgetId::MakeRoot())
	, ChildId(InChild->GetWidgetId())
{

}

bool FUIFrameworkWidgetTreeEntry::IsParentValid() const
{
	return (Parent && Parent->GetWidgetId() == ParentId) || ParentId.IsRoot();
}

bool FUIFrameworkWidgetTreeEntry::IsChildValid() const
{
	return Child && Child->GetWidgetId() == ChildId;
}

/**
 *
 */
FUIFrameworkWidgetTree::FUIFrameworkWidgetTree(AActor* InReplicatedOwner, IUIFrameworkWidgetTreeOwner* InOwner)
	: ReplicatedOwner(InReplicatedOwner)
	, Owner(InOwner)
{
#if UE_UIFRAMEWORK_WITH_DEBUG
	UE::UIFramework::Private::GTrees.Add(this);
#endif
}

FUIFrameworkWidgetTree::~FUIFrameworkWidgetTree()
{
#if UE_UIFRAMEWORK_WITH_DEBUG
	UE::UIFramework::Private::GTrees.RemoveSingleSwap(this);
#endif
}

void FUIFrameworkWidgetTree::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	for (int32 Index : RemovedIndices)
	{
		FUIFrameworkWidgetTreeEntry& Entry = Entries[Index];
		if (Entry.Child)
		{
			if (ensure(Owner))
			{
				Owner->LocalWidgetRemovedFromTree(Entry);
			}

			Entry.Child->LocalDestroyUMGWidget();
			WidgetByIdMap.Remove(Entry.Child->GetWidgetId());
		}
	}
}

void FUIFrameworkWidgetTree::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		FUIFrameworkWidgetTreeEntry& Entry = Entries[Index];
		if (Entry.ParentId.IsValid() && Entry.ChildId.IsValid())
		{
			if (ensure(Owner))
			{
				Owner->LocalWidgetWasAddedToTree(Entry);
			}
			WidgetByIdMap.FindOrAdd(Entry.ChildId) = Entry.Child;
			if (!Entry.ParentId.IsRoot())
			{
				WidgetByIdMap.FindOrAdd(Entry.ParentId) = Entry.Parent;
			}
		}
	}
}

void FUIFrameworkWidgetTree::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	for (int32 Index : ChangedIndices)
	{
		// Note these events should only be called when the widget was not constructed and are now constructed.
		FUIFrameworkWidgetTreeEntry& Entry = Entries[Index];
		if (Entry.ParentId.IsValid() && Entry.ChildId.IsValid())
		{
			if (ensure(Owner))
			{
				Owner->LocalWidgetWasAddedToTree(Entry);
			}
			WidgetByIdMap.FindOrAdd(Entry.ChildId) = Entry.Child;
			if (!Entry.ParentId.IsRoot())
			{
				WidgetByIdMap.FindOrAdd(Entry.ParentId) = Entry.Parent;
			}
		}
	}
}

bool FUIFrameworkWidgetTree::ReplicateSubWidgets(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool bWroteSomething = false;
#if DO_CHECK
	TSet<UUIFrameworkWidget*> AllChildren;
#endif
	for (FUIFrameworkWidgetTreeEntry& Entry : Entries)
	{
		UUIFrameworkWidget* Widget = Entry.Child;
		if (IsValid(Widget))
		{
#if DO_CHECK
			bool bAlreadyInSet = false;
			AllChildren.Add(Widget, &bAlreadyInSet);
			ensureMsgf(bAlreadyInSet == false, TEXT("The widget has more than one parent."));
#endif

			bWroteSomething |= Channel->ReplicateSubobject(Widget, *Bunch, *RepFlags);
		}
	}
	return bWroteSomething;
}

void FUIFrameworkWidgetTree::AuthorityAddRoot(UUIFrameworkWidget* Widget)
{
	check(Widget);
	AuthorityAddChildInternal(nullptr, Widget);
}

void FUIFrameworkWidgetTree::AuthorityAddWidget(UUIFrameworkWidget* Parent, UUIFrameworkWidget* Child)
{
	check(Parent);
	check(Child);
	AuthorityAddChildInternal(Parent, Child);
}

void FUIFrameworkWidgetTree::AuthorityAddChildInternal(UUIFrameworkWidget* Parent, UUIFrameworkWidget* Child)
{
	if (int32* PreviousEntryIndexPtr = AuthorityIndexByWidgetMap.Find(Child))
	{
		check(Entries.IsValidIndex(*PreviousEntryIndexPtr));
		FUIFrameworkWidgetTreeEntry& PreviousEntry = Entries[*PreviousEntryIndexPtr];
		if (PreviousEntry.Parent != Parent)
		{
			// Same child, different parent. Need to build a new entry for replication.
			PreviousEntry = FUIFrameworkWidgetTreeEntry(Parent, Child);
			MarkItemDirty(PreviousEntry);
		}
	}
	else
	{
		int32 NewEntryIndex = Entries.Emplace(Parent, Child);

		FUIFrameworkWidgetTreeEntry& NewEntry = Entries[NewEntryIndex];
		MarkItemDirty(NewEntry);

		AuthorityIndexByWidgetMap.Add(Child) = NewEntryIndex;
		WidgetByIdMap.Add(Child->GetWidgetId()) = Child;

		if (ensure(ReplicatedOwner) && ReplicatedOwner->IsUsingRegisteredSubObjectList())
		{
			ReplicatedOwner->AddReplicatedSubObject(Child);
		}

		AuthorityAddChildRecursiveInternal(Child);
	}
}

void FUIFrameworkWidgetTree::AuthorityAddChildRecursiveInternal(UUIFrameworkWidget* InParentWidget)
{
	FUIFrameworkWidgetTree* Self = this;
	InParentWidget->AuthorityForEachChildren([Self, InParentWidget](UUIFrameworkWidget* ChildWidget)
		{
			if (ChildWidget != nullptr)
			{
				Self->AuthorityAddChildInternal(InParentWidget, ChildWidget);
			}
		});
}

void FUIFrameworkWidgetTree::AuthorityRemoveWidget(UUIFrameworkWidget* Widget)
{
	check(Widget);

	if (AuthorityRemoveChildRecursiveInternal(Widget))
	{
		MarkArrayDirty();
	}
}

bool FUIFrameworkWidgetTree::AuthorityRemoveChildRecursiveInternal(UUIFrameworkWidget* Widget)
{
	if (int32* PreviousEntryIndexPtr = AuthorityIndexByWidgetMap.Find(Widget))
	{
		check(Entries.IsValidIndex(*PreviousEntryIndexPtr));

		AuthorityIndexByWidgetMap.Remove(Widget);
		WidgetByIdMap.Remove(Widget->GetWidgetId());

		if (ensure(ReplicatedOwner) && ReplicatedOwner->IsUsingRegisteredSubObjectList())
		{
			ReplicatedOwner->RemoveReplicatedSubObject(Widget);
		}

		Entries.RemoveAtSwap(*PreviousEntryIndexPtr);

		// Fix up the swap item
		if (Entries.IsValidIndex(*PreviousEntryIndexPtr))
		{
			if (int32* FixUpChildIndexPtr = AuthorityIndexByWidgetMap.Find(Entries[*PreviousEntryIndexPtr].Child))
			{
				*FixUpChildIndexPtr = *PreviousEntryIndexPtr;
			}
		}

		FUIFrameworkWidgetTree* Self = this;
		Widget->AuthorityForEachChildren([Self](UUIFrameworkWidget* ChildWidget)
			{
				if (ChildWidget)
				{
					Self->AuthorityRemoveChildRecursiveInternal(ChildWidget);
				}
			});	

		return true;
	}
	return false;

}

FUIFrameworkWidgetTreeEntry* FUIFrameworkWidgetTree::LocalGetEntryByReplicationId(int32 ReplicationId)
{
	if (const int32* Index = ItemMap.Find(ReplicationId))
	{
		return &Entries[*Index];
	}
	return nullptr;
}

const FUIFrameworkWidgetTreeEntry* FUIFrameworkWidgetTree::LocalGetEntryByReplicationId(int32 ReplicationId) const
{
	return const_cast<FUIFrameworkWidgetTree*>(this)->LocalGetEntryByReplicationId(ReplicationId);
}

UUIFrameworkWidget* FUIFrameworkWidgetTree::FindWidgetById(FUIFrameworkWidgetId WidgetId)
{
	const TWeakObjectPtr<UUIFrameworkWidget>* Found = WidgetByIdMap.Find(WidgetId);
	return Found ? Found->Get() : nullptr;
}

const UUIFrameworkWidget* FUIFrameworkWidgetTree::FindWidgetById(FUIFrameworkWidgetId WidgetId) const
{
	return const_cast<FUIFrameworkWidgetTree*>(this)->FindWidgetById(WidgetId);
}

#if UE_UIFRAMEWORK_WITH_DEBUG
void FUIFrameworkWidgetTree::AuthorityTest() const
{
	if (ReplicatedOwner == nullptr || !ReplicatedOwner->HasAuthority())
	{
		return;
	}

	TSet<FUIFrameworkWidgetId> UniqueIds;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FUIFrameworkWidgetTreeEntry& Entry = Entries[Index];

		ensureAlwaysMsgf(Entry.ParentId.IsValid(), TEXT("Invalid ParentId"));
		if (!Entry.ParentId.IsRoot())
		{
			ensureAlwaysMsgf(Entry.Parent, TEXT("Invalid Parent"));
		}
		else
		{
			ensureAlwaysMsgf(!Entry.Parent, TEXT("Valid Parent"));
		}
		ensureAlwaysMsgf(Entry.Child, TEXT("Invalid Child"));
		ensureAlwaysMsgf(Entry.ChildId.IsValid(), TEXT("Invalid ChildId"));

		if (Entry.ChildId.IsValid())
		{
			ensureAlwaysMsgf(!UniqueIds.Contains(Entry.ChildId), TEXT("Duplicated id"));
			UniqueIds.Add(Entry.ChildId);
		}

		if (Entry.Child)
		{
			ensureAlwaysMsgf(Entry.ChildId == Entry.Child->GetWidgetId(), TEXT("Id do not matches"));
		}
		
		if (Entry.Parent)
		{
			bool bFound = false;
			const UUIFrameworkWidget* ToFindWidget = Entry.Child;
			Entry.Parent->AuthorityForEachChildren([&bFound, ToFindWidget](UUIFrameworkWidget* ChildWidget)
				{
					bFound = bFound || ToFindWidget == ChildWidget;
				});

			ensureAlwaysMsgf(bFound, TEXT("Widget is in the tree but not in the AuthorityForEachChildren"));
		}


		const int32* FoundIndexPtr = AuthorityIndexByWidgetMap.Find(Entry.Child);
		if (FoundIndexPtr)
		{
			int32 FoundIndex = *FoundIndexPtr;
			ensureAlwaysMsgf(FoundIndex == Index, TEXT("Widget index doesn't match what is in the map"));
		}
		else
		{
			ensureAlwaysMsgf(false, TEXT("Widget no in the map"));
		}
		

		if (Entry.Child)
		{
			ensureAlwaysMsgf(Entry.Child->GetWidgetId().IsValid(), TEXT("The id is not valid."));
			const TWeakObjectPtr<UUIFrameworkWidget>* FoundWidgetPtr = WidgetByIdMap.Find(Entry.ChildId);
			if (FoundWidgetPtr)
			{
				ensureAlwaysMsgf(FoundWidgetPtr->Get(), TEXT("The found widget is invalid"));
				if (UUIFrameworkWidget* Widget = FoundWidgetPtr->Get())
				{
					ensureAlwaysMsgf(Widget == Entry.Child, TEXT("Widget in the map doesn't matches with the entry widget."));
				}
			}
			else
			{
				ensureAlwaysMsgf(false, TEXT("Widget no in the map"));
			}
		}
		
	}
}
#endif