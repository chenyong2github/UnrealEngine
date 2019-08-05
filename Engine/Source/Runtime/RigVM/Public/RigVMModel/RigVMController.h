// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMGraph.h"
#include "RigVMModel/RigVMStructNode.h"
#include "RigVMController.generated.h"

UCLASS()
class RIGVM_API URigVMController : public UObject
{
	GENERATED_BODY()

public:

	~URigVMController();

	URigVMGraph* GetGraph() const;
	void SetGraph(URigVMGraph* InGraph);

	FRigVMGraphModifiedEvent& OnModified();

	URigVMStructNode* AddStructNode(UScriptStruct* InScriptStruct, const FName& InMethodName, const FVector2D& InPosition = FVector2D::ZeroVector, bool bUndo = true);

	bool RemoveNode(URigVMNode* InNode, bool bUndo = true);
	bool RemoveNode(const FName& InNodeName, bool bUndo = true);

	bool SelectNode(URigVMNode* InNode, bool bSelect = true, bool bUndo = true);
	bool SelectNode(const FName& InNodeName, bool bSelect = true, bool bUndo = true);
	bool ClearNodeSelection(bool bUndo = true);

private:

	FRigVMGraphModifiedEvent ModifiedEvent;
	void Notify(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	bool IsValidNodeForGraph(URigVMNode* InNode);

	UPROPERTY()
	URigVMGraph* Graph;
};

