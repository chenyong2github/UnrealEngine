// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDeformer.h"

#include "OptimusActionStack.h"
#include "OptimusNodeGraph.h"

#include "OptimusNode.h"
#include "OptimusNodePin.h"

#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "OptimusDeformer"


UOptimusDeformer::UOptimusDeformer()
{
	UOptimusNodeGraph *UpdateGraph = CreateDefaultSubobject<UOptimusNodeGraph>(TEXT("UpdateGraph"));
	UpdateGraph->SetGraphType(EOptimusNodeGraphType::Update);
	Graphs.Add(UpdateGraph);

	ActionStack = CreateDefaultSubobject<UOptimusActionStack>(TEXT("ActionStack"));
}


UOptimusNodeGraph* UOptimusDeformer::AddSetupGraph()
{
	// Do we already have a setup graph?
	for (UOptimusNodeGraph* Graph : Graphs)
	{
		if (Graph->GetGraphType() == EOptimusNodeGraphType::Setup)
		{
			return nullptr;
		}
	}

	UOptimusNodeGraph* SetupGraph = CreateDefaultSubobject<UOptimusNodeGraph>(TEXT("SetupGraph"));
	SetupGraph->SetGraphType(EOptimusNodeGraphType::Setup);
	Graphs.Add(SetupGraph);

	// FIXME: Notify!

	return SetupGraph;
}


UOptimusNodeGraph* UOptimusDeformer::AddTriggerGraph()
{
	return nullptr;
}


UOptimusNodeGraph* UOptimusDeformer::ResolveGraphPath(
	const FString& InPath,
	FString& OutRemainingPath
	)
{
	FString Path = InPath;
	UObject* ScopeObject = this;
	UOptimusNodeGraph* LastGraph = nullptr;

	// FIXME: This will require tweaking once we have graph/function nodes.
	for (;;)
	{
		FString GraphName;

		if (!Path.Split(TEXT("/"), &GraphName, &OutRemainingPath))
		{
			GraphName = Path;
		}

		UOptimusNodeGraph* CandidateGraph = FindObject<UOptimusNodeGraph>(ScopeObject, *GraphName);
		if (CandidateGraph == nullptr)
		{
			return LastGraph;
		}
		if (OutRemainingPath.IsEmpty())
		{
			return CandidateGraph;
		}

		LastGraph = CandidateGraph;
		ScopeObject = CandidateGraph;
	}
}

UOptimusNode* UOptimusDeformer::ResolveNodePath(
	const FString& InPath,
	FString& OutRemainingPath
	)
{
	FString NodePath;

	UOptimusNodeGraph* Graph = ResolveGraphPath(InPath, NodePath);
	if (!Graph || NodePath.IsEmpty())
	{
		return nullptr;
	}

	FString NodeName;
	if (!NodePath.Split(TEXT("."), &NodeName, &OutRemainingPath))
	{
		NodeName = NodePath;
	}

	return FindObject<UOptimusNode>(Graph, *NodeName);
}


UOptimusNodeGraph* UOptimusDeformer::ResolveGraphPath(const FString& InGraphPath)
{
	FString PathRemainder;

	UOptimusNodeGraph* Graph = ResolveGraphPath(InGraphPath, PathRemainder);

	// The graph is only valid if the path was fully consumed.
	return PathRemainder.IsEmpty() ? Graph : nullptr;
}


UOptimusNode* UOptimusDeformer::ResolveNodePath(const FString& InNodePath)
{
	FString PathRemainder;

	UOptimusNode* Node = ResolveNodePath(InNodePath, PathRemainder);

	// The graph is only valid if the path was fully consumed.
	return PathRemainder.IsEmpty() ? Node : nullptr;
}


UOptimusNodePin* UOptimusDeformer::ResolvePinPath(const FString& InPinPath)
{
	FString PinPath;

	UOptimusNode* Node = ResolveNodePath(InPinPath, PinPath);

	return Node ? Node->FindPin(PinPath) : nullptr;
}



#undef LOCTEXT_NAMESPACE

