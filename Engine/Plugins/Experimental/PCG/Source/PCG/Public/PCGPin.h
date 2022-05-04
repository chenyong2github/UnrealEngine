// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGPin.generated.h"

class UPCGNode;
class UPCGEdge;

UCLASS(ClassGroup = (Procedural))
class PCG_API UPCGPin : public UObject
{
	GENERATED_BODY()

public:
	UPCGPin(const FObjectInitializer& ObjectInitializer);

	UPROPERTY()
	TObjectPtr<UPCGNode> Node = nullptr;

	UPROPERTY(EditAnywhere, Category = Settings)
	FName Label = NAME_None;

	UPROPERTY(TextExportTransient)
	TArray<TObjectPtr<UPCGEdge>> Edges;

	bool AddEdgeTo(UPCGPin* OtherPin);
	bool BreakEdgeTo(UPCGPin* OtherPin);
	bool BreakAllEdges();
	bool IsConnected() const;
	int32 EdgeCount() const;
};