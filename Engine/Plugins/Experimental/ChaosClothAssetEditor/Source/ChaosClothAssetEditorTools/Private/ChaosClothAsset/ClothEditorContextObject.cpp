// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorContextObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothEditorContextObject)

void UClothEditorContextObject::Init(TWeakPtr<SDataflowGraphEditor> InDataflowGraphEditor, TObjectPtr<UDataflow> InDataflowGraph)
{
	DataflowGraphEditor = InDataflowGraphEditor;
	DataflowGraph = InDataflowGraph;
}

TWeakPtr<SDataflowGraphEditor> UClothEditorContextObject::GetDataflowGraphEditor()
{
	return DataflowGraphEditor;
}

const TWeakPtr<const SDataflowGraphEditor> UClothEditorContextObject::GetDataflowGraphEditor() const
{
	return DataflowGraphEditor;
}

TObjectPtr<UDataflow> UClothEditorContextObject::GetDataflowGraph()
{
	return DataflowGraph;
}

const TObjectPtr<const UDataflow> UClothEditorContextObject::GetDataflowGraph() const
{
	return DataflowGraph;
}


