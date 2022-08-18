// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "UIFLayer.h"

#include "UIFPlayerComponent.generated.h"

class UUIFPlayerComponent;


/**
 *
 */
USTRUCT(BlueprintType)
struct FUIFLayerEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FUIFLayerEntry() = default;

	UPROPERTY()
	TObjectPtr<UUIFLayer> Layer = nullptr;

	//~ In the LayerEntry instead of UILayer because it is needed for the correct initialization and cannot changed at runtime
	UPROPERTY()
	int32 ZOrder = 0;

	//~ In the LayerEntry instead of UILayer because it is needed for the correct initialization and cannot changed at runtime
	UPROPERTY()
	EUIFLayerType Type = EUIFLayerType::Viewport;

	UPROPERTY(NotReplicated)
	bool bAdded = false;
};


/**
 * 
 */
USTRUCT(BlueprintType)
struct UIFRAMEWORK_API FUIFLayerList : public FFastArraySerializer
{
	GENERATED_BODY()

	friend UUIFPlayerComponent;

	FUIFLayerList() = default;

	FUIFLayerList(UActorComponent* InOwnerComponent)
		: OwnerComponent(InOwnerComponent)
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
		return FFastArraySerializer::FastArrayDeltaSerialize<FUIFLayerEntry, FUIFLayerList>(Entries, DeltaParms, *this);
	}

	UUIFLayer* AddEntry(TSubclassOf<UUIFLayer> LayerClass, int32 ZOrder, EUIFLayerType Type);
	void RemoveEntry(UUIFLayer* Layer);

private:
	UPROPERTY()
	TArray<FUIFLayerEntry> Entries;

	UPROPERTY()
	TObjectPtr<UActorComponent> OwnerComponent = nullptr;
};

template<>
struct TStructOpsTypeTraits<FUIFLayerList> : public TStructOpsTypeTraitsBase2<FUIFLayerList>
{
	enum { WithNetDeltaSerializer = true };
};


/**
 * 
 */
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class UIFRAMEWORK_API UUIFPlayerComponent : public UActorComponent
{
	GENERATED_BODY()

	UUIFPlayerComponent();

public:
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework", meta = (DeterminesOutputType = LayerClass))
	UUIFLayer* CreateViewportLayer(UPARAM(meta = (AllowAbstract = false))TSubclassOf<UUIFLayer> LayerClass, int32 ZOrder);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework", meta = (DeterminesOutputType = LayerClass))
	UUIFLayer* CreatePlayerScreenLayer(UPARAM(meta = (AllowAbstract = false))TSubclassOf<UUIFLayer> LayerClass, int32 ZOrder);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	void RemoveLayer(UUIFLayer* Layer);

	//~ Begin UActorComponent
	virtual void UninitializeComponent() override;
	virtual bool ReplicateSubobjects(class UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	//~ End UActorComponent

private:
	UPROPERTY(Replicated)
	FUIFLayerList LayerList;
};
