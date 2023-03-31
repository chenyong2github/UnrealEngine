// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "ClothEditorContextObject.generated.h"

class SDataflowGraphEditor;
class UDataflow;

UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothEditorContextObject : public UObject
{
	GENERATED_BODY()

public:

	void Init(TWeakPtr<SDataflowGraphEditor> DataflowGraphEditor, TObjectPtr<UDataflow> DataflowGraph);

	TWeakPtr<SDataflowGraphEditor> GetDataflowGraphEditor();
	const TWeakPtr<const SDataflowGraphEditor> GetDataflowGraphEditor() const;

	TObjectPtr<UDataflow> GetDataflowGraph();
	const TObjectPtr<const UDataflow> GetDataflowGraph() const;

private:

	TWeakPtr<SDataflowGraphEditor> DataflowGraphEditor;

	UPROPERTY()
	TObjectPtr<UDataflow> DataflowGraph = nullptr;

};
