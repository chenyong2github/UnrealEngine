// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Rigs/RigHierarchyContainer.h"
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

	void CacheNameLists(const FRigHierarchyContainer* Container);

	const TArray<TSharedPtr<FString>>& GetBoneNameList() const;
	const TArray<TSharedPtr<FString>>& GetControlNameList() const;
	const TArray<TSharedPtr<FString>>& GetSpaceNameList() const;
	const TArray<TSharedPtr<FString>>& GetCurveNameList() const;

	bool bSuspendModelNotifications;
	bool bIsTemporaryGraphForCopyPaste;

	UEdGraphNode* FindNodeFromPropertyName(const FName& InPropertyName);

private:

	void HandleModelModified(const UControlRigModel* InModel, EControlRigModelNotifType InType, const void* InPayload);

	template<class T>
	void CacheNameList(const T& Hierarchy, TArray<TSharedPtr<FString>>& OutNameList)
	{
		DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

		TArray<FString> Names;
		for (auto Element : Hierarchy)
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

	TArray<UControlRigGraphNode*> FoundHierarchyRefVariableNodes;
	TArray<UControlRigGraphNode*> FoundHierarchyRefMutableNodes;
	TMap<UControlRigGraphNode*, TArray<UControlRigGraphNode*>> FoundHierarchyRefConnections;

	TArray<TSharedPtr<FString>> BoneNameList;
	TArray<TSharedPtr<FString>> ControlNameList;
	TArray<TSharedPtr<FString>> SpaceNameList;
	TArray<TSharedPtr<FString>> CurveNameList;
#endif
};

