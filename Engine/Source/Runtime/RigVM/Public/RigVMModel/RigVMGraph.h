// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMNode.h"
#include "RigVMNotifications.h"
#include "RigVMGraph.generated.h"

UCLASS()
class RIGVM_API URigVMGraph : public UObject
{
	GENERATED_BODY()

public:

	const TArray<URigVMNode*>& GetNodes() const;
	URigVMNode* FindNode(const FName& InNodeName);

	bool IsNodeSelected(const FName& InNodeName) const;
	const TArray<FName>& GetSelectNodes() const;

	FRigVMGraphModifiedEvent& OnModified();

private:

	FRigVMGraphModifiedEvent ModifiedEvent;
	void Notify(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	UPROPERTY()
	TArray<URigVMNode*> Nodes;

	UPROPERTY()
	TArray<FName> SelectedNodes;

	bool IsNameAvailable(const FString& InName);

	friend class URigVMController;
};

