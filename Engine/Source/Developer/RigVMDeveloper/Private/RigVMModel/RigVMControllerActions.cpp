// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMControllerActions.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Algo/Transform.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMControllerActions)

#if WITH_EDITOR
#include "Misc/TransactionObjectEvent.h"
#endif

UScriptStruct* FRigVMActionKey::GetScriptStruct() const
{
	return FindObjectChecked<UScriptStruct>(nullptr, *ScriptStructPath);		
}

TSharedPtr<FStructOnScope> FRigVMActionKey::GetAction() const
{
	UScriptStruct* ScriptStruct = GetScriptStruct();
	TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(ScriptStruct));
	ScriptStruct->ImportText(*ExportedText, StructOnScope->GetStructMemory(), nullptr, PPF_None, nullptr, ScriptStruct->GetName());
	return StructOnScope;
}

FRigVMActionWrapper::FRigVMActionWrapper(const FRigVMActionKey& Key)
{
	StructOnScope = Key.GetAction();
}
FRigVMActionWrapper::~FRigVMActionWrapper()
{
}

const UScriptStruct* FRigVMActionWrapper::GetScriptStruct() const
{
	return CastChecked<UScriptStruct>(StructOnScope->GetStruct());
}

FRigVMBaseAction* FRigVMActionWrapper::GetAction() const
{
	return (FRigVMBaseAction*)StructOnScope->GetStructMemory();
}

FString FRigVMActionWrapper::ExportText() const
{
	FString ExportedText;
	if (StructOnScope.IsValid() && StructOnScope->IsValid())
	{
		const UScriptStruct* ScriptStruct = GetScriptStruct();
		FStructOnScope DefaultScope(ScriptStruct);
		ScriptStruct->ExportText(ExportedText, GetAction(), DefaultScope.GetStructMemory(), nullptr, PPF_None, nullptr);
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

bool URigVMActionStack::CloseUndoBracket(URigVMController* InController)
{
	ensure(BracketActions.Num() > 0);
	if(BracketActions.Last()->IsEmpty())
	{
		return CancelUndoBracket(InController);
	}
	FRigVMBaseAction* Action = BracketActions.Pop();
	EndAction(*Action);
	delete(Action);
	return true;
}

bool URigVMActionStack::CancelUndoBracket(URigVMController* InController)
{
	ensure(BracketActions.Num() > 0);
	FRigVMBaseAction* Action = BracketActions.Pop();
	CancelAction(*Action, InController);
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
	ActionIndex = UndoActions.Num();
	
	FRigVMActionWrapper Wrapper(KeyToUndo);

#if RIGVM_ACTIONSTACK_VERBOSE_LOG		
	TGuardValue<int32> TabDepthGuard(LogActionDepth, 0);
	LogAction(Wrapper.GetScriptStruct(), *Wrapper.GetAction(), FRigVMBaseAction::UndoPrefix);
#endif
	
	if (Wrapper.GetAction()->Undo(InController))
	{
		RedoActions.Add(KeyToUndo);
		return true;
	}

	InController->ReportAndNotifyErrorf(TEXT("Error while undoing action %s."), *Wrapper.GetAction()->Title);
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

	FRigVMActionWrapper Wrapper(KeyToRedo);

#if RIGVM_ACTIONSTACK_VERBOSE_LOG		
	TGuardValue<int32> TabDepthGuard(LogActionDepth, 0);
	LogAction(Wrapper.GetScriptStruct(), *Wrapper.GetAction(), FRigVMBaseAction::RedoPrefix);
#endif
	
	if (Wrapper.GetAction()->Redo(InController))
	{
		UndoActions.Add(KeyToRedo);
		ActionIndex = UndoActions.Num();
		return true;
	}

	InController->ReportAndNotifyErrorf(TEXT("Error while undoing action %s."), *Wrapper.GetAction()->Title);
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
				ModifiedEvent.Broadcast(ERigVMGraphNotifType::InteractionBracketCanceled, nullptr, nullptr);
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
				ModifiedEvent.Broadcast(ERigVMGraphNotifType::InteractionBracketCanceled, nullptr, nullptr);
				return;
			}
		}

		ModifiedEvent.Broadcast(ERigVMGraphNotifType::InteractionBracketClosed, nullptr, nullptr);
	}
}

#if RIGVM_ACTIONSTACK_VERBOSE_LOG		

void URigVMActionStack::LogAction(const UScriptStruct* InActionStruct, const FRigVMBaseAction& InAction, const FString& InPrefix)
{
	if(bSuspendLogActions)
	{
		return;
	}
	
	if(URigVMController* Controller = GetTypedOuter<URigVMController>())
	{
		static TArray<FString> TabPrefix = {TEXT(""), TEXT("  ")};

		while(TabPrefix.Num() <= LogActionDepth)
		{
			TabPrefix.Add(TabPrefix.Last() + TabPrefix[1]);
		}
		
		const FString ActionContent = FRigVMStruct::ExportToFullyQualifiedText(InActionStruct, (const uint8*)&InAction);
		Controller->ReportInfof(TEXT("%s%s: %s (%s) '%s"), *TabPrefix[LogActionDepth], *InPrefix, *InAction.GetTitle(), *InActionStruct->GetStructCPPName(), *ActionContent);
	}

	if(InPrefix == FRigVMBaseAction::AddActionPrefix)
	{
		TGuardValue<int32> ActionDepthGuard(LogActionDepth, LogActionDepth + 1);
		for(const FRigVMActionKey& SubAction : InAction.SubActions)
		{
			const FRigVMActionWrapper Wrapper(SubAction);
			LogAction(Wrapper.GetScriptStruct(), *Wrapper.GetAction(), InPrefix);
		}
	}
}

#endif

#endif


bool FRigVMBaseAction::Merge(const FRigVMBaseAction* Other)
{
	return SubActions.Num() == 0 && Other->SubActions.Num() == 0;
}

bool FRigVMBaseAction::MakesObsolete(const FRigVMBaseAction* Other) const
{
	return false;
}

bool FRigVMBaseAction::Undo(URigVMController* InController)
{
#if RIGVM_ACTIONSTACK_VERBOSE_LOG		
	URigVMActionStack* Stack = InController->ActionStack;
	check(Stack);
	TGuardValue<int32> TabDepthGuard(Stack->LogActionDepth, Stack->LogActionDepth + 1);
#endif
	TGuardValue<bool> TransactionGuard(InController->bIsTransacting, true);

	bool Result = true;
	for (int32 KeyIndex = SubActions.Num() - 1; KeyIndex >= 0; KeyIndex--)
	{
		FRigVMActionWrapper Wrapper(SubActions[KeyIndex]);
#if RIGVM_ACTIONSTACK_VERBOSE_LOG		
		Wrapper.GetAction()->LogAction(InController, UndoPrefix);
#endif
		if(!Wrapper.GetAction()->Undo(InController))
		{
			InController->ReportAndNotifyErrorf(TEXT("Error while undoing action '%s'."), *Wrapper.GetAction()->Title);
			Result = false;
		}
	}
	return Result;
}

bool FRigVMBaseAction::Redo(URigVMController* InController)
{
#if RIGVM_ACTIONSTACK_VERBOSE_LOG		
	URigVMActionStack* Stack = InController->ActionStack;
	check(Stack);
	TGuardValue<int32> TabDepthGuard(Stack->LogActionDepth, Stack->LogActionDepth + 1);
#endif
	TGuardValue<bool> TransactionGuard(InController->bIsTransacting, true);

	bool Result = true;
	for (int32 KeyIndex = 0; KeyIndex < SubActions.Num(); KeyIndex++)
	{
		FRigVMActionWrapper Wrapper(SubActions[KeyIndex]);
#if RIGVM_ACTIONSTACK_VERBOSE_LOG		
		Wrapper.GetAction()->LogAction(InController, RedoPrefix);
#endif
		if (!Wrapper.GetAction()->Redo(InController))
		{
			InController->ReportAndNotifyErrorf(TEXT("Error while redoing action '%s'."), *Wrapper.GetAction()->Title);
			Result = false;
		}
	}
	return Result;
}

bool FRigVMBaseAction::StoreNode(const URigVMNode* InNode, URigVMController* InController, bool bIsPriorChange)
{
	check(InNode);
	check(InController);

	const FString& Content = InController->ExportNodesToText({InNode->GetFName()}, true);
	if(!Content.IsEmpty())
	{
		FRigVMActionNodeContent& StoredContent = ExportedNodes.FindOrAdd(InNode->GetFName());
		if(bIsPriorChange)
		{
			StoredContent.Old = Content;
		}
		else
		{
			StoredContent.New = Content;
		}
	}
	return !Content.IsEmpty();
}

bool FRigVMBaseAction::RestoreNode(const FName& InNodeName, URigVMController* InController, bool bIsUndoing)
{
	URigVMGraph* Graph = InController->GetGraph();
	check(Graph);

	FRigVMActionNodeContent& Content = ExportedNodes.FindChecked(InNodeName);

	//TArray<URigVMController::FLinkedPath> LinkedPaths;
	if(URigVMNode* ExistingNode = Graph->FindNodeByName(InNodeName))
	{
		//LinkedPaths = InController->GetLinkedPaths(ExistingNode);
		if(!InController->RemoveNode(ExistingNode, false, false))
		{
			return false;
		}
	}

	const FString& ContentToImport = bIsUndoing ? Content.Old : Content.New;
	const TArray<FName> ImportedNodeNames = InController->ImportNodesFromText(ContentToImport, false, false);
	if(ImportedNodeNames.Num() == 1)
	{
		if(!ImportedNodeNames[0].IsEqual(InNodeName, ENameCase::CaseSensitive))
		{
			return false;
		}
	}

	/*
	if(!LinkedPaths.IsEmpty())
	{
		if(!InController->RestoreLinkedPaths(LinkedPaths))
		{
			return false;
		}
	}
	*/
	
	return true;
}

#if RIGVM_ACTIONSTACK_VERBOSE_LOG

void FRigVMBaseAction::LogAction(URigVMController* InController, const FString& InPrefix) const
{
	check(InController);
	URigVMActionStack* Stack = InController->ActionStack;
	check(Stack);
	TGuardValue<int32> TabDepthGuard(Stack->LogActionDepth, Stack->LogActionDepth + 1);

	Stack->LogAction(GetScriptStruct(), *this, InPrefix);
}

#endif

FRigVMInjectNodeIntoPinAction::FRigVMInjectNodeIntoPinAction()
	: PinPath()
	, bAsInput(false)
	, InputPinName(NAME_None)
	, OutputPinName(NAME_None)
	, NodePath()
{
}

FRigVMInjectNodeIntoPinAction::FRigVMInjectNodeIntoPinAction(URigVMInjectionInfo* InInjectionInfo)
	: PinPath(InInjectionInfo->GetPin()->GetPinPath())
	, bAsInput(InInjectionInfo->bInjectedAsInput)
	, NodePath(InInjectionInfo->Node->GetName())
{
	if (InInjectionInfo->InputPin)
	{
		InputPinName = InInjectionInfo->InputPin->GetFName();
	}
	if (InInjectionInfo->OutputPin)
	{
		OutputPinName = InInjectionInfo->OutputPin->GetFName();
	}
}

bool FRigVMInjectNodeIntoPinAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->EjectNodeFromPin(*PinPath, false) != nullptr;
}

bool FRigVMInjectNodeIntoPinAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (URigVMInjectionInfo* InjectionInfo = InController->InjectNodeIntoPin(PinPath, bAsInput, InputPinName, OutputPinName, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMEjectNodeFromPinAction::FRigVMEjectNodeFromPinAction()
	: FRigVMInjectNodeIntoPinAction()
{
}

FRigVMEjectNodeFromPinAction::FRigVMEjectNodeFromPinAction(URigVMInjectionInfo* InInjectionInfo)
	: FRigVMInjectNodeIntoPinAction(InInjectionInfo)
{
}

bool FRigVMEjectNodeFromPinAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
#if WITH_EDITOR
	const URigVMInjectionInfo* InjectionInfo = InController->InjectNodeIntoPin(PinPath, bAsInput, InputPinName, OutputPinName, false);
	return InjectionInfo != nullptr;
#else
	return false;
#endif
}

bool FRigVMEjectNodeFromPinAction::Redo(URigVMController* InController)
{
	if (InController->EjectNodeFromPin(*PinPath, false) != nullptr)
	{
		return FRigVMBaseAction::Redo(InController);
	}
	return false;
}

FRigVMRemoveNodesAction::FRigVMRemoveNodesAction(TArray<URigVMNode*> InNodes, URigVMController* InController)
{
	Algo::Transform(InNodes, NodeNames, [](const URigVMNode* Node) -> FName
	{
		return Node->GetFName();
	});
	ExportedContent = InController->ExportNodesToText(NodeNames, true);
}

bool FRigVMRemoveNodesAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	const TArray<FName> NewNodeNames = InController->ImportNodesFromText(ExportedContent, false, false);
	return NewNodeNames.Num() == NodeNames.Num();
}

bool FRigVMRemoveNodesAction::Redo(URigVMController* InController)
{
	if(InController->RemoveNodesByName(NodeNames, false, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
	return false;
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

FRigVMSetNodeCategoryAction::FRigVMSetNodeCategoryAction(URigVMCollapseNode* InNode, const FString& InNewCategory)
	: NodePath(InNode->GetNodePath())
	, OldCategory(InNode->GetNodeCategory())
	, NewCategory(InNewCategory)
{
}

bool FRigVMSetNodeCategoryAction::Merge(const FRigVMBaseAction* Other)
{
	if (!FRigVMBaseAction::Merge(Other))
	{
		return false;
	}

	const FRigVMSetNodeCategoryAction* Action = (const FRigVMSetNodeCategoryAction*)Other;
	if (NodePath != Action->NodePath)
	{
		return false;
	}

	NewCategory = Action->NewCategory;
	return true;
}

bool FRigVMSetNodeCategoryAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetNodeCategoryByName(*NodePath, OldCategory, false);
}

bool FRigVMSetNodeCategoryAction::Redo(URigVMController* InController)
{
	if (!InController->SetNodeCategoryByName(*NodePath, NewCategory, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetNodeKeywordsAction::FRigVMSetNodeKeywordsAction(URigVMCollapseNode* InNode, const FString& InNewKeywords)
	: NodePath(InNode->GetNodePath())
	, OldKeywords(InNode->GetNodeKeywords())
	, NewKeywords(InNewKeywords)
{
}

bool FRigVMSetNodeKeywordsAction::Merge(const FRigVMBaseAction* Other)
{
	if (!FRigVMBaseAction::Merge(Other))
	{
		return false;
	}

	const FRigVMSetNodeKeywordsAction* Action = (const FRigVMSetNodeKeywordsAction*)Other;
	if (NodePath != Action->NodePath)
	{
		return false;
	}

	NewKeywords = Action->NewKeywords;
	return true;
}

bool FRigVMSetNodeKeywordsAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetNodeKeywordsByName(*NodePath, OldKeywords, false);
}

bool FRigVMSetNodeKeywordsAction::Redo(URigVMController* InController)
{
	if (!InController->SetNodeKeywordsByName(*NodePath, NewKeywords, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetNodeDescriptionAction::FRigVMSetNodeDescriptionAction(URigVMCollapseNode* InNode, const FString& InNewDescription)
	: NodePath(InNode->GetNodePath())
	, OldDescription(InNode->GetNodeDescription())
	, NewDescription(InNewDescription)
{
}

bool FRigVMSetNodeDescriptionAction::Merge(const FRigVMBaseAction* Other)
{
	if (!FRigVMBaseAction::Merge(Other))
	{
		return false;
	}

	const FRigVMSetNodeDescriptionAction* Action = (const FRigVMSetNodeDescriptionAction*)Other;
	if (NodePath != Action->NodePath)
	{
		return false;
	}

	NewDescription = Action->NewDescription;
	return true;
}

bool FRigVMSetNodeDescriptionAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetNodeDescriptionByName(*NodePath, OldDescription, false);
}

bool FRigVMSetNodeDescriptionAction::Redo(URigVMController* InController)
{
	if (!InController->SetNodeDescriptionByName(*NodePath, NewDescription, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetCommentTextAction::FRigVMSetCommentTextAction()
: NodePath()
, OldText()
, NewText()
, OldFontSize(18)
, NewFontSize(18)
, bOldBubbleVisible(false)
, bNewBubbleVisible(false)
, bOldColorBubble(false)
, bNewColorBubble(false)
{
	
}


FRigVMSetCommentTextAction::FRigVMSetCommentTextAction(URigVMCommentNode* InNode, const FString& InNewText, const int32& InNewFontSize, const bool& bInNewBubbleVisible, const bool& bInNewColorBubble)
: NodePath(InNode->GetNodePath())
, OldText(InNode->GetCommentText())
, NewText(InNewText)
, OldFontSize(InNode->GetCommentFontSize())
, NewFontSize(InNewFontSize)
, bOldBubbleVisible(InNode->GetCommentBubbleVisible())
, bNewBubbleVisible(bInNewBubbleVisible)
, bOldColorBubble(InNode->GetCommentColorBubble())
, bNewColorBubble(bInNewColorBubble)
{
}

bool FRigVMSetCommentTextAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetCommentTextByName(*NodePath, OldText, OldFontSize, bOldBubbleVisible, bOldColorBubble, false);
}

bool FRigVMSetCommentTextAction::Redo(URigVMController* InController)
{
	if(!InController->SetCommentTextByName(*NodePath, NewText, NewFontSize, bNewBubbleVisible, bNewColorBubble, false))
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
	return InController->OnExternalVariableRenamed(*NewVariableName, *OldVariableName, false);
}

bool FRigVMRenameVariableAction::Redo(URigVMController* InController)
{
	if(!InController->OnExternalVariableRenamed(*OldVariableName, *NewVariableName, false))
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
	/* Since for template we are chaning types - it is possible that the
	 * pin is no longer compliant with the old value
	if(!OldDefaultValue.IsEmpty())
	{
		check(InPin->IsValidDefaultValue(OldDefaultValue));
	}
	*/
	if(!NewDefaultValue.IsEmpty())
	{
		check(InPin->IsValidDefaultValue(NewDefaultValue));
	}
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
	GraphPath = TSoftObjectPtr<URigVMGraph>(Cast<URigVMGraph>(InOutputPin->GetGraph())).GetUniqueID();
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

FRigVMChangePinTypeAction::FRigVMChangePinTypeAction()
: PinPath()
, OldTypeIndex(INDEX_NONE)
, NewTypeIndex(INDEX_NONE)
, bSetupOrphanPins(true)
, bBreakLinks(true)
, bRemoveSubPins(true)
{}

FRigVMChangePinTypeAction::FRigVMChangePinTypeAction(URigVMPin* InPin, int32 InTypeIndex, bool InSetupOrphanPins, bool InBreakLinks, bool InRemoveSubPins)
: PinPath(InPin->GetPinPath())
, OldTypeIndex(InPin->GetTypeIndex())
, NewTypeIndex(InTypeIndex)
, bSetupOrphanPins(InSetupOrphanPins)
, bBreakLinks(InBreakLinks)
, bRemoveSubPins(InRemoveSubPins)
{
}

bool FRigVMChangePinTypeAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}

	if(const URigVMGraph* Graph = InController->GetGraph())
	{
		if(URigVMPin* Pin = Graph->FindPin(PinPath))
		{
			return InController->ChangePinType(Pin, OldTypeIndex, false, bSetupOrphanPins, bBreakLinks, bRemoveSubPins);
		}
	}
	return false;
}

bool FRigVMChangePinTypeAction::Redo(URigVMController* InController)
{
	if(const URigVMGraph* Graph = InController->GetGraph())
	{
		if(URigVMPin* Pin = Graph->FindPin(PinPath))
		{
			if(!InController->ChangePinType(Pin, NewTypeIndex, false, bSetupOrphanPins, bBreakLinks, bRemoveSubPins))
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMCollapseNodesAction::FRigVMCollapseNodesAction()
	: LibraryNodePath(), bIsAggregate(false)
{
}

FRigVMCollapseNodesAction::FRigVMCollapseNodesAction(URigVMController* InController, const TArray<URigVMNode*>& InNodes, const FString& InNodePath, const bool bIsAggregate)
	: LibraryNodePath(InNodePath), bIsAggregate(bIsAggregate)
{
	TArray<FName> NodesToExport;
	for (const URigVMNode* InNode : InNodes)
	{
		CollapsedNodesPaths.Add(InNode->GetName());
		NodesToExport.Add(InNode->GetFName());
	}

	CollapsedNodesContent = InController->ExportNodesToText(NodesToExport, true);
}

bool FRigVMCollapseNodesAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}

	// remove the library node
	{
		if(!InController->RemoveNodeByName(*LibraryNodePath, false, false))
		{
			return false;
		}
	}

	const TArray<FName> RecoveredNodes = InController->ImportNodesFromText(CollapsedNodesContent, false, false);
	if(RecoveredNodes.Num() != CollapsedNodesPaths.Num())
	{
		return false;
	}

	return true;
}

bool FRigVMCollapseNodesAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	TArray<FName> NodeNames;
	for (const FString& NodePath : CollapsedNodesPaths)
	{
		NodeNames.Add(*NodePath);
	}

	URigVMLibraryNode* LibraryNode = InController->CollapseNodes(NodeNames, LibraryNodePath, false, false, bIsAggregate);
	if (LibraryNode)
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMExpandNodeAction::FRigVMExpandNodeAction()
	: LibraryNodePath()
{
}

FRigVMExpandNodeAction::FRigVMExpandNodeAction(URigVMController* InController, URigVMLibraryNode* InLibraryNode)
	: LibraryNodePath(InLibraryNode->GetName())
{
	TArray<FName> NodesToExport;
	NodesToExport.Add(InLibraryNode->GetFName());
	LibraryNodeContent = InController->ExportNodesToText(NodesToExport, true);
}

bool FRigVMExpandNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}

	// remove the expanded nodes
	TArray<FName> NodeNames;
	Algo::Transform(ExpandedNodePaths, NodeNames, [](const FString& NodePath)
	{
		return FName(*NodePath); 
	});
	if(!InController->RemoveNodesByName(NodeNames, false, false))
	{
		return false;
	}

	const TArray<FName> RecoveredNodes = InController->ImportNodesFromText(LibraryNodeContent, false, false);
	if(RecoveredNodes.Num() != 1)
	{
		return false;
	}

	return true;
}

bool FRigVMExpandNodeAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	const TArray<URigVMNode*> ExpandedNodes = InController->ExpandLibraryNode(*LibraryNodePath, false);
	if (ExpandedNodes.Num() == ExpandedNodePaths.Num())
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMRenameNodeAction::FRigVMRenameNodeAction(const FName& InOldNodeName, const FName& InNewNodeName)
	: OldNodeName(InOldNodeName.ToString())
	, NewNodeName(InNewNodeName.ToString())
{
}

bool FRigVMRenameNodeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	if (URigVMNode* Node = InController->GetGraph()->FindNode(NewNodeName))
	{
		return InController->RenameNode(Node, *OldNodeName, false);
	}
	return false;
}

bool FRigVMRenameNodeAction::Redo(URigVMController* InController)
{
	if (URigVMNode* Node = InController->GetGraph()->FindNode(OldNodeName))
	{
		return InController->RenameNode(Node, *NewNodeName, false);
	}
	else
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMPushGraphAction::FRigVMPushGraphAction(UObject* InGraph)
{
	GraphPath = TSoftObjectPtr<URigVMGraph>(Cast<URigVMGraph>(InGraph)).GetUniqueID();
}

bool FRigVMPushGraphAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	
	if(URigVMGraph* PoppedGraph = InController->PopGraph(false))
	{
		const FSoftObjectPath PoppedGraphPath = TSoftObjectPtr<URigVMGraph>(PoppedGraph).GetUniqueID();
		return PoppedGraphPath == GraphPath;
	}
	
	return false;
}

bool FRigVMPushGraphAction::Redo(URigVMController* InController)
{
	TSoftObjectPtr<URigVMGraph> GraphPtr(GraphPath);
	if(URigVMGraph* Graph = GraphPtr.Get())
	{
		InController->PushGraph(Graph, false);
		return FRigVMBaseAction::Redo(InController);
	}
	return false;
}

bool FRigVMPushGraphAction::MakesObsolete(const FRigVMBaseAction* Other) const
{
	if (Other->GetScriptStruct() == FRigVMPopGraphAction::StaticStruct())
	{
		const FRigVMPopGraphAction* PopGraphAction = (const FRigVMPopGraphAction*)Other;
		if(PopGraphAction->GraphPath == GraphPath)
		{
			return true;
		}
	}
	return false;
}

FRigVMPopGraphAction::FRigVMPopGraphAction(UObject* InGraph)
{
	GraphPath = TSoftObjectPtr<URigVMGraph>(Cast<URigVMGraph>(InGraph)).GetUniqueID();
}

bool FRigVMPopGraphAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}

	TSoftObjectPtr<URigVMGraph> GraphPtr(GraphPath);
	if (URigVMGraph* Graph = GraphPtr.Get())
	{
		InController->PushGraph(Graph, false);
		return true;
	}
	return false;
}

bool FRigVMPopGraphAction::Redo(URigVMController* InController)
{
	if(URigVMGraph* PoppedGraph = InController->PopGraph(false))
	{
		const FSoftObjectPath PoppedGraphPath = TSoftObjectPtr<URigVMGraph>(PoppedGraph).GetUniqueID();
		if(PoppedGraphPath == GraphPath)
		{
			return FRigVMBaseAction::Redo(InController);
		}
	}
	return false;
}

bool FRigVMPopGraphAction::MakesObsolete(const FRigVMBaseAction* Other) const
{
	if (Other->GetScriptStruct() == FRigVMPushGraphAction::StaticStruct())
	{
		const FRigVMPushGraphAction* PushGraphAction = (const FRigVMPushGraphAction*)Other;
		if(PushGraphAction->GraphPath == GraphPath)
		{
			return true;
		}
	}
	return false;
}

FRigVMAddExposedPinAction::FRigVMAddExposedPinAction(URigVMPin* InPin)
	: PinName(InPin->GetName())
	, Direction(InPin->GetDirection())
	, CPPType(InPin->GetCPPType())
	, CPPTypeObjectPath()
	, DefaultValue(InPin->GetDefaultValue())
{
	if (UObject* CPPTypeObject = InPin->GetCPPTypeObject())
	{
		CPPTypeObjectPath = CPPTypeObject->GetPathName();
	}
}

bool FRigVMAddExposedPinAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveExposedPin(*PinName, false);
}

bool FRigVMAddExposedPinAction::Redo(URigVMController* InController)
{
	if (!InController->AddExposedPin(*PinName, Direction, CPPType, *CPPTypeObjectPath, DefaultValue, false).IsNone())
	{
		return FRigVMBaseAction::Redo(InController);
	}
	return false;
}

FRigVMRemoveExposedPinAction::FRigVMRemoveExposedPinAction()
	: PinIndex(INDEX_NONE)
{
}

FRigVMRemoveExposedPinAction::FRigVMRemoveExposedPinAction(URigVMPin* InPin)
	: PinName(InPin->GetName())
	, Direction(InPin->GetDirection())
	, CPPType(InPin->GetCPPType())
	, CPPTypeObjectPath()
	, DefaultValue(InPin->GetDefaultValue())
	, PinIndex(InPin->GetPinIndex())
{
	if (UObject* CPPTypeObject = InPin->GetCPPTypeObject())
	{
		CPPTypeObjectPath = CPPTypeObject->GetPathName();
	}
}

bool FRigVMRemoveExposedPinAction::Undo(URigVMController* InController)
{
	if(!InController->AddExposedPin(*PinName, Direction, CPPType, *CPPTypeObjectPath, DefaultValue, false).IsNone())
	{
		if (InController->SetExposedPinIndex(*PinName, PinIndex, false))
		{
			return FRigVMBaseAction::Undo(InController);
		}
	}
	return false;
}

bool FRigVMRemoveExposedPinAction::Redo(URigVMController* InController)
{
	if(FRigVMBaseAction::Redo(InController))
	{
		return InController->RemoveExposedPin(*PinName, false);
	}
	return false;
}

FRigVMRenameExposedPinAction::FRigVMRenameExposedPinAction(const FName& InOldPinName, const FName& InNewPinName)
	: OldPinName(InOldPinName.ToString())
	, NewPinName(InNewPinName.ToString())
{
}

bool FRigVMRenameExposedPinAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RenameExposedPin(*NewPinName, *OldPinName, false);
}

bool FRigVMRenameExposedPinAction::Redo(URigVMController* InController)
{
	if(!InController->RenameExposedPin(*OldPinName, *NewPinName, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetPinIndexAction::FRigVMSetPinIndexAction()
	: PinPath()
	, OldIndex(INDEX_NONE)
	, NewIndex(INDEX_NONE)
{
}

FRigVMSetPinIndexAction::FRigVMSetPinIndexAction(URigVMPin* InPin, int32 InNewIndex)
	: PinPath(InPin->GetName())
	, OldIndex(InPin->GetPinIndex())
	, NewIndex(InNewIndex)
{
}

bool FRigVMSetPinIndexAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetExposedPinIndex(*PinPath, OldIndex, false);
}

bool FRigVMSetPinIndexAction::Redo(URigVMController* InController)
{
	if (!InController->SetExposedPinIndex(*PinPath, NewIndex, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMSetRemappedVariableAction::FRigVMSetRemappedVariableAction(URigVMFunctionReferenceNode* InFunctionRefNode,
	const FName& InInnerVariableName, const FName& InOldOuterVariableName, const FName& InNewOuterVariableName)
	: NodePath()
	, InnerVariableName(InInnerVariableName)
	, OldOuterVariableName(InOldOuterVariableName)
	, NewOuterVariableName(InNewOuterVariableName)
{
	if(InFunctionRefNode)
	{
		NodePath = InFunctionRefNode->GetName();
	}
}

bool FRigVMSetRemappedVariableAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	if (URigVMFunctionReferenceNode* Node = Cast<URigVMFunctionReferenceNode>(InController->GetGraph()->FindNode(NodePath)))
	{
		return InController->SetRemappedVariable(Node, InnerVariableName, OldOuterVariableName, false);
	}
	return false;
}

bool FRigVMSetRemappedVariableAction::Redo(URigVMController* InController)
{
	if (URigVMFunctionReferenceNode* Node = Cast<URigVMFunctionReferenceNode>(InController->GetGraph()->FindNode(NodePath)))
	{
		return InController->SetRemappedVariable(Node, InnerVariableName, NewOuterVariableName, false);
	}
	else
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMAddLocalVariableAction::FRigVMAddLocalVariableAction(const FRigVMGraphVariableDescription& InLocalVariable)
	: LocalVariable(InLocalVariable)
{
	
}

bool FRigVMAddLocalVariableAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveLocalVariable(LocalVariable.Name, false);
}

bool FRigVMAddLocalVariableAction::Redo(URigVMController* InController)
{
	if (!LocalVariable.Name.IsNone())
	{
		return InController->AddLocalVariable(LocalVariable.Name, LocalVariable.CPPType, LocalVariable.CPPTypeObject, LocalVariable.DefaultValue, false).Name.IsNone() == false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMRemoveLocalVariableAction::FRigVMRemoveLocalVariableAction(const FRigVMGraphVariableDescription& InLocalVariable)
	: LocalVariable(InLocalVariable)
{
}

bool FRigVMRemoveLocalVariableAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return !InController->AddLocalVariable(LocalVariable.Name, LocalVariable.CPPType, LocalVariable.CPPTypeObject, LocalVariable.DefaultValue, false).Name.IsNone();
}

bool FRigVMRemoveLocalVariableAction::Redo(URigVMController* InController)
{
	if (!LocalVariable.Name.IsNone())
	{
		return InController->RemoveLocalVariable(LocalVariable.Name, false);
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMRenameLocalVariableAction::FRigVMRenameLocalVariableAction(const FName& InOldName, const FName& InNewName)
	: OldVariableName(InOldName), NewVariableName(InNewName)
{
	
}

bool FRigVMRenameLocalVariableAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RenameLocalVariable(NewVariableName, OldVariableName, false);
}

bool FRigVMRenameLocalVariableAction::Redo(URigVMController* InController)
{
	if (!InController->RenameLocalVariable(OldVariableName, NewVariableName, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMChangeLocalVariableTypeAction::FRigVMChangeLocalVariableTypeAction()
	: LocalVariable()
	, CPPType()
	, CPPTypeObject(nullptr)
{
}

FRigVMChangeLocalVariableTypeAction::FRigVMChangeLocalVariableTypeAction(
	const FRigVMGraphVariableDescription& InLocalVariable, const FString& InCPPType, UObject* InCPPTypeObject)
		: LocalVariable(InLocalVariable), CPPType(InCPPType), CPPTypeObject(InCPPTypeObject)
{
}

bool FRigVMChangeLocalVariableTypeAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetLocalVariableType(LocalVariable.Name, LocalVariable.CPPType, LocalVariable.CPPTypeObject, false);
}

bool FRigVMChangeLocalVariableTypeAction::Redo(URigVMController* InController)
{
	if (!InController->SetLocalVariableType(LocalVariable.Name, CPPType, CPPTypeObject, false))
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMChangeLocalVariableDefaultValueAction::FRigVMChangeLocalVariableDefaultValueAction()
	: LocalVariable()
	, DefaultValue()
{
}

FRigVMChangeLocalVariableDefaultValueAction::FRigVMChangeLocalVariableDefaultValueAction(
	const FRigVMGraphVariableDescription& InLocalVariable, const FString& InDefaultValue)
		: LocalVariable(InLocalVariable), DefaultValue(InDefaultValue)
{
}

bool FRigVMChangeLocalVariableDefaultValueAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->SetLocalVariableDefaultValue(LocalVariable.Name, LocalVariable.DefaultValue, false);
}

bool FRigVMChangeLocalVariableDefaultValueAction::Redo(URigVMController* InController)
{
	if (!InController->SetLocalVariableDefaultValue(LocalVariable.Name, DefaultValue, false))
	{
		return false;		
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMPromoteNodeAction::FRigVMPromoteNodeAction()
	: LibraryNodePath()
	, FunctionDefinitionPath()
	, bFromFunctionToCollapseNode(false)
{
}

FRigVMPromoteNodeAction::FRigVMPromoteNodeAction(const URigVMNode* InNodeToPromote, const FString& InNodePath, const FString& InFunctionDefinitionPath)
	: LibraryNodePath(InNodePath)
	, FunctionDefinitionPath(InFunctionDefinitionPath)
	, bFromFunctionToCollapseNode(InNodeToPromote->IsA<URigVMFunctionReferenceNode>())
{
}

bool FRigVMPromoteNodeAction::Undo(URigVMController* InController)
{
	if(bFromFunctionToCollapseNode)
	{
		const FName FunctionRefNodeName = InController->PromoteCollapseNodeToFunctionReferenceNode(*LibraryNodePath, false, false, FunctionDefinitionPath);
		return FunctionRefNodeName.ToString() == LibraryNodePath;
	}

	const FName CollapseNodeName = InController->PromoteFunctionReferenceNodeToCollapseNode(*LibraryNodePath, false, false, true);
	return CollapseNodeName.ToString() == LibraryNodePath;
}

bool FRigVMPromoteNodeAction::Redo(URigVMController* InController)
{
	if(bFromFunctionToCollapseNode)
	{
		const FName CollapseNodeName = InController->PromoteFunctionReferenceNodeToCollapseNode(*LibraryNodePath, false, false, false);
		return CollapseNodeName.ToString() == LibraryNodePath;
	}
	const FName FunctionRefNodeName = InController->PromoteCollapseNodeToFunctionReferenceNode(*LibraryNodePath, false, false);
	return FunctionRefNodeName.ToString() == LibraryNodePath;
}

FRigVMMarkFunctionPublicAction::FRigVMMarkFunctionPublicAction()
	: FunctionName(NAME_None)
	, bIsPublic(false)
{
}

FRigVMMarkFunctionPublicAction::FRigVMMarkFunctionPublicAction(const FName& InFunctionName, bool bInIsPublic)
	: FunctionName(InFunctionName)
	, bIsPublic(bInIsPublic)
{
}

bool FRigVMMarkFunctionPublicAction::Undo(URigVMController* InController)
{
	if (!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->MarkFunctionAsPublic(FunctionName, !bIsPublic, false);
}

bool FRigVMMarkFunctionPublicAction::Redo(URigVMController* InController)
{
#if WITH_EDITOR
	if (InController->MarkFunctionAsPublic(FunctionName, bIsPublic, false))
	{
		return FRigVMBaseAction::Redo(InController);
	}
#endif
	return false;
}

FRigVMImportFromTextAction::FRigVMImportFromTextAction(const FString& InContent, const TArray<FName>& InTopLevelNodeNames)
	: Content(InContent)
	, TopLevelNodeNames(InTopLevelNodeNames)
{
}

FRigVMImportFromTextAction::FRigVMImportFromTextAction(URigVMNode* InNode, URigVMController* InController, bool bIncludeExteriorLinks)
{
	SetContent({InNode}, InController, bIncludeExteriorLinks);
}

FRigVMImportFromTextAction::FRigVMImportFromTextAction(const TArray<URigVMNode*>& InNodes, URigVMController* InController, bool bIncludeExteriorLinks)
{
	SetContent(InNodes, InController, bIncludeExteriorLinks);
}

void FRigVMImportFromTextAction::SetContent(const TArray<URigVMNode*>& InNodes, URigVMController* InController, bool bIncludeExteriorLinks)
{
	TopLevelNodeNames.Reset();
	Algo::Transform(InNodes, TopLevelNodeNames, [](const URigVMNode* Node)
	{
		return Node->GetFName();
	});
	Content = InController->ExportNodesToText(TopLevelNodeNames, bIncludeExteriorLinks);
}

bool FRigVMImportFromTextAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}
	return InController->RemoveNodesByName(TopLevelNodeNames, false, false);
}

bool FRigVMImportFromTextAction::Redo(URigVMController* InController)
{
	const TArray<FName> ImportedNames = InController->ImportNodesFromText(Content, false, false);
	if(ImportedNames.Num() != TopLevelNodeNames.Num())
	{
		return false;
	}
	return FRigVMBaseAction::Redo(InController);
}

FRigVMReplaceNodesAction::FRigVMReplaceNodesAction(const TArray<URigVMNode*>& InNodes, URigVMController* InController)
{
	for(const URigVMNode* Node : InNodes)
	{
		StoreNode(Node, InController);
	}
}

bool FRigVMReplaceNodesAction::Undo(URigVMController* InController)
{
	if(!FRigVMBaseAction::Undo(InController))
	{
		return false;
	}

	for(const TPair<FName, FRigVMActionNodeContent>& Pair : ExportedNodes)
	{
		if(!RestoreNode(Pair.Key, InController, true))
		{
			return false;
		}
	}

	return true;
}

bool FRigVMReplaceNodesAction::Redo(URigVMController* InController)
{
	for(const TPair<FName, FRigVMActionNodeContent>& Pair : ExportedNodes)
	{
		if(!RestoreNode(Pair.Key, InController, false))
		{
			return false;
		}
	}
	
	return FRigVMBaseAction::Redo(InController);
}

