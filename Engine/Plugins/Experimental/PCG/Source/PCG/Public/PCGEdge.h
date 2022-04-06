// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGEdge.generated.h"

class UPCGNode;

UCLASS(ClassGroup = (Procedural))
class PCG_API UPCGEdge : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Edge)
	FName InboundLabel = NAME_None;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Edge)
	TObjectPtr<UPCGNode> InboundNode;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Edge)
	FName OutboundLabel = NAME_None;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Edge)
	TObjectPtr<UPCGNode> OutboundNode;

	void BreakEdge();
};