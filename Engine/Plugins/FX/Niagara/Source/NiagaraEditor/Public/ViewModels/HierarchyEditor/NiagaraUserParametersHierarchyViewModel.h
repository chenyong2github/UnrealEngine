// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/HierarchyEditor/NiagaraHierarchyViewModelBase.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraUserParametersHierarchyViewModel.generated.h"

UCLASS()
class UNiagaraHierarchyUserParameter : public UNiagaraHierarchyItem
{
	GENERATED_BODY()

public:
	UNiagaraHierarchyUserParameter() {}
	virtual ~UNiagaraHierarchyUserParameter() override {}
	
	void Initialize(UNiagaraScriptVariable& InUserParameterScriptVariable, UNiagaraSystem& System);

	virtual void RefreshDataInternal() override;
	
	const FNiagaraVariable& GetUserParameter() const { return UserParameterScriptVariable->Variable; }

	virtual FString ToString() const override { return GetUserParameter().GetName().ToString(); }
private:
	UPROPERTY()
	TObjectPtr<UNiagaraScriptVariable> UserParameterScriptVariable;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> System;
};

UCLASS()
class UNiagaraUserParametersHierarchyViewModel : public UNiagaraHierarchyViewModelBase
{
	GENERATED_BODY()
public:
	UNiagaraUserParametersHierarchyViewModel() {}
	virtual ~UNiagaraUserParametersHierarchyViewModel() override
	{
	}

	TSharedRef<FNiagaraSystemViewModel> GetSystemViewModel() const;

	void Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel);
	
	virtual UNiagaraHierarchyRoot* GetHierarchyDataRoot() const override;
	
	virtual void PrepareSourceItems() override;
	virtual void SetupCommands() override;

	virtual TOptional<EItemDropZone> CanDropOn(TSharedPtr<FNiagaraHierarchyItemViewModelBase> SourceDropItem, TSharedPtr<FNiagaraHierarchyItemViewModelBase> TargetDropItem, EItemDropZone DropZone = EItemDropZone::OntoItem) override;
	virtual TSharedRef<FNiagaraHierarchyDragDropOp> CreateDragDropOp(TSharedRef<FNiagaraHierarchyItemViewModelBase> Item) override;

	virtual void OnSelectionChanged(TSharedPtr<FNiagaraHierarchyItemViewModelBase> HierarchyItem) override;
	
	virtual bool SupportsDetailsPanel() override { return true; }
	virtual TArray<TTuple<UClass*, FOnGetDetailCustomizationInstance>> GetInstanceCustomizations() override;
	virtual TSharedRef<SWidget> GenerateRowContentWidget(TSharedRef<FNiagaraHierarchyItemViewModelBase>) const override;
protected:
	virtual void FinalizeInternal() override;
private:
	TWeakPtr<FNiagaraSystemViewModel> SystemViewModelWeak;
};

class FNiagaraUserParameterHierarchyDragDropOp : public FNiagaraHierarchyDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FNiagaraUserParameterDragDropOp, FNiagaraHierarchyDragDropOp)

	FNiagaraUserParameterHierarchyDragDropOp(TSharedPtr<FNiagaraHierarchyItemViewModelBase> UserParameterItem) : FNiagaraHierarchyDragDropOp(UserParameterItem) {}

	FNiagaraVariable GetUserParameter() const
	{
		const UNiagaraHierarchyUserParameter* HierarchyUserParameter = CastChecked<UNiagaraHierarchyUserParameter>(DraggedItem.Pin()->GetData());
		return HierarchyUserParameter->GetUserParameter();
	}
	
	virtual TSharedRef<SWidget> CreateCustomDecorator() const override;
};
