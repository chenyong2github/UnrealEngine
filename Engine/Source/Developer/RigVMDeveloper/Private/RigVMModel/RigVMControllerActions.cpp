// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMControllerActions.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "Misc/ITransaction.h"
#endif

FRigVMActionWrapper::FRigVMActionWrapper(const FRigVMActionKey& Key)
{
	ScriptStruct = FindObjectChecked<UScriptStruct>(ANY_PACKAGE, *Key.ScriptStructPath);
	Data.SetNumUninitialized(ScriptStruct->GetStructureSize());
	ScriptStruct->InitializeStruct(Data.GetData(), 1);
	ScriptStruct->ImportText(*Key.ExportedText, Data.GetData(), nullptr, PPF_None, nullptr, ScriptStruct->GetName());
}
FRigVMActionWrapper::~FRigVMActionWrapper()
{
	if (Data.Num() > 0 && ScriptStruct != nullptr)
	{
		ScriptStruct->DestroyStruct(Data.GetData(), 1);
	}
}

FRigVMBaseAction* FRigVMActionWrapper::GetAction()
{
	return (FRigVMBaseAction*)Data.GetData();
}

FString FRigVMActionWrapper::ExportText()
{
	FString ExportedText;
	if (Data.Num() > 0 && ScriptStruct != nullptr)
	{
		ScriptStruct->ExportText(ExportedText, GetAction(), nullptr, nullptr, PPF_None, nullptr);
	}
	return ExportedText;
}

bool URigVMActionStack::OpenUndoBracket(const FString& InTitle)
{
	FRigVMBaseAction* Action = new FRigVMBaseAction;
	Action->Title = InTitle;
	BracketActions.Add(Action);
	BeginAction(*Action);
	return true;
}

bool URigVMActionStack::CloseUndoBracket()
{
	ensure(BracketActions.Num() > 0);
	FRigVMBaseAction* Action = BracketActions.Pop();
	EndAction(*Action);
	delete(Action);
	return true;
}

bool URigVMActionStack::CancelUndoBracket()
{
	ensure(BracketActions.Num() > 0);
	FRigVMBaseAction* Action = BracketActions.Pop();
	CancelAction(*Action);
	delete(Action);
	return true;
}

bool URigVMActionStack::Undo(URigVMController* InController)
{
	check(InController)

	if (UndoActions.Num() == 0)
	{
		InController->ReportWarning(TEXT("Nothing to undo."));
		return false;
	}

	FRigVMActionKey KeyToUndo = UndoActions.Pop();
	ActionIndex--;
	FRigVMActionWrapper Wrapper(KeyToUndo);
	if (Wrapper.GetAction()->Undo(InController))
	{
		RedoActions.Add(KeyToUndo);
		return true;
	}
	return false;
}

bool URigVMActionStack::Redo(URigVMController* InController)
{
	check(InController)

	if (RedoActions.Num() == 0)
	{
		InController->ReportWarning(TEXT("Nothing to redo."));
		return false;
	}

	FRigVMActionKey KeyToRedo = RedoActions.Pop();
	ActionIndex++;
	FRigVMActionWrapper Wrapper(KeyToRedo);
	if (Wrapper.GetAction()->Redo(InController))
	{
		UndoActions.Add(KeyToRedo);
		return true;
	}
	return false;
}

#if WITH_EDITOR

void URigVMActionStack::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		URigVMController* Controller = Cast< URigVMController>(GetOuter());
		if (Controller == nullptr)
		{
			return;
		}

		int32 DesiredActionIndex = ActionIndex;
		ActionIndex = UndoActions.Num();

		if (DesiredActionIndex == ActionIndex)
		{
			return;
		}

		ModifiedEvent.Broadcast(ERigVMGraphNotifType::InteractionBracketOpened, nullptr, nullptr);

		while (DesiredActionIndex < ActionIndex)
		{
			if (UndoActions.Num() == 0)
			{
				break;
			}
			if (!Undo(Controller))
			{
				return;
			}
		}
		while (DesiredActionIndex > ActionIndex)
		{
			if (RedoActions.Num() == 0)
			{
				break;
			}
			if (!Redo(Controller))
			{
				return;
			}
		}

		ModifiedEvent.Broadcast(ERigVMGraphNotifType::InteractionBracketClosed, nullptr, nullptr);
	}
}

#endif


bool FRigVMBaseAction::Merge(const FRigVMBaseAction* Other)
{
	return SubActions.Num() == 0 && Other->SubActions.Num() == 0;
}

bool FRigVMBaseAction::Undo(URigVMController* InController)
{
	bool Result = true;
	for (int32 KeyIndex = SubActions.Num() - 1; KeyIndex >= 0; KeyIndex--)
	{
		FRigVMActionWrapper Wrapper(SubActions[KeyIndex]);
		if(!Wrapper.GetAction()->Undo(InController))
		{
			Result = false;
		}
	}
	return Result;
}

bool FRigVMBaseAction::Redo(URigVMController* InController)
{
	bool Result = true;
	for (int32 KeyIndex = 0; KeyIndex < SubActions.Num(); KeyIndex++)
	{
		FRigVMActionWrapper Wrapper(SubActions[KeyIndex]);
		if (!Wrapper.GetAction()->Redo(InController))
		{
			Result = false;
		}
	}
	return Result;
}

bool FRigVMInverseAction::Undo(URigVMController* InController)
{
	return FRigVMBaseAction::Redo(InController);
}

bool FRigVMInverseAction::Redo(URigVMController* InController)
{
	return FRigVMBaseAction::Undo(InController);
}

FRigVMAddStructNodeAction::FRigVMAddStructNodeAction()
	: ScriptStructPath()
	, MethodName(NAME_None)
	, Position(FVector2D::ZeroVector)
	, NodePath()
{
}

FRigVMAddStructNodeAction::FRigVMAddStructNodeAction(URigVMStructNode* InNode)
	: ScriptStructPath(InNode->GetScriptStruct()->GetPathName())
	, MethodName(InNode->GetMethodName())
	, Position(InNode->GetPosition())
	, NodePath(InNode->GetNodePath())
{
}

bool FRigVMAddStructNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddStructNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMStructNode* Node = InController->AddStructNodeFromStructPath(ScriptStructPath, MethodName, Position, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddVariableNodeAction::FRigVMAddVariableNodeAction()
	: VariableName(NAME_None)
	, CPPType()
	, CPPTypeObjectPath()
	, bIsGetter(false)
	, DefaultValue()
	, Position(FVector2D::ZeroVector)
	, NodePath()
{
}

FRigVMAddVariableNodeAction::FRigVMAddVariableNodeAction(URigVMVariableNode* InNode)
	: VariableName(InNode->GetVariableName())
	, CPPType(InNode->GetCPPType())
	, CPPTypeObjectPath()
	, bIsGetter(InNode->IsGetter())
	, DefaultValue(InNode->GetDefaultValue())
	, Position(InNode->GetPosition())
	, NodePath(InNode->GetNodePath())
{
	if (InNode->GetCPPTypeObject())
	{
		CPPTypeObjectPath = InNode->GetCPPTypeObject()->GetPathName();
	}
}

bool FRigVMAddVariableNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddVariableNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMVariableNode* Node = InController->AddVariableNodeFromObjectPath(VariableName, CPPType, CPPTypeObjectPath, bIsGetter, DefaultValue, Position, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddParameterNodeAction::FRigVMAddParameterNodeAction()
	: ParameterName(NAME_None)
	, CPPType()
	, CPPTypeObjectPath()
	, bIsInput(false)
	, DefaultValue()
	, Position(FVector2D::ZeroVector)
	, NodePath()
{
}

FRigVMAddParameterNodeAction::FRigVMAddParameterNodeAction(URigVMParameterNode* InNode)
	: ParameterName(InNode->GetParameterName())
	, CPPType(InNode->GetCPPType())
	, CPPTypeObjectPath()
	, bIsInput(InNode->IsInput())
	, DefaultValue(InNode->GetDefaultValue())
	, Position(InNode->GetPosition())
	, NodePath(InNode->GetNodePath())
{
	if (InNode->GetCPPTypeObject())
	{
		CPPTypeObjectPath = InNode->GetCPPTypeObject()->GetPathName();
	}
}

bool FRigVMAddParameterNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddParameterNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMParameterNode* Node = InController->AddParameterNodeFromObjectPath(ParameterName, CPPType, CPPTypeObjectPath, bIsInput, DefaultValue, Position, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddCommentNodeAction::FRigVMAddCommentNodeAction()
	: CommentText()
	, Position(FVector2D::ZeroVector)
	, Size(FVector2D::ZeroVector)
	, Color(FLinearColor::Black)
	, NodePath()
{
}

FRigVMAddCommentNodeAction::FRigVMAddCommentNodeAction(URigVMCommentNode* InNode)
	: CommentText(InNode->GetCommentText())
	, Position(InNode->GetPosition())
	, Size(InNode->GetSize())
	, Color(InNode->GetNodeColor())
	, NodePath(InNode->GetNodePath())
{
}

bool FRigVMAddCommentNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddCommentNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMCommentNode* Node = InController->AddCommentNode(CommentText, Position, Size, Color, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddRerouteNodeAction::FRigVMAddRerouteNodeAction()
	: bShowAsFullNode(false)
	, CPPType()
	, CPPTypeObjectPath(NAME_None)
	, DefaultValue()
	, bIsConstant(false)
	, CustomWidgetName(NAME_None)
	, Position(FVector2D::ZeroVector)
	, NodePath()
{
}

FRigVMAddRerouteNodeAction::FRigVMAddRerouteNodeAction(URigVMRerouteNode* InNode)
	: bShowAsFullNode(InNode->GetShowsAsFullNode())
	, CPPType(InNode->GetPins()[0]->GetCPPType())
	, CPPTypeObjectPath(NAME_None)
	, DefaultValue(InNode->GetPins()[0]->GetDefaultValue())
	, bIsConstant(InNode->GetPins()[0]->IsDefinedAsConstant())
	, CustomWidgetName(InNode->GetPins()[0]->GetCustomWidgetName())
	, Position(InNode->GetPosition())
	, NodePath(InNode->GetNodePath())
{
	if (InNode->GetPins()[0]->GetCPPTypeObject())
	{
		CPPTypeObjectPath = *InNode->GetPins()[0]->GetCPPTypeObject()->GetPathName();
	}
}

bool FRigVMAddRerouteNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddRerouteNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMRerouteNode* Node = InController->AddFreeRerouteNode(bShowAsFullNode, CPPType, CPPTypeObjectPath, bIsConstant, CustomWidgetName, DefaultValue, Position, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddBranchNodeAction::FRigVMAddBranchNodeAction()
	: Position(FVector2D::ZeroVector)
	, NodePath()
{
}

FRigVMAddBranchNodeAction::FRigVMAddBranchNodeAction(URigVMBranchNode* InNode)
	: Position(InNode->GetPosition())
	, NodePath(InNode->GetNodePath())
{
}

bool FRigVMAddBranchNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddBranchNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMBranchNode* Node = InController->AddBranchNode(Position, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddIfNodeAction::FRigVMAddIfNodeAction()
	: CPPType()
	, CPPTypeObjectPath(NAME_None)
	, Position(FVector2D::ZeroVector)
	, NodePath()
{
}

FRigVMAddIfNodeAction::FRigVMAddIfNodeAction(URigVMIfNode* InNode)
	: CPPType()
	, CPPTypeObjectPath(NAME_None)
	, Position(InNode->GetPosition())
	, NodePath(InNode->GetNodePath())
{
	if(URigVMPin* ResultPin = InNode->FindPin(URigVMIfNode::ResultName))
	{
		CPPType = ResultPin->GetCPPType();
		if (ResultPin->GetCPPTypeObject())
		{
			CPPTypeObjectPath = *ResultPin->GetCPPTypeObject()->GetPathName();
		}
	}
}

bool FRigVMAddIfNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddIfNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMIfNode* Node = InController->AddIfNode(CPPType, CPPTypeObjectPath, Position, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddSelectNodeAction::FRigVMAddSelectNodeAction()
	: CPPType()
	, CPPTypeObjectPath(NAME_None)
	, Position(FVector2D::ZeroVector)
	, NodePath()
{
}

FRigVMAddSelectNodeAction::FRigVMAddSelectNodeAction(URigVMSelectNode* InNode)
	: CPPType()
	, CPPTypeObjectPath(NAME_None)
	, Position(InNode->GetPosition())
	, NodePath(InNode->GetNodePath())
{
	if(URigVMPin* ResultPin = InNode->FindPin(URigVMSelectNode::ResultName))
	{
		CPPType = ResultPin->GetCPPType();
		if (ResultPin->GetCPPTypeObject())
		{
			CPPTypeObjectPath = *ResultPin->GetCPPTypeObject()->GetPathName();
		}
	}
}

bool FRigVMAddSelectNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddSelectNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMSelectNode* Node = InController->AddSelectNode(CPPType, CPPTypeObjectPath, Position, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddEnumNodeAction::FRigVMAddEnumNodeAction()
	: CPPTypeObjectPath(NAME_None)
	, Position(FVector2D::ZeroVector)
	, NodePath()
{
}

FRigVMAddEnumNodeAction::FRigVMAddEnumNodeAction(URigVMEnumNode* InNode)
	: CPPTypeObjectPath(NAME_None)
	, Position(InNode->GetPosition())
	, NodePath(InNode->GetNodePath())
{
	CPPTypeObjectPath = *InNode->GetCPPTypeObject()->GetPathName();
}

bool FRigVMAddEnumNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddEnumNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMEnumNode* Node = InController->AddEnumNode(CPPTypeObjectPath, Position, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddPrototypeNodeAction::FRigVMAddPrototypeNodeAction()
	: PrototypeNotation(NAME_None)
	, Position(FVector2D::ZeroVector)
	, NodePath()
{
}

FRigVMAddPrototypeNodeAction::FRigVMAddPrototypeNodeAction(URigVMPrototypeNode* InNode)
	: PrototypeNotation(InNode->GetNotation())
	, Position(InNode->GetPosition())
	, NodePath(InNode->GetNodePath())
{
}

bool FRigVMAddPrototypeNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false);
}

bool FRigVMAddPrototypeNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMPrototypeNode* Node = InController->AddPrototypeNode(PrototypeNotation, Position, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMAddInjectedNodeAction::FRigVMAddInjectedNodeAction()
	: PinPath()
	, bAsInput(false)
	, ScriptStructPath()
	, MethodName(NAME_None)
	, InputPinName(NAME_None)
	, OutputPinName(NAME_None)
	, NodePath()
{
}

FRigVMAddInjectedNodeAction::FRigVMAddInjectedNodeAction(URigVMInjectionInfo* InInjectionInfo)
	: PinPath(InInjectionInfo->GetPin()->GetPinPath())
	, bAsInput(InInjectionInfo->bInjectedAsInput)
	, ScriptStructPath(InInjectionInfo->StructNode->GetScriptStruct()->GetPathName())
	, MethodName(InInjectionInfo->StructNode->GetMethodName())
	, InputPinName(InInjectionInfo->InputPin->GetFName())
	, OutputPinName(InInjectionInfo->OutputPin->GetFName())
	, NodePath(InInjectionInfo->StructNode->GetName())
{
}

bool FRigVMAddInjectedNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodeByName(*NodePath, false, true);
}

bool FRigVMAddInjectedNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMInjectionInfo* InjectionInfo = InController->AddInjectedNodeFromStructPath(PinPath, bAsInput, ScriptStructPath, MethodName, InputPinName, OutputPinName, NodePath, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMRemoveNodeAction::FRigVMRemoveNodeAction(URigVMNode* InNode)
{
	FRigVMInverseAction InverseAction;

	if (URigVMInjectionInfo* InjectionInfo = InNode->GetInjectionInfo())
	{
		InverseAction.AddAction(FRigVMAddInjectedNodeAction(InjectionInfo));
		for (URigVMPin* Pin : InNode->GetPins())
		{
			if (Pin->GetDirection() == ERigVMPinDirection::Input ||
				Pin->GetDirection() == ERigVMPinDirection::Visible)
			{
				InverseAction.AddAction(FRigVMSetPinDefaultValueAction(Pin, Pin->GetDefaultValue()));
			}
		}
	}
	else if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddStructNodeAction(StructNode));
		for (URigVMPin* Pin : StructNode->GetPins())
		{
			if (Pin->GetDirection() == ERigVMPinDirection::Input ||
				Pin->GetDirection() == ERigVMPinDirection::Visible)
			{
				InverseAction.AddAction(FRigVMSetPinDefaultValueAction(Pin, Pin->GetDefaultValue()));
			}
		}
	}
	else if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddVariableNodeAction(VariableNode));
		URigVMPin* ValuePin = VariableNode->FindPin(TEXT("Value"));
		InverseAction.AddAction(FRigVMSetPinDefaultValueAction(ValuePin, ValuePin->GetDefaultValue()));
	}
	else if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddParameterNodeAction(ParameterNode));
		URigVMPin* ValuePin = ParameterNode->FindPin(TEXT("Value"));
		InverseAction.AddAction(FRigVMSetPinDefaultValueAction(ValuePin, ValuePin->GetDefaultValue()));
	}
	else if (URigVMCommentNode* CommentNode = Cast<URigVMCommentNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddCommentNodeAction(CommentNode));
	}
	else if (URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddRerouteNodeAction(RerouteNode));
	}
	else if (URigVMBranchNode* BranchNode = Cast<URigVMBranchNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddBranchNodeAction(BranchNode));
	}
	else if (URigVMIfNode* IfNode = Cast<URigVMIfNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddIfNodeAction(IfNode));
	}
	else if (URigVMSelectNode* SelectNode = Cast<URigVMSelectNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddSelectNodeAction(SelectNode));
	}
	else if (URigVMPrototypeNode* PrototypeNode = Cast<URigVMPrototypeNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddPrototypeNodeAction(PrototypeNode));
	}
	else if (URigVMEnumNode* EnumNode = Cast<URigVMEnumNode>(InNode))
	{
		InverseAction.AddAction(FRigVMAddEnumNodeAction(EnumNode));
	}
	else
	{
		ensure(false);
	}

	for (URigVMPin* Pin : InNode->GetPins())
	{
		if(Pin->IsExpanded())
		{
			FRigVMSetPinExpansionAction ExpansionAction(Pin, true);
			ExpansionAction.OldIsExpanded = false;
			InverseAction.AddAction(ExpansionAction);
		}
	}

	InverseActionKey.Set<FRigVMInverseAction>(InverseAction);
}

bool FRigVMRemoveNodeAction::Undo(URigVMController* InController)
{
	FRigVMActionWrapper InverseWrapper(InverseActionKey);
	if (!InverseWrapper.GetAction()->Undo(InController))
	{
		return false;
	}
	return FRigVMBaseAction::Undo(InController);
}

bool FRigVMRemoveNodeAction::Redo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Redo(InController))
	{
		return false;
	}
	FRigVMActionWrapper InverseWrapper(InverseActionKey);
	return InverseWrapper.GetAction()->Redo(InController);
}

FRigVMSetNodeSelectionAction::FRigVMSetNodeSelectionAction()
{

}

FRigVMSetNodeSelectionAction::FRigVMSetNodeSelectionAction(URigVMGraph* InGraph, TArray<FName> InNewSelection)
{
	OldSelection = InGraph->GetSelectNodes();
	NewSelection = InNewSelection;
}

bool FRigVMSetNodeSelectionAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetNodeSelection(OldSelection, false);
}

bool FRigVMSetNodeSelectionAction::Redo(URigVMController* InController)
{
	if(!InController->SetNodeSelection(NewSelection, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetNodePositionAction::FRigVMSetNodePositionAction(URigVMNode* InNode, const FVector2D& InNewPosition)
: NodePath(InNode->GetNodePath())
, OldPosition(InNode->GetPosition())
, NewPosition(InNewPosition)
{
}

bool FRigVMSetNodePositionAction::Merge(const FRigVMBaseAction* Other)
{
	if (!FRigVMBaseAction::Merge(Other))
	{
		return false;
	}

	const FRigVMSetNodePositionAction* Action = (const FRigVMSetNodePositionAction*)Other;
	if (NodePath != Action->NodePath)
	{
		return false;
	}

	NewPosition = Action->NewPosition;
	return true;
}

bool FRigVMSetNodePositionAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetNodePositionByName(*NodePath, OldPosition, false);
}

bool FRigVMSetNodePositionAction::Redo(URigVMController* InController)
{
	if(!InController->SetNodePositionByName(*NodePath, NewPosition, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetNodeSizeAction::FRigVMSetNodeSizeAction(URigVMNode* InNode, const FVector2D& InNewSize)
: NodePath(InNode->GetNodePath())
, OldSize(InNode->GetSize())
, NewSize(InNewSize)
{
}

bool FRigVMSetNodeSizeAction::Merge(const FRigVMBaseAction* Other)
{
	if (!FRigVMBaseAction::Merge(Other))
	{
		return false;
	}

	const FRigVMSetNodeSizeAction* Action = (const FRigVMSetNodeSizeAction*)Other;
	if (NodePath != Action->NodePath)
	{
		return false;
	}

	NewSize = Action->NewSize;
	return true;
}

bool FRigVMSetNodeSizeAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetNodeSizeByName(*NodePath, OldSize, false);
}

bool FRigVMSetNodeSizeAction::Redo(URigVMController* InController)
{
	if(!InController->SetNodeSizeByName(*NodePath, NewSize, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetNodeColorAction::FRigVMSetNodeColorAction(URigVMNode* InNode, const FLinearColor& InNewColor)
: NodePath(InNode->GetNodePath())
, OldColor(InNode->GetNodeColor())
, NewColor(InNewColor)
{
}

bool FRigVMSetNodeColorAction::Merge(const FRigVMBaseAction* Other)
{
	if (!FRigVMBaseAction::Merge(Other))
	{
		return false;
	}

	const FRigVMSetNodeColorAction* Action = (const FRigVMSetNodeColorAction*)Other;
	if (NodePath != Action->NodePath)
	{
		return false;
	}

	NewColor = Action->NewColor;
	return true;
}

bool FRigVMSetNodeColorAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetNodeColorByName(*NodePath, OldColor, false);
}

bool FRigVMSetNodeColorAction::Redo(URigVMController* InController)
{
	if(!InController->SetNodeColorByName(*NodePath, NewColor, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetCommentTextAction::FRigVMSetCommentTextAction(URigVMCommentNode* InNode, const FString& InNewText)
: NodePath(InNode->GetNodePath())
, OldText(InNode->GetCommentText())
, NewText(InNewText)
{
}

bool FRigVMSetCommentTextAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetCommentTextByName(*NodePath, OldText, false);
}

bool FRigVMSetCommentTextAction::Redo(URigVMController* InController)
{
	if(!InController->SetCommentTextByName(*NodePath, NewText, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetRerouteCompactnessAction::FRigVMSetRerouteCompactnessAction()
	: NodePath()
	, OldShowAsFullNode(false)
	, NewShowAsFullNode(false)
{

}
FRigVMSetRerouteCompactnessAction::FRigVMSetRerouteCompactnessAction(URigVMRerouteNode* InNode, bool InShowAsFullNode)
	: NodePath(InNode->GetNodePath())
	, OldShowAsFullNode(InNode->GetShowsAsFullNode())
	, NewShowAsFullNode(InShowAsFullNode)
{
}

bool FRigVMSetRerouteCompactnessAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetRerouteCompactnessByName(*NodePath, OldShowAsFullNode, false);
}

bool FRigVMSetRerouteCompactnessAction::Redo(URigVMController* InController)
{
	if(!InController->SetRerouteCompactnessByName(*NodePath, NewShowAsFullNode, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMRenameVariableAction::FRigVMRenameVariableAction(const FName& InOldVariableName, const FName& InNewVariableName)
: OldVariableName(InOldVariableName.ToString())
, NewVariableName(InNewVariableName.ToString())
{
}

bool FRigVMRenameVariableAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RenameVariable(*NewVariableName, *OldVariableName, false);
}

bool FRigVMRenameVariableAction::Redo(URigVMController* InController)
{
	if(!InController->RenameVariable(*OldVariableName, *NewVariableName, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMRenameParameterAction::FRigVMRenameParameterAction(const FName& InOldParameterName, const FName& InNewParameterName)
: OldParameterName(InOldParameterName.ToString())
, NewParameterName(InNewParameterName.ToString())
{
}

bool FRigVMRenameParameterAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RenameParameter(*NewParameterName, *OldParameterName, false);
}

bool FRigVMRenameParameterAction::Redo(URigVMController* InController)
{
	if(!InController->RenameParameter(*OldParameterName, *NewParameterName, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetPinExpansionAction::FRigVMSetPinExpansionAction(URigVMPin* InPin, bool bNewIsExpanded)
: PinPath(InPin->GetPinPath())
, OldIsExpanded(InPin->IsExpanded())
, NewIsExpanded(bNewIsExpanded)
{
}

bool FRigVMSetPinExpansionAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetPinExpansion(PinPath, OldIsExpanded, false);
}

bool FRigVMSetPinExpansionAction::Redo(URigVMController* InController)
{
	if(!InController->SetPinExpansion(PinPath, NewIsExpanded, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetPinWatchAction::FRigVMSetPinWatchAction(URigVMPin* InPin, bool bNewIsWatched)
: PinPath(InPin->GetPinPath())
, OldIsWatched(InPin->RequiresWatch())
, NewIsWatched(bNewIsWatched)
{
}

bool FRigVMSetPinWatchAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetPinIsWatched(PinPath, OldIsWatched, false);
}

bool FRigVMSetPinWatchAction::Redo(URigVMController* InController)
{
	if(!InController->SetPinIsWatched(PinPath, NewIsWatched, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetPinDefaultValueAction::FRigVMSetPinDefaultValueAction(URigVMPin* InPin, const FString& InNewDefaultValue)
: PinPath(InPin->GetPinPath())
, OldDefaultValue(InPin->GetDefaultValue())
, NewDefaultValue(InNewDefaultValue)
{
}

bool FRigVMSetPinDefaultValueAction::Merge(const FRigVMBaseAction* Other)
{
	if (!FRigVMBaseAction::Merge(Other))
	{
		return false;
	}

	const FRigVMSetPinDefaultValueAction* Action = (const FRigVMSetPinDefaultValueAction*)Other;
	if (PinPath != Action->PinPath)
	{
		return false;
	}

	NewDefaultValue = Action->NewDefaultValue;
	return true;
}

bool FRigVMSetPinDefaultValueAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	if (OldDefaultValue.IsEmpty())
	{
		return true;
	}
	return InController->SetPinDefaultValue(PinPath, OldDefaultValue, true, false);
}

bool FRigVMSetPinDefaultValueAction::Redo(URigVMController* InController)
{
	if (!NewDefaultValue.IsEmpty())
	{
		if (!InController->SetPinDefaultValue(PinPath, NewDefaultValue, true, false))
		{
			return false;
		}
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMInsertArrayPinAction::FRigVMInsertArrayPinAction(URigVMPin* InArrayPin, int32 InIndex, const FString& InNewDefaultValue)
: ArrayPinPath(InArrayPin->GetPinPath())
, Index(InIndex)
, NewDefaultValue(InNewDefaultValue)
{
}

bool FRigVMInsertArrayPinAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveArrayPin(FString::Printf(TEXT("%s.%d"), *ArrayPinPath, Index), false);
}

bool FRigVMInsertArrayPinAction::Redo(URigVMController* InController)
{
	if(InController->InsertArrayPin(ArrayPinPath, Index, NewDefaultValue, false).IsEmpty())
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMRemoveArrayPinAction::FRigVMRemoveArrayPinAction(URigVMPin* InArrayElementPin)
: ArrayPinPath(InArrayElementPin->GetParentPin()->GetPinPath())
, Index(InArrayElementPin->GetPinIndex())
, DefaultValue(InArrayElementPin->GetDefaultValue())
{
}

bool FRigVMRemoveArrayPinAction::Undo(URigVMController* InController)
{
	if (InController->InsertArrayPin(*ArrayPinPath, Index, DefaultValue, false).IsEmpty())
	{
		return false;
	}
	return FRigVMBaseAction::Undo(InController);
}

bool FRigVMRemoveArrayPinAction::Redo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Redo(InController))
	{
		return false;
	}
	return InController->RemoveArrayPin(FString::Printf(TEXT("%s.%d"), *ArrayPinPath, Index), false);
}

FRigVMAddLinkAction::FRigVMAddLinkAction(URigVMPin* InOutputPin, URigVMPin* InInputPin)
: OutputPinPath(InOutputPin->GetPinPath())
, InputPinPath(InInputPin->GetPinPath())
{
}

bool FRigVMAddLinkAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->BreakLink(OutputPinPath, InputPinPath, false);
}

bool FRigVMAddLinkAction::Redo(URigVMController* InController)
{
	if(!InController->AddLink(OutputPinPath, InputPinPath, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMBreakLinkAction::FRigVMBreakLinkAction(URigVMPin* InOutputPin, URigVMPin* InInputPin)
: OutputPinPath(InOutputPin->GetPinPath())
, InputPinPath(InInputPin->GetPinPath())
{
}

bool FRigVMBreakLinkAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->AddLink(OutputPinPath, InputPinPath, false);
}

bool FRigVMBreakLinkAction::Redo(URigVMController* InController)
{
	if(!InController->BreakLink(OutputPinPath, InputPinPath, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMChangePinTypeAction::FRigVMChangePinTypeAction(URigVMPin* InPin, const FString& InCppType, const FName& InCppTypeObjectPath)
: PinPath(InPin->GetPinPath())
, OldCPPType(InPin->GetCPPType())
, OldCPPTypeObjectPath(NAME_None)
, NewCPPType(InCppType)
, NewCPPTypeObjectPath(InCppTypeObjectPath)
{
	if (UObject* CPPTypeObject = InPin->GetCPPTypeObject())
	{
		OldCPPTypeObjectPath = *CPPTypeObject->GetPathName();
	}
}

bool FRigVMChangePinTypeAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->ChangePinType(PinPath, OldCPPType, OldCPPTypeObjectPath, false);
}

bool FRigVMChangePinTypeAction::Redo(URigVMController* InController)
{
	if(!InController->ChangePinType(PinPath, NewCPPType, NewCPPTypeObjectPath, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}
