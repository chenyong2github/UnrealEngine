// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UIFLayer.h"
#include "UObject/ObjectPtr.h"
#include "UObject/SoftObjectPtr.h"

#include "UIFCanvasLayer.generated.h"

class UUICanvasLayerUserWidget;


USTRUCT(BlueprintType)
struct FUIFCanvasLayerSlot
{
	GENERATED_BODY()

	/** Anchors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Canvas)
	FAnchors Anchors = FAnchors(0.0f, 0.0f);

	/** Offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Canvas)
	FMargin Offsets = FMargin(0.f, 0.f, 0.f, 0.f);

	/**
	 * Alignment is the pivot point of the widget.  Starting in the upper left at (0,0),
	 * ending in the lower right at (1,1).  Moving the alignment point allows you to move
	 * the origin of the widget.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Canvas)
	FVector2D Alignment = FVector2D::ZeroVector;

	/** The order priority this widget is rendered inside the layer. Higher values are rendered last (and so they will appear to be on top). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Canvas)
	int32 ZOrder = 0;

	/** When true we use the widget's desired size */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Canvas)
	bool bSizeToContent = false;
};


/**
 *
 */
USTRUCT(BlueprintType)
struct FUIFCanvasWidgetEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FUIFCanvasWidgetEntry() = default;

	UPROPERTY()
	TObjectPtr<UUIFWidget> Widget = nullptr;
	
	UPROPERTY()
	FUIFCanvasLayerSlot Slot;

	UPROPERTY(NotReplicated)
	bool bAdded = false;
};


/**
 * 
 */
USTRUCT(BlueprintType)
struct UIFRAMEWORK_API FUIFCanvasWidgetList : public FFastArraySerializer
{
	GENERATED_BODY()

	friend UUIFCanvasLayer;

	FUIFCanvasWidgetList() = default;

	FUIFCanvasWidgetList(UUIFCanvasLayer* InOwnerLayer)
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
		return FFastArraySerializer::FastArrayDeltaSerialize<FUIFCanvasWidgetEntry, FUIFCanvasWidgetList>(Entries, DeltaParms, *this);
	}

	void AddEntry(UUIFWidget* Widget, const FUIFCanvasLayerSlot& Slot);
	void RemoveEntry(UUIFWidget* Widget);
	void UpdateEntry(UUIFWidget* Widget, const FUIFCanvasLayerSlot& Slot);

	TArrayView<const FUIFCanvasWidgetEntry> GetEntries() const
	{
		return Entries;
	}

private:
	UPROPERTY()
	TArray<FUIFCanvasWidgetEntry> Entries;

	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UUIFCanvasLayer> OwnerLayer = nullptr;
};

template<>
struct TStructOpsTypeTraits<FUIFCanvasWidgetList> : public TStructOpsTypeTraitsBase2<FUIFCanvasWidgetList>
{
	enum { WithNetDeltaSerializer = true };
};


/**
 *
 */
UCLASS(DisplayName="Canvas Layer UIFramework")
class UIFRAMEWORK_API UUIFCanvasLayer : public UUIFLayer
{
	GENERATED_BODY()

	friend FUIFCanvasWidgetList;

public:
	UUIFCanvasLayer();

	//~ Begin UUILayer
	virtual bool ReplicateSubobjects(UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
protected:
	virtual void OnLocalLayerWidgetAdded() override;
	//~ End UUILayer

public:
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework", meta = (DeterminesOutputType = WidgetClass))
	UUIFWidget* CreateWidget(UPARAM(meta = (AllowAbstract = false)) TSubclassOf<UUIFWidget> WidgetClass, FUIFCanvasLayerSlot Slot);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	void RemoveWidget(UUIFWidget* Widget);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	void SetSlot(UUIFWidget* Widget, FUIFCanvasLayerSlot Slot);

private:
	void LocalAddWidget(UUIFWidget* Widget);
	void LocalRemoveWidget(UUIFWidget* Widget);
	void LocalSetSlot(UUIFWidget* Widget, const FUIFCanvasLayerSlot& Slot);
	void LocalRemoveEmptySlot();

private:
	UPROPERTY(Replicated)
	FUIFCanvasWidgetList WidgetList;
};
