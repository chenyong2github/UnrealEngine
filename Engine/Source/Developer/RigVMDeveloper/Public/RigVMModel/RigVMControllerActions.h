// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/PropertyPortFlags.h"
#include "RigVMController.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif
#include "RigVMControllerActions.generated.h"

/**
 * ================================================================================
 * The RigVMController doesn't rely on the transaction system for performing,
 * tracking, undoing and redoing changes. Instead it uses an action stack which
 * stores small serialized structs for each occured action. The reason for this 
 * is the subscription model and Python support: We need the Graph to broadcast
 * events to all subscribers - independently from where the action is coming from.
 * This includes UI views, scripting or undo / redo.
 * Each action supports the concept of 'merging', so multiple color change actions
 * for example can be merged into a single action. This avoids the need for tracking
 * action scope - and makes the integration with UI code simple.
 * The Controller's ActionStack integrates into the editor's transaction stack
 * using the ActionIndex property: Transactions on that property cause the 
 * actionstack to consecutively undo or redo actions until the expected stack
 * size is reached.
 * ================================================================================
 */

struct FRigVMBaseAction;

/**
 * The action key is used for serializing and storing an action in the stack,
 * or within another action.
 */
USTRUCT()
struct FRigVMActionKey
{
	GENERATED_BODY()

	UPROPERTY()
	FString ScriptStructPath;

	UPROPERTY()
	FString ExportedText;

	template<class ActionType>
	void Set(const ActionType& InAction)
	{
		UScriptStruct* ScriptStruct = ActionType::StaticStruct();
		FRigVMActionKey Key;
		ScriptStructPath = ScriptStruct->GetPathName();
		ScriptStruct->ExportText(ExportedText, &InAction, nullptr, nullptr, PPF_None, nullptr);
	}
};

/**
 * The action wrapper is used to extract an action from a serialized key.
 */
struct FRigVMActionWrapper
{
public:
	FRigVMActionWrapper(const FRigVMActionKey& Key);
	~FRigVMActionWrapper();

	FRigVMBaseAction* GetAction();
	FString ExportText();

private:
	FRigVMActionWrapper(const FRigVMActionWrapper& Other) = delete;
	FRigVMActionWrapper& operator = (const FRigVMActionWrapper& Other) = delete;

	UScriptStruct* ScriptStruct;
	FRigVMByteArray Data;
};

/**
 * The base action is the base struct for all actions, and provides
 * access to sub actions, merge functionality as well as undo and redo
 * base implementations.
 */
USTRUCT()
struct FRigVMBaseAction
{
	GENERATED_BODY()

	// Default constructor
	FRigVMBaseAction()
		 : Title(TEXT("ACtion"))
	{
	}

	// Default destructor
	virtual ~FRigVMBaseAction() {};

	// Returns the title of the action - used for the Edit menu's undo / redo
	virtual FString GetTitle() const { return Title; }

	// Trys to merge the action with another action and 
	// returns true if successfull.
	virtual bool Merge(const FRigVMBaseAction* Other);

	// Un-does the action and returns true if successfull.
	virtual bool Undo(URigVMController* InController);

	// Re-does the action and returns true if successfull.
	virtual bool Redo(URigVMController* InController);

	// Adds a child / sub action to this one
	template<class ActionType>
	void AddAction(const ActionType& InAction)
	{
		FRigVMActionKey Key;
		Key.Set<ActionType>(InAction);
		SubActions.Add(Key);
	}

	UPROPERTY()
	FString Title;

	UPROPERTY()
	TArray<FRigVMActionKey> SubActions;
};

/**
 * The Action Stack can be used to track actions happening on a
 * Graph. Currently the only owner of the ActionStack is the Controller.
 * Actions can be added to the stack, or they can be understood as
 * scopes / brackets. For this you can use BeginAction / EndAction / CancelAction
 * to open / close a bracket. Open brackets automatically record additional
 * actions occuring during the bracket's lifetime.
 */
UCLASS()
class URigVMActionStack : public UObject
{
	GENERATED_BODY()

public:

	// Begins an action and opens a bracket / scope.
	template<class T>
	void BeginAction(T& InAction)
	{
		if (CurrentActions.Num() > 0)
		{
			// catch erroreous duplicate calls to begin action
			ensure(CurrentActions.Last() != &InAction);
		}
		CurrentActions.Add((FRigVMBaseAction*)&InAction);

		ModifiedEvent.Broadcast(ERigVMGraphNotifType::InteractionBracketOpened, nullptr, nullptr);
	}

	// Ends an action and closes a bracket / scope.
	template<class ActionType>
	void EndAction(ActionType& InAction, bool bPerformMerge = false)
	{
		ensure(CurrentActions.Num() > 0);
		ensure((FRigVMBaseAction*)&InAction == CurrentActions.Last());
		CurrentActions.Pop();
		AddAction(InAction, bPerformMerge);

		ModifiedEvent.Broadcast(ERigVMGraphNotifType::InteractionBracketClosed, nullptr, nullptr);
	}

	// Cancels an action, closes a bracket / scope and discards all 
	// actions to this point.
	template<class ActionType>
	void CancelAction(ActionType& InAction)
	{
		ensure(CurrentActions.Num() > 0);
		ensure((FRigVMBaseAction*)&InAction == CurrentActions.Last());
		CurrentActions.Pop();

		ModifiedEvent.Broadcast(ERigVMGraphNotifType::InteractionBracketCanceled, nullptr, nullptr);
	}

	// Adds an action to the stack. Optionally this can perform
	// a potential merge of this action with the previous action to
	// compact the stack.
	template<class ActionType>
	void AddAction(const ActionType& InAction, bool bPerformMerge = false)
	{
		TArray<FRigVMActionKey>* ActionList = &UndoActions;
		if (CurrentActions.Num() > 0)
		{
			ActionList = &CurrentActions[CurrentActions.Num()-1]->SubActions;
		}

		bool bMergeIfPossible = bPerformMerge;
		if (bMergeIfPossible)
		{
			bMergeIfPossible = false;
			if (ActionList->Num() > 0 && InAction.SubActions.Num() == 0)
			{
				if (ActionList->Last().ScriptStructPath == ActionType::StaticStruct()->GetPathName())
				{
					FRigVMActionWrapper Wrapper((*ActionList)[ActionList->Num() - 1]);
					if (Wrapper.GetAction()->SubActions.Num() == 0)
					{
						bMergeIfPossible = Wrapper.GetAction()->Merge(&InAction);
						if (bMergeIfPossible)
						{
							(*ActionList)[ActionList->Num()-1].ExportedText = Wrapper.ExportText();
						}
					}
				}
			}
		}

		if (!bMergeIfPossible)
		{
			FRigVMActionKey Key;
			Key.Set<ActionType>(InAction);

			ActionList->Add(Key);

			if (CurrentActions.Num() == 0)
			{
				RedoActions.Reset();

#if WITH_EDITOR
				const FScopedTransaction Transaction(FText::FromString(InAction.GetTitle()));
				SetFlags(RF_Transactional);
				Modify();
#endif

				ActionIndex = ActionIndex + 1;
			}
		}
	}

	// Opens an undo bracket / scope to record actions into.
	// This is primary useful for Python.
	UFUNCTION()
	bool OpenUndoBracket(const FString& InTitle);

	// Closes an undo bracket / scope.
	// This is primary useful for Python.
	UFUNCTION()
	bool CloseUndoBracket();

	// Cancels an undo bracket / scope.
	// This is primary useful for Python.
	UFUNCTION()
	bool CancelUndoBracket();

	// Pops the last action from the undo stack and perform undo on it.
	// Note: This should really only be used for unit tests,
	// use the GEditor's main Undo method instead.
	UFUNCTION()
	bool Undo(URigVMController* InController);

	// Pops the last action from the redo stack and perform redo on it.
	// Note: This should really only be used for unit tests,
	// use the GEditor's main Redo method instead.
	UFUNCTION()
	bool Redo(URigVMController* InController);

#if WITH_EDITOR
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
#endif

	FRigVMGraphModifiedEvent& OnModified() { return ModifiedEvent; }

private:

	UPROPERTY()
	int32 ActionIndex;

	UPROPERTY(NonTransactional)
	TArray<FRigVMActionKey> UndoActions;

	UPROPERTY(NonTransactional)
	TArray<FRigVMActionKey> RedoActions;

	TArray<FRigVMBaseAction*> CurrentActions;
	TArray<FRigVMBaseAction*> BracketActions;

	FRigVMGraphModifiedEvent ModifiedEvent;
};

/**
 * An action which inverses the child actions,
 * it performs undo on redo and vice versa.
 */
USTRUCT()
struct FRigVMInverseAction : public FRigVMBaseAction
{
	GENERATED_BODY()

	virtual ~FRigVMInverseAction() {};
	virtual bool Undo(URigVMController* InController);
	virtual bool Redo(URigVMController* InController);
};

/**
 * An action adding a struct node to the graph.
 */
USTRUCT()
struct FRigVMAddStructNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddStructNodeAction();
	FRigVMAddStructNodeAction(URigVMStructNode* InNode);
	virtual ~FRigVMAddStructNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString ScriptStructPath;

	UPROPERTY()
	FName MethodName;
	
	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding a variable node to the graph.
 */
USTRUCT()
struct FRigVMAddVariableNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddVariableNodeAction();
	FRigVMAddVariableNodeAction(URigVMVariableNode* InNode);
	virtual ~FRigVMAddVariableNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FName VariableName;
	
	UPROPERTY()
	FString CPPType;

	UPROPERTY()
	FString CPPTypeObjectPath;

	UPROPERTY()
	bool bIsGetter;

	UPROPERTY()
	FString DefaultValue;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding a parameter node to the graph.
 */
USTRUCT()
struct FRigVMAddParameterNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddParameterNodeAction();
	FRigVMAddParameterNodeAction(URigVMParameterNode* InNode);
	virtual ~FRigVMAddParameterNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FName ParameterName;
	
	UPROPERTY()
	FString CPPType;

	UPROPERTY()
	FString CPPTypeObjectPath;

	UPROPERTY()
	bool bIsInput;

	UPROPERTY()
	FString DefaultValue;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding a comment node to the graph.
 */
USTRUCT()
struct FRigVMAddCommentNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddCommentNodeAction();
	FRigVMAddCommentNodeAction(URigVMCommentNode* InNode);
	virtual ~FRigVMAddCommentNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString CommentText;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FVector2D Size;

	UPROPERTY()
	FLinearColor Color;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding a reroute node to the graph.
 */
USTRUCT()
struct FRigVMAddRerouteNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddRerouteNodeAction();
	FRigVMAddRerouteNodeAction(URigVMRerouteNode* InNode);
	virtual ~FRigVMAddRerouteNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	bool bShowAsFullNode;

	UPROPERTY()
	FString CPPType;

	UPROPERTY()
	FName CPPTypeObjectPath;

	UPROPERTY()
	FString DefaultValue;

	UPROPERTY()
	bool bIsConstant;

	UPROPERTY()
	FName CustomWidgetName;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding a branch node to the graph.
 */
USTRUCT()
struct FRigVMAddBranchNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddBranchNodeAction();
	FRigVMAddBranchNodeAction(URigVMBranchNode* InNode);
	virtual ~FRigVMAddBranchNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding an if node to the graph.
 */
USTRUCT()
struct FRigVMAddIfNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddIfNodeAction();
	FRigVMAddIfNodeAction(URigVMIfNode* InNode);
	virtual ~FRigVMAddIfNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString CPPType;

	UPROPERTY()
	FName CPPTypeObjectPath;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding a select node to the graph.
 */
USTRUCT()
struct FRigVMAddSelectNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddSelectNodeAction();
	FRigVMAddSelectNodeAction(URigVMSelectNode* InNode);
	virtual ~FRigVMAddSelectNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString CPPType;

	UPROPERTY()
	FName CPPTypeObjectPath;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding an enum node to the graph.
 */
USTRUCT()
struct FRigVMAddEnumNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddEnumNodeAction();
	FRigVMAddEnumNodeAction(URigVMEnumNode* InNode);
	virtual ~FRigVMAddEnumNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString CPPType;

	UPROPERTY()
	FName CPPTypeObjectPath;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding a prototype node to the graph.
 */
USTRUCT()
struct FRigVMAddPrototypeNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddPrototypeNodeAction();
	FRigVMAddPrototypeNodeAction(URigVMPrototypeNode* InNode);
	virtual ~FRigVMAddPrototypeNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FName PrototypeNotation;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action adding an injected node to the graph.
 */
USTRUCT()
struct FRigVMAddInjectedNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddInjectedNodeAction();
	FRigVMAddInjectedNodeAction(URigVMInjectionInfo* InInjectionInfo);
	virtual ~FRigVMAddInjectedNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString PinPath;

	UPROPERTY()
	bool bAsInput;

	UPROPERTY()
	FString ScriptStructPath;

	UPROPERTY()
	FName MethodName;

	UPROPERTY()
	FName InputPinName;

	UPROPERTY()
	FName OutputPinName;

	UPROPERTY()
	FString NodePath;
};

/**
 * An action removing a node from the graph.
 */
USTRUCT()
struct FRigVMRemoveNodeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMRemoveNodeAction() {}
	FRigVMRemoveNodeAction(URigVMNode* InNode);
	virtual ~FRigVMRemoveNodeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FRigVMActionKey InverseActionKey;
};

/**
 * An action selecting or deselecting a node in the graph.
 */
USTRUCT()
struct FRigVMSetNodeSelectionAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMSetNodeSelectionAction();
	FRigVMSetNodeSelectionAction(URigVMGraph* InGraph, TArray<FName> InNewSelection);
	virtual ~FRigVMSetNodeSelectionAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	TArray<FName> NewSelection;

	UPROPERTY()
	TArray<FName> OldSelection;
};

/**
 * An action setting a node's position in the graph.
 */
USTRUCT()
struct FRigVMSetNodePositionAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMSetNodePositionAction()
	{
		OldPosition = NewPosition = FVector2D::ZeroVector;
	}
	FRigVMSetNodePositionAction(URigVMNode* InNode, const FVector2D& InNewPosition);
	virtual ~FRigVMSetNodePositionAction() {};
	virtual bool Merge(const FRigVMBaseAction* Other);
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString NodePath;

	UPROPERTY()
	FVector2D OldPosition;

	UPROPERTY()
	FVector2D NewPosition;
};

/**
 * An action setting a node's size in the graph.
 */
USTRUCT()
struct FRigVMSetNodeSizeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMSetNodeSizeAction()
	{
		OldSize = NewSize = FVector2D::ZeroVector;
	}
	FRigVMSetNodeSizeAction(URigVMNode* InNode, const FVector2D& InNewSize);
	virtual ~FRigVMSetNodeSizeAction() {};
	virtual bool Merge(const FRigVMBaseAction* Other);
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString NodePath;

	UPROPERTY()
	FVector2D OldSize;

	UPROPERTY()
	FVector2D NewSize;
};

/**
 * An action setting a node's color in the graph.
 */
USTRUCT()
struct FRigVMSetNodeColorAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMSetNodeColorAction()
	{
		OldColor = NewColor = FLinearColor::Black;
	}
	FRigVMSetNodeColorAction(URigVMNode* InNode, const FLinearColor& InNewColor);
	virtual ~FRigVMSetNodeColorAction() {};
	virtual bool Merge(const FRigVMBaseAction* Other);
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString NodePath;

	UPROPERTY()
	FLinearColor OldColor;

	UPROPERTY()
	FLinearColor NewColor;
};

/**
 * An action setting a comment node's text in the graph.
 */
USTRUCT()
struct FRigVMSetCommentTextAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMSetCommentTextAction() {}
	FRigVMSetCommentTextAction(URigVMCommentNode* InNode, const FString& InNewText);
	virtual ~FRigVMSetCommentTextAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString NodePath;

	UPROPERTY()
	FString OldText;

	UPROPERTY()
	FString NewText;
};

/**
 * An action setting a reroute node's compactness in the graph.
 */
USTRUCT()
struct FRigVMSetRerouteCompactnessAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMSetRerouteCompactnessAction();
	FRigVMSetRerouteCompactnessAction(URigVMRerouteNode* InNode, bool InShowAsFullNode);
	virtual ~FRigVMSetRerouteCompactnessAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString NodePath;

	UPROPERTY()
	bool OldShowAsFullNode;

	UPROPERTY()
	bool NewShowAsFullNode;
};

/**
 * An action renaming a variable in the graph.
 */
USTRUCT()
struct FRigVMRenameVariableAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMRenameVariableAction() {}
	FRigVMRenameVariableAction(const FName& InOldVariableName, const FName& InNewVariableName);
	virtual ~FRigVMRenameVariableAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString OldVariableName;

	UPROPERTY()
	FString NewVariableName;
};

/**
 * An action renaming a parameter in the graph.
 */
USTRUCT()
struct FRigVMRenameParameterAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMRenameParameterAction() {}
	FRigVMRenameParameterAction(const FName& InOldParameterName, const FName& InNewParameterName);
	virtual ~FRigVMRenameParameterAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString OldParameterName;

	UPROPERTY()
	FString NewParameterName;
};

/**
 * An action setting a pin's expansion state in the graph.
 */
USTRUCT()
struct FRigVMSetPinExpansionAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMSetPinExpansionAction()
	{
		PinPath = FString();
		OldIsExpanded = NewIsExpanded = false;
	}

	FRigVMSetPinExpansionAction(URigVMPin* InPin, bool bNewIsExpanded);
	virtual ~FRigVMSetPinExpansionAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString PinPath;

	UPROPERTY()
	bool OldIsExpanded;

	UPROPERTY()
	bool NewIsExpanded;
};

/**
 * An action setting a pin's watch state in the graph.
 */
USTRUCT()
struct FRigVMSetPinWatchAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMSetPinWatchAction()
	{
		OldIsWatched = NewIsWatched = false;
	}

	FRigVMSetPinWatchAction(URigVMPin* InPin, bool bNewIsWatched);
	virtual ~FRigVMSetPinWatchAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString PinPath;

	UPROPERTY()
	bool OldIsWatched;

	UPROPERTY()
	bool NewIsWatched;
};

/**
 * An action setting a pin's default value in the graph.
 */
USTRUCT()
struct FRigVMSetPinDefaultValueAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMSetPinDefaultValueAction() {}
	FRigVMSetPinDefaultValueAction(URigVMPin* InPin, const FString& InNewDefaultValue);
	virtual ~FRigVMSetPinDefaultValueAction() {};
	virtual bool Merge(const FRigVMBaseAction* Other);
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString PinPath;

	UPROPERTY()
	FString OldDefaultValue;

	UPROPERTY()
	FString NewDefaultValue;
};

/**
 * An action inserting a new array pin in the graph.
 */
USTRUCT()
struct FRigVMInsertArrayPinAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMInsertArrayPinAction()
		: Index(0)
	{
	}
	FRigVMInsertArrayPinAction(URigVMPin* InArrayPin, int32 InIndex, const FString& InNewDefaultValue);
	virtual ~FRigVMInsertArrayPinAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString ArrayPinPath;

	UPROPERTY()
	int32 Index;

	UPROPERTY()
	FString NewDefaultValue;
};

/**
 * An action removing an array pin from the graph.
 */
USTRUCT()
struct FRigVMRemoveArrayPinAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMRemoveArrayPinAction()
		: Index(0)
	{
	}
	FRigVMRemoveArrayPinAction(URigVMPin* InArrayElementPin);
	virtual ~FRigVMRemoveArrayPinAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString ArrayPinPath;

	UPROPERTY()
	int32 Index;

	UPROPERTY()
	FString DefaultValue;
};

/**
 * An action adding a new link to the graph.
 */
USTRUCT()
struct FRigVMAddLinkAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMAddLinkAction() {}
	FRigVMAddLinkAction(URigVMPin* InOutputPin, URigVMPin* InInputPin);
	virtual ~FRigVMAddLinkAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString OutputPinPath;

	UPROPERTY()
	FString InputPinPath;
};

/**
 * An action removing a link from the graph.
 */
USTRUCT()
struct FRigVMBreakLinkAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMBreakLinkAction() {}
	FRigVMBreakLinkAction(URigVMPin* InOutputPin, URigVMPin* InInputPin);
	virtual ~FRigVMBreakLinkAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString OutputPinPath;

	UPROPERTY()
	FString InputPinPath;
};

/**
 * An action changing a pin type
 */
USTRUCT()
struct FRigVMChangePinTypeAction : public FRigVMBaseAction
{
	GENERATED_BODY()

public:

	FRigVMChangePinTypeAction() {}
	FRigVMChangePinTypeAction(URigVMPin* InPin, const FString& InCppType, const FName& InCppTypeObjectPath);
	virtual ~FRigVMChangePinTypeAction() {};
	virtual bool Undo(URigVMController* InController) override;
	virtual bool Redo(URigVMController* InController) override;

	UPROPERTY()
	FString PinPath;

	UPROPERTY()
	FString OldCPPType;

	UPROPERTY()
	FName OldCPPTypeObjectPath;

	UPROPERTY()
	FString NewCPPType;

	UPROPERTY()
	FName NewCPPTypeObjectPath;
};
