// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"

#include "CommonInputActionDomain.generated.h"

UENUM()
enum class ECommonInputEventFlowBehavior {
	BlockIfActive,
	BlockIfHandled,
	NeverBlock,
};

/**
 * Describes an input-event handling domain. It's InnerBehavior determines how events
 * flow between widgets within the domain and Behavior determines how events will flow to
 * other Domains in the DomainTable.
 */
UCLASS()
class COMMONINPUT_API UCommonInputActionDomain : public UDataAsset
{
	GENERATED_BODY()

public:

	// Behavior of an input event between Action Domains, i.e., how an event flows into the next Action Domain
	UPROPERTY(EditDefaultsOnly, Category = "Default")
	ECommonInputEventFlowBehavior Behavior = ECommonInputEventFlowBehavior::BlockIfActive;

	// Behavior of an input event within an Action Domain, i.e., how an event flows to a lower ZOrder active widget
	// within the same Action Domain
	UPROPERTY(EditDefaultsOnly, Category = "Default")
	ECommonInputEventFlowBehavior InnerBehavior = ECommonInputEventFlowBehavior::BlockIfHandled;

	bool ShouldBreakInnerEventFlow(bool bInputEventHandled) const;

	bool ShouldBreakEventFlow(bool bDomainHadActiveRoots, bool bInputEventHandledAtLeastOnce) const;
};

/**
 * An ordered array of ActionDomains.
 */
UCLASS()
class COMMONINPUT_API UCommonInputActionDomainTable : public UDataAsset
{
	GENERATED_BODY()

public:
	// Domains will receive events in ascending index order
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default")
	TArray<UCommonInputActionDomain*> ActionDomains;
};