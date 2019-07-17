// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Hierarchy.h"
#include "ControlRigModel.h"
#include "ControlRigGraph.generated.h"

class UControlRigBlueprint;
class UControlRigGraphSchema;
class UControlRig;
struct FRigCurveContainer;

UCLASS()
class CONTROLRIGDEVELOPER_API UControlRigGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	UControlRigGraph();

	/** Set up this graph */
	void Initialize(UControlRigBlueprint* InBlueprint);

	/** Get the skeleton graph schema */
	const UControlRigGraphSchema* GetControlRigGraphSchema();

#if WITH_EDITORONLY_DATA
	/** Customize blueprint changes based on backwards compatibility */
	virtual void Serialize(FArchive& Ar) override;
#endif
#if WITH_EDITOR

	virtual void PostLoad() override;
	void OnBlueprintCompiledPostLoad(UBlueprint*);
	FDelegateHandle BlueprintOnCompiledHandle;
	void CacheBoneNameList(const FRigHierarchy& Hierarchy);
	const TArray<TSharedPtr<FString>>& GetBoneNameList() const;
	void CacheCurveNameList(const FRigCurveContainer& Container);
	const TArray<TSharedPtr<FString>>& GetCurveNameList() const;

	bool bSuspendModelNotifications;
	bool bIsTemporaryGraphForCopyPaste;

	UEdGraphNode* FindNodeFromPropertyName(const FName& InPropertyName);

private:

	void HandleModelModified(const UControlRigModel* InModel, EControlRigModelNotifType InType, const void* InPayload);

	TArray<UControlRigGraphNode*> FoundHierarchyRefVariableNodes;
	TArray<UControlRigGraphNode*> FoundHierarchyRefMutableNodes;
	TMap<UControlRigGraphNode*, TArray<UControlRigGraphNode*>> FoundHierarchyRefConnections;
	TArray<TSharedPtr<FString>> BoneNameList;
	TArray<TSharedPtr<FString>> CurveNameList;
#endif
};

