// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMCore/RigVM.h"
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


	void CacheNameLists(URigHierarchy* InHierarchy, const FControlRigDrawContainer* DrawContainer);

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

	UControlRigBlueprint* GetBlueprint() const;
	URigVMGraph* GetModel() const;
	URigVMController* GetController() const;

	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	int32 GetInstructionIndex(UControlRigGraphNode* InNode);

	UPROPERTY()
	FString ModelNodePath;

	UPROPERTY()
	bool bIsFunctionDefinition;

private:

	template<class T>
	void CacheNameListForHierarchy(URigHierarchy* InHierarchy, TArray<TSharedPtr<FString>>& OutNameList)
	{
        TArray<FString> Names;
		for (auto Element : *InHierarchy)
		{
			if(Element->IsA<T>())
			{
				Names.Add(Element->GetName().ToString());
			}
		}
		Names.Sort();

		OutNameList.Reset();
		OutNameList.Add(MakeShared<FString>(FName(NAME_None).ToString()));
		for (const FString& Name : Names)
		{
			OutNameList.Add(MakeShared<FString>(Name));
		}
	}

	template<class T>
	void CacheNameList(const T& ElementList, TArray<TSharedPtr<FString>>& OutNameList)
	{
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

	TMap<URigVMNode*, int32> CachedInstructionIndices;

#endif
#if WITH_EDITORONLY_DATA

	UPROPERTY(transient)
	URigVMController* TemplateController;

#endif
#if WITH_EDITOR

	URigVMController* GetTemplateController();
	void HandleVMCompiledEvent(UBlueprint* InBlueprint, URigVM* InVM);

	friend class UControlRigUnitNodeSpawner;
	friend class UControlRigVariableNodeSpawner;
	friend class UControlRigParameterNodeSpawner;
	friend class UControlRigRerouteNodeSpawner;
	friend class UControlRigBranchNodeSpawner;
	friend class UControlRigIfNodeSpawner;
	friend class UControlRigSelectNodeSpawner;
	friend class UControlRigPrototypeNodeSpawner;
	friend class UControlRigEnumNodeSpawner;
	friend class UControlRigFunctionRefNodeSpawner;

#endif
	friend class UControlRigGraphNode;
	friend class FControlRigEditor;
	friend class SControlRigGraphNode;
	friend class UControlRigBlueprint;
};

