// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UIFLayer.h"

#include "UIFSlotLayer.generated.h"


/**
 *
 */
USTRUCT(BlueprintType)
struct FUIFSlotWidgetEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FUIFSlotWidgetEntry() = default;

	UPROPERTY()
	TObjectPtr<UUIFWidget> Widget = nullptr;
	
	UPROPERTY()
	FName SlotName;

	UPROPERTY(NotReplicated)
	bool bAdded = false;
};


/**
 * 
 */
USTRUCT(BlueprintType)
struct UIFRAMEWORK_API FUIFSlotWidgetList : public FFastArraySerializer
{
	GENERATED_BODY()

	friend UUIFSlotLayer;

	FUIFSlotWidgetList() = default;

	FUIFSlotWidgetList(UUIFSlotLayer* InOwnerLayer)
		: OwnerLayer(InOwnerLayer)
	{
	}

public:
	//~ Begin of FFastArraySerializer
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	//~ End of FFastArraySerializer

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FUIFSlotWidgetEntry, FUIFSlotWidgetList>(Entries, DeltaParms, *this);
	}

	void AddEntry(UUIFWidget* Widget, FName Slot);
	void RemoveEntry(UUIFWidget* Widget);

	TArrayView<const FUIFSlotWidgetEntry> GetEntries() const
	{
		return Entries;
	}

private:
	UPROPERTY()
	TArray<FUIFSlotWidgetEntry> Entries;

	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UUIFSlotLayer> OwnerLayer = nullptr;
};

template<>
struct TStructOpsTypeTraits<FUIFSlotWidgetList> : public TStructOpsTypeTraitsBase2<FUIFSlotWidgetList>
{
	enum { WithNetDeltaSerializer = true };
};


/**
 *
 */
UCLASS(Abstract, DisplayName="Slot Layer UIFramework")
class UIFRAMEWORK_API UUIFSlotLayer : public UUIFLayer
{
	GENERATED_BODY()

	friend FUIFSlotWidgetList;

public:
	UUIFSlotLayer();

	//~ Begin UUILayer
	virtual bool ReplicateSubobjects(UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
protected:
	virtual void OnLocalLayerWidgetAdded() override;
	//~ End UUILayer

public:
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework", meta = (DeterminesOutputType = WidgetClass))
	UUIFWidget* CreateWidget(UPARAM(meta = (AllowAbstract = false)) TSubclassOf<UUIFWidget> WidgetClass, FName Slot);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	void RemoveWidget(UUIFWidget* Widget);

private:
	void LocalAddWidget(UUIFWidget* Widget, FName SlotName);
	void LocalRemoveWidget(UUIFWidget* Widget, FName SlotName);
	void LocalRemoveEmptySlots();

private:
	UPROPERTY(Replicated)
	FUIFSlotWidgetList WidgetList;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UUIFWidget>> LocalSlotContents;
};
