// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMGraph.h"
#include "RigVMModel/Nodes/RigVMStructNode.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"
#include "RigVMModel/Nodes/RigVMParameterNode.h"
#include "RigVMModel/Nodes/RigVMCommentNode.h"
#include "RigVMModel/Nodes/RigVMRerouteNode.h"
#include "RigVMModel/Nodes/RigVMBranchNode.h"
#include "RigVMModel/Nodes/RigVMIfNode.h"
#include "RigVMModel/Nodes/RigVMSelectNode.h"
#include "RigVMModel/Nodes/RigVMPrototypeNode.h"
#include "RigVMModel/Nodes/RigVMEnumNode.h"
#include "RigVMModel/Nodes/RigVMCollapseNode.h"
#include "RigVMController.generated.h"

class URigVMActionStack;

DECLARE_DELEGATE_RetVal_OneParam(bool, FRigVMController_ShouldStructUnfoldDelegate, const UStruct*)
DECLARE_DELEGATE_RetVal(TArray<FRigVMExternalVariable>, FRigVMController_GetExternalVariablesDelegate)

/**
 * The Controller is the sole authority to perform changes
 * on the Graph. The Controller itself is stateless.
 * The Controller offers a Modified event to subscribe to 
 * for user interface views - so they can be informed about
 * any change that's happening within the Graph.
 * The Controller routes all changes through the Graph itself,
 * so you can have N Controllers performing edits on 1 Graph,
 * and N Views subscribing to 1 Controller.
 * In Python you can also subscribe to this event to be 
 * able to react to topological changes of the Graph there.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMController : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	// Default constructor
	URigVMController();

	// Default destructor
	~URigVMController();

	// Returns the currently edited Graph of this controller.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMGraph* GetGraph() const;

	// Sets the currently edited Graph of this controller.
	// This causes a GraphChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	void SetGraph(URigVMGraph* InGraph);

	// The Modified event used to subscribe to changes
	// happening within the Graph. This is broadcasted to 
	// for any change happening - not only the changes 
	// performed by this Controller - so it can be used
	// for UI Views to react accordingly.
	FRigVMGraphModifiedEvent& OnModified();

	// Submits an event to the graph for broadcasting.
	void Notify(ERigVMGraphNotifType InNotifType, UObject* InSubject);

	// Resends all notifications
	void ResendAllNotifications();

	// Enables or disables the error reporting of this Controller.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	void EnableReporting(bool bEnabled = true) { bReportWarningsAndErrors = bEnabled; }

	// Returns true if reporting is enabled
	UFUNCTION(BlueprintPure, Category = RigVMController)
	bool IsReportingEnabled() const { return bReportWarningsAndErrors; }

#if WITH_EDITOR
	// Note: The functions below are scoped with WITH_EDITOR since we are considering
	// to move this code into the runtime in the future. Right now there's a dependency
	// on the metadata of the USTRUCT - which is only available in the editor.

	// Adds a Function / Struct Node to the edited Graph.
	// StructNode represent a RIGVM_METHOD declaration on a USTRUCT.
	// This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMStructNode* AddStructNode(UScriptStruct* InScriptStruct, const FName& InMethodName, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Function / Struct Node to the edited Graph given its struct object path name.
	// StructNode represent a RIGVM_METHOD declaration on a USTRUCT.
	// This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMStructNode* AddStructNodeFromStructPath(const FString& InScriptStructPath, const FName& InMethodName, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Variable Node to the edited Graph.
	// Variables represent local work state for the function and
	// can be read from and written to. 
	// This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMVariableNode* AddVariableNode(const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Variable Node to the edited Graph given a struct object path name.
	// Variables represent local work state for the function and
	// can be read from (bIsGetter == true) or written to (bIsGetter == false). 
	// This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMVariableNode* AddVariableNodeFromObjectPath(const FName& InVariableName, const FString& InCPPType, const FString& InCPPTypeObjectPath, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Refreshes the variable node with the new data
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	void RefreshVariableNode(const FName& InNodeName, const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo);

	// Removes all nodes related to a given variable
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	void OnExternalVariableRemoved(const FName& InVarName, bool bSetupUndoRedo);

	// Renames the variable name in all relevant nodes
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	void OnExternalVariableRenamed(const FName& InOldVarName, const FName& InNewVarName, bool bSetupUndoRedo);

	// Changes the data type of all nodes matching a given variable name
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	void OnExternalVariableTypeChanged(const FName& InVarName, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo);

	// Refreshes the variable node with the new data
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMVariableNode* ReplaceParameterNodeWithVariable(const FName& InNodeName, const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo);

	// Adds a Parameter Node to the edited Graph.
	// Parameters represent input or output arguments to the Graph / Function.
	// Input Parameters are constant values / literals.
	// This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMParameterNode* AddParameterNode(const FName& InParameterName, const FString& InCPPType, UObject* InCPPTypeObject, bool bIsInput, const FString& InDefaultValue, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Parameter Node to the edited Graph given a struct object path name.
	// Parameters represent input or output arguments to the Graph / Function.
	// Input Parameters are constant values / literals.
	// This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMParameterNode* AddParameterNodeFromObjectPath(const FName& InParameterName, const FString& InCPPType, const FString& InCPPTypeObjectPath, bool bIsInput, const FString& InDefaultValue, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Comment Node to the edited Graph.
	// Comments can be used to annotate the Graph.
	// This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMCommentNode* AddCommentNode(const FString& InCommentText, const FVector2D& InPosition = FVector2D::ZeroVector, const FVector2D& InSize = FVector2D(400.f, 300.f), const FLinearColor& InColor = FLinearColor::Black, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Reroute Node on an existing Link to the edited Graph.
	// Reroute Nodes can be used to visually improve the data flow,
	// they don't require any additional memory though and are purely
	// cosmetic. This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMRerouteNode* AddRerouteNodeOnLink(URigVMLink* InLink, bool bShowAsFullNode, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Reroute Node on an existing Link to the edited Graph given the Link's string representation.
	// Reroute Nodes can be used to visually improve the data flow,
	// they don't require any additional memory though and are purely
	// cosmetic. This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMRerouteNode* AddRerouteNodeOnLinkPath(const FString& InLinkPinPathRepresentation, bool bShowAsFullNode, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Reroute Node on an existing Pin to the editor Graph.
	// Reroute Nodes can be used to visually improve the data flow,
	// they don't require any additional memory though and are purely
	// cosmetic. This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMRerouteNode* AddRerouteNodeOnPin(const FString& InPinPath, bool bAsInput, bool bShowAsFullNode, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a free Reroute Node
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMRerouteNode* AddFreeRerouteNode(bool bShowAsFullNode, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bIsConstant, const FName& InCustomWidgetName, const FString& InDefaultValue, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a branch node to the graph.
	// Branch nodes can be used to split the execution of into multiple branches,
	// allowing to drive behavior by logic.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMBranchNode* AddBranchNode(const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds an if node to the graph.
	// If nodes can be used to pick between two values based on a condition.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMIfNode* AddIfNode(const FString& InCPPType, const FName& InCPPTypeObjectPath, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a select node to the graph.
	// Select nodes can be used to pick between multiple values based on an index.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMSelectNode* AddSelectNode(const FString& InCPPType, const FName& InCPPTypeObjectPath, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a prototype node to the graph.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMPrototypeNode* AddPrototypeNode(const FName& InNotation, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Function / Struct Node to the edited Graph as an injected node
	// StructNode represent a RIGVM_METHOD declaration on a USTRUCT.
	// This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMInjectionInfo* AddInjectedNode(const FString& InPinPath, bool bAsInput, UScriptStruct* InScriptStruct, const FName& InMethodName, const FName& InInputPinName, const FName& InOutputPinName, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Adds a Function / Struct Node to the edited Graph as an injected node
	// StructNode represent a RIGVM_METHOD declaration on a USTRUCT.
	// This causes a NodeAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMInjectionInfo* AddInjectedNodeFromStructPath(const FString& InPinPath, bool bAsInput, const FString& InScriptStructPath, const FName& InMethodName, const FName& InInputPinName, const FName& InOutputPinName, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Ejects the last injected node on a pin
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMNode* EjectNodeFromPin(const FString& InPinPath, bool bSetupUndoRedo = true);

	// Adds an enum node to the graph
	// Enum nodes can be used to represent constant enum values within the graph
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMEnumNode* AddEnumNode(const FName& InCPPTypeObjectPath, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Un-does the last action on the stack.
	// Note: This should really only be used for unit tests,
	// use the GEditor's main Undo method instead.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool Undo();

	// Re-does the last action on the stack.
	// Note: This should really only be used for unit tests,
	// use the GEditor's main Undo method instead.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool Redo();

	// Opens an undo bracket / scoped transaction for
	// a series of actions to be performed as one step on the 
	// Undo stack. This is primarily useful for Python.
	// This causes a UndoBracketOpened modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool OpenUndoBracket(const FString& InTitle);

	// Closes an undo bracket / scoped transaction.
	// This is primarily useful for Python.
	// This causes a UndoBracketClosed modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool CloseUndoBracket();

	// Cancels an undo bracket / scoped transaction.
	// This is primarily useful for Python.
	// This causes a UndoBracketCanceled modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool CancelUndoBracket();

	// Exports the given nodes as text
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	FString ExportNodesToText(const TArray<FName>& InNodeNames);

	// Exports the selected nodes as text
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	FString ExportSelectedNodesToText();

	// Exports the given nodes as text
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool CanImportNodesFromText(const FString& InText);

	// Exports the given nodes as text
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	TArray<FName> ImportNodesFromText(const FString& InText, bool bSetupUndoRedo = true);

	// Returns a unique name
	static FName GetUniqueName(const FName& InName, TFunction<bool(const FName&)> IsNameAvailableFunction);

	// Turns a series of nodes into a Collapse node
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	URigVMCollapseNode* CollapseNodes(const TArray<FName>& InNodeNames, const FString& InCollapseNodeName = TEXT(""), bool bSetupUndoRedo = true);

	// Turns a library node into its contained nodes
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	TArray<URigVMNode*> ExpandLibraryNode(const FName& InNodeName, bool bSetupUndoRedo = true);

#endif

	// Removes a node from the graph
	// This causes a NodeRemoved modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool RemoveNode(URigVMNode* InNode, bool bSetupUndoRedo = true, bool bRecursive = false);

	// Removes a node from the graph given the node's name.
	// This causes a NodeRemoved modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool RemoveNodeByName(const FName& InNodeName, bool bSetupUndoRedo = true, bool bRecursive = false);

	// Selects a single node in the graph.
	// This causes a NodeSelected / NodeDeselected modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SelectNode(URigVMNode* InNode, bool bSelect = true, bool bSetupUndoRedo = true);

	// Selects a single node in the graph by name.
	// This causes a NodeSelected / NodeDeselected modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SelectNodeByName(const FName& InNodeName, bool bSelect = true, bool bSetupUndoRedo = true);

	// Deselects all currently selected nodes in the graph.
	// This might cause several NodeDeselected modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool ClearNodeSelection(bool bSetupUndoRedo = true);

	// Selects the nodes given the selection
	// This might cause several NodeDeselected modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetNodeSelection(const TArray<FName>& InNodeNames, bool bSetupUndoRedo = true);

	// Sets the position of a node in the graph.
	// This causes a NodePositionChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetNodePosition(URigVMNode* InNode, const FVector2D& InPosition, bool bSetupUndoRedo = true, bool bMergeUndoAction = false);

	// Sets the position of a node in the graph by name.
	// This causes a NodePositionChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetNodePositionByName(const FName& InNodeName, const FVector2D& InPosition, bool bSetupUndoRedo = true, bool bMergeUndoAction = false);

	// Sets the size of a node in the graph.
	// This causes a NodeSizeChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetNodeSize(URigVMNode* InNode, const FVector2D& InSize, bool bSetupUndoRedo = true, bool bMergeUndoAction = false);

	// Sets the size of a node in the graph by name.
	// This causes a NodeSizeChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetNodeSizeByName(const FName& InNodeName, const FVector2D& InSize, bool bSetupUndoRedo = true, bool bMergeUndoAction = false);

	// Sets the color of a node in the graph.
	// This causes a NodeColorChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetNodeColor(URigVMNode* InNode, const FLinearColor& InColor, bool bSetupUndoRedo = true, bool bMergeUndoAction = false);

	// Sets the color of a node in the graph by name.
	// This causes a NodeColorChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetNodeColorByName(const FName& InNodeName, const FLinearColor& InColor, bool bSetupUndoRedo = true, bool bMergeUndoAction = false);

	// Sets the comment text of a comment node in the graph.
	// This causes a CommentTextChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetCommentText(URigVMNode* InNode, const FString& InCommentText, bool bSetupUndoRedo = true);

	// Sets the comment text of a comment node in the graph by name.
	// This causes a CommentTextChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetCommentTextByName(const FName& InNodeName, const FString& InCommentText, bool bSetupUndoRedo = true);

	// Sets the compactness of a reroute node in the graph.
	// This causes a RerouteCompactnessChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetRerouteCompactness(URigVMNode* InNode, bool bShowAsFullNode, bool bSetupUndoRedo = true);

	// Sets the compactness of a reroute node in the graph by name.
	// This causes a RerouteCompactnessChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetRerouteCompactnessByName(const FName& InNodeName, bool bShowAsFullNode, bool bSetupUndoRedo = true);

	// Renames a variable in the graph.
	// This causes a VariableRenamed modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool RenameVariable(const FName& InOldName, const FName& InNewName, bool bSetupUndoRedo = true);

	// Renames a parameter in the graph.
	// This causes a ParameterRenamed modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool RenameParameter(const FName& InOldName, const FName& InNewName, bool bSetupUndoRedo = true);

	// Sets the pin to be expanded or not
	// This causes a PinExpansionChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetPinExpansion(const FString& InPinPath, bool bIsExpanded, bool bSetupUndoRedo = true);

	// Sets the pin to be watched (or not)
	// This causes a PinWatchedChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetPinIsWatched(const FString& InPinPath, bool bIsWatched, bool bSetupUndoRedo = true);

	// Returns the default value of a pin given its pinpath.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	FString GetPinDefaultValue(const FString& InPinPath);

	// Sets the default value of a pin given its pinpath.
	// This causes a PinDefaultValueChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetPinDefaultValue(const FString& InPinPath, const FString& InDefaultValue, bool bResizeArrays = true, bool bSetupUndoRedo = true, bool bMergeUndoAction = false);

	// Resets the default value of a pin given its pinpath.
	// This causes a PinDefaultValueChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool ResetPinDefaultValue(const FString& InPinPath, bool bSetupUndoRedo = true);

	// Adds an array element pin to the end of an array pin.
	// This causes a PinArraySizeChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	FString AddArrayPin(const FString& InArrayPinPath, const FString& InDefaultValue = TEXT(""), bool bSetupUndoRedo = true);

	// Duplicates an array element pin.
	// This causes a PinArraySizeChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	FString DuplicateArrayPin(const FString& InArrayElementPinPath, bool bSetupUndoRedo = true);

	// Inserts an array element pin into an array pin.
	// This causes a PinArraySizeChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	FString InsertArrayPin(const FString& InArrayPinPath, int32 InIndex = -1, const FString& InDefaultValue = TEXT(""), bool bSetupUndoRedo = true);

	// Removes an array element pin from an array pin.
	// This causes a PinArraySizeChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool RemoveArrayPin(const FString& InArrayElementPinPath, bool bSetupUndoRedo = true);

	// Removes all (but one) array element pin from an array pin.
	// This causes a PinArraySizeChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool ClearArrayPin(const FString& InArrayPinPath, bool bSetupUndoRedo = true);

	// Sets the size of the array pin
	// This causes a PinArraySizeChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool SetArrayPinSize(const FString& InArrayPinPath, int32 InSize, const FString& InDefaultValue = TEXT(""), bool bSetupUndoRedo = true);

	// Binds a pin to a variable (or removes the binding given NAME_None)
	// This causes a PinBoundVariableChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool BindPinToVariable(const FString& InPinPath, const FString& InNewBoundVariablePath, bool bSetupUndoRedo = true);

	// Removes the binging of a pin to a variable
	// This causes a PinBoundVariableChanged modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool UnbindPinFromVariable(const FString& InPinPath, bool bSetupUndoRedo = true);

	// Turns a variable node into one or more bindings
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool MakeBindingsFromVariableNode(const FName& InNodeName, bool bSetupUndoRedo = true);

	// Turns a binding to a variable node
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool MakeVariableNodeFromBinding(const FString& InPinPath, const FVector2D& InNodePosition = FVector2D::ZeroVector, bool bSetupUndoRedo = true);

	// Promotes a pin to a variable
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool PromotePinToVariable(const FString& InPinPath, bool bCreateVariableNode, const FVector2D& InNodePosition = FVector2D::ZeroVector, bool bSetupUndoRedo = true);

	// Adds a link to the graph.
	// This causes a LinkAdded modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool AddLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bSetupUndoRedo = true);

	// Removes a link from the graph.
	// This causes a LinkRemoved modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool BreakLink(const FString& InOutputPinPath, const FString& InInputPinPath, bool bSetupUndoRedo = true);

	// Removes all links on a given pin from the graph.
	// This might cause multiple LinkRemoved modified event.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	bool BreakAllLinks(const FString& InPinPath, bool bAsInput = true, bool bSetupUndoRedo = true);

	// Sets the execute context struct type to use
	void SetExecuteContextStruct(UStruct* InExecuteContextStruct);

	// A delegate that can be set to change the struct unfolding behaviour
	FRigVMController_ShouldStructUnfoldDelegate UnfoldStructDelegate;

	// A delegate to retrieve the list of external variables
	FRigVMController_GetExternalVariablesDelegate GetExternalVariablesDelegate;

	int32 DetachLinksFromPinObjects(const TArray<URigVMLink*>* InLinks = nullptr);
	int32 ReattachLinksToPinObjects(bool bFollowCoreRedirectors = false, const TArray<URigVMLink*>* InLinks = nullptr);
	void AddPinRedirector(bool bInput, bool bOutput, const FString& OldPinPath, const FString& NewPinPath);

	// Removes nodes which went stale.
	void RemoveStaleNodes();

#if WITH_EDITOR

	bool ShouldRedirectPin(UScriptStruct* InOwningStruct, const FString& InOldRelativePinPath, FString& InOutNewRelativePinPath) const;
	bool ShouldRedirectPin(const FString& InOldPinPath, FString& InOutNewPinPath) const;

	void RepopulatePinsOnNode(URigVMNode* InNode, bool bFollowCoreRedirectors = true);
#endif

	FRigVMStructNodeCreatedContext& GetStructNodeCreatedContext() { return StructNodeCreatedContext; }

	// Wires the struct node delegates to the default controller delegates.
	// this is used only within the Control Rig Editor currently.
	void SetupDefaultStructNodeDelegates(TDelegate<FName(FRigVMExternalVariable)> InCreateExternalVariableDelegate);
	void ResetStructNodeDelegates();

private:

	UPROPERTY(BlueprintReadOnly, Category = RigVMController, meta = (ScriptName = "ModifiedEvent", AllowPrivateAccess = "true"))
	FRigVMGraphModifiedDynamicEvent ModifiedEventDynamic;

	FRigVMGraphModifiedEvent ModifiedEventStatic;
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	FString GetValidNodeName(const FString& InPrefix);
	bool IsValidGraph();
	bool IsValidNodeForGraph(URigVMNode* InNode);
	bool IsValidPinForGraph(URigVMPin* InPin);
	bool IsValidLinkForGraph(URigVMLink* InLink);
	void AddPinsForStruct(UStruct* InStruct, URigVMNode* InNode, URigVMPin* InParentPin, ERigVMPinDirection InPinDirection, const FString& InDefaultValue, bool bAutoExpandArrays);
	void AddPinsForArray(FArrayProperty* InArrayProperty, URigVMNode* InNode, URigVMPin* InParentPin, ERigVMPinDirection InPinDirection, const TArray<FString>& InDefaultValues, bool bAutoExpandArrays);
	void ConfigurePinFromProperty(FProperty* InProperty, URigVMPin* InOutPin, ERigVMPinDirection InPinDirection = ERigVMPinDirection::Invalid);
	void ConfigurePinFromPin(URigVMPin* InOutPin, URigVMPin* InPin);
	virtual bool ShouldStructBeUnfolded(const UStruct* InStruct);
	virtual bool ShouldPinBeUnfolded(URigVMPin* InPin);
	void SetPinDefaultValue(URigVMPin* InPin, const FString& InDefaultValue, bool bResizeArrays, bool bSetupUndoRedo, bool bMergeUndoAction);
	bool ResetPinDefaultValue(URigVMPin* InPin, bool bSetupUndoRedo);
	static TArray<FString> SplitDefaultValue(const FString& InDefaultValue);
	URigVMPin* InsertArrayPin(URigVMPin* ArrayPin, int32 InIndex, const FString& InDefaultValue, bool bSetupUndoRedo);
	bool RemovePin(URigVMPin* InPinToRemove, bool bSetupUndoRedo);
	FProperty* FindPropertyForPin(const FString& InPinPath);
	bool BindPinToVariable(URigVMPin* InPin, const FString& InNewBoundVariablePath, bool bSetupUndoRedo);
	bool MakeBindingsFromVariableNode(URigVMVariableNode* InNode, bool bSetupUndoRedo);
	bool PromotePinToVariable(URigVMPin* InPin, bool bCreateVariableNode, const FVector2D& InNodePosition, bool bSetupUndoRedo);
	bool AddLink(URigVMPin* OutputPin, URigVMPin* InputPin, bool bSetupUndoRedo);
	bool BreakLink(URigVMPin* OutputPin, URigVMPin* InputPin, bool bSetupUndoRedo);
	bool BreakAllLinks(URigVMPin* Pin, bool bAsInput, bool bSetupUndoRedo);
	void BreakAllLinksRecursive(URigVMPin* Pin, bool bAsInput, bool bTowardsParent, bool bSetupUndoRedo);
	void UpdateRerouteNodeAfterChangingLinks(URigVMPin* PinChanged, bool bSetupUndoRedo = true);
	bool SetPinExpansion(URigVMPin* InPin, bool bIsExpanded, bool bSetupUndoRedo = true);
	void ExpandPinRecursively(URigVMPin* InPin, bool bSetupUndoRedo);
	bool SetPinIsWatched(URigVMPin* InPin, bool bIsWatched, bool bSetupUndoRedo);
	bool SetVariableName(URigVMVariableNode* InVariableNode, const FName& InVariableName, bool bSetupUndoRedo);
	bool SetParameterName(URigVMParameterNode* InParameterNode, const FName& InParameterName, bool bSetupUndoRedo);
	static void ForEveryPinRecursively(URigVMPin* InPin, TFunction<void(URigVMPin*)> OnEachPinFunction);
	static void ForEveryPinRecursively(URigVMNode* InNode, TFunction<void(URigVMPin*)> OnEachPinFunction);
	URigVMCollapseNode* CollapseNodes(const TArray<URigVMNode*>& InNodes, const FString& InCollapseNodeName, bool bSetupUndoRedo);
	TArray<URigVMNode*> ExpandLibraryNode(URigVMLibraryNode* InNode, bool bSetupUndoRedo);
	void RefreshFunctionPins(URigVMNode* InNode);

	struct FPinState
	{
		FString DefaultValue;
		FString BoundVariable;
		bool bIsExpanded;
		TArray<URigVMInjectionInfo*> InjectionInfos;
	};

	TMap<FString, FString> GetRedirectedPinPaths(URigVMNode* InNode) const;
	FPinState GetPinState(URigVMPin* InPin) const;
	TMap<FString, FPinState> GetPinStates(URigVMNode* InNode) const;
	void ApplyPinState(URigVMPin* InPin, const FPinState& InPinState);
	void ApplyPinStates(URigVMNode* InNode, const TMap<FString, FPinState>& InPinStates, const TMap<FString, FString>& InRedirectedPinPaths = TMap<FString, FString>());

	void ReportWarning(const FString& InMessage);
	void ReportError(const FString& InMessage);

	template <typename FmtType, typename... Types>
	void ReportWarningf(const FmtType& Fmt, Types... Args)
	{
		ReportWarning(FString::Printf(Fmt, Args...));
	}

	template <typename FmtType, typename... Types>
	void ReportErrorf(const FmtType& Fmt, Types... Args)
	{
		ReportError(FString::Printf(Fmt, Args...));
	}

	static FLinearColor GetColorFromMetadata(const FString& InMetadata);
	static void CreateDefaultValueForStructIfRequired(UScriptStruct* InStruct, FString& OutDefaultValue);
	static void PostProcessDefaultValue(URigVMPin* Pin, FString& OutDefaultValue);

	void PotentiallyResolvePrototypeNode(URigVMPrototypeNode* InNode, bool bSetupUndoRedo);
	void PotentiallyResolvePrototypeNode(URigVMPrototypeNode* InNode, bool bSetupUndoRedo, TArray<URigVMNode*>& NodesVisited);
	bool ChangePinType(const FString& InPinPath, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bSetupUndoRedo);
	bool ChangePinType(URigVMPin* InPin, const FString& InCPPType, const FName& InCPPTypeObjectPath, bool bSetupUndoRedo);

#if WITH_EDITOR
	void RewireLinks(URigVMPin* OldPin, URigVMPin* NewPin, bool bAsInput, bool bSetupUndoRedo, TArray<URigVMLink*> InLinks = TArray<URigVMLink*>());
#endif

	void DestroyObject(UObject* InObjectToDestroy);

	FRigVMExternalVariable GetExternalVariableByName(const FName& InExternalVariableName);
	TArray<FRigVMExternalVariable> GetExternalVariables();

	UPROPERTY(transient)
	URigVMGraph* Graph;

	UPROPERTY()
	UStruct* ExecuteContextStruct;

	UPROPERTY(transient)
	URigVMActionStack* ActionStack;

	bool bSuspendNotifications;
	bool bReportWarningsAndErrors;
	bool bIgnoreRerouteCompactnessChanges;

	// temporary maps used for pin redirection
	// only valid between Detach & ReattachLinksToPinObjects
	TMap<FString, FString> InputPinRedirectors;
	TMap<FString, FString> OutputPinRedirectors;

	struct FControlRigStructPinRedirectorKey
	{
		FControlRigStructPinRedirectorKey()
		{
		}

		FControlRigStructPinRedirectorKey(UScriptStruct* InScriptStruct, const FString& InPinPathInNode)
		: Struct(InScriptStruct)
		, PinPathInNode(InPinPathInNode)
		{
		}

		friend FORCEINLINE uint32 GetTypeHash(const FControlRigStructPinRedirectorKey& Cache)
		{
			return HashCombine(GetTypeHash(Cache.Struct), GetTypeHash(Cache.PinPathInNode));
		}

		FORCEINLINE bool operator ==(const FControlRigStructPinRedirectorKey& Other) const
		{
			return Struct == Other.Struct && PinPathInNode == Other.PinPathInNode;
		}

		FORCEINLINE bool operator !=(const FControlRigStructPinRedirectorKey& Other) const
		{
			return Struct != Other.Struct || PinPathInNode != Other.PinPathInNode;
		}

		UScriptStruct* Struct;
		FString PinPathInNode;
	};

	static TMap<FControlRigStructPinRedirectorKey, FString> PinPathCoreRedirectors;
	FCriticalSection PinPathCoreRedirectorsLock;

	FRigVMStructNodeCreatedContext StructNodeCreatedContext;

	friend class URigVMGraph;
	friend class URigVMActionStack;
	friend class URigVMCompiler;
	friend struct FRigVMControllerObjectFactory;
	friend struct FRigVMAddRerouteNodeAction;
	friend struct FRigVMChangePinTypeAction;
	friend class FRigVMParserAST;
};

