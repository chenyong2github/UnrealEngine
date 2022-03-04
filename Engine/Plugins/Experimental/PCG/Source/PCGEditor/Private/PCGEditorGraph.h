// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EdGraph/EdGraph.h"

#include "PCGEditorGraph.generated.h"

class UPCGGraph;

UCLASS()
class UPCGEditorGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	/** Initialize the editor graph from a PCGGraph */
	void InitFromNodeGraph(UPCGGraph* InPCGGraph);

	UPCGGraph* GetPCGGraph() { return PCGGraph; }

private:
	UPROPERTY()
	TObjectPtr<UPCGGraph> PCGGraph = nullptr;
};
