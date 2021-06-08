// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetCompiler.h"
#include "Animation/AnimNodeBase.h"
#include "AnimGraphNode_Base.h"
#include "KismetCompilerModule.h"
#include "AnimBlueprintCompilerHandlerCollection.h"
#include "IAnimBlueprintCompilerHandlerCollection.h"
#include "IAnimBlueprintCompilerCreationContext.h"

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
struct FAnimGraphNodePropertyBinding;
class FAnimBlueprintCompilerContext;

//////////////////////////////////////////////////////////////////////////
// FAnimBlueprintCompilerContext
class FAnimBlueprintCompilerContext : public FKismetCompilerContext
{
	friend class FAnimBlueprintCompilerCreationContext;
	friend class FAnimBlueprintCompilationContext;
	friend class FAnimBlueprintVariableCreationContext;
	friend class FAnimBlueprintCompilationBracketContext;
	friend class FAnimBlueprintPostExpansionStepContext;
	friend class FAnimBlueprintCopyTermDefaultsContext;

protected:
	typedef FKismetCompilerContext Super;
public:
	FAnimBlueprintCompilerContext(UAnimBlueprint* SourceSketch, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions);
	virtual ~FAnimBlueprintCompilerContext();

protected:
	// Implementation of FKismetCompilerContext interface
	virtual void CreateClassVariablesFromBlueprint() override;
	virtual UEdGraphSchema_K2* CreateSchema() override;
	virtual void MergeUbergraphPagesIn(UEdGraph* Ubergraph) override;
	virtual void ProcessOneFunctionGraph(UEdGraph* SourceGraph, bool bInternalFunction = false) override;
	virtual void SpawnNewClass(const FString& NewClassName) override;
	virtual void OnNewClassSet(UBlueprintGeneratedClass* ClassToUse) override;
	virtual void OnPostCDOCompiled() override;
	virtual void CopyTermDefaultsToDefaultObject(UObject* DefaultObject) override;
	virtual void PostCompile() override;
	virtual void PostCompileDiagnostics() override;
	virtual void EnsureProperGeneratedClass(UClass*& TargetClass) override;
	virtual void CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOldCDO) override;
	virtual void FinishCompilingClass(UClass* Class) override;
	virtual void PrecompileFunction(FKismetFunctionContext& Context, EInternalCompilerFlags InternalFlags) override;
	virtual void SetCalculatedMetaDataAndFlags(UFunction* Function, UK2Node_FunctionEntry* EntryNode, const UEdGraphSchema_K2* Schema ) override;
	virtual bool ShouldForceKeepNode(const UEdGraphNode* Node) const override;
	virtual void PostExpansionStep(const UEdGraph* Graph) override;
	// End of FKismetCompilerContext interface

protected:
	typedef TArray<UEdGraphPin*> UEdGraphPinArray;

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

	// Stub graphs we generated for animation graph functions
	TArray<UEdGraph*> GeneratedStubGraphs;

	// True if any parent class is also generated from an animation blueprint
	bool bIsDerivedAnimBlueprint;

	// Handlers that this context is hosting
	FAnimBlueprintCompilerHandlerCollection AnimBlueprintCompilerHandlerCollection;

	// Graph schema classes that this compiler is aware of - they will skip default function processing
	TArray<TSubclassOf<UEdGraphSchema>> KnownGraphSchemas;

	/** Delegate fired when the class starts compiling. The class may be new or recycled. */
	FOnStartCompilingClass OnStartCompilingClassDelegate;

	/** Delegate fired before all animation nodes are processed */
	FOnPreProcessAnimationNodes OnPreProcessAnimationNodesDelegate;

	/** Delegate fired after all animation nodes are processed */
	FOnPostProcessAnimationNodes OnPostProcessAnimationNodesDelegate;

	/** Delegate fired post- graph expansion */
	FOnPostExpansionStep OnPostExpansionStepDelegate;

	/** Delegate fired when the class has finished compiling */
	FOnFinishCompilingClass OnFinishCompilingClassDelegate;

	/** Delegate fired when data is being copied to the CDO */
	FOnCopyTermDefaultsToDefaultObject OnCopyTermDefaultsToDefaultObjectDelegate;

	// Expose compile options to handlers
	using FKismetCompilerContext::CompileOptions;

private:
	// Run a function on the passed-in graph and each subgraph of it
	void ForAllSubGraphs(UEdGraph* InGraph, TFunctionRef<void(UEdGraph*)> InPerGraphFunction);

	// Prunes any nodes that aren't reachable via a pose link
	void PruneIsolatedAnimationNodes(const TArray<UAnimGraphNode_Base*>& RootSet, TArray<UAnimGraphNode_Base*>& GraphNodes);

	// Compiles one animation node
	void ProcessAnimationNode(UAnimGraphNode_Base* VisualAnimNode);

	// Compiles one root node
	void ProcessRoot(UAnimGraphNode_Root* Root);

	// Compiles an entire animation graph
	void ProcessAllAnimationNodes();

	// Processes all the supplied anim nodes
	void ProcessAnimationNodes(TArray<UAnimGraphNode_Base*>& AnimNodeList);

	// Gets all anim graph nodes that are piped into the provided node (traverses input pins)
	void GetLinkedAnimNodes(UAnimGraphNode_Base* InGraphNode, TArray<UAnimGraphNode_Base*>& LinkedAnimNodes) const;
	void GetLinkedAnimNodes_TraversePin(UEdGraphPin* InPin, TArray<UAnimGraphNode_Base*>& LinkedAnimNodes) const;
	void GetLinkedAnimNodes_ProcessAnimNode(UAnimGraphNode_Base* AnimNode, TArray<UAnimGraphNode_Base*>& LinkedAnimNodes) const;

	// Returns the allocation index of the specified node, processing it if it was pending
	int32 GetAllocationIndexOfNode(UAnimGraphNode_Base* VisualAnimNode);

	// Create transient stub functions for each anim graph we are compiling
	void CreateAnimGraphStubFunctions();

	// Clean up transient stub functions
	void DestroyAnimGraphStubFunctions();

	// Expands split pins for a graph
	void ExpandSplitPins(UEdGraph* InGraph);

	// Create a uniquely named variable corresponding to an object in the current class
	FProperty* CreateUniqueVariable(UObject* InForObject, const FEdGraphPinType& Type);
};

