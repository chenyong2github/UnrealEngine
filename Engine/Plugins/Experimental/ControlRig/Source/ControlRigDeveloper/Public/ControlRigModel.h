// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "ControlRigModel.generated.h"

#define CONTROLRIG_UNDO WITH_EDITOR

class UControlRigModel;

enum class EControlRigModelNotifType : uint8
{
	ModelError,
	ModelCleared,
	NodeAdded,
	NodeRemoved,
	NodeRenamed,
	NodeChanged,
	NodeSelected,
	NodeDeselected,
	LinkAdded,
	LinkRemoved,
	PinAdded,
	PinRemoved,
	PinChanged,
	Invalid
};

/**
 * Describes a single error within the model
 */
struct CONTROLRIGDEVELOPER_API FControlRigModelError
{
	FString Message;

	FControlRigModelError()
		: Message(FString())
	{
	}
};

/**
 * A pair of node + pin index
 */
struct CONTROLRIGDEVELOPER_API FControlRigModelPair
{
	int32 Node;
	int32 Pin;

	FControlRigModelPair()
		: Node(INDEX_NONE)
		, Pin(INDEX_NONE)
	{
	}

	FControlRigModelPair(int32 InNode, int32 InPin)
		: Node(InNode)
		, Pin(InPin)
	{
	}

	bool operator==(const FControlRigModelPair& Other) const
	{
		return Node == Other.Node && Pin == Other.Pin;
	}

	bool IsValid() const
	{
		return Node != INDEX_NONE && Pin != INDEX_NONE;
	}

#if CONTROLRIG_UNDO

	static int32 ArgumentSize();
	void AppendArgumentsForAction(TArray<FString>& InOutArguments, const UControlRigModel* InModel) const;
	void ConfigureFromActionArguments(const TArray<FString>& InOutArguments, int32 ArgumentIndex, const UControlRigModel* InModel);

#endif
};

/**
 * A link between two pins.
 */
struct CONTROLRIGDEVELOPER_API FControlRigModelLink
{
	int32 Index;
	FControlRigModelPair Source;
	FControlRigModelPair Target;

	FControlRigModelLink()
		: Index(INDEX_NONE)
		, Source(FControlRigModelPair())
		, Target(FControlRigModelPair())
	{
	}

	bool IsValid() const
	{
		return Index != INDEX_NONE && Source.IsValid() && Target.IsValid();
	}

#if CONTROLRIG_UNDO

	static int32 ArgumentSize();
	void AppendArgumentsForAction(TArray<FString>& InOutArguments, const UControlRigModel* InModel) const;
	void ConfigureFromActionArguments(const TArray<FString>& InOutArguments, int32 ArgumentIndex, const UControlRigModel* InModel);

#endif
};

/**
 * A single node within the control rig's model
 */
struct CONTROLRIGDEVELOPER_API FControlRigModelPin
{
	FName Name;
	FText DisplayNameText;
	int32 Node;
	int32 Index;
	int32 ParentIndex;
	TArray<int32> SubPins;
	TEnumAsByte<enum EEdGraphPinDirection> Direction;
	FEdGraphPinType Type;
	FString DefaultValue;
	bool bExpanded;
	bool bIsConstant;
	FName CustomWidgetName;
	TArray<int32> Links;
	FText TooltipText;

	FControlRigModelPin()
		: Name(NAME_None)
		, DisplayNameText(FText())
		, Node(INDEX_NONE)
		, Index(INDEX_NONE)
		, ParentIndex(INDEX_NONE)
		, Direction(EGPD_Input)
		, Type(FEdGraphPinType())
		, DefaultValue(FString())
		, bExpanded(false)
		, bIsConstant(false)
		, CustomWidgetName(NAME_None)
	{
	}

	FControlRigModelPair GetPair() const
	{
		FControlRigModelPair Pair;
		Pair.Node = Node;
		Pair.Pin = Index;
		return Pair;
	}

	bool IsValid() const
	{
		return Index != INDEX_NONE && Node != INDEX_NONE && Name != NAME_None;
	}

	bool IsSingleValue() const
	{
		return Type.ContainerType == EPinContainerType::None;
	}

	bool IsArray() const
	{
		return Type.IsArray();
	}

	int32 ArraySize() const
	{
		return SubPins.Num();
	}

#if CONTROLRIG_UNDO

	static int32 ArgumentSize();
	void AppendArgumentsForAction(TArray<FString>& InOutArguments) const;
	void ConfigureFromActionArguments(const TArray<FString>& InOutArguments, int32 ArgumentIndex = 0);

#endif
};

enum class EControlRigModelParameterType : uint8
{
	None,
	Input,
	Output,
	Hidden
};

enum class EControlRigModelNodeType : uint8
{
	Function,
	Parameter,
	Comment,
	Invalid
};

/**
 * A single node within the model.
 */
struct CONTROLRIGDEVELOPER_API FControlRigModelNode
{
	FName Name;
	int32 Index;
	EControlRigModelNodeType NodeType;
	FName FunctionName;
	FVector2D Position;
	FVector2D Size;
	FLinearColor Color;
	TArray<FControlRigModelPin> Pins;
	EControlRigModelParameterType ParameterType;
	FString Text;

	FControlRigModelNode()
		: Name(NAME_None)
		, Index(INDEX_NONE)
		, NodeType(EControlRigModelNodeType::Invalid)
		, FunctionName(NAME_None)
		, Position(FVector2D::ZeroVector)
		, Size(FVector2D::ZeroVector)
		, Color(FLinearColor::Black)
		, ParameterType(EControlRigModelParameterType::None)
		, Text(FString())
	{
	}

	bool IsValid() const
	{
		return Index != INDEX_NONE && NodeType != EControlRigModelNodeType::Invalid && Name != NAME_None;
	}

	bool IsFunction() const
	{
		return NodeType == EControlRigModelNodeType::Function;
	}

	bool IsParameter() const
	{
		return NodeType == EControlRigModelNodeType::Parameter && ParameterType != EControlRigModelParameterType::None;
	}

	bool IsComment() const
	{
		return NodeType == EControlRigModelNodeType::Comment;
	}

	FString GetPinPath(int32 InPinIndex, bool bIncludeNodeName = true) const;

	bool IsMutable() const;
	bool IsBeginExecution() const;
	const UStruct* UnitStruct() const;
	const FControlRigModelPin* FindPin(const FName& InName, bool bLookForInput = true) const;

#if CONTROLRIG_UNDO

	static int32 ArgumentSize();
	void AppendArgumentsForAction(TArray<FString>& InOutArguments) const;
	void ConfigureFromActionArguments(const TArray<FString>& InOutArguments, int32 ArgumentIndex = 0);

#endif
};

// A struct used for passing on information about a rename operation
struct CONTROLRIGDEVELOPER_API FControlRigModelNodeRenameInfo
{
	FName OldName;
	FName NewName;
	FControlRigModelNode Node;

	FControlRigModelNodeRenameInfo()
		: OldName(NAME_None)
		, NewName(NAME_None)
		, Node(FControlRigModelNode())
	{
	}
};

/**
 * The ControlRigModel represents the low level data required to compile
 * a Control Rig. This is similar to the UI aspects of the Control Rig,
 * such as the ControlRigGraph. The model centralizes all of the relevant
 * data in one place.
 * The only class which can mutate the model's data is the ControlRigController.
 */
UCLASS()
class CONTROLRIGDEVELOPER_API UControlRigModel : public UObject
{
	GENERATED_BODY()

public:

	static const FName ValueName;

	UControlRigModel();
	virtual ~UControlRigModel();

	const TArray<FControlRigModelNode>& Nodes() const;
	TArray<FControlRigModelNode> SelectedNodes() const;
	bool IsNodeSelected(const FName& InName) const;
	const TArray<FControlRigModelLink>& Links() const;
	TArray<FControlRigModelPin> LinkedPins(const FControlRigModelPair& InPin) const;
	TArray<FControlRigModelPin> LinkedPins(const FName& InNodeName, const FName& InPinName, bool bLookForInput = true) const;
	TArray<FControlRigModelNode> Parameters() const;

	DECLARE_EVENT_ThreeParams(UControlRigModel, FModifiedEvent, const UControlRigModel*, EControlRigModelNotifType, const void*);

	const FControlRigModelNode* FindNode(const FName& InName) const;
	const FControlRigModelNode* FindNode(int32 InNodeIndex) const;
	const FControlRigModelPin* FindPin(const FName& InNodeName, const FName& InPinName, bool bLookForInput = true) const;
	const FControlRigModelPin* FindPin(const FControlRigModelPair& InPin) const;
	const FControlRigModelPin* FindSubPin(const FControlRigModelPin* InParentPin, const FName& InSubPinName) const;
	const FControlRigModelPin* FindParentPin(const FControlRigModelPin* InSubPin) const;
	const FControlRigModelPin* FindPinFromPath(const FString& InPinPath, bool bLookForInput = true) const;
	const FControlRigModelLink* FindLink(int32 InLinkIndex) const;

	FName GetUniqueNodeName(const FName& InName) const;
	static void SplitPinPath(const FString& InPinPath, FString& OutLeft, FString& OutRight, bool bInSplitForNodeName = true);
	const FControlRigModelPin* GetParentPin(const FControlRigModelPair& InPin) const;
	FString GetPinPath(const FControlRigModelPair& InPin, bool bIncludeNodeName = true) const;

#if WITH_EDITORONLY_DATA

	UPROPERTY()
	int32 ActionCount;

#endif

#if CONTROLRIG_UNDO

	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;

	bool Undo();
	bool Redo();

#endif

private:

	FModifiedEvent& OnModified();

	bool Clear();
	bool IsNodeNameAvailable(const FName& InName) const;
	bool AddNode(const FControlRigModelNode& InNode, bool bUndo = true);
	bool AddParameter(const FName& InName, const FEdGraphPinType& InDataType, EControlRigModelParameterType InParameterType, const FVector2D& InPosition, bool bUndo = true);
	bool AddComment(const FName& InName, const FString& InText, const FVector2D& InPosition, const FVector2D& InSize, const FLinearColor& InColor, bool bUndo = true);
	bool RemoveNode(const FName& InName, bool bUndo = true);
	bool SetNodePosition(const FName& InName, const FVector2D& InPosition, bool bUndo = true);
	bool SetNodeSize(const FName& InName, const FVector2D& InSize, bool bUndo = true);
	bool SetNodeColor(const FName& InName, const FLinearColor& InColor, bool bUndo = true);
	bool SetParameterType(const FName& InName, EControlRigModelParameterType InParameterType, bool bUndo = true);
	bool SetCommentText(const FName& InName, const FString& InText, bool bUndo = true);
	bool RenameNode(const FName& InOldNodeName, const FName& InNewNodeName, bool bUndo = true);
	bool SelectNode(const FName& InName, bool bInSelected);
	bool AreCompatibleTypes(const FEdGraphPinType& A, const FEdGraphPinType& B) const;
	bool PrepareCycleCheckingForPin(int32 InNodeIndex, int32 InPinIndex);
	void ResetCycleCheck();
	bool CanLink(int32 InSourceNodeIndex, int32 InSourcePinIndex, int32 InTargetNodeIndex, int32 InTargetPinIndex, FString* OutFailureReason);
	bool MakeLink(int32 InSourceNodeIndex, int32 InSourcePinIndex, int32 InTargetNodeIndex, int32 InTargetPinIndex, bool bUndo = true);
	bool BreakLink(int32 InSourceNodeIndex, int32 InSourcePinIndex, int32 InTargetNodeIndex, int32 InTargetPinIndex, bool bUndo = true);
	bool BreakLinks(int32 InNodeIndex, int32 InPinIndex, bool bUndo = true);
	bool GetPinDefaultValue(const FName& InNodeName, const FName& InPinName, FString& OutValue) const;
	bool GetPinDefaultValue(const FControlRigModelPair& InPin, FString& OutValue) const;
	bool SetPinDefaultValue(const FName& InNodeName, const FName& InPinName, const FString& InValue, bool bUndo = true);
	bool SetPinDefaultValue(const FControlRigModelPair& InPin, const FString& InValue, bool bUndo = true);

	bool SetPinArraySize(const FControlRigModelPair& InPin, int32 InArraySize, const FString& InDefaultValue, bool bUndo = true);
	bool ExpandPin(const FName& InNodeName, const FName& InPinName, bool bIsInput, bool bInExpanded, bool bUndo = true);
	bool ResendAllNotifications();
	bool ResendAllPinDefaultNotifications();

	static bool ShouldStructBeUnfolded(const UStruct* Struct);
	static FEdGraphPinType GetPinTypeFromField(UProperty* Property);
	static void AddNodePinsForFunction(FControlRigModelNode& Node);
	static void SetNodePinDefaultsForFunction(FControlRigModelNode& Node);
	static void AddNodePinsForParameter(FControlRigModelNode& Node, const FEdGraphPinType& InDataType);
	static void SetNodePinDefaultsForParameter(FControlRigModelNode& Node, const FEdGraphPinType& InDataType);
	static void ConfigurePinFromField(FControlRigModelPin& Pin, UProperty* Property, FControlRigModelNode& Node);
	static int32 AddPinsRecursive(FControlRigModelNode& Node, int32 ParentIndex, const UStruct* Struct, EEdGraphPinDirection PinDirection, int32& LastAddedIndex);
	int32 RemovePinsRecursive(FControlRigModelNode& Node, int32 ParentIndex, bool bUndo = true);
	static void ConfigurePinIndices(FControlRigModelNode& Node);
	static void GetParameterPinTypes(TArray<FEdGraphPinType>& PinTypes);

	TArray<FControlRigModelNode> _Nodes;
	TArray<FControlRigModelLink> _Links;

	bool bIsSelecting;
	TArray<FName> _SelectedNodes;
	FModifiedEvent _ModifiedEvent;

	FControlRigModelPair _CycleCheckSubject;
	TArray<bool> _NodeIsOnCycle;

#if CONTROLRIG_UNDO

	/**
	 * An action performed with additional context / arguments
	 */
	struct FAction
	{
		EControlRigModelNotifType Type;
		FString Title;
		TArray<FString> Arguments;
		TArray<FAction> SubActions;

		FAction()
		{
			Type = EControlRigModelNotifType::Invalid;
		}

		bool IsValid() const
		{
			return (Type != EControlRigModelNotifType::Invalid) != (Arguments.Num() == 0);
		}
	};

	TArray<FAction*> CurrentActions;
	TArray<FAction> UndoActions;
	TArray<FAction> RedoActions;

	void PushAction(const FAction& InAction);
	bool UndoAction(const FAction& InAction);
	bool RedoAction(const FAction& InAction);

#endif

	friend class UControlRigController;
};
