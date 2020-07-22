// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorGraph.h"

#include "EdGraph/EdGraphNode.h"


UMetasound* UMetasoundEditorGraph::GetMetasound() const
{
	return ParentMetasound;
}

UMetasound& UMetasoundEditorGraph::GetMetasoundChecked() const
{
	check(ParentMetasound);
	return *ParentMetasound;
}
