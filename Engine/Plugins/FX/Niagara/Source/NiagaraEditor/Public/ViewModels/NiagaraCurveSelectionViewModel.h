// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UObject/ObjectKey.h"
#include "NiagaraCurveSelectionViewModel.generated.h"

class FNiagaraSystemViewModel;
class UNiagaraDataInterfaceCurveBase;
class UNiagaraSystem;
class UNiagaraNodeFunctionCall;
class UNiagaraScript;
struct FNiagaraEmitterHandle;
struct FRichCurve;

struct NIAGARAEDITOR_API FNiagaraCurveSelectionTreeNodeDataId
{
	FNiagaraCurveSelectionTreeNodeDataId()
		: Object(nullptr)
	{
	}

	bool operator==(const FNiagaraCurveSelectionTreeNodeDataId& Other) const;

	FName UniqueName;
	FGuid Guid;
	UObject* Object;

	static FNiagaraCurveSelectionTreeNodeDataId FromUniqueName(FName UniqueName);
	static FNiagaraCurveSelectionTreeNodeDataId FromGuid(FGuid Guid);
	static FNiagaraCurveSelectionTreeNodeDataId FromObject(UObject* Object);
};

enum class ENiagaraCurveSelectionNodeStyleMode
{
	TopLevelObject,
	Script,
	Module,
	DynamicInput,
	DataInterface,
	CurveComponent
};

struct NIAGARAEDITOR_API FNiagaraCurveSelectionTreeNode : TSharedFromThis<FNiagaraCurveSelectionTreeNode>
{
public:
	FNiagaraCurveSelectionTreeNode();

	const FNiagaraCurveSelectionTreeNodeDataId& GetDataId() const;

	void SetDataId(const FNiagaraCurveSelectionTreeNodeDataId& InDataId);

	FGuid GetNodeUniqueId() const;

	FText GetDisplayName() const;

	void SetDisplayName(FText InDisplayName);

	FText GetSecondDisplayName() const;

	void SetSecondDisplayName(FText InSecondDisplayName);

	ENiagaraCurveSelectionNodeStyleMode GetStyleMode() const;

	FName GetExecutionCategory() const;

	FName GetExecutionSubcategory() const;

	bool GetIsParameter() const;

	void SetStyle(ENiagaraCurveSelectionNodeStyleMode InStyleMode, FName InExecutionCategory, FName InExecutionSubcategory, bool bInIsParameter);

	TSharedPtr<FNiagaraCurveSelectionTreeNode> GetParent() const;

protected:
	void SetParent(TSharedPtr<FNiagaraCurveSelectionTreeNode> InParent);

public:
	const TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>>& GetChildNodes() const;

	void SetChildNodes(const TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>> InChildNodes);

	static TSharedPtr<FNiagaraCurveSelectionTreeNode> FindNodeWithDataId(const TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>>& Nodes, FNiagaraCurveSelectionTreeNodeDataId DataId);

	TWeakObjectPtr<UNiagaraDataInterfaceCurveBase> GetCurveDataInterface() const;

	FRichCurve* GetCurve() const;

	FName GetCurveName() const;

	FLinearColor GetCurveColor() const;

	void SetCurveDataInterface(UNiagaraDataInterfaceCurveBase* InCurveDataInterface);

	void SetCurveData(UNiagaraDataInterfaceCurveBase* InCurveDataInterface, FRichCurve* InCurve, FName InCurveName, FLinearColor InCurveColor);

	const TOptional<FObjectKey>& GetDisplayedObjectKey() const;

	void SetDisplayedObjectKey(FObjectKey InDisplayedObjectKey);

	bool GetShowInTree() const;

	void SetShowInTree(bool bInShouldShowInTree);

	bool GetIsExpanded() const;

	void SetIsExpanded(bool bInIsExpanded);

	const TArray<int32>& GetSortIndices() const;

	void UpdateSortIndices(int32 Index);

	FSimpleMulticastDelegate& GetOnCurveChanged();

	void NotifyCurveChanged();

private:
	FNiagaraCurveSelectionTreeNodeDataId DataId;
	FGuid NodeUniqueId;
	FText DisplayName;
	FText SecondDisplayName;
	TWeakPtr<FNiagaraCurveSelectionTreeNode> ParentWeak;
	TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>> ChildNodes;

	TWeakObjectPtr<UNiagaraDataInterfaceCurveBase> CurveDataInterface;
	FRichCurve* Curve;
	FName CurveName;
	FLinearColor CurveColor;

	ENiagaraCurveSelectionNodeStyleMode StyleMode;
	FName ExecutionCategory;
	FName ExecutionSubcategory;
	bool bIsParameter;
	TOptional<FObjectKey> DisplayedObjectKey;
	bool bShowInTree;
	bool bIsExpanded;
	TArray<int32> SortIndices;

	FSimpleMulticastDelegate OnCurveChangedDelegate;
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraCurveSelectionViewModel : public UObject
{
public:
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRequestSelectNode, FGuid /* NodeIdToSelect */);

public:
	void Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel);

	void Finalize();

	const TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>>& GetRootNodes();

	void FocusAndSelectCurveDataInterface(UNiagaraDataInterfaceCurveBase& CurveDataInterface);

	void Refresh();

	FSimpleMulticastDelegate& OnRefreshed();

	FOnRequestSelectNode& OnRequestSelectNode();

private:
	TSharedRef<FNiagaraCurveSelectionTreeNode> CreateNodeForCurveDataInterface(const FNiagaraCurveSelectionTreeNodeDataId& DataId, UNiagaraDataInterfaceCurveBase& CurveDataInterface, FName DataInterfaceName, bool bIsParameter) const;

	TSharedPtr<FNiagaraCurveSelectionTreeNode> CreateNodeForUserParameters(TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>> OldParentChildNodes, UNiagaraSystem& System) const;

	TSharedPtr<FNiagaraCurveSelectionTreeNode> CreateNodeForFunction(
		const TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>> OldParentChildNodes, UNiagaraNodeFunctionCall& FunctionCallNode,
		FName ExecutionCategory, FName ExecutionSubCategory,
		FName InputName, bool bIsParameterDynamicInput) const;

	TSharedPtr<FNiagaraCurveSelectionTreeNode> CreateNodeForScript(TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>> OldParentChildNodes, UNiagaraScript& Script, FString ScriptDisplayName, FName ExecutionCategory, FName ExecutionSubcategory) const;

	TSharedPtr<FNiagaraCurveSelectionTreeNode> CreateNodeForSystem(TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>> OldParentChildNodes, UNiagaraSystem& System) const;

	TSharedPtr<FNiagaraCurveSelectionTreeNode> CreateNodeForEmitter(TArray<TSharedRef<FNiagaraCurveSelectionTreeNode>> OldParentChildNodes, const FNiagaraEmitterHandle& EmitterHandle) const;

	void DataInterfaceCurveChanged(TWeakObjectPtr<UNiagaraDataInterfaceCurveBase> ChangedCurveDataInterfaceWeak) const;

private:
	TWeakPtr<FNiagaraSystemViewModel> SystemViewModelWeak;

	TSharedPtr<FNiagaraCurveSelectionTreeNode> RootCurveSelectionTreeNode;

	FSimpleMulticastDelegate OnRefreshedDelegate;

	FOnRequestSelectNode OnRequestSelectNodeDelegate;

	mutable bool bHandlingInternalCurveChanged;
};