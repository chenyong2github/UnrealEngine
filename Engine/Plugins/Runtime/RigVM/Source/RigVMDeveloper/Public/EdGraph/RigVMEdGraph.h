// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraph.h"
#include "EdGraph/RigVMEdGraphNode.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMCore/RigVM.h"
#include "RigVMModel/RigVMClient.h"
#include "RigVMEdGraph.generated.h"

class URigVMBlueprint;
class URigVMEdGraphSchema;
class URigVMController;

DECLARE_MULTICAST_DELEGATE_OneParam(FRigVMEdGraphNodeClicked, URigVMEdGraphNode*);

UCLASS()
class RIGVMDEVELOPER_API URigVMEdGraph : public UEdGraph, public IRigVMEditorSideObject
{
	GENERATED_BODY()

public:
	URigVMEdGraph();

	/** IRigVMEditorSideObject interface */
	virtual FRigVMClient* GetRigVMClient() const override;
	virtual FString GetRigVMNodePath() const override;
	virtual void HandleRigVMGraphRenamed(const FString& InOldNodePath, const FString& InNewNodePath) override;

	/** Set up this graph */
	virtual void Initialize(URigVMBlueprint* InBlueprint);

	/** Get the ed graph schema */
	const URigVMEdGraphSchema* GetRigVMEdGraphSchema();

#if WITH_EDITORONLY_DATA
	/** Customize blueprint changes based on backwards compatibility */
	virtual void Serialize(FArchive& Ar) override;
#endif

#if WITH_EDITOR

	bool bSuspendModelNotifications;
	bool bIsTemporaryGraphForCopyPaste;

	UEdGraphNode* FindNodeForModelNodeName(const FName& InModelNodeName, const bool bCacheIfRequired = true);

	URigVMBlueprint* GetBlueprint() const;
	URigVMGraph* GetModel() const;
	URigVMController* GetController() const;
	bool IsRootGraph() const { return GetRootGraph() == this; }
	const URigVMEdGraph* GetRootGraph() const;

	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);
	void ConsumeQueuedNotifications();

	int32 GetInstructionIndex(const URigVMEdGraphNode* InNode, bool bAsInput);

	UPROPERTY()
	FString ModelNodePath;

	UPROPERTY()
	bool bIsFunctionDefinition;

private:

	bool bIsSelecting;

	FRigVMEdGraphNodeClicked OnGraphNodeClicked;

	TMap<URigVMNode*, TPair<int32, int32>> CachedInstructionIndices;

	void RemoveNode(UEdGraphNode* InNode);

#endif
#if WITH_EDITORONLY_DATA

	UPROPERTY(transient)
	TObjectPtr<URigVMController> TemplateController;

#endif
#if WITH_EDITOR

	URigVMController* GetTemplateController();

protected:
	void HandleVMCompiledEvent(UObject* InCompiledObject, URigVM* InVM);

private:
	TMap<FName, UEdGraphNode*> ModelNodePathToEdNode;
	mutable TWeakObjectPtr<URigVMGraph> CachedModelGraph;

	/*
	friend class UControlRigUnitNodeSpawner;
	friend class UControlRigVariableNodeSpawner;
	friend class UControlRigParameterNodeSpawner;
	friend class UControlRigBranchNodeSpawner;
	friend class UControlRigIfNodeSpawner;
	friend class UControlRigSelectNodeSpawner;
	friend class UControlRigTemplateNodeSpawner;
	friend class UControlRigEnumNodeSpawner;
	friend class UControlRigFunctionRefNodeSpawner;
	friend class UControlRigArrayNodeSpawner;
	friend class UControlRigInvokeEntryNodeSpawner;
	*/

#endif
	friend class URigVMEdGraphNode;
	friend class URigVMBlueprint;
	friend class UControlRigGraphNode;
	friend class FControlRigEditor;
	friend class SControlRigGraphNode;
	friend class URigVMBlueprint;
};
