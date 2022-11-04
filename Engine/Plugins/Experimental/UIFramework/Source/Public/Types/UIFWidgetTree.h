// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Types/UIFWidgetId.h"

#include "UIFWidgetTree.generated.h"

#ifndef UE_UIFRAMEWORK_WITH_DEBUG
	#define UE_UIFRAMEWORK_WITH_DEBUG !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

struct FReplicationFlags;
struct FUIFrameworkWidgetTreeEntry;
class AActor;
class FOutBunch;
class UActorChannel;
class UUIFrameworkWidget;

class IUIFrameworkWidgetTreeOwner
{
public:
	/** A widget was added to the tree. */
	virtual void LocalWidgetWasAddedToTree(const FUIFrameworkWidgetTreeEntry& Entry) {};
	/** A widget was removed to the tree. */
	virtual void LocalWidgetRemovedFromTree(const FUIFrameworkWidgetTreeEntry& Entry) {};
};

/**
 *
 */
USTRUCT()
struct UIFRAMEWORK_API FUIFrameworkWidgetTreeEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FUIFrameworkWidgetTreeEntry() = default;
	FUIFrameworkWidgetTreeEntry(UUIFrameworkWidget* Parent, UUIFrameworkWidget* Child);

	bool IsParentValid() const;
	bool IsChildValid() const;

	UPROPERTY()
	TObjectPtr<UUIFrameworkWidget> Parent = nullptr;

	UPROPERTY()
	TObjectPtr<UUIFrameworkWidget> Child = nullptr;
	
	UPROPERTY()
	FUIFrameworkWidgetId ParentId;

	UPROPERTY()
	FUIFrameworkWidgetId ChildId;
};


/**
 * A valid snapshot of the widget tree that can be replicated to local instance.
 * Authority widgets know their parent/children relation. That information is not replicated to the local widgets.
 * When a widget is added to the tree, the tree is updated. The widget now has to inform the tree when that relationship changes until it's remove from the tree.
 */
USTRUCT()
struct UIFRAMEWORK_API FUIFrameworkWidgetTree : public FFastArraySerializer
{
	GENERATED_BODY()

public:
	FUIFrameworkWidgetTree() = default;
	FUIFrameworkWidgetTree(AActor* InReplicatedOwner, IUIFrameworkWidgetTreeOwner* InOwner);
	~FUIFrameworkWidgetTree();

public:
	//~ Begin of FFastArraySerializer
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	//~ End of FFastArraySerializer

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FUIFrameworkWidgetTreeEntry, FUIFrameworkWidgetTree>(Entries, DeltaParms, *this);
	}

	bool ReplicateSubWidgets(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags);

	/** Add a new widget to the top hierarchy. */
	void AuthorityAddRoot(UUIFrameworkWidget* Widget);
	/**
	 * Change the parent / child relationship of the child widget.
	 * If the child widget had a parent, that relationship entry will replaced it by a new one.
	 */
	void AuthorityAddWidget(UUIFrameworkWidget* Parent, UUIFrameworkWidget* Child);
	/**
	 * Remove the widget and all of its children and grand-children from the tree.
	 * It will clean all the parent relationship from the tree.
	 */
	void AuthorityRemoveWidget(UUIFrameworkWidget* Widget);

	//~ It is not safe to use the ReplicationId on the Authority because any add or remove would clear the ItemMap.
	FUIFrameworkWidgetTreeEntry* LocalGetEntryByReplicationId(int32 WidgetId);
	const FUIFrameworkWidgetTreeEntry* LocalGetEntryByReplicationId(int32 WidgetId) const;

	/** Find the widget by its unique Id. The widget needs to be in the Tree. */
	UUIFrameworkWidget* FindWidgetById(FUIFrameworkWidgetId WidgetId);
	const UUIFrameworkWidget* FindWidgetById(FUIFrameworkWidgetId WidgetId) const;

	/** Add all widgets in the tree to the ActorChannel replicated list */
	void AuthorityAddAllWidgetsFromActorChannel();

	/** Removes all widgets added to the ActorChannel replicated list */
	void AuthorityRemoveAllWidgetsFromActorChannel();

#if UE_UIFRAMEWORK_WITH_DEBUG
	void AuthorityTest() const;
#endif

private:
	void AuthorityAddChildInternal(UUIFrameworkWidget* Parent, UUIFrameworkWidget* Child);
	void AuthorityAddChildRecursiveInternal(UUIFrameworkWidget* Widget);
	bool AuthorityRemoveChildRecursiveInternal(UUIFrameworkWidget* Widget);

private:
	UPROPERTY()
	TArray<FUIFrameworkWidgetTreeEntry> Entries;

	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<AActor> ReplicatedOwner;

	TMap<TWeakObjectPtr<UUIFrameworkWidget>, int32> AuthorityIndexByWidgetMap;
	TMap<FUIFrameworkWidgetId, TWeakObjectPtr<UUIFrameworkWidget>> WidgetByIdMap;
	IUIFrameworkWidgetTreeOwner* Owner = nullptr;
};

template<>
struct TStructOpsTypeTraits<FUIFrameworkWidgetTree> : public TStructOpsTypeTraitsBase2<FUIFrameworkWidgetTree>
{
	enum { WithNetDeltaSerializer = true };
};