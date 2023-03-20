// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraph.h"

#include "PCGEditorGraph.generated.h"

class FPCGEditor;
class UPCGNode;
class UPCGGraph;
class UPCGEditorGraphNodeBase;

UCLASS()
class UPCGEditorGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	/** Initialize the editor graph from a PCGGraph */
	void InitFromNodeGraph(UPCGGraph* InPCGGraph);

	/** Creates the links for a given node */
	void CreateLinks(UPCGEditorGraphNodeBase* InGraphNode, bool bCreateInbound, bool bCreateOutbound);

	UPCGGraph* GetPCGGraph() { return PCGGraph; }

	void SetEditor(TWeakPtr<const FPCGEditor> InEditor) { PCGEditor = InEditor; }
	TWeakPtr<const FPCGEditor> GetEditor() const { return PCGEditor; }

protected:
	void CreateLinks(UPCGEditorGraphNodeBase* InGraphNode, bool bCreateInbound, bool bCreateOutbound, const TMap<UPCGNode*, UPCGEditorGraphNodeBase*>& InGraphNodeToPCGNodeMap);

private:
	UPROPERTY()
	TObjectPtr<UPCGGraph> PCGGraph = nullptr;

	TWeakPtr<const FPCGEditor> PCGEditor = nullptr;
};
