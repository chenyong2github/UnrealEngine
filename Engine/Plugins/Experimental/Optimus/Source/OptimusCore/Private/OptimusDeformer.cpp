// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDeformer.h"

#include "Actions/OptimusNodeGraphActions.h"
#include "OptimusActionStack.h"
#include "OptimusNodeGraph.h"

#include "OptimusNode.h"
#include "OptimusNodePin.h"

#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "OptimusDeformer"

static const FName SetupGraphName("SetupGraph");
static const FName UpdateGraphName("UpdateGraph");



UOptimusDeformer::UOptimusDeformer()
{
	UOptimusNodeGraph *UpdateGraph = CreateDefaultSubobject<UOptimusNodeGraph>(UpdateGraphName);
	UpdateGraph->SetGraphType(EOptimusNodeGraphType::Update);
	Graphs.Add(UpdateGraph);

	ActionStack = CreateDefaultSubobject<UOptimusActionStack>(TEXT("ActionStack"));
}


UOptimusNodeGraph* UOptimusDeformer::AddSetupGraph()
{
	FOptimusNodeGraphAction_AddGraph* AddGraphAction = 
		new FOptimusNodeGraphAction_AddGraph(this, EOptimusNodeGraphType::Setup, SetupGraphName, 0);

	if (GetActionStack()->RunAction(AddGraphAction))
	{
		return AddGraphAction->GetGraph(this);
	}
	else
	{
		return nullptr;
	}
}


UOptimusNodeGraph* UOptimusDeformer::AddTriggerGraph(const FString &InName)
{
	FName Name(InName);

	if (Name == SetupGraphName || Name == UpdateGraphName)
	{
		return nullptr;
	}

	FOptimusNodeGraphAction_AddGraph* AddGraphAction =
	    new FOptimusNodeGraphAction_AddGraph(this, EOptimusNodeGraphType::ExternalTrigger, Name, INDEX_NONE);

	if (GetActionStack()->RunAction(AddGraphAction))
	{
		return AddGraphAction->GetGraph(this);
	}
	else
	{
		return nullptr;
	}
}

bool UOptimusDeformer::RemoveGraph(UOptimusNodeGraph* InGraph)
{
    return GetActionStack()->RunAction<FOptimusNodeGraphAction_RemoveGraph>(InGraph);
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


void UOptimusDeformer::Notify(EOptimusNodeGraphNotifyType InNotifyType, UOptimusNodeGraph* InGraph)
{
    ModifiedEventDelegate.Broadcast(InNotifyType, InGraph, nullptr);
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



UOptimusNodeGraph* UOptimusDeformer::CreateGraph(
	EOptimusNodeGraphType InType, 
	FName InName, 
	TOptional<int32> InInsertBefore
	)
{
	if (InType == EOptimusNodeGraphType::Update)
	{
		return nullptr;
	}
	else if (InType == EOptimusNodeGraphType::Setup)
	{
		// Do we already have a setup graph?
		if (Graphs.Num() > 1 && Graphs[0]->GetGraphType() == EOptimusNodeGraphType::Setup)
		{
			return nullptr;
		}

		// The name of the setup graph is fixed.
		InName = SetupGraphName;
	}
	else if (InType == EOptimusNodeGraphType::ExternalTrigger)
	{
		if (InName == SetupGraphName || InName == UpdateGraphName)
		{
			return nullptr;
		}
	}

	// If there's already an object with this name, then attempt to make the name unique.
	// For soem reason, MakeUniqueObjectName does not already do this check.
	if (StaticFindObjectFast(UOptimusNodeGraph::StaticClass(), this, InName) != nullptr)
	{
		InName = MakeUniqueObjectName(this, UOptimusNodeGraph::StaticClass(), InName);
	}

	UOptimusNodeGraph* Graph = NewObject<UOptimusNodeGraph>(this, UOptimusNodeGraph::StaticClass(), InName, RF_Transactional);

	Graph->SetGraphType(InType);

	if (InInsertBefore.IsSet())
	{
		if (AddGraph(Graph, InInsertBefore.GetValue()))
		{
			return Graph;
		}
		else
		{
			Graph->Rename(nullptr, GetTransientPackage());
			Graph->MarkPendingKill();

			return nullptr;
		}
	}
	else
	{
		return Graph;
	}
}


bool UOptimusDeformer::AddGraph(
	UOptimusNodeGraph* InGraph,
	int32 InInsertBefore
	)
{
	if (InGraph == nullptr)
	{
		return false;
	}

	const bool bHaveSetupGraph = (Graphs.Num() > 1 && Graphs[0]->GetGraphType() == EOptimusNodeGraphType::Setup);

	// If INDEX_NONE, insert at the end.
	if (InInsertBefore == INDEX_NONE)
	{
		InInsertBefore = Graphs.Num();
	}
		

	switch (InGraph->GetGraphType())
	{
	case EOptimusNodeGraphType::Update:
	case EOptimusNodeGraphType::Setup:
		// Do we already have a setup graph?
		if (bHaveSetupGraph)
		{
			return false;
		}
		InInsertBefore = 0;
		break;
		
	case EOptimusNodeGraphType::ExternalTrigger:
		// Trigger graphs are always sandwiched between setup and update.
		InInsertBefore = FMath::Clamp(InInsertBefore, bHaveSetupGraph ? 1 : 0, Graphs.Num() - 1);
		break;
	}
	
	if (InGraph->GetOuter() != this)
	{
		IOptimusNodeGraphCollectionOwner* GraphOwner = Cast<IOptimusNodeGraphCollectionOwner>(InGraph->GetOuter());
		if (GraphOwner)
		{
			GraphOwner->RemoveGraph(InGraph, /* bDeleteGraph = */ false);
		}

		// Ensure that the object has a unique name within our namespace.
		FString NewNameStr;
		
		if (StaticFindObjectFast(UOptimusNodeGraph::StaticClass(), this, InGraph->GetFName()) != nullptr)
		{
			FName NewName = MakeUniqueObjectName(this, UOptimusNodeGraph::StaticClass(), InGraph->GetFName());
			if (NewName != InGraph->GetFName())
			{
				NewNameStr = NewName.ToString();
			}
		}

		InGraph->Rename(!NewNameStr.IsEmpty() ? *NewNameStr : nullptr, this);
	}

	Graphs.Insert(InGraph, InInsertBefore);

	Notify(EOptimusNodeGraphNotifyType::GraphAdded, InGraph);

	return true;
}


bool UOptimusDeformer::RemoveGraph(
	UOptimusNodeGraph* InGraph,
	bool bDeleteGraph
	)
{
	// Not ours?
	int32 GraphIndex = Graphs.IndexOfByKey(InGraph);
	if (GraphIndex == INDEX_NONE)
	{
		return false;
	}

	if (InGraph->GetGraphType() == EOptimusNodeGraphType::Update)
	{
		return false;
	}

	Graphs.RemoveAt(GraphIndex);

	Notify(EOptimusNodeGraphNotifyType::GraphRemoved, InGraph);

	if (bDeleteGraph)
	{
		// Un-parent this graph to a temporary storage and mark it for kill.
		InGraph->Rename(nullptr, GetTransientPackage());
		InGraph->MarkPendingKill();
	}

	return true;
}



bool UOptimusDeformer::MoveGraph(
	UOptimusNodeGraph* InGraph, 
	int32 InInsertBefore
	)
{
	int32 GraphOldIndex = Graphs.IndexOfByKey(InGraph);
	if (GraphOldIndex == INDEX_NONE)
	{
		return false;
	}

	if (InGraph->GetGraphType() != EOptimusNodeGraphType::ExternalTrigger)
	{
		return false;
	}

	// Less than num graphs, because the index is based on the node being moved not being
	// in the list.
	// [S T1 T2 U] -> Move T2 to slot 1 in list [S T1 U]
	if (InInsertBefore == INDEX_NONE)
	{
		InInsertBefore = Graphs.Num() - 1;
	}
	else
	{
		const bool bHaveSetupGraph = (Graphs.Num() > 1 && Graphs[0]->GetGraphType() == EOptimusNodeGraphType::Setup);
		InInsertBefore = FMath::Clamp(InInsertBefore, bHaveSetupGraph ? 1 : 0, Graphs.Num() - 1);
	}

	if (GraphOldIndex == InInsertBefore)
	{
		return true;
	}

	Graphs.RemoveAt(GraphOldIndex);
	Graphs.Insert(InGraph, InInsertBefore);

	Notify(EOptimusNodeGraphNotifyType::GraphIndexChanged, InGraph);

	return true;
}


bool UOptimusDeformer::RenameGraph(UOptimusNodeGraph* InGraph, const FString& InNewName)
{
	// Not ours?
	int32 GraphIndex = Graphs.IndexOfByKey(InGraph);
	if (GraphIndex == INDEX_NONE)
	{
		return false;
	}

	// Setup and Update graphs cannot be renamed.
	if (InGraph->GetGraphType() == EOptimusNodeGraphType::Setup ||
		InGraph->GetGraphType() == EOptimusNodeGraphType::Update)
	{
		return false;
	}

	// The Setup and Update graph names are reserved.
	if (InNewName.Compare(SetupGraphName.ToString(), ESearchCase::IgnoreCase) == 0 ||
		InNewName.Compare(UpdateGraphName.ToString(), ESearchCase::IgnoreCase) == 0)
	{
		return false;
	}

	// Do some verification on the name. Ideally we ought to be able to sink FOptimusNameValidator down
	// to here but that would pull in editor dependencies.
	if (!FName::IsValidXName(InNewName, TEXT("./")))
	{
		return false;
	}

	bool bSuccess = GetActionStack()->RunAction<FOptimusNodeGraphAction_RenameGraph>(InGraph, FName(*InNewName));
	if (bSuccess)
	{
		Notify(EOptimusNodeGraphNotifyType::GraphNameChanged, InGraph);
	}
	return bSuccess;
}


#undef LOCTEXT_NAMESPACE
