// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Widgets/Views/STableRow.h"
#include "NiagaraStackEntry.generated.h"

class FNiagaraSystemViewModel;
class FNiagaraEmitterViewModel;
class FNiagaraScriptViewModel;
class UNiagaraStackEditorData;
class UNiagaraStackErrorItem;

UENUM()
enum class EStackIssueSeverity : uint8
{
	Error = 0,
	Warning, 
	Info
};

class FNiagaraStackEntryDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FNiagaraStackEntryDragDropOp, FDecoratedDragDropOp)

public:
	FNiagaraStackEntryDragDropOp(TArray<UNiagaraStackEntry*> InDraggedEntries)
	{
		DraggedEntries = InDraggedEntries;
	}

	const TArray<UNiagaraStackEntry*> GetDraggedEntries() const
	{
		return DraggedEntries;
	}

private:
	TArray<UNiagaraStackEntry*> DraggedEntries;
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackEntry : public UObject
{
	GENERATED_BODY()

public:
	enum class EDragOptions
	{
		Copy,
		None
	};

	enum class EDropOptions
	{
		Overview,
		None
	};

	struct FDropRequest
	{
		FDropRequest(TSharedRef<const FDragDropOperation> InDragDropOperation, EItemDropZone InDropZone, EDragOptions InDragOptions, EDropOptions InDropOptions)
			: DragDropOperation(InDragDropOperation)
			, DropZone(InDropZone)
			, DragOptions(InDragOptions)
			, DropOptions(InDropOptions)
		{
		}

		const TSharedRef<const FDragDropOperation> DragDropOperation;
		const EItemDropZone DropZone;
		const EDragOptions DragOptions;
		const EDropOptions DropOptions;
	};

	struct FDropRequestResponse
	{
		FDropRequestResponse(TOptional<EItemDropZone> InDropZone, FText InDropMessage = FText())
			: DropZone(InDropZone)
			, DropMessage(InDropMessage)
		{
		}

		const TOptional<EItemDropZone> DropZone;
		const FText DropMessage;
	};

	DECLARE_MULTICAST_DELEGATE(FOnStructureChanged);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDataObjectModified, UObject*);
	DECLARE_MULTICAST_DELEGATE(FOnRequestFullRefresh);
	DECLARE_MULTICAST_DELEGATE(FOnRequestFullRefreshDeferred);
	DECLARE_DELEGATE_RetVal_TwoParams(TOptional<FDropRequestResponse>, FOnRequestDrop, const UNiagaraStackEntry& /*TargetEntry*/, const FDropRequest& /*DropRequest*/);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnFilterChild, const UNiagaraStackEntry&);
	DECLARE_DELEGATE(FStackIssueFixDelegate);

public:
	struct NIAGARAEDITOR_API FExecutionCategoryNames
	{
		static const FName System;
		static const FName Emitter;
		static const FName Particle;
		static const FName Render;
	};

	struct NIAGARAEDITOR_API FExecutionSubcategoryNames
	{
		static const FName Settings;
		static const FName Spawn;
		static const FName Update;
		static const FName Event;
		static const FName Render;
	};

	enum class EStackRowStyle
	{
		None,
		GroupHeader,
		GroupFooter,
		ItemHeader,
		ItemContent,
		ItemContentAdvanced,
		ItemFooter,
		ItemCategory,
		StackIssue
	};

	struct FRequiredEntryData
	{
		FRequiredEntryData(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel, TSharedPtr<FNiagaraEmitterViewModel> InEmitterViewModel, FName InExecutionCategoryName, FName InExecutionSubcategoryName, UNiagaraStackEditorData& InStackEditorData)
			: SystemViewModel(InSystemViewModel)
			, EmitterViewModel(InEmitterViewModel)
			, ExecutionCategoryName(InExecutionCategoryName)
			, ExecutionSubcategoryName(InExecutionSubcategoryName)
			, StackEditorData(&InStackEditorData)
		{
		}

		const TSharedRef<FNiagaraSystemViewModel> SystemViewModel;
		const TSharedPtr<FNiagaraEmitterViewModel> EmitterViewModel;
		const FName ExecutionCategoryName;
		const FName ExecutionSubcategoryName;
		UNiagaraStackEditorData* const StackEditorData;
	};

	struct FStackSearchItem
	{
		FName Key;
		FText Value;
		
		inline bool operator==(FStackSearchItem Item) {
			return (Item.Key == Key 
				&& Item.Value.ToString() == Value.ToString());
		}
	};

	// stack issue stuff
	struct FStackIssueFix
	{
		FStackIssueFix();

		FStackIssueFix(FText InDescription, FStackIssueFixDelegate InFixDelegate);

		bool IsValid() const;

		const FText& GetDescription() const;

		void SetFixDelegate(const FStackIssueFixDelegate& InFixDelegate);

		const FStackIssueFixDelegate& GetFixDelegate() const;

		const FString& GetUniqueIdentifier() const;

	private:
		FText Description;
		FStackIssueFixDelegate FixDelegate;
		FString UniqueIdentifier;
	};

	struct NIAGARAEDITOR_API FStackIssue
	{
		FStackIssue();

		FStackIssue(EStackIssueSeverity InSeverity, FText InShortDescription, FText InLongDescription, FString InStackEditorDataKey, bool bInCanBeDismissed, const TArray<FStackIssueFix>& InFixes);

		FStackIssue(EStackIssueSeverity InSeverity, FText InShortDescription, FText InLongDescription, FString InStackEditorDataKey, bool bInCanBeDismissed, FStackIssueFix InFix);

		FStackIssue(EStackIssueSeverity InSeverity, FText InShortDescription, FText InLongDescription, FString InStackEditorDataKey, bool bInCanBeDismissed);

		bool IsValid();

		EStackIssueSeverity GetSeverity() const;

		const FText& GetShortDescription() const;

		const FText& GetLongDescription() const;

		const FString& GetUniqueIdentifier() const;

		bool GetCanBeDismissed() const;

		const TArray<FStackIssueFix>& GetFixes() const;

		void InsertFix(int32 InsertionIdx, const FStackIssueFix& Fix);

	private:
		EStackIssueSeverity Severity;
		FText ShortDescription;
		FText LongDescription;
		FString UniqueIdentifier;
		bool bCanBeDismissed;
		TArray<FStackIssueFix> Fixes;
	};

public:
	UNiagaraStackEntry();

	void Initialize(FRequiredEntryData InRequiredEntryData, FString InStackEditorDataKey);

	void Finalize();

	bool IsFinalized() const;

	virtual FText GetDisplayName() const;

	virtual UObject* GetDisplayedObject() const;

	UNiagaraStackEditorData& GetStackEditorData() const;

	FString GetStackEditorDataKey() const;

	virtual FText GetTooltipText() const;

	virtual bool GetCanExpand() const;

	virtual bool IsExpandedByDefault() const;

	bool GetIsExpanded() const;

	// Calling this doesn't broadcast structure change automatically due to the expense of synchronizing
	// expanded state with the tree which is done to prevent items being expanded on tick.
	void SetIsExpanded(bool bInExpanded);

	void SetIsExpanded_Recursive(bool bInExpanded);

	virtual bool GetIsEnabled() const;

	bool GetOwnerIsEnabled() const;

	bool GetIsEnabledAndOwnerIsEnabled() const { return GetIsEnabled() && GetOwnerIsEnabled(); }

	FName GetExecutionCategoryName() const;

	FName GetExecutionSubcategoryName() const;

	virtual EStackRowStyle GetStackRowStyle() const;

	int32 GetIndentLevel() const;

	virtual bool GetShouldShowInStack() const;

	void GetFilteredChildren(TArray<UNiagaraStackEntry*>& OutFilteredChildren) const;

	void GetUnfilteredChildren(TArray<UNiagaraStackEntry*>& OutUnfilteredChildren) const;

	template<typename T>
	void GetUnfilteredChildrenOfType(TArray<T*>& OutUnfilteredChldrenOfType) const
	{
		TArray<UNiagaraStackEntry*> UnfilteredChildren;
		GetUnfilteredChildren(UnfilteredChildren);
		for (UNiagaraStackEntry* UnfilteredChild : UnfilteredChildren)
		{
			T* UnfilteredChildOfType = Cast<T>(UnfilteredChild);
			if (UnfilteredChildOfType != nullptr)
			{
				OutUnfilteredChldrenOfType.Add(UnfilteredChildOfType);
			}
		}
	}

	FOnStructureChanged& OnStructureChanged();

	FOnDataObjectModified& OnDataObjectModified();

	FOnRequestFullRefresh& OnRequestFullRefresh();

	const FOnRequestFullRefresh& OnRequestFullRefreshDeferred() const;

	FOnRequestFullRefresh& OnRequestFullRefreshDeferred();

	void RefreshChildren();

	void RefreshChildrenDeferred();

	FDelegateHandle AddChildFilter(FOnFilterChild ChildFilter);
	void RemoveChildFilter(FDelegateHandle FilterHandle);

	TSharedRef<FNiagaraSystemViewModel> GetSystemViewModel() const;
	TSharedPtr<FNiagaraEmitterViewModel> GetEmitterViewModel() const;

	template<typename ChildType, typename PredicateType>
	static ChildType* FindCurrentChildOfTypeByPredicate(const TArray<UNiagaraStackEntry*>& CurrentChildren, PredicateType Predicate)
	{
		for (UNiagaraStackEntry* CurrentChild : CurrentChildren)
		{
			ChildType* TypedCurrentChild = Cast<ChildType>(CurrentChild);
			if (TypedCurrentChild != nullptr && Predicate(TypedCurrentChild))
			{
				return TypedCurrentChild;
			}
		}
		return nullptr;
	}

	virtual void GetSearchItems(TArray<FStackSearchItem>& SearchItems) const;

	virtual UObject* GetExternalAsset() const;

	virtual bool CanDrag() const;

	TOptional<FDropRequestResponse> CanDrop(const FDropRequest& DropRequest);

	TOptional<FDropRequestResponse> Drop(const FDropRequest& DropRequest);

	void SetOnRequestCanDrop(FOnRequestDrop InOnRequestCanDrop);

	void SetOnRequestDrop(FOnRequestDrop InOnRequestCanDrop);

	const bool GetIsSearchResult() const;

	void SetIsSearchResult(bool bInIsSearchResult);

	bool HasBaseEmitter() const;

	bool HasIssuesOrAnyChildHasIssues() const;

	int32 GetTotalNumberOfInfoIssues() const;

	int32 GetTotalNumberOfWarningIssues() const;

	int32 GetTotalNumberOfErrorIssues() const;

	const TArray<FStackIssue>& GetIssues() const;

	const TArray<UNiagaraStackEntry*>& GetAllChildrenWithIssues() const;

	virtual bool SupportsCut() const { return false; }

	virtual bool TestCanCutWithMessage(FText& OutMessage) const { return false; }

	virtual void Cut() { }

	virtual bool SupportsCopy() const { return false; }

	virtual bool TestCanCopyWithMessage(FText& OutMessage) const { return false; }

	virtual void Copy() const { }

	virtual bool SupportsPaste() const { return false; }

	virtual bool TestCanPasteWithMessage(FText& OutMessage) const { return false; }

	virtual void Paste() { }

protected:
	virtual void BeginDestroy() override;

	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues);

	virtual void PostRefreshChildrenInternal();

	FRequiredEntryData CreateDefaultChildRequiredData() const;

	virtual int32 GetChildIndentLevel() const;

	virtual TOptional<FDropRequestResponse> CanDropInternal(const FDropRequest& DropRequest);

	virtual TOptional<FDropRequestResponse> DropInternal(const FDropRequest& DropRequest);

	virtual TOptional<FDropRequestResponse> ChildRequestCanDropInternal(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

	virtual TOptional<FDropRequestResponse> ChildRequestDropInternal(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

	virtual void ChlildStructureChangedInternal();

	virtual void FinalizeInternal();

private:
	void ChildStructureChanged();
	
	void ChildDataObjectModified(UObject* ChangedObject);

	void ChildRequestFullRefresh();

	void ChildRequestFullRefreshDeferred();

	TOptional<FDropRequestResponse> ChildRequestCanDrop(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

	TOptional<FDropRequestResponse> ChildRequestDrop(const UNiagaraStackEntry& TargetChild, const FDropRequest& DropRequest);

	void RefreshStackErrorChildren();

	void IssueModified();
	
private:
	TWeakPtr<FNiagaraSystemViewModel> SystemViewModel;
	TWeakPtr<FNiagaraEmitterViewModel> EmitterViewModel;

	UNiagaraStackEditorData* StackEditorData;

	FString StackEditorDataKey;

	FOnStructureChanged StructureChangedDelegate;

	FOnDataObjectModified DataObjectModifiedDelegate;

	FOnRequestFullRefresh RequestFullRefreshDelegate;

	FOnRequestFullRefresh RequestFullRefreshDeferredDelegate;

	TArray<FOnFilterChild> ChildFilters;

	UPROPERTY()
	TArray<UNiagaraStackEntry*> Children;

	UPROPERTY()
	TArray<UNiagaraStackErrorItem*> ErrorChildren;

	mutable TOptional<bool> bIsExpandedCache;

	int32 IndentLevel;

	FName ExecutionCategoryName;
	FName ExecutionSubcategoryName;

	FOnRequestDrop OnRequestCanDropDelegate;
	FOnRequestDrop OnRequestDropDelegate;
	
	TArray<FStackIssue> StackIssues;

	TArray<UNiagaraStackEntry*> ChildrenWithIssues;

	bool bIsFinalized;

	bool bIsSearchResult;

	mutable TOptional<bool> bHasBaseEmitterCache;

	bool bOwnerIsEnabled;

	int32 TotalNumberOfInfoIssues;
	int32 TotalNumberOfWarningIssues;
	int32 TotalNumberOfErrorIssues;
};