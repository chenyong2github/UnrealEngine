// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMControllerActions.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "RigVMEditorModule.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/Package.h"
#include "Misc/CoreMisc.h"

int32 URigVMController::sRemovedPinIndex = 0;

URigVMController::URigVMController()
	: bReportWarningsAndErrors(true)
{
}

URigVMController::URigVMController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bReportWarningsAndErrors(true)
{
	ActionStack = CreateDefaultSubobject<URigVMActionStack>(TEXT("ActionStack"));
}

URigVMController::~URigVMController()
{
	SetGraph(nullptr);
}

URigVMGraph* URigVMController::GetGraph() const
{
	return Graph;
}

void URigVMController::SetGraph(URigVMGraph* InGraph)
{
	if (Graph)
	{
		Graph->OnModified().RemoveAll(this);
	}

	Graph = InGraph;

	if (Graph)
	{
		Graph->OnModified().AddUObject(this, &URigVMController::HandleModifiedEvent);
	}

	HandleModifiedEvent(ERigVMGraphNotifType::GraphChanged, Graph, nullptr);
}

FRigVMGraphModifiedEvent& URigVMController::OnModified()
{
	return ModifiedEventStatic;
}

void URigVMController::Notify(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	if (Graph)
	{
		Graph->Notify(InNotifType, InGraph, InSubject);
	}
}

void URigVMController::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	ModifiedEventStatic.Broadcast(InNotifType, InGraph, InSubject);
	if (ModifiedEventDynamic.IsBound())
	{
		ModifiedEventDynamic.Broadcast(InNotifType, InGraph, InSubject);
	}
}

#if WITH_EDITOR

URigVMStructNode* URigVMController::AddStructNode(UScriptStruct* InScriptStruct, const FName& InMethodName, const FVector2D& InPosition, const FString& InNodeName, bool bUndo)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}
	if (InScriptStruct == nullptr)
	{
		ReportError(TEXT("InScriptStruct is null."));
		return nullptr;
	}
	if (InMethodName == NAME_None)
	{
		ReportError(TEXT("InMethodName is None."));
		return nullptr;
	}

	FString FunctionName = FString::Printf(TEXT("F%s::%s"), *InScriptStruct->GetName(), *InMethodName.ToString());
	FRigVMFunctionPtr Function = FRigVMRegistry::Get().Find(*FunctionName);
	if (Function == nullptr)
	{
		ReportErrorf(TEXT("RIGVM_METHOD '%s' cannot be found. is None."), *FunctionName);
		return nullptr;
	}

	FString Name = GetValidName(InNodeName.IsEmpty() ? InScriptStruct->GetName() : InNodeName);
	URigVMStructNode* Node = NewObject<URigVMStructNode>(Graph, *Name);
	Node->ScriptStruct = InScriptStruct;
	Node->MethodName = InMethodName;
	Node->Position = InPosition;
	Node->NodeTitle = InScriptStruct->GetMetaData(TEXT("DisplayName"));
	
	FString NodeColorMetadata;
	InScriptStruct->GetStringMetaDataHierarchical(*URigVMNode::NodeColorName, &NodeColorMetadata);
	if (!NodeColorMetadata.IsEmpty())
	{
		Node->NodeColor = GetColorFromMetadata(NodeColorMetadata);
	}

	TArray<uint8> TempBuffer;
	TempBuffer.AddUninitialized(InScriptStruct->GetStructureSize());
	InScriptStruct->InitializeDefaultValue(TempBuffer.GetData());

	FString ExportedDefaultValue;
	InScriptStruct->ExportText(ExportedDefaultValue, TempBuffer.GetData(), nullptr, nullptr, PPF_None, nullptr);
	InScriptStruct->DestroyStruct(TempBuffer.GetData());
	AddPinsForStruct(InScriptStruct, Node, nullptr, ERigVMPinDirection::Invalid, ExportedDefaultValue);

	Graph->Nodes.Add(Node);
	Graph->MarkPackageDirty();

	FRigVMAddStructNodeAction Action;
	if (bUndo)
	{
		Action = FRigVMAddStructNodeAction(Node);
		Action.Title = FString::Printf(TEXT("Add %s Node"), *Node->GetNodeTitle());
		ActionStack->BeginAction(Action);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Graph, Node);

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return Node;
}

URigVMStructNode* URigVMController::AddStructNodeFromStructPath(const FString& InScriptStructPath, const FName& InMethodName, const FVector2D& InPosition, const FString& InNodeName, bool bUndo)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	UScriptStruct* ScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *InScriptStructPath);
	if (ScriptStruct == nullptr)
	{
		ReportErrorf(TEXT("Cannot find struct for path '%s'."), *InScriptStructPath);
		return nullptr;
	}

	return AddStructNode(ScriptStruct, InMethodName, InPosition, InNodeName, bUndo);
}

URigVMVariableNode* URigVMController::AddVariableNode(const FName& InVariableName, const FString& InCPPType, UScriptStruct* InScriptStruct, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bUndo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	TArray<FRigVMGraphVariableDescription> ExistingVariables = Graph->GetVariableDescriptions();
	for (const FRigVMGraphVariableDescription& ExistingVariable : ExistingVariables)
	{
		if (ExistingVariable.Name == InVariableName)
		{
			if (ExistingVariable.CPPType != InCPPType ||
				ExistingVariable.ScriptStruct != InScriptStruct)
			{
				ReportErrorf(TEXT("Cannot add variable '%s' - variable already exists."), *InVariableName.ToString());
				return nullptr;
			}
		}
	}

	FString Name = GetValidName(InNodeName.IsEmpty() ? FString(TEXT("VariableNode")) : InNodeName);
	URigVMVariableNode* Node = NewObject<URigVMVariableNode>(Graph, *Name);
	Node->Position = InPosition;
	Node->VariableName = InVariableName;

	if (!bIsGetter)
	{
		URigVMPin* ExecutePin = NewObject<URigVMPin>(Node, *URigVMNode::ExecuteName);
		ExecutePin->CPPType = TEXT("FRigVMExecuteContext");
		ExecutePin->ScriptStruct = FRigVMExecuteContext::StaticStruct();
		ExecutePin->ScriptStructPath = *ExecutePin->ScriptStruct->GetPathName();
		ExecutePin->Direction = ERigVMPinDirection::IO;
		Node->Pins.Add(ExecutePin);
	}

	URigVMPin* ValuePin = NewObject<URigVMPin>(Node, *URigVMVariableNode::ValueName);
	ValuePin->CPPType = InCPPType;
	if (InScriptStruct)
	{
		ValuePin->ScriptStruct = InScriptStruct;
		ValuePin->ScriptStructPath = *ValuePin->ScriptStruct->GetPathName();
	}
	ValuePin->Direction = bIsGetter ? ERigVMPinDirection::Output : ERigVMPinDirection::Input;
	Node->Pins.Add(ValuePin);

	Graph->Nodes.Add(Node);

	if (ValuePin->IsStruct())
	{
		FString DefaultValue = InDefaultValue;
		if (DefaultValue.IsEmpty() || DefaultValue == TEXT("()"))
		{
			TArray<uint8> TempBuffer;
			TempBuffer.AddUninitialized(ValuePin->ScriptStruct->GetStructureSize());
			ValuePin->ScriptStruct->InitializeDefaultValue(TempBuffer.GetData());
			ValuePin->ScriptStruct->ExportText(DefaultValue, TempBuffer.GetData(), nullptr, nullptr, PPF_None, nullptr);
			ValuePin->ScriptStruct->DestroyStruct(TempBuffer.GetData());
		}
		AddPinsForStruct(ValuePin->ScriptStruct, Node, ValuePin, ValuePin->Direction, DefaultValue);
	}
	else if (!InDefaultValue.IsEmpty() && InDefaultValue != TEXT("()"))
	{
		SetPinDefaultValue(ValuePin, InDefaultValue, true, false, false);
	}


	Graph->MarkPackageDirty();

	FRigVMAddVariableNodeAction Action;
	if (bUndo)
	{
		Action = FRigVMAddVariableNodeAction(Node);
		Action.Title = FString::Printf(TEXT("Add %s Variable"), *InVariableName.ToString());
		ActionStack->BeginAction(Action);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Graph, Node);

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return Node;
}

URigVMVariableNode* URigVMController::AddVariableNodeFromStructPath(const FName& InVariableName, const FString& InCPPType, const FString& InScriptStructPath, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bUndo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	UScriptStruct* ScriptStruct = nullptr;
	if (!InScriptStructPath.IsEmpty())
	{
		ScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *InScriptStructPath);
		if (ScriptStruct == nullptr)
		{
			ReportErrorf(TEXT("Cannot find struct for path '%s'."), *InScriptStructPath);
			return nullptr;
		}
	}

	return AddVariableNode(InVariableName, InCPPType, ScriptStruct, bIsGetter, InDefaultValue, InPosition, InNodeName, bUndo);
}

URigVMParameterNode* URigVMController::AddParameterNode(const FName& InParameterName, const FString& InCPPType, UScriptStruct* InScriptStruct, bool bIsInput, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bUndo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	TArray<FRigVMGraphParameterDescription> ExistingParameters = Graph->GetParameterDescriptions();
	for (const FRigVMGraphParameterDescription& ExistingParameter : ExistingParameters)
	{
		if (ExistingParameter.Name == InParameterName)
		{
			if (ExistingParameter.CPPType != InCPPType ||
				ExistingParameter.ScriptStruct != InScriptStruct ||
				ExistingParameter.bIsInput != bIsInput)
			{
				ReportErrorf(TEXT("Cannot add parameter '%s' - parameter already exists."), *InParameterName.ToString());
				return nullptr;
			}
		}
	}

	FString Name = GetValidName(InNodeName.IsEmpty() ? FString(TEXT("ParameterNode")) : InNodeName);
	URigVMParameterNode* Node = NewObject<URigVMParameterNode>(Graph, *Name);
	Node->Position = InPosition;
	Node->ParameterName = InParameterName;

	URigVMPin* ValuePin = NewObject<URigVMPin>(Node, *URigVMParameterNode::ValueName);
	ValuePin->CPPType = InCPPType;
	if (InScriptStruct)
	{
		ValuePin->ScriptStruct = InScriptStruct;
		ValuePin->ScriptStructPath = *ValuePin->ScriptStruct->GetPathName();
	}
	ValuePin->Direction = bIsInput ? ERigVMPinDirection::Output : ERigVMPinDirection::Input;
	Node->Pins.Add(ValuePin);

	Graph->Nodes.Add(Node);

	if (ValuePin->IsStruct())
	{
		FString DefaultValue = InDefaultValue;
		if(DefaultValue.IsEmpty() || DefaultValue == TEXT("()"))
		{
			TArray<uint8> TempBuffer;
			TempBuffer.AddUninitialized(ValuePin->ScriptStruct->GetStructureSize());
			ValuePin->ScriptStruct->InitializeDefaultValue(TempBuffer.GetData());
			ValuePin->ScriptStruct->ExportText(DefaultValue, TempBuffer.GetData(), nullptr, nullptr, PPF_None, nullptr);
			ValuePin->ScriptStruct->DestroyStruct(TempBuffer.GetData());
		}
		AddPinsForStruct(ValuePin->ScriptStruct, Node, ValuePin, ValuePin->Direction, DefaultValue);
	}
	else if (!InDefaultValue.IsEmpty() && InDefaultValue != TEXT("()"))
	{
		SetPinDefaultValue(ValuePin, InDefaultValue, true, false, false);
	}

	Graph->MarkPackageDirty();

	FRigVMAddParameterNodeAction Action;
	if (bUndo)
	{
		Action = FRigVMAddParameterNodeAction(Node);
		Action.Title = FString::Printf(TEXT("Add %s Parameter"), *InParameterName.ToString());
		ActionStack->BeginAction(Action);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Graph, Node);

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return Node;
}

URigVMParameterNode* URigVMController::AddParameterNodeFromStructPath(const FName& InParameterName, const FString& InCPPType, const FString& InScriptStructPath, bool bIsInput, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bUndo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	UScriptStruct* ScriptStruct = nullptr;
	if (!InScriptStructPath.IsEmpty())
	{
		ScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *InScriptStructPath);
		if (ScriptStruct == nullptr)
		{
			ReportErrorf(TEXT("Cannot find struct for path '%s'."), *InScriptStructPath);
			return nullptr;
		}
	}

	return AddParameterNode(InParameterName, InCPPType, ScriptStruct, bIsInput, InDefaultValue, InPosition, InNodeName, bUndo);
}

URigVMCommentNode* URigVMController::AddCommentNode(const FString& InCommentText, const FVector2D& InPosition, const FVector2D& InSize, const FLinearColor& InColor, const FString& InNodeName, bool bUndo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	FString Name = GetValidName(InNodeName.IsEmpty() ? FString(TEXT("CommentNode")) : InNodeName);
	URigVMCommentNode* Node = NewObject<URigVMCommentNode>(Graph, *Name);
	Node->Position = InPosition;
	Node->Size = InSize;
	Node->NodeColor = InColor;
	Node->CommentText = InCommentText;

	Graph->Nodes.Add(Node);
	Graph->MarkPackageDirty();

	FRigVMAddCommentNodeAction Action;
	if (bUndo)
	{
		Action = FRigVMAddCommentNodeAction(Node);
		Action.Title = FString::Printf(TEXT("Add Comment"));
		ActionStack->BeginAction(Action);
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Graph, Node);

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return Node;
}

URigVMRerouteNode* URigVMController::AddRerouteNodeOnLink(URigVMLink* InLink, bool bShowAsFullNode, const FVector2D& InPosition, const FString& InNodeName, bool bUndo)
{
	if(!IsValidLinkForGraph(InLink))
	{
		return nullptr;
	}

	URigVMPin* SourcePin = InLink->GetSourcePin();
	URigVMPin* TargetPin = InLink->GetTargetPin();

	FRigVMBaseAction Action;
	if (bUndo)
	{
		Action.Title = FString::Printf(TEXT("Add Reroute"));
		ActionStack->BeginAction(Action);
	}

	URigVMRerouteNode* Node = AddRerouteNodeOnPin(TargetPin->GetPinPath(), true, bShowAsFullNode, InPosition, InNodeName, bUndo);
	if (Node == nullptr)
	{
		if (bUndo)
		{
			ActionStack->CancelAction(Action);
		}
		return nullptr;
	}

	URigVMPin* ValuePin = Node->Pins[0];
	AddLink(SourcePin, ValuePin, bUndo);

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return Node;
}

URigVMRerouteNode* URigVMController::AddRerouteNodeOnLinkPath(const FString& InLinkPinPathRepresentation, bool bShowAsFullNode, const FVector2D& InPosition, const FString& InNodeName, bool bUndo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMLink* Link = Graph->FindLink(InLinkPinPathRepresentation);
	return AddRerouteNodeOnLink(Link, bShowAsFullNode, InPosition, InNodeName, bUndo);
}

URigVMRerouteNode* URigVMController::AddRerouteNodeOnPin(const FString& InPinPath, bool bAsInput, bool bShowAsFullNode, const FVector2D& InPosition, const FString& InNodeName, bool bUndo)
{
	if (!IsValidGraph())
	{
		return nullptr;
	}

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if(Pin == nullptr)
	{
		return nullptr;
	}

	FRigVMBaseAction Action;
	if (bUndo)
	{
		Action.Title = FString::Printf(TEXT("Add Reroute"));
		ActionStack->BeginAction(Action);
	}

	BreakAllLinks(Pin, bAsInput, bUndo);

	FString Name = GetValidName(InNodeName.IsEmpty() ? FString(TEXT("RerouteNode")) : InNodeName);
	URigVMRerouteNode* Node = NewObject<URigVMRerouteNode>(Graph, *Name);
	Node->Position = InPosition;
	Node->bShowAsFullNode = bShowAsFullNode;

	URigVMPin* ValuePin = NewObject<URigVMPin>(Node, *URigVMRerouteNode::ValueName);
	ConfigurePinFromPin(ValuePin, Pin);
	ValuePin->Direction = ERigVMPinDirection::IO;
	Node->Pins.Add(ValuePin);

	if (ValuePin->IsStruct())
	{
		AddPinsForStruct(ValuePin->GetScriptStruct(), Node, ValuePin, ValuePin->Direction, FString());
	}

	FString DefaultValue = Pin->GetDefaultValue();
	if (!DefaultValue.IsEmpty())
	{
		SetPinDefaultValue(ValuePin, Pin->GetDefaultValue(), true, false, false);
	}

	Graph->Nodes.Add(Node);
	Graph->MarkPackageDirty();

	if (bUndo)
	{
		ActionStack->AddAction(FRigVMAddRerouteNodeAction(Node, InPinPath, bAsInput));
	}

	Notify(ERigVMGraphNotifType::NodeAdded, Graph, Node);

	if (bAsInput)
	{
		AddLink(ValuePin, Pin, bUndo);
	}
	else
	{
		AddLink(Pin, ValuePin, bUndo);
	}

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return Node;
}

bool URigVMController::Undo()
{
	if (!IsValidGraph())
	{
		return false;
	}
	return ActionStack->Undo(this);
}

bool URigVMController::Redo()
{
	if (!IsValidGraph())
	{
		return false;
	}
	return ActionStack->Redo(this);
}

bool URigVMController::OpenUndoBracket(const FString& InTitle)
{
	if (!IsValidGraph())
	{
		return false;
	}
	return ActionStack->OpenUndoBracket(InTitle);
}

bool URigVMController::CloseUndoBracket()
{
	if (!IsValidGraph())
	{
		return false;
	}
	return ActionStack->CloseUndoBracket();
}

#endif

bool URigVMController::RemoveNode(URigVMNode* InNode, bool bUndo)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	FRigVMRemoveNodeAction Action;
	if (bUndo)
	{
		Action = FRigVMRemoveNodeAction(InNode);
		Action.Title = FString::Printf(TEXT("Remove %s Node"), *InNode->GetNodeTitle());
		ActionStack->BeginAction(Action);
	}

	SelectNode(InNode, false, bUndo);

	for (URigVMPin* Pin : InNode->Pins)
	{
		BreakAllLinks(Pin, true, bUndo);
		BreakAllLinks(Pin, false, bUndo);
		BreakAllLinksRecursive(Pin, true, false, bUndo);
		BreakAllLinksRecursive(Pin, false, false, bUndo);
	}

	Graph->Nodes.Remove(InNode);
	Graph->MarkPackageDirty();

	Notify(ERigVMGraphNotifType::NodeRemoved, Graph, InNode);

	InNode->MarkPendingKill();

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

bool URigVMController::RemoveNodeByName(const FName& InNodeName, bool bUndo)
{
	if (!IsValidGraph())
	{
		return false;
	}
	return RemoveNode(Graph->FindNodeByName(InNodeName), bUndo);
}

bool URigVMController::SelectNode(URigVMNode* InNode, bool bSelect, bool bUndo)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (InNode->IsSelected() == bSelect)
	{
		return false;
	}

	FRigVMSelectNodeAction Action;
	if (bUndo)
	{
		Action = FRigVMSelectNodeAction(InNode);
		if (bSelect)
		{
			Action.Title = FString::Printf(TEXT("Select %s Node"), *InNode->GetNodeTitle());
		}
		else
		{
			Action.Title = FString::Printf(TEXT("Deselect %s Node"), *InNode->GetNodeTitle());
		}
		ActionStack->BeginAction(Action);
	}

	if (bSelect)
	{
		Graph->SelectedNodes.Add(InNode->GetFName());
		Notify(ERigVMGraphNotifType::NodeSelected, Graph, InNode);
	}
	else
	{
		Graph->SelectedNodes.Remove(InNode->GetFName());
		Notify(ERigVMGraphNotifType::NodeDeselected, Graph, InNode);
	}

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

bool URigVMController::SelectNodeByName(const FName& InNodeName, bool bSelect, bool bUndo)
{
	if (!IsValidGraph())
	{
		return false;
	}
	return SelectNode(Graph->FindNodeByName(InNodeName), bSelect, bUndo);
}

bool URigVMController::ClearNodeSelection(bool bUndo)
{
	if (!IsValidGraph())
	{
		return false;
	}

	FRigVMBaseAction Action;
	if (bUndo)
	{
		Action.Title = FString::Printf(TEXT("Clear selection"));
		ActionStack->BeginAction(Action);
	}

	TArray<FName> Selection = Graph->SelectedNodes;
	for (const FName& SelectedNode : Selection)
	{
		SelectNodeByName(SelectedNode, false, bUndo);
	}

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return Selection.Num() > 0;
}

bool URigVMController::SetNodePosition(URigVMNode* InNode, const FVector2D& InPosition, bool bUndo, bool bMergeUndoAction)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	FRigVMSetNodePositionAction Action;
	if (bUndo)
	{
		Action = FRigVMSetNodePositionAction(InNode, InPosition);
		Action.Title = FString::Printf(TEXT("Set Node Position"));
		ActionStack->BeginAction(Action);
	}

	InNode->Position = InPosition;
	Notify(ERigVMGraphNotifType::NodePositionChanged, Graph, InNode);

	if (bUndo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	return true;
}

bool URigVMController::SetNodePositionByName(const FName& InNodeName, const FVector2D& InPosition, bool bUndo, bool bMergeUndoAction)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetNodePosition(Node, InPosition, bUndo, bMergeUndoAction);
}

bool URigVMController::SetNodeSize(URigVMNode* InNode, const FVector2D& InSize, bool bUndo, bool bMergeUndoAction)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	FRigVMSetNodeSizeAction Action;
	if (bUndo)
	{
		Action = FRigVMSetNodeSizeAction(InNode, InSize);
		Action.Title = FString::Printf(TEXT("Set Node Size"));
		ActionStack->BeginAction(Action);
	}

	InNode->Size = InSize;
	Notify(ERigVMGraphNotifType::NodeSizeChanged, Graph, InNode);

	if (bUndo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	return true;
}

bool URigVMController::SetNodeSizeByName(const FName& InNodeName, const FVector2D& InSize, bool bUndo, bool bMergeUndoAction)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetNodeSize(Node, InSize, bUndo, bMergeUndoAction);
}

bool URigVMController::SetNodeColor(URigVMNode* InNode, const FLinearColor& InColor, bool bUndo, bool bMergeUndoAction)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	FRigVMSetNodeColorAction Action;
	if (bUndo)
	{
		Action = FRigVMSetNodeColorAction(InNode, InColor);
		Action.Title = FString::Printf(TEXT("Set Node Color"));
		ActionStack->BeginAction(Action);
	}

	InNode->NodeColor = InColor;
	Notify(ERigVMGraphNotifType::NodeColorChanged, Graph, InNode);

	if (bUndo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}

	return true;
}

bool URigVMController::SetNodeColorByName(const FName& InNodeName, const FLinearColor& InColor, bool bUndo, bool bMergeUndoAction)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetNodeColor(Node, InColor, bUndo, bMergeUndoAction);
}

bool URigVMController::SetCommentText(URigVMNode* InNode, const FString& InCommentText, bool bUndo)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (URigVMCommentNode* CommentNode = Cast<URigVMCommentNode>(InNode))
	{
		FRigVMSetCommentTextAction Action;
		if (bUndo)
		{
			Action = FRigVMSetCommentTextAction(CommentNode, InCommentText);
			Action.Title = FString::Printf(TEXT("Set Comment Text"));
			ActionStack->BeginAction(Action);
		}

		CommentNode->CommentText = InCommentText;
		Notify(ERigVMGraphNotifType::CommentTextChanged, Graph, InNode);

		if (bUndo)
		{
			ActionStack->EndAction(Action);
		}

		return true;
	}

	return false;
}

bool URigVMController::SetCommentTextByName(const FName& InNodeName, const FString& InCommentText, bool bUndo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetCommentText(Node, InCommentText, bUndo);
}

bool URigVMController::SetRerouteCompactness(URigVMNode* InNode, bool bShowAsFullNode, bool bUndo)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(InNode))
	{
		FRigVMSetRerouteCompactnessAction Action;
		if (bUndo)
		{
			Action = FRigVMSetRerouteCompactnessAction(RerouteNode, bShowAsFullNode);
			Action.Title = FString::Printf(TEXT("Set Reroute Size"));
			ActionStack->BeginAction(Action);
		}

		RerouteNode->bShowAsFullNode = bShowAsFullNode;
		Notify(ERigVMGraphNotifType::RerouteCompactnessChanged, Graph, InNode);

		if (bUndo)
		{
			ActionStack->EndAction(Action);
		}

		return true;
	}

	return false;
}

bool URigVMController::SetRerouteCompactnessByName(const FName& InNodeName, bool bShowAsFullNode, bool bUndo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMNode* Node = Graph->FindNodeByName(InNodeName);
	return SetRerouteCompactness(Node, bShowAsFullNode, bUndo);
}

bool URigVMController::RenameVariable(const FName& InOldName, const FName& InNewName, bool bUndo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InOldName == InNewName)
	{
		ReportWarning(TEXT("RenameVariable: InOldName and InNewName are equal."));
		return false;
	}

	TArray<FRigVMGraphVariableDescription> ExistingVariables = Graph->GetVariableDescriptions();
	for (const FRigVMGraphVariableDescription& ExistingVariable : ExistingVariables)
	{
		if (ExistingVariable.Name == InNewName)
		{
			ReportErrorf(TEXT("Cannot rename variable to '%s' - variable already exists."), *InNewName.ToString());
			return false;
		}
	}

	FRigVMRenameVariableAction Action;
	if (bUndo)
	{
		Action = FRigVMRenameVariableAction(InOldName, InNewName);
		Action.Title = FString::Printf(TEXT("Rename Variable"));
		ActionStack->BeginAction(Action);
	}

	TArray<URigVMNode*> RenamedNodes;
	for (URigVMNode* Node : Graph->Nodes)
	{
		if(URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			if (VariableNode->VariableName == InOldName)
			{
				VariableNode->VariableName = InNewName;
				RenamedNodes.Add(Node);
			}
		}
	}

	for (URigVMNode* RenamedNode : RenamedNodes)
	{
		Graph->Notify(ERigVMGraphNotifType::VariableRenamed, Graph, RenamedNode);
		Graph->MarkPackageDirty();
	}

	if (bUndo)
	{
		if (RenamedNodes.Num() > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	return RenamedNodes.Num() > 0;
}

bool URigVMController::RenameParameter(const FName& InOldName, const FName& InNewName, bool bUndo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InOldName == InNewName)
	{
		ReportWarning(TEXT("RenameParameter: InOldName and InNewName are equal."));
		return false;
	}

	TArray<FRigVMGraphParameterDescription> ExistingParameters = Graph->GetParameterDescriptions();
	for (const FRigVMGraphParameterDescription& ExistingParameter : ExistingParameters)
	{
		if (ExistingParameter.Name == InNewName)
		{
			ReportErrorf(TEXT("Cannot rename parameter to '%s' - parameter already exists."), *InNewName.ToString());
			return false;
		}
	}

	FRigVMRenameParameterAction Action;
	if (bUndo)
	{
		Action = FRigVMRenameParameterAction(InOldName, InNewName);
		Action.Title = FString::Printf(TEXT("Rename Parameter"));
		ActionStack->BeginAction(Action);
	}

	TArray<URigVMNode*> RenamedNodes;
	for (URigVMNode* Node : Graph->Nodes)
	{
		if(URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
		{
			if (ParameterNode->ParameterName == InOldName)
			{
				ParameterNode->ParameterName = InNewName;
				RenamedNodes.Add(Node);
			}
		}
	}

	for (URigVMNode* RenamedNode : RenamedNodes)
	{
		Graph->Notify(ERigVMGraphNotifType::ParameterRenamed, Graph, RenamedNode);
		Graph->MarkPackageDirty();
	}

	if (bUndo)
	{
		if (RenamedNodes.Num() > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	return RenamedNodes.Num() > 0;
}

FString URigVMController::GetPinDefaultValue(const FString& InPinPath)
{
	if (!IsValidGraph())
	{
		return FString();
	}

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return FString();
	}

	return Pin->GetDefaultValue();
}

bool URigVMController::SetPinDefaultValue(const FString& InPinPath, const FString& InDefaultValue, bool bResizeArrays, bool bUndo, bool bMergeUndoAction)
{
	if (!IsValidGraph())
	{
		return false;
	}

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}

	SetPinDefaultValue(Pin, InDefaultValue, bResizeArrays, bUndo, bMergeUndoAction);
	return true;
}

void URigVMController::SetPinDefaultValue(URigVMPin* InPin, const FString& InDefaultValue, bool bResizeArrays, bool bUndo, bool bMergeUndoAction)
{
	check(InPin);
	ensure(!InDefaultValue.IsEmpty());

	FRigVMSetPinDefaultValueAction Action;
	if (bUndo)
	{
		Action = FRigVMSetPinDefaultValueAction(InPin, InDefaultValue);
		Action.Title = FString::Printf(TEXT("Set Pin Default Value"));
		ActionStack->BeginAction(Action);
	}

	bool bSetPinDefaultValueSucceeded = false;
	if (InPin->IsArray())
	{
		if (ShouldPinBeUnfolded(InPin))
		{
			TArray<FString> Elements = SplitDefaultValue(InDefaultValue);

			if (bResizeArrays)
			{
				while (Elements.Num() > InPin->SubPins.Num())
				{
					InsertArrayPin(InPin, INDEX_NONE, FString(), bUndo);
				}
				while (Elements.Num() < InPin->SubPins.Num())
				{
					RemovePin(InPin->SubPins.Last(), bUndo);
				}
			}
			else
			{
				ensure(Elements.Num() == InPin->SubPins.Num());
			}

			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ElementIndex++)
			{
				URigVMPin* SubPin = InPin->SubPins[ElementIndex];
				if (SubPin->IsStringType())
				{
					Elements[ElementIndex].Mid(1, Elements[ElementIndex].Len() - 2);
				}
				SetPinDefaultValue(SubPin, Elements[ElementIndex], bResizeArrays, false, false);
				bSetPinDefaultValueSucceeded = true;
			}
		}
	}
	else if (InPin->IsStruct())
	{
		TArray<FString> MemberValuePairs = SplitDefaultValue(InDefaultValue);

		for (const FString& MemberValuePair : MemberValuePairs)
		{
			FString MemberName, MemberValue;
			if (MemberValuePair.Split(TEXT("="), &MemberName, &MemberValue))
			{
				URigVMPin* SubPin = InPin->FindSubPin(MemberName);
				if (SubPin)
				{
					if (SubPin->IsStringType())
					{
						MemberValue.Mid(1, MemberValue.Len() - 2);
					}
					SetPinDefaultValue(SubPin, MemberValue, bResizeArrays, false, false);
					bSetPinDefaultValueSucceeded = true;
				}
			}
		}
	}
	
	if(!bSetPinDefaultValueSucceeded)
	{
		ensure(InPin->GetSubPins().Num() == 0);;
		InPin->DefaultValue = InDefaultValue;
		Graph->Notify(ERigVMGraphNotifType::PinDefaultValueChanged, Graph, InPin);
		Graph->MarkPackageDirty();
	}

	if (bUndo)
	{
		ActionStack->EndAction(Action, bMergeUndoAction);
	}
}

TArray<FString> URigVMController::SplitDefaultValue(const FString& InDefaultValue)
{
	TArray<FString> Parts;
	if (InDefaultValue.IsEmpty())
	{
		return Parts;
	}

	ensure(InDefaultValue[0] == TCHAR('('));
	ensure(InDefaultValue[InDefaultValue.Len() - 1] == TCHAR(')'));

	FString Content = InDefaultValue.Mid(1, InDefaultValue.Len() - 2);
	int32 BraceCount = 0;
	int32 QuoteCount = 0;

	int32 LastPartStartIndex = 0;
	for (int32 CharIndex = 0; CharIndex < Content.Len(); CharIndex++)
	{
		TCHAR Char = Content[CharIndex];
		if (QuoteCount > 0)
		{
			if (Content[CharIndex - 1] == TCHAR('\\') && Char == TCHAR('"'))
			{
				QuoteCount = 0;
			}
		}
		else if (Char == TCHAR('"'))
		{
			QuoteCount = 1;
		}
		else if (Char == TCHAR('('))
		{
			BraceCount++;
		}
		else if (Char == TCHAR(')'))
		{
			BraceCount--;
			BraceCount = FMath::Max<int32>(BraceCount, 0);
		}
		else if (Char == TCHAR(',') && BraceCount == 0)
		{
			Parts.Add(Content.Mid(LastPartStartIndex, CharIndex - LastPartStartIndex));
			LastPartStartIndex = CharIndex + 1;
		}
	}

	Parts.Add(Content.Mid(LastPartStartIndex));
	return Parts;
}

FString URigVMController::AddArrayPin(const FString& InArrayPinPath, const FString& InDefaultValue, bool bUndo)
{
	return InsertArrayPin(InArrayPinPath, INDEX_NONE, InDefaultValue, bUndo);
}

FString URigVMController::DuplicateArrayPin(const FString& InArrayElementPinPath, bool bUndo)
{
	if (!IsValidGraph())
	{
		return FString();
	}

	URigVMPin* ElementPin = Graph->FindPin(InArrayElementPinPath);
	if (ElementPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InArrayElementPinPath);
		return FString();
	}

	if (!ElementPin->IsArrayElement())
	{
		ReportErrorf(TEXT("Pin '%s' is not an array element."), *InArrayElementPinPath);
		return FString();
	}

	URigVMPin* ArrayPin = ElementPin->GetParentPin();
	check(ArrayPin);
	ensure(ArrayPin->IsArray());

	FString DefaultValue = ElementPin->GetDefaultValue();
	return InsertArrayPin(ArrayPin->GetPinPath(), ElementPin->GetPinIndex() + 1, DefaultValue, bUndo);
}

FString URigVMController::InsertArrayPin(const FString& InArrayPinPath, int32 InIndex, const FString& InDefaultValue, bool bUndo)
{
	if (!IsValidGraph())
	{
		return FString();
	}

	URigVMPin* ArrayPin = Graph->FindPin(InArrayPinPath);
	if (ArrayPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InArrayPinPath);
		return FString();
	}

	URigVMPin* ElementPin = InsertArrayPin(ArrayPin, InIndex, InDefaultValue, bUndo);
	if (ElementPin)
	{
		return ElementPin->GetPinPath();
	}

	return FString();
}

URigVMPin* URigVMController::InsertArrayPin(URigVMPin* ArrayPin, int32 InIndex, const FString& InDefaultValue, bool bUndo)
{
	if (!ArrayPin->IsArray())
	{
		ReportErrorf(TEXT("Pin '%s' is not an array."), *ArrayPin->GetPinPath());
		return nullptr;
	}

	if (!ShouldPinBeUnfolded(ArrayPin))
	{
		ReportErrorf(TEXT("Cannot insert array pin under '%s'."), *ArrayPin->GetPinPath());
		return nullptr;
	}

	if (InIndex == INDEX_NONE)
	{
		InIndex = ArrayPin->GetSubPins().Num();
	}

	FRigVMInsertArrayPinAction Action;
	if (bUndo)
	{
		Action = FRigVMInsertArrayPinAction(ArrayPin, InIndex, InDefaultValue);
		Action.Title = FString::Printf(TEXT("Insert Array Pin"));
		ActionStack->BeginAction(Action);
	}

	for (int32 ExistingIndex = ArrayPin->GetSubPins().Num() - 1; ExistingIndex >= InIndex; ExistingIndex--)
	{
		URigVMPin* ExistingPin = ArrayPin->GetSubPins()[ExistingIndex];
		ExistingPin->Rename(*FString::FormatAsNumber(ExistingIndex + 1));
	}

	URigVMPin* Pin = NewObject<URigVMPin>(ArrayPin, *FString::FormatAsNumber(InIndex));
	ConfigurePinFromPin(Pin, ArrayPin);
	Pin->CPPType = ArrayPin->GetArrayElementCppType();
	ArrayPin->SubPins.Insert(Pin, InIndex);

	if (Pin->IsStruct())
	{
		UScriptStruct* ScriptStruct = Pin->GetScriptStruct();
		if (ScriptStruct)
		{
			AddPinsForStruct(ScriptStruct, Pin->GetNode(), Pin, Pin->Direction, InDefaultValue);
		}
	}
	else if (Pin->IsArray())
	{
		FArrayProperty * ArrayProperty = CastField<FArrayProperty>(FindPropertyForPin(Pin->GetPinPath()));
		if (ArrayProperty)
		{
			TArray<FString> ElementDefaultValues = SplitDefaultValue(InDefaultValue);
			AddPinsForArray(ArrayProperty, Pin->GetNode(), Pin, Pin->Direction, ElementDefaultValues);
		}
	}
	else
	{
		Pin->DefaultValue = InDefaultValue;
	}

	Graph->Notify(ERigVMGraphNotifType::PinArraySizeChanged, Graph, ArrayPin);
	Graph->MarkPackageDirty();

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return Pin;
}

bool URigVMController::RemoveArrayPin(const FString& InArrayElementPinPath, bool bUndo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMPin* ArrayElementPin = Graph->FindPin(InArrayElementPinPath);
	if (ArrayElementPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InArrayElementPinPath);
		return false;
	}

	if (!ArrayElementPin->IsArrayElement())
	{
		ReportErrorf(TEXT("Pin '%s' is not an array element."), *InArrayElementPinPath);
		return false;
	}

	URigVMPin* ArrayPin = ArrayElementPin->GetParentPin();
	check(ArrayPin);
	ensure(ArrayPin->IsArray());

	if (ArrayPin->GetArraySize() == 1)
	{
		ReportErrorf(TEXT("Cannot remove the last element from array pin '%s'."), *ArrayPin->GetPinPath());
		return false;
	}

	FRigVMRemoveArrayPinAction Action;
	if (bUndo)
	{
		Action = FRigVMRemoveArrayPinAction(ArrayElementPin);
		Action.Title = FString::Printf(TEXT("Remove Array Pin"));
		ActionStack->BeginAction(Action);
	}

	int32 IndexToRemove = ArrayElementPin->GetPinIndex();
	if (!RemovePin(ArrayElementPin, bUndo))
	{
		return false;
	}

	for (int32 ExistingIndex = ArrayPin->GetSubPins().Num() - 1; ExistingIndex >= IndexToRemove; ExistingIndex--)
	{
		URigVMPin* ExistingPin = ArrayPin->GetSubPins()[ExistingIndex];
		ExistingPin->SetNameFromIndex();
	}

	Graph->MarkPackageDirty();
	Graph->Notify(ERigVMGraphNotifType::PinArraySizeChanged, Graph, ArrayPin);

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

bool URigVMController::RemovePin(URigVMPin* InPinToRemove, bool bUndo)
{
	if (bUndo)
	{
		BreakAllLinks(InPinToRemove, true, bUndo);
		BreakAllLinks(InPinToRemove, false, bUndo);
		BreakAllLinksRecursive(InPinToRemove, true, false, bUndo);
		BreakAllLinksRecursive(InPinToRemove, false, false, bUndo);
	}

	URigVMPin* ParentPin = InPinToRemove->GetParentPin();
	ParentPin->SubPins.Remove(InPinToRemove);
	InPinToRemove->Rename(*FString::Printf(TEXT("URigVMPin_%d_Removed"), ++sRemovedPinIndex), GetTransientPackage());

	TArray<URigVMPin*> SubPins = InPinToRemove->GetSubPins();
	for (URigVMPin* SubPin : SubPins)
	{
		if (!RemovePin(SubPin, bUndo))
		{
			return false;
		}
	}

	InPinToRemove->MarkPendingKill();
	return true;
}

bool URigVMController::ClearArrayPin(const FString& InArrayPinPath, bool bUndo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMPin* Pin = Graph->FindPin(InArrayPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InArrayPinPath);
		return false;
	}

	if (!Pin->IsArray())
	{
		ReportErrorf(TEXT("Pin '%s' is not an array."), *InArrayPinPath);
		return false;
	}

	TArray<URigVMPin*> ElementPins = Pin->GetSubPins();
	if (ElementPins.Num() == 0)
	{
		return false;
	}

	FRigVMBaseAction Action;
	if (bUndo)
	{
		Action.Title = FString::Printf(TEXT("Clear Array Pin"));
		ActionStack->BeginAction(Action);
	}

	int32 RemovedPins = 0;
	for (int32 ElementIndex = ElementPins.Num() - 1; ElementIndex > 0; ElementIndex--)
	{
		if (!RemoveArrayPin(ElementPins[ElementIndex]->GetPinPath(), bUndo))
		{
			if (bUndo)
			{
				ActionStack->CancelAction(Action);
			}
			return false;
		}
		RemovedPins++;
	}

	if (bUndo)
	{
		if (RemovedPins > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	return RemovedPins > 0;
}

bool URigVMController::AddLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bUndo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMPin* OutputPin = Graph->FindPin(InOutputPinPath);
	if (OutputPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InOutputPinPath);
		return false;
	}
	URigVMPin* InputPin = Graph->FindPin(InInputPinPath);
	if (InputPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InInputPinPath);
		return false;
	}

	return AddLink(OutputPin, InputPin, bUndo);
}

bool URigVMController::AddLink(URigVMPin* OutputPin, URigVMPin* InputPin, bool bUndo)
{
	if(OutputPin == nullptr)
	{
		ReportError(TEXT("OutputPin is nullptr."));
		return false;
	}

	if(InputPin == nullptr)
	{
		ReportError(TEXT("InputPin is nullptr."));
		return false;
	}

	if(!IsValidPinForGraph(OutputPin) || !IsValidPinForGraph(InputPin))
	{
		return false;
	}

	FString FailureReason;
	if (!Graph->CanLink(OutputPin, InputPin, &FailureReason))
	{
		ReportErrorf(TEXT("Cannot link '%s' to '%s': %s."), *OutputPin->GetPinPath(), *InputPin->GetPinPath(), *FailureReason);
		return false;
	}

	ensure(!OutputPin->IsLinkedTo(InputPin));
	ensure(!InputPin->IsLinkedTo(OutputPin));

	FRigVMAddLinkAction Action;
	if (bUndo)
	{
		Action = FRigVMAddLinkAction(OutputPin, InputPin);
		Action.Title = FString::Printf(TEXT("Add Link"));
		ActionStack->BeginAction(Action);
	}

	BreakAllLinks(InputPin, true, bUndo);
	if (bUndo)
	{
		BreakAllLinksRecursive(InputPin, true, true, bUndo);
		BreakAllLinksRecursive(InputPin, true, false, bUndo);
	}

	URigVMLink* Link = NewObject<URigVMLink>(Graph);
	Link->SourcePin = OutputPin;
	Link->TargetPin = InputPin;
	Link->SourcePinPath = OutputPin->GetPinPath();
	Link->TargetPinPath = InputPin->GetPinPath();
	Graph->Links.Add(Link);
	OutputPin->Links.Add(Link);
	InputPin->Links.Add(Link);

	Graph->MarkPackageDirty();
	Graph->Notify(ERigVMGraphNotifType::LinkAdded, Graph, Link);

	if (bUndo)
	{
		ActionStack->EndAction(Action);
	}

	return true;
}

bool URigVMController::BreakLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bUndo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMPin* OutputPin = Graph->FindPin(InOutputPinPath);
	if (OutputPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InOutputPinPath);
		return false;
	}
	URigVMPin* InputPin = Graph->FindPin(InInputPinPath);
	if (InputPin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InInputPinPath);
		return false;
	}

	return BreakLink(OutputPin, InputPin, bUndo);
}

bool URigVMController::BreakLink(URigVMPin* OutputPin, URigVMPin* InputPin, bool bUndo)
{
	if(!IsValidPinForGraph(OutputPin) || !IsValidPinForGraph(InputPin))
	{
		return false;
	}

	if (!OutputPin->IsLinkedTo(InputPin))
	{
		return false;
	}
	ensure(InputPin->IsLinkedTo(OutputPin));

	for (URigVMLink* Link : InputPin->Links)
	{
		if (Link->SourcePin == OutputPin && Link->TargetPin == InputPin)
		{
			FRigVMBreakLinkAction Action;
			if (bUndo)
			{
				Action = FRigVMBreakLinkAction(OutputPin, InputPin);
				Action.Title = FString::Printf(TEXT("Break Link"));
				ActionStack->BeginAction(Action);
			}

			OutputPin->Links.Remove(Link);
			InputPin->Links.Remove(Link);
			Graph->Links.Remove(Link);
			
			Graph->MarkPackageDirty();
			Graph->Notify(ERigVMGraphNotifType::LinkRemoved, Graph, Link);

			Link->MarkPendingKill();

			if (bUndo)
			{
				ActionStack->EndAction(Action);
			}

			return true;
		}
	}

	return false;
}

bool URigVMController::BreakAllLinks(const FString& InPinPath, bool bAsInput, bool bUndo)
{
	if(!IsValidGraph())
	{
		return false;
	}

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return false;
	}

	return BreakAllLinks(Pin, bAsInput, bUndo);
}

bool URigVMController::BreakAllLinks(URigVMPin* Pin, bool bAsInput, bool bUndo)
{
	if(!IsValidPinForGraph(Pin))
	{
		return false;
	}

	FRigVMBaseAction Action;
	if (bUndo)
	{
		Action.Title = FString::Printf(TEXT("Break All Links"));
		ActionStack->BeginAction(Action);
	}

	int32 LinksBroken = 0;
	TArray<URigVMLink*> Links = Pin->GetLinks();
	for (int32 LinkIndex = Links.Num() - 1; LinkIndex >= 0; LinkIndex--)
	{
		URigVMLink* Link = Links[LinkIndex];
		if (bAsInput && Link->GetTargetPin() == Pin)
		{
			LinksBroken += BreakLink(Link->GetSourcePin(), Pin, bUndo) ? 1 : 0;
		}
		else if (!bAsInput && Link->GetSourcePin() == Pin)
		{
			LinksBroken += BreakLink(Pin, Link->GetTargetPin(), bUndo) ? 1 : 0;
		}
	}

	if (bUndo)
	{
		if (LinksBroken > 0)
		{
			ActionStack->EndAction(Action);
		}
		else
		{
			ActionStack->CancelAction(Action);
		}
	}

	return LinksBroken > 0;
}

void URigVMController::BreakAllLinksRecursive(URigVMPin* Pin, bool bAsInput, bool bTowardsParent, bool bUndo)
{
	if (bTowardsParent)
	{
		URigVMPin* ParentPin = Pin->GetParentPin();
		if (ParentPin)
		{
			BreakAllLinks(ParentPin, bAsInput, bUndo);
			BreakAllLinksRecursive(ParentPin, bAsInput, bTowardsParent, bUndo);
		}
	}
	else
	{
		for (URigVMPin* SubPin : Pin->SubPins)
		{
			BreakAllLinks(SubPin, bAsInput, bUndo);
			BreakAllLinksRecursive(SubPin, bAsInput, bTowardsParent, bUndo);
		}
	}
}

FString URigVMController::GetValidName(const FString& InPrefix)
{
	int32 NameSuffix = 0;
	FString Name = InPrefix;

	while (!Graph->IsNameAvailable(Name))
	{
		Name = FString::Printf(TEXT("%s_%d"), *InPrefix, ++NameSuffix);
	}

	return Name;
}

bool URigVMController::IsValidGraph()
{
	if (Graph == nullptr)
	{
		ReportError(TEXT("Controller does not have a graph associated - use SetGraph / set_graph."));
		return false;
	}
	return true;
}

bool URigVMController::IsValidNodeForGraph(URigVMNode* InNode)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InNode == nullptr)
	{
		ReportError(TEXT("InNode is nullptr."));
		return false;
	}

	if (InNode->GetGraph() != Graph)
	{
		ReportErrorf(TEXT("InNode '%s' is on a different graph."), *InNode->GetNodePath());
		return false;
	}

	if (InNode->GetNodeIndex() == INDEX_NONE)
	{
		ReportErrorf(TEXT("InNode '%s' is transient (not yet nested to a graph)."), *InNode->GetName());
	}

	return true;
}

bool URigVMController::IsValidPinForGraph(URigVMPin* InPin)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InPin == nullptr)
	{
		ReportError(TEXT("InPin is nullptr."));
		return false;
	}

	if (!IsValidNodeForGraph(InPin->GetNode()))
	{
		return false;
	}

	if (InPin->GetPinIndex() == INDEX_NONE)
	{
		ReportErrorf(TEXT("InPin '%s' is transient (not yet nested properly)."), *InPin->GetName());
	}

	return true;
}

bool URigVMController::IsValidLinkForGraph(URigVMLink* InLink)
{
	if(!IsValidGraph())
	{
		return false;
	}

	if (InLink == nullptr)
	{
		ReportError(TEXT("InLink is nullptr."));
		return false;
	}

	if (InLink->GetGraph() != Graph)
	{
		ReportError(TEXT("InLink is on a different graph."));
		return false;
	}

	if(InLink->GetSourcePin() == nullptr)
	{
		ReportError(TEXT("InLink has no source pin."));
		return false;
	}

	if(InLink->GetTargetPin() == nullptr)
	{
		ReportError(TEXT("InLink has no target pin."));
		return false;
	}

	if (InLink->GetLinkIndex() == INDEX_NONE)
	{
		ReportError(TEXT("InLink is transient (not yet nested properly)."));
	}

	if(!IsValidPinForGraph(InLink->GetSourcePin()))
	{
		return false;
	}

	if(!IsValidPinForGraph(InLink->GetTargetPin()))
	{
		return false;
	}

	return true;
}

void URigVMController::AddPinsForStruct(UStruct* InStruct, URigVMNode* InNode, URigVMPin* InParentPin, ERigVMPinDirection InPinDirection, const FString& InDefaultValue)
{
	TArray<FString> MemberNameValuePairs = SplitDefaultValue(InDefaultValue);
	TMap<FName, FString> MemberValues;
	for (const FString& MemberNameValuePair : MemberNameValuePairs)
	{
		FString MemberName, MemberValue;
		if (MemberNameValuePair.Split(TEXT("="), &MemberName, &MemberValue))
		{
			MemberValues.Add(*MemberName, MemberValue);
		}
	}

	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		FName PropertyName = It->GetFName();

		URigVMPin* Pin = NewObject<URigVMPin>(InParentPin == nullptr ? Cast<UObject>(InNode) : Cast<UObject>(InParentPin), PropertyName);
		ConfigurePinFromProperty(*It, Pin, InPinDirection);

		if (InParentPin)
		{
			InParentPin->SubPins.Add(Pin);
		}
		else
		{
			InNode->Pins.Add(Pin);
		}

		FString* DefaultValuePtr = MemberValues.Find(Pin->GetFName());

		FStructProperty* StructProperty = CastField<FStructProperty>(*It);
		if (StructProperty)
		{
			if (ShouldStructBeUnfolded(StructProperty->Struct))
			{
				FString DefaultValue;
				if (DefaultValuePtr != nullptr)
				{
					DefaultValue = *DefaultValuePtr;
				}
				AddPinsForStruct(StructProperty->Struct, InNode, Pin, Pin->GetDirection(), DefaultValue);
			}
			else if(DefaultValuePtr != nullptr)
			{
				Pin->DefaultValue = *DefaultValuePtr;
			}
		}

		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(*It);
		if (ArrayProperty)
		{
			ensure(Pin->IsArray());

			if (DefaultValuePtr)
			{
				if (ShouldPinBeUnfolded(Pin))
				{
					TArray<FString> ElementDefaultValues = SplitDefaultValue(*DefaultValuePtr);
					AddPinsForArray(ArrayProperty, InNode, Pin, Pin->Direction, ElementDefaultValues);
				}
				else
				{
					Pin->DefaultValue = *DefaultValuePtr;
				}
			}
		}
		
		if (!Pin->IsArray() && !Pin->IsStruct() && DefaultValuePtr != nullptr)
		{
			FString DefaultValue = *DefaultValuePtr;
			if (Pin->IsStringType())
			{
				DefaultValue = DefaultValue.Mid(1, DefaultValue.Len() - 2);
			}
			Pin->DefaultValue = DefaultValue;
		}
	}
}

void URigVMController::AddPinsForArray(FArrayProperty* InArrayProperty, URigVMNode* InNode, URigVMPin* InParentPin, ERigVMPinDirection InPinDirection, const TArray<FString>& InDefaultValues)
{
	check(InParentPin);
	if (!ShouldPinBeUnfolded(InParentPin))
	{
		return;
	}

	for (int32 ElementIndex = 0; ElementIndex < InDefaultValues.Num(); ElementIndex++)
	{
		FString ElementName = FString::FormatAsNumber(InParentPin->SubPins.Num());
		URigVMPin* Pin = NewObject<URigVMPin>(InParentPin, *ElementName);
		ConfigurePinFromProperty(InArrayProperty->Inner, Pin, InPinDirection);
		FString DefaultValue = InDefaultValues[ElementIndex];

		InParentPin->SubPins.Add(Pin);

		FStructProperty* StructProperty = CastField<FStructProperty>(InArrayProperty->Inner);
		if (StructProperty)
		{
			if (ShouldPinBeUnfolded(Pin))
			{
				AddPinsForStruct(StructProperty->Struct, InNode, Pin, Pin->Direction, DefaultValue);
			}
			else if(!DefaultValue.IsEmpty())
			{
				Pin->DefaultValue = DefaultValue;
			}
		}

		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InArrayProperty->Inner);
		if (ArrayProperty)
		{
			if (ShouldPinBeUnfolded(Pin))
			{
				TArray<FString> ElementDefaultValues = SplitDefaultValue(DefaultValue);
				AddPinsForArray(ArrayProperty, InNode, Pin, Pin->Direction, ElementDefaultValues);
			}
			else if (!DefaultValue.IsEmpty())
			{
				Pin->DefaultValue = DefaultValue;
			}
		}

		if (!Pin->IsArray() && !Pin->IsStruct())
		{
			if (Pin->IsStringType())
			{
				DefaultValue = DefaultValue.Mid(1, DefaultValue.Len() - 2);
			}
			Pin->DefaultValue = DefaultValue;
		}
	}
}

void URigVMController::ConfigurePinFromProperty(FProperty* InProperty, URigVMPin* InOutPin, ERigVMPinDirection InPinDirection)
{
	if (InPinDirection == ERigVMPinDirection::Invalid)
	{
#if WITH_EDITOR
		bool bIsInput = InProperty->HasMetaData(TEXT("Input"));
		bool bIsOutput = InProperty->HasMetaData(TEXT("Output"));

		if (bIsInput)
		{
			InOutPin->Direction = bIsOutput ? ERigVMPinDirection::IO : ERigVMPinDirection::Input;
		}
		else
		{
			InOutPin->Direction = bIsOutput ? ERigVMPinDirection::Output : ERigVMPinDirection::Hidden;
		}
#endif
	}
	else
	{
		InOutPin->Direction = InPinDirection;
	}

#if WITH_EDITOR
	InOutPin->bIsConstant = InProperty->HasMetaData(TEXT("Constant"));
	FString CustomWidgetName = InProperty->GetMetaData(TEXT("Widget"));
	InOutPin->CustomWidgetName = CustomWidgetName.IsEmpty() ? FName(NAME_None) : FName(*CustomWidgetName);
#endif

	FString ExtendedCppType;
	InOutPin->CPPType = InProperty->GetCPPType(&ExtendedCppType);
	InOutPin->CPPType += ExtendedCppType;

	FStructProperty* StructProperty = CastField<FStructProperty>(InProperty);
	if (StructProperty)
	{
		InOutPin->ScriptStruct = StructProperty->Struct;
		InOutPin->ScriptStructPath = *InOutPin->ScriptStruct->GetPathName();
	}
}

void URigVMController::ConfigurePinFromPin(URigVMPin* InOutPin, URigVMPin* InPin)
{
	InOutPin->bIsConstant = InPin->bIsConstant;
	InOutPin->Direction = InPin->Direction;
	InOutPin->CPPType = InPin->CPPType;
	InOutPin->ScriptStructPath = InPin->ScriptStructPath;
	InOutPin->ScriptStruct = InPin->ScriptStruct;
	InOutPin->DefaultValue = InPin->DefaultValue;
}

bool URigVMController::ShouldStructBeUnfolded(const UStruct* Struct)
{
	if (Struct == nullptr)
	{
		return false;
	}
	if (Struct->IsChildOf(UClass::StaticClass()))
	{
		return false;
	}
	if(Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
	{
		return false;
	}
	return true;
}

bool URigVMController::ShouldPinBeUnfolded(URigVMPin* InPin)
{
	if (InPin->IsStruct())
	{
		return ShouldStructBeUnfolded(InPin->GetScriptStruct());
	}
	else if (InPin->IsArray())
	{
		return InPin->GetDirection() == ERigVMPinDirection::Input ||
			InPin->GetDirection() == ERigVMPinDirection::IO;
	}
	return false;
}

FProperty* URigVMController::FindPropertyForPin(const FString& InPinPath)
{
	if(!IsValidGraph())
	{
		return nullptr;
	}

	TArray<FString> Parts;
	if (!URigVMPin::SplitPinPath(InPinPath, Parts))
	{
		return nullptr;
	}

	URigVMPin* Pin = Graph->FindPin(InPinPath);
	if (Pin == nullptr)
	{
		ReportErrorf(TEXT("Cannot find pin '%s'."), *InPinPath);
		return nullptr;
	}

	URigVMNode* Node = Pin->GetNode();

	URigVMStructNode* StructNode = Cast<URigVMStructNode>(Node);
	if (StructNode)
	{
		int32 PartIndex = 1; // cut off the first one since it's the node

		UStruct* Struct = StructNode->ScriptStruct;
		FProperty* Property = Struct->FindPropertyByName(*Parts[PartIndex++]);

		while (PartIndex < Parts.Num() && Property != nullptr)
		{
			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				Property = ArrayProperty->Inner;
				PartIndex++;
				continue;
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				Struct = StructProperty->Struct;
				Property = Struct->FindPropertyByName(*Parts[PartIndex++]);
				continue;
			}

			break;
		}

		if (PartIndex == Parts.Num())
		{
			return Property;
		}
	}

	return nullptr;
}

void URigVMController::DetachLinksFromPinObjects()
{
	check(Graph);

	for (URigVMLink* Link : Graph->Links)
	{
		URigVMPin* SourcePin = Link->GetSourcePin();
		URigVMPin* TargetPin = Link->GetTargetPin();

		Link->SourcePinPath = SourcePin->GetPinPath();
		Link->TargetPinPath = TargetPin->GetPinPath();
		Link->SourcePin = nullptr;
		Link->TargetPin = nullptr;
	}
}

void URigVMController::ReattachLinksToPinObjects()
{
	check(Graph);

	// fix up the pin links based on the persisted data
	TArray<URigVMLink*> NewLinks;
	for (URigVMLink* Link : Graph->Links)
	{
		URigVMPin* SourcePin = Link->GetSourcePin();
		URigVMPin* TargetPin = Link->GetTargetPin();
		if (SourcePin == nullptr)
		{
			if (TargetPin != nullptr)
			{
				TargetPin->Links.Remove(Link);
			}
			continue;
		}
		if (TargetPin == nullptr)
		{
			if (SourcePin != nullptr)
			{
				SourcePin->Links.Remove(Link);
			}
			continue;
		}

		SourcePin->Links.AddUnique(Link);
		TargetPin->Links.AddUnique(Link);
		NewLinks.Add(Link);
	}
	Graph->Links = NewLinks;
}

#if WITH_EDITOR

void URigVMController::RepopulatePinsOnNode(URigVMNode* InNode)
{
	if (InNode == nullptr)
	{
		ReportError(TEXT("InNode is nullptr."));
		return;
	}

	check(Graph);

	TMap<FName, FString> DefaultValues;
	for (URigVMPin* Pin : InNode->Pins)
	{
		FString DefaultValue = Pin->GetDefaultValue();
		if (!DefaultValue.IsEmpty())
		{
			DefaultValues.Add(Pin->GetFName(), DefaultValue);
		}
	}

	if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(InNode))
	{
		StructNode->Pins.Reset();

		UScriptStruct* ScriptStruct = StructNode->GetScriptStruct();

		FString NodeColorMetadata;
		ScriptStruct->GetStringMetaDataHierarchical(*URigVMNode::NodeColorName, &NodeColorMetadata);
		if(!NodeColorMetadata.IsEmpty())
		{
			StructNode->NodeColor = GetColorFromMetadata(NodeColorMetadata);
		}

		TArray<uint8> TempBuffer;
		TempBuffer.AddUninitialized(ScriptStruct->GetStructureSize());
		ScriptStruct->InitializeDefaultValue(TempBuffer.GetData());

		FString ExportedDefaultValue;
		ScriptStruct->ExportText(ExportedDefaultValue, TempBuffer.GetData(), nullptr, nullptr, PPF_None, nullptr);
		ScriptStruct->DestroyStruct(TempBuffer.GetData());
		AddPinsForStruct(ScriptStruct, StructNode, nullptr, ERigVMPinDirection::Invalid, ExportedDefaultValue);
	}
	else
	{
		return;
	}

	for (URigVMPin* Pin : InNode->Pins)
	{
		FString* DefaultValue = DefaultValues.Find(Pin->GetFName());
		if (DefaultValue)
		{
			SetPinDefaultValue(Pin, *DefaultValue, true, false, false);
		}
	}
}

#endif

FLinearColor URigVMController::GetColorFromMetadata(const FString& InMetadata)
{
	FLinearColor Color = FLinearColor::Black;

	FString Metadata = InMetadata;
	Metadata.TrimStartAndEnd();
	FString SplitString(TEXT(" "));
	FString Red, Green, Blue, GreenAndBlue;
	if (Metadata.Split(SplitString, &Red, &GreenAndBlue))
	{
		Red.TrimEnd();
		GreenAndBlue.TrimStart();
		if (GreenAndBlue.Split(SplitString, &Green, &Blue))
		{
			Green.TrimEnd();
			Blue.TrimStart();

			float RedValue = FCString::Atof(*Red);
			float GreenValue = FCString::Atof(*Green);
			float BlueValue = FCString::Atof(*Blue);
			Color = FLinearColor(RedValue, GreenValue, BlueValue);
		}
	}

	return Color;
}

void URigVMController::ReportWarning(const FString& InMessage)
{
	if(!bReportWarningsAndErrors)
	{
		return;
	}
	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Warning, *InMessage, *FString());
}

void URigVMController::ReportError(const FString& InMessage)
{
	if(!bReportWarningsAndErrors)
	{
		return;
	}
	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Error, *InMessage, *FString());
}
