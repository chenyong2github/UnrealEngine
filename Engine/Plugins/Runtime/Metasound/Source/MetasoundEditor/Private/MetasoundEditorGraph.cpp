// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraph.h"

#include "EdGraph/EdGraphNode.h"


UObject* UMetasoundEditorGraph::GetMetasound() const
{
	return ParentMetasound;
}

UObject& UMetasoundEditorGraph::GetMetasoundChecked() const
{
	check(ParentMetasound);
	return *ParentMetasound;
}
