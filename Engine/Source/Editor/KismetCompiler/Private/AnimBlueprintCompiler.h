// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetCompiler.h"
#include "Animation/AnimNodeBase.h"
#include "AnimGraphNode_Base.h"
#include "KismetCompilerModule.h"

class UAnimationGraphSchema;
class UAnimGraphNode_SaveCachedPose;
class UAnimGraphNode_StateMachineBase;
class UAnimGraphNode_StateResult;
class UAnimGraphNode_CustomProperty;

class UAnimGraphNode_UseCachedPose;
class UAnimStateTransitionNode;
class UK2Node_CallFunction;

//
// Forward declarations.
//
class UAnimGraphNode_SaveCachedPose;
class UAnimGraphNode_UseCachedPose;
class UAnimGraphNode_LinkedInputPose;
class UAnimGraphNode_LinkedAnimGraphBase;
class UAnimGraphNode_LinkedAnimGraph;
class UAnimGraphNode_Root;

class FStructProperty;
class UBlueprintGeneratedClass;
struct FPoseLinkMappingRecord;

//////////////////////////////////////////////////////////////////////////
// FAnimBlueprintCompilerContext
class FAnimBlueprintCompilerContext : public FKismetCompilerContext
{
protected:
	typedef FKismetCompilerContext Super;
public:
	FAnimBlueprintCompilerContext(UAnimBlueprint* SourceSketch, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions);
	virtual ~FAnimBlueprintCompilerContext();

	virtual void PostCompile() override;

protected:
	// Implementation of FKismetCompilerContext interface
	virtual void CreateClassVariablesFromBlueprint() override;
	virtual UEdGraphSchema_K2* CreateSchema() override;
	virtual void MergeUbergraphPagesIn(UEdGraph* Ubergraph) override;
	virtual void ProcessOneFunctionGraph(UEdGraph* SourceGraph, bool bInternalFunction = false) override;
	virtual void CreateFunctionList() override;
	virtual void SpawnNewClass(const FString& NewClassName) override;
	virtual void OnNewClassSet(UBlueprintGeneratedClass* ClassToUse) override;
	virtual void OnPostCDOCompiled() override;
	virtual void CopyTermDefaultsToDefaultObject(UObject* DefaultObject) override;
	virtual void PostCompileDiagnostics() override;
	virtual void EnsureProperGeneratedClass(UClass*& TargetClass) override;
	virtual void CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOldCDO) override;
	virtual void FinishCompilingClass(UClass* Class) override;
	virtual void PrecompileFunction(FKismetFunctionContext& Context, EInternalCompilerFlags InternalFlags) override;
	virtual void SetCalculatedMetaDataAndFlags(UFunction* Function, UK2Node_FunctionEntry* EntryNode, const UEdGraphSchema_K2* Schema ) override;
	// End of FKismetCompilerContext interface

protected:
	typedef TArray<UEdGraphPin*> UEdGraphPinArray;

protected:
	/** Record of a single copy operation */
	struct FPropertyCopyRecord
	{
		FPropertyCopyRecord(UEdGraphPin* InDestPin, FProperty* InDestProperty, int32 InDestArrayIndex)
			: DestPin(InDestPin)
			, DestProperty(InDestProperty)
			, DestArrayIndex(InDestArrayIndex)
			, SourcePropertyName(NAME_None)
			, SourceSubStructPropertyName(NAME_None)
			, Operation(EPostCopyOperation::None)
		{}

		bool IsFastPath() const
		{
			return DestProperty != nullptr && SourcePropertyName != NAME_None;
		}

		void InvalidateFastPath()
		{
			SourcePropertyName = NAME_None;
			SourceSubStructPropertyName = NAME_None;
		}

		void ValidateFastPath(UClass* InCompiledClass);

		/** The destination pin we are copying to */
		UEdGraphPin* DestPin;

		/** The destination property we are copying to (on an animation node) */
		FProperty* DestProperty;

		/** The array index we use if the destination property is an array */
		int32 DestArrayIndex;

		/** The source property we are copying from (on an anim instance) */
		FName SourcePropertyName;

		/** The source sub-struct property we are copying from (if the source property is a struct property) */
		FName SourceSubStructPropertyName;

		/** Any operation we want to perform post-copy on the destination data */
		EPostCopyOperation Operation;
	};

	// Wireup record for a single anim node property (which might be an array)
	struct FAnimNodeSinglePropertyHandler
	{
		/** Copy records */
		TArray<FPropertyCopyRecord> CopyRecords;

		// If the anim instance is the container target instead of the node.
		bool bInstanceIsTarget;

		FAnimNodeSinglePropertyHandler()
			: bInstanceIsTarget(false)
		{
		}
	};

	// Record for a property that was exposed as a pin, but wasn't wired up (just a literal)
	struct FEffectiveConstantRecord
	{
	public:
		// The node variable that the handler is in
		class FStructProperty* NodeVariableProperty;

		// The property within the struct to set
		class FProperty* ConstantProperty;

		// The array index if ConstantProperty is an array property, or INDEX_NONE otherwise
		int32 ArrayIndex;

		// The pin to pull the DefaultValue/DefaultObject from
		UEdGraphPin* LiteralSourcePin;

		FEffectiveConstantRecord()
			: NodeVariableProperty(NULL)
			, ConstantProperty(NULL)
			, ArrayIndex(INDEX_NONE)
			, LiteralSourcePin(NULL)
		{
		}

		FEffectiveConstantRecord(FStructProperty* ContainingNodeProperty, UEdGraphPin* SourcePin, FProperty* SourcePinProperty, int32 SourceArrayIndex)
			: NodeVariableProperty(ContainingNodeProperty)
			, ConstantProperty(SourcePinProperty)
			, ArrayIndex(SourceArrayIndex)
			, LiteralSourcePin(SourcePin)
		{
		}

		bool Apply(UObject* Object);
	};

	/** BP execution handler for Anim node - possibly */
	struct FEvaluationHandlerRecord
	{
	public:

		// The node variable that the handler is in
		FStructProperty* NodeVariableProperty;

		// The specific evaluation handler inside the specified node
		int32 EvaluationHandlerIdx;

		// Whether or not our serviced properties are actually on the anim node 
		bool bServicesNodeProperties;

		// Whether or not our serviced properties are actually on the instance instead of the node
		bool bServicesInstanceProperties;

		// Set of properties serviced by this handler (Map from property name to the record for that property)
		TMap<FName, FAnimNodeSinglePropertyHandler> ServicedProperties;

		// The resulting function name
		FName HandlerFunctionName;

	public:

		FEvaluationHandlerRecord()
			: NodeVariableProperty(nullptr)
			, EvaluationHandlerIdx(INDEX_NONE)
			, bServicesNodeProperties(false)
			, bServicesInstanceProperties(false)
			, HandlerFunctionName(NAME_None)
		{}

		bool IsFastPath() const
		{
			for(TMap<FName, FAnimNodeSinglePropertyHandler>::TConstIterator It(ServicedProperties); It; ++It)
			{
				const FAnimNodeSinglePropertyHandler& AnimNodeSinglePropertyHandler = It.Value();
				for (const FPropertyCopyRecord& CopyRecord : AnimNodeSinglePropertyHandler.CopyRecords)
				{
					if (!CopyRecord.IsFastPath())
					{
						return false;
					}
				}
			}

			return true;
		}

		bool IsValid() const
		{
			return NodeVariableProperty != nullptr;
		}

		void PatchFunctionNameAndCopyRecordsInto(FExposedValueHandler& Handler) const;

		void RegisterPin(UEdGraphPin* DestPin, FProperty* AssociatedProperty, int32 AssociatedPropertyArrayIndex);

		FStructProperty* GetHandlerNodeProperty() const { return NodeVariableProperty; }

		void BuildFastPathCopyRecords();

		void ValidateFastPath(UClass* InCompiledClass);

	private:

		bool CheckForVariableGet(FPropertyCopyRecord& CopyRecord, UEdGraphPin* DestPin);

		bool CheckForLogicalNot(FPropertyCopyRecord& CopyRecord, UEdGraphPin* DestPin);

		bool CheckForStructMemberAccess(FPropertyCopyRecord& CopyRecord, UEdGraphPin* DestPin);

		bool CheckForMemberOnlyAccess(FPropertyCopyRecord& CopyRecord, UEdGraphPin* DestPin);
	};

	// State machines may get processed before their inner graphs, so their node index needs to be patched up later
	// This structure records pending fixups.
	struct FStateRootNodeIndexFixup
	{
	public:
		int32 MachineIndex;
		int32 StateIndex;
		UAnimGraphNode_StateResult* ResultNode;

	public:
		FStateRootNodeIndexFixup(int32 InMachineIndex, int32 InStateIndex, UAnimGraphNode_StateResult* InResultNode)
			: MachineIndex(InMachineIndex)
			, StateIndex(InStateIndex)
			, ResultNode(InResultNode)
		{
		}
	};

protected:
	UAnimBlueprintGeneratedClass* NewAnimBlueprintClass;
	UAnimBlueprint* AnimBlueprint;

	UAnimationGraphSchema* AnimSchema;

	// Map of allocated v3 nodes that are members of the class
	TMap<class UAnimGraphNode_Base*, FProperty*> AllocatedAnimNodes;
	TMap<FProperty*, class UAnimGraphNode_Base*> AllocatedNodePropertiesToNodes;
	TMap<int32, FProperty*> AllocatedPropertiesByIndex;

	// Map of true source objects (user edited ones) to the cloned ones that are actually compiled
	TMap<class UAnimGraphNode_Base*, UAnimGraphNode_Base*> SourceNodeToProcessedNodeMap;

	// Index of the nodes (must match up with the runtime discovery process of nodes, which runs thru the property chain)
	int32 AllocateNodeIndexCounter;
	TMap<class UAnimGraphNode_Base*, int32> AllocatedAnimNodeIndices;

	// Map from pose link LinkID address
	//@TODO: Bad structure for a list of these
	TArray<FPoseLinkMappingRecord> ValidPoseLinkList;

	// List of successfully created evaluation handlers
	TArray<FEvaluationHandlerRecord> ValidEvaluationHandlerList;

	// List of animation node literals (values exposed as pins but never wired up) that need to be pushed into the CDO
	TArray<FEffectiveConstantRecord> ValidAnimNodePinConstants;

	// Map of cache name to encountered save cached pose nodes
	TMap<FString, UAnimGraphNode_SaveCachedPose*> SaveCachedPoseNodes;

	// List of getter node's we've found so the auto-wire can be deferred till after state machine compilation
	TArray<class UK2Node_AnimGetter*> FoundGetterNodes;

	// Set of used handler function names
	TSet<FName> HandlerFunctionNames;

	// Stub graphs we generated for animation graph functions
	TArray<UEdGraph*> GeneratedStubGraphs;

	// True if any parent class is also generated from an animation blueprint
	bool bIsDerivedAnimBlueprint;
private:
	int32 FindOrAddNotify(FAnimNotifyEvent& Notify);

	UK2Node_CallFunction* SpawnCallAnimInstanceFunction(UEdGraphNode* SourceNode, FName FunctionName);

	// Creates an evaluation handler for an FExposedValue property in an animation node
	void CreateEvaluationHandler(UAnimGraphNode_Base* VisualAnimNode, FEvaluationHandlerRecord& Record);

	// Prunes any nodes that aren't reachable via a pose link
	void PruneIsolatedAnimationNodes(const TArray<UAnimGraphNode_Base*>& RootSet, TArray<UAnimGraphNode_Base*>& GraphNodes);

	// Compiles one animation node
	void ProcessAnimationNode(UAnimGraphNode_Base* VisualAnimNode);

	// Compiles one state machine
	void ProcessStateMachine(UAnimGraphNode_StateMachineBase* StateMachineInstance);

	// Compiles one use cached pose instance
	void ProcessUseCachedPose(UAnimGraphNode_UseCachedPose* UseCachedPose);

	// Compiles one custom property node
	void ProcessCustomPropertyNode(UAnimGraphNode_CustomProperty* CustomPropNode);

	// Compiles one linked anim graph node
	void ProcessLinkedAnimGraph(UAnimGraphNode_LinkedAnimGraphBase* InLinkedAnimGraph, bool bCheckForCycles);

	// Compiles one linked input pose
	void ProcessLinkedInputPose(UAnimGraphNode_LinkedInputPose* InLinkedInputPose);

	// Compiles one root node
	void ProcessRoot(UAnimGraphNode_Root* Root);

	// Compiles one state result node
	void ProcessStateResult(UAnimGraphNode_StateResult* StateResult);

	// Traverses linked anim graph links looking for slot names and state machine names, returning their count in a name map
	typedef TMap<FName, int32> NameToCountMap;
	void GetDuplicatedSlotAndStateNames(UAnimGraphNode_LinkedAnimGraphBase* InLinkedAnimGraph, NameToCountMap& OutStateMachineNameToCountMap, NameToCountMap& OutSlotNameToCountMap);

	// Compiles an entire animation graph
	void ProcessAllAnimationNodes();

	// Convert transition getters into a function call/etc...
	void ProcessTransitionGetter(class UK2Node_TransitionRuleGetter* Getter, UAnimStateTransitionNode* TransitionNode);

	//
	void ProcessAnimationNodesGivenRoot(TArray<UAnimGraphNode_Base*>& AnimNodeList, const TArray<UAnimGraphNode_Base*>& RootSet);

	// Builds the update order list for saved pose nodes in this blueprint
	void BuildCachedPoseNodeUpdateOrder();

	// Traverses a graph to collect save pose nodes starting at InRootNode, then processes each node
	void CachePoseNodeOrdering_StartNewTraversal(UAnimGraphNode_Base* InRootNode, TArray<UAnimGraphNode_SaveCachedPose*> &OrderedSavePoseNodes, TArray<UAnimGraphNode_Base*> VisitedRootNodes);

	// Traverses a graph to collect save pose nodes starting at InAnimGraphNode, does NOT process saved pose nodes afterwards
	void CachePoseNodeOrdering_TraverseInternal(UAnimGraphNode_Base* InAnimGraphNode, TArray<UAnimGraphNode_SaveCachedPose*> &OrderedSavePoseNodes);

	// Gets all anim graph nodes that are piped into the provided node (traverses input pins)
	void GetLinkedAnimNodes(UAnimGraphNode_Base* InGraphNode, TArray<UAnimGraphNode_Base*>& LinkedAnimNodes);
	void GetLinkedAnimNodes_TraversePin(UEdGraphPin* InPin, TArray<UAnimGraphNode_Base*>& LinkedAnimNodes);
	void GetLinkedAnimNodes_ProcessAnimNode(UAnimGraphNode_Base* AnimNode, TArray<UAnimGraphNode_Base*>& LinkedAnimNodes);

	// Automatically fill in parameters for the specified Getter node
	void AutoWireAnimGetter(class UK2Node_AnimGetter* Getter, UAnimStateTransitionNode* InTransitionNode);

	// This function does the following steps:
	//   Clones the nodes in the specified source graph
	//   Merges them into the ConsolidatedEventGraph
	//   Processes any animation nodes
	//   Returns the index of the processed cloned version of SourceRootNode
	//	 If supplied, will also return an array of all cloned nodes
	int32 ExpandGraphAndProcessNodes(UEdGraph* SourceGraph, UAnimGraphNode_Base* SourceRootNode, UAnimStateTransitionNode* TransitionNode = NULL, TArray<UEdGraphNode*>* ClonedNodes = NULL);

	// Returns the allocation index of the specified node, processing it if it was pending
	int32 GetAllocationIndexOfNode(UAnimGraphNode_Base* VisualAnimNode);

	// Create transient stub functions for each anim graph we are compiling
	void CreateAnimGraphStubFunctions();

	// Clean up transient stub functions
	void DestroyAnimGraphStubFunctions();
};

