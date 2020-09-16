// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDeformer.h"

#include "Actions/OptimusNodeGraphActions.h"
#include "Actions/OptimusResourceActions.h"
#include "Actions/OptimusVariableActions.h"
#include "OptimusActionStack.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusNodeGraph.h"
#include "OptimusResourceDescription.h"
#include "OptimusVariableDescription.h"
#include "OptimusHelpers.h"
#include "OptimusNode.h"
#include "OptimusNodePin.h"
#include "OptimusCoreModule.h"

#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "OptimusDeformer"

static const FName SetupGraphName("SetupGraph");
static const FName UpdateGraphName("UpdateGraph");

static const FName DefaultResourceName("Resource");

static const FName DefaultVariableName("Variable");


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


UOptimusVariableDescription* UOptimusDeformer::AddVariable(
	FOptimusDataTypeRef InDataTypeRef, 
	FName InName /*= NAME_None */
	)
{
	if (InName.IsNone())
	{
		InName = DefaultVariableName;
	}

	if (!InDataTypeRef.IsValid())
	{
		// Default to float.
		InDataTypeRef.Set(FOptimusDataTypeRegistry::Get().FindType(*FFloatProperty::StaticClass()));
	}

	// Is this data type compatible with resources?
	FOptimusDataTypeHandle DataType = InDataTypeRef.Resolve();
	if (!DataType.IsValid() || !EnumHasAnyFlags(DataType->UsageFlags, EOptimusDataTypeUsageFlags::Variable))
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Invalid data type for variables."));
		return nullptr;
	}

	FOptimusVariableAction_AddVariable* AddVariabAction =
	    new FOptimusVariableAction_AddVariable(this, InDataTypeRef, InName);

	if (GetActionStack()->RunAction(AddVariabAction))
	{
		return AddVariabAction->GetVariable(this);
	}
	else
	{
		return nullptr;
	}
}


bool UOptimusDeformer::RemoveVariable(
	UOptimusVariableDescription* InVariableDesc
	)
{
	if (!ensure(InVariableDesc))
	{
		return false;
	}
	if (InVariableDesc->GetOuter() != this)
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Variable not owned by this deformer."));
		return false;
	}

	return GetActionStack()->RunAction<FOptimusVariableAction_RemoveVariable>(InVariableDesc);
}


bool UOptimusDeformer::RenameVariable(
	UOptimusVariableDescription* InVariableDesc, 
	FName InNewName
	)
{
	if (InNewName.IsNone())
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Invalid resource name."));
		return false;
	}
	if (InVariableDesc->GetOuter() != this)
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Variable not owned by this deformer."));
		return false;
	}

	return GetActionStack()->RunAction<FOptimusVariableAction_RenameVariable>(InVariableDesc, InNewName);
}


UOptimusVariableDescription* UOptimusDeformer::ResolveVariable(
	FName InVariableName
	)
{
	for (UOptimusVariableDescription* Variable : GetVariables())
	{
		if (Variable->GetFName() == InVariableName)
		{
			return Variable;
		}
	}
	return nullptr;
}


UOptimusVariableDescription* UOptimusDeformer::CreateVariableDirect(
	FName InName
	)
{
	if (InName.IsNone())
	{
		InName = DefaultResourceName;
	}

	// If there's already an object with this name, then attempt to make the name unique.
	InName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusVariableDescription::StaticClass(), InName);

	UOptimusVariableDescription* Variable = NewObject<UOptimusVariableDescription>(this, UOptimusVariableDescription::StaticClass(), InName, RF_Transactional);

	MarkPackageDirty();

	return Variable;
}


bool UOptimusDeformer::AddVariableDirect(
	UOptimusVariableDescription* InVariableDesc
	)
{
	if (!ensure(InVariableDesc))
	{
		return false;
	}

	if (!ensure(InVariableDesc->GetOuter() == this))
	{
		return false;
	}


	VariableDescriptions.Add(InVariableDesc);

	Notify(EOptimusGlobalNotifyType::VariableAdded, InVariableDesc);

	return true;
}


bool UOptimusDeformer::RemoveVariableDirect(
	UOptimusVariableDescription* InVariableDesc
	)
{
	// Do we actually own this resource?
	int32 ResourceIndex = VariableDescriptions.Add(InVariableDesc);
	if (ResourceIndex == INDEX_NONE)
	{
		return false;
	}

	VariableDescriptions.RemoveAt(ResourceIndex);

	Notify(EOptimusGlobalNotifyType::VariableRemoved, InVariableDesc);

	InVariableDesc->Rename(nullptr, GetTransientPackage());
	InVariableDesc->MarkPendingKill();

	MarkPackageDirty();

	return true;
}


bool UOptimusDeformer::RenameVariableDirect(
	UOptimusVariableDescription* InVariableDesc, 
	FName InNewName
	)
{
	// Do we actually own this variable?
	int32 ResourceIndex = VariableDescriptions.IndexOfByKey(InVariableDesc);
	if (ResourceIndex == INDEX_NONE)
	{
		return false;
	}

	InNewName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusVariableDescription::StaticClass(), InNewName);

	bool bChanged = false;
	if (InVariableDesc->VariableName != InNewName)
	{
		InVariableDesc->Modify();
		InVariableDesc->VariableName = InNewName;
		bChanged = true;
	}

	if (InVariableDesc->GetFName() != InNewName)
	{
		InVariableDesc->Rename(*InNewName.ToString(), nullptr);
		bChanged = true;
	}

	if (bChanged)
	{
		Notify(EOptimusGlobalNotifyType::VariableRenamed, InVariableDesc);

		MarkPackageDirty();
	}

	return bChanged;
}


UOptimusResourceDescription* UOptimusDeformer::AddResource(
	FOptimusDataTypeRef InDataTypeRef,
	FName InName
	)
{
	if (InName.IsNone())
	{
		InName = DefaultResourceName;
	}

	if (!InDataTypeRef.IsValid())
	{
		// Default to float.
		InDataTypeRef.Set(FOptimusDataTypeRegistry::Get().FindType(*FFloatProperty::StaticClass()));
	}

	// Is this data type compatible with resources?
	FOptimusDataTypeHandle DataType = InDataTypeRef.Resolve();
	if (!DataType.IsValid() || !EnumHasAnyFlags(DataType->UsageFlags, EOptimusDataTypeUsageFlags::Resource))
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Invalid data type for resources."));
		return nullptr;
	}

	FOptimusResourceAction_AddResource *AddResourceAction = 	
	    new FOptimusResourceAction_AddResource(this, InDataTypeRef, InName);

	if (GetActionStack()->RunAction(AddResourceAction))
	{
		return AddResourceAction->GetResource(this);
	}
	else
	{
		return nullptr;
	}
}


bool UOptimusDeformer::RemoveResource(UOptimusResourceDescription* InResourceDesc)
{
	if (!ensure(InResourceDesc))
	{
		return false;
	}
	if (InResourceDesc->GetOuter() != this)
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Resource not owned by this deformer."));
		return false;
	}

	return GetActionStack()->RunAction<FOptimusResourceAction_RemoveResource>(InResourceDesc);
}


bool UOptimusDeformer::RenameResource(
	UOptimusResourceDescription* InResourceDesc, 
	FName InNewName
	)
{
	if (InNewName.IsNone())
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Invalid resource name."));
		return false;
	}

	return GetActionStack()->RunAction<FOptimusResourceAction_RenameResource>(InResourceDesc, InNewName);
}


UOptimusResourceDescription* UOptimusDeformer::ResolveResource(FName InResourceName)
{
	for (UOptimusResourceDescription* Resource : GetResources())
	{
		if (Resource->GetFName() == InResourceName)
		{
			return Resource;
		}
	}
	return nullptr;
}


UOptimusResourceDescription* UOptimusDeformer::CreateResourceDirect(
	FName InName
	)
{
	if (InName.IsNone())
	{
		InName = DefaultResourceName;
	}

	// If there's already an object with this name, then attempt to make the name unique.
	InName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusResourceDescription::StaticClass(), InName);

	UOptimusResourceDescription* Resource = NewObject<UOptimusResourceDescription>(this, UOptimusResourceDescription::StaticClass(), InName, RF_Transactional);

	MarkPackageDirty();
	
	return Resource;
}


bool UOptimusDeformer::AddResourceDirect(UOptimusResourceDescription* InResourceDesc)
{
	if (!ensure(InResourceDesc))
	{
		return false;
	}

	if (!ensure(InResourceDesc->GetOuter() == this))
	{
		return false;
	}


	ResourceDescriptions.Add(InResourceDesc);

	Notify(EOptimusGlobalNotifyType::ResourceAdded, InResourceDesc);

	return true;
}


bool UOptimusDeformer::RemoveResourceDirect(
	UOptimusResourceDescription* InResourceDesc
	)
{
	// Do we actually own this resource?
	int32 ResourceIndex = ResourceDescriptions.IndexOfByKey(InResourceDesc);
	if (ResourceIndex == INDEX_NONE)
	{
		return false;
	}

	ResourceDescriptions.RemoveAt(ResourceIndex);

	Notify(EOptimusGlobalNotifyType::ResourceRemoved, InResourceDesc);

	InResourceDesc->Rename(nullptr, GetTransientPackage());
	InResourceDesc->MarkPendingKill();

	MarkPackageDirty();

	return true;
}


bool UOptimusDeformer::RenameResourceDirect(
	UOptimusResourceDescription* InResourceDesc, 
	FName InNewName
	)
{
	// Do we actually own this resource?
	int32 ResourceIndex = ResourceDescriptions.IndexOfByKey(InResourceDesc);
	if (ResourceIndex == INDEX_NONE)
	{
		return false;
	}
	
	InNewName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusResourceDescription::StaticClass(), InNewName);

	bool bChanged = false;
	if (InResourceDesc->ResourceName != InNewName)
	{
		InResourceDesc->Modify();
		InResourceDesc->ResourceName = InNewName;
		bChanged = true;
	}

	if (InResourceDesc->GetFName() != InNewName)
	{
		InResourceDesc->Rename(*InNewName.ToString(), nullptr);
		bChanged = true;
	}

	if (bChanged)
	{
		Notify(EOptimusGlobalNotifyType::ResourceRenamed, InResourceDesc);

		MarkPackageDirty();
	}

	return bChanged;
}


UOptimusNodeGraph* UOptimusDeformer::ResolveGraphPath(
	const FString& InPath,
	FString& OutRemainingPath
	)
{
	FString GraphName;

	if (!InPath.Split(TEXT("/"), &GraphName, &OutRemainingPath))
	{
		GraphName = InPath;
	}
	
	// FIXME: Once we have encapsulation, we need to do a recursive traversal here.
	for (UOptimusNodeGraph* Graph : Graphs)
	{
		if (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	return nullptr;
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

	for (UOptimusNode* Node : Graph->GetAllNodes())
	{
		if (Node->GetName().Equals(NodeName, ESearchCase::IgnoreCase))
		{
			return Node;
		}
	}

	return nullptr;
}


void UOptimusDeformer::Notify(EOptimusGlobalNotifyType InNotifyType, UObject* InObject)
{
	switch (InNotifyType)
	{
	case EOptimusGlobalNotifyType::GraphAdded: 
	case EOptimusGlobalNotifyType::GraphRemoved:
	case EOptimusGlobalNotifyType::GraphIndexChanged:
	case EOptimusGlobalNotifyType::GraphRenamed:
		checkSlow(Cast<UOptimusNodeGraph>(InObject) != nullptr);
		break;

	case EOptimusGlobalNotifyType::ResourceAdded:
	case EOptimusGlobalNotifyType::ResourceRemoved:
	case EOptimusGlobalNotifyType::ResourceIndexChanged:
	case EOptimusGlobalNotifyType::ResourceRenamed:
	case EOptimusGlobalNotifyType::ResourceTypeChanged:
		checkSlow(Cast<UOptimusResourceDescription>(InObject) != nullptr);
		break;

	case EOptimusGlobalNotifyType::VariableAdded:
	case EOptimusGlobalNotifyType::VariableRemoved:
	case EOptimusGlobalNotifyType::VariableIndexChanged:
	case EOptimusGlobalNotifyType::VariableRenamed:
	case EOptimusGlobalNotifyType::VariabelTypeChanged:
		checkSlow(Cast<UOptimusVariableDescription>(InObject) != nullptr);
		break;
	default:
		checkfSlow(false, TEXT("Unchecked EOptimusGlobalNotifyType!"));
		break;
	}

	GlobalNotifyDelegate.Broadcast(InNotifyType, InObject);
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
	InName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNodeGraph::StaticClass(), InName);

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
		FName NewName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNodeGraph::StaticClass(), InGraph->GetFName());

		if (NewName == InGraph->GetFName())
		{
			InGraph->Rename(nullptr, this);
		}
		else
		{
			InGraph->Rename(*NewName.ToString(), this);
		}
	}

	Graphs.Insert(InGraph, InInsertBefore);

	Notify(EOptimusGlobalNotifyType::GraphAdded, InGraph);

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

	Notify(EOptimusGlobalNotifyType::GraphRemoved, InGraph);

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

	Notify(EOptimusGlobalNotifyType::GraphIndexChanged, InGraph);

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
		Notify(EOptimusGlobalNotifyType::GraphRenamed, InGraph);
	}
	return bSuccess;
}


#undef LOCTEXT_NAMESPACE
