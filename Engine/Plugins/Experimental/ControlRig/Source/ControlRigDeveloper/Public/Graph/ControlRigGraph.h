// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Rigs/RigHierarchyContainer.h"
#include "RigVMModel/RigVMGraph.h"
#include "Drawing/ControlRigDrawContainer.h"
#include "ControlRigGraph.generated.h"

class UControlRigBlueprint;
class UControlRigGraphSchema;
class UControlRig;
class URigVMController;
struct FRigCurveContainer;

DECLARE_MULTICAST_DELEGATE_OneParam(FControlRigGraphNodeClicked, UControlRigGraphNode*);

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


	void CacheNameLists(const FRigHierarchyContainer* HierarchyContainer, const FControlRigDrawContainer* DrawContainer);

	const TArray<TSharedPtr<FString>>& GetBoneNameList(URigVMPin* InPin = nullptr) const;
	const TArray<TSharedPtr<FString>>& GetControlNameList(URigVMPin* InPin = nullptr) const;
	const TArray<TSharedPtr<FString>>& GetSpaceNameList(URigVMPin* InPin = nullptr) const;
	const TArray<TSharedPtr<FString>>& GetCurveNameList(URigVMPin* InPin = nullptr) const;
	const TArray<TSharedPtr<FString>>& GetElementNameList(URigVMPin* InPin = nullptr) const;
	const TArray<TSharedPtr<FString>>& GetElementNameList(ERigElementType InElementType) const;
	const TArray<TSharedPtr<FString>>& GetDrawingNameList(URigVMPin* InPin = nullptr) const;

	bool bSuspendModelNotifications;
	bool bIsTemporaryGraphForCopyPaste;

	UEdGraphNode* FindNodeForModelNodeName(const FName& InModelNodeName);

private:

	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	template<class T>
	void CacheNameList(const T& ElementList, TArray<TSharedPtr<FString>>& OutNameList)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		TArray<FString> Names;
		for (auto Element : ElementList)
		{
			Names.Add(Element.Name.ToString());
		}
		Names.Sort();

		OutNameList.Reset();
		OutNameList.Add(MakeShared<FString>(FName(NAME_None).ToString()));
		for (const FString& Name : Names)
		{
			OutNameList.Add(MakeShared<FString>(Name));
		}
	}


	TArray<TSharedPtr<FString>> BoneNameList;
	TArray<TSharedPtr<FString>> ControlNameList;
	TArray<TSharedPtr<FString>> SpaceNameList;
	TArray<TSharedPtr<FString>> CurveNameList;
	TArray<TSharedPtr<FString>> DrawingNameList;

	bool bIsSelecting;

	FControlRigGraphNodeClicked OnGraphNodeClicked;

#endif
#if WITH_EDITORONLY_DATA

	UPROPERTY(transient)
	URigVMGraph* TemplateModel;

	UPROPERTY(transient)
	URigVMController* TemplateController;

#endif
#if WITH_EDITOR

	URigVMController* GetTemplateController();

	friend class UControlRigUnitNodeSpawner;
	friend class UControlRigVariableNodeSpawner;
	friend class UControlRigParameterNodeSpawner;
	friend class UControlRigRerouteNodeSpawner;
	friend class UControlRigBranchNodeSpawner;
	friend class UControlRigIfNodeSpawner;
	friend class UControlRigSelectNodeSpawner;
	friend class UControlRigPrototypeNodeSpawner;
	friend class UControlRigEnumNodeSpawner;
#endif
	friend class UControlRigGraphNode;
	friend class FControlRigEditor;
	friend class SControlRigGraphNode;
};

