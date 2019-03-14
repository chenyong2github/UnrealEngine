// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "IDetailPropertyRow.h"
#include "NiagaraMetaDataCollectionViewModel.h"
#include "NiagaraMetaDataViewModel.h"
#include "IDetailChildrenBuilder.h"
#include "UObject/StructOnScope.h"
#include "IDetailGroup.h"
#include "NiagaraEditorModule.h"
#include "Modules/ModuleManager.h"


#define LOCTEXT_NAMESPACE "NiagaraMetaDataCustomNodeBuilder"

class FNiagaraMetaDataCustomNodeBuilder : public IDetailCustomNodeBuilder
{
public:
	FNiagaraMetaDataCustomNodeBuilder(TSharedRef<FNiagaraMetaDataCollectionViewModel> InViewModel)
		: ViewModel(InViewModel)
	{
		ViewModel->OnCollectionChanged().AddRaw(this, &FNiagaraMetaDataCustomNodeBuilder::OnCollectionViewModelChanged);
	}

	~FNiagaraMetaDataCustomNodeBuilder()
	{
		ViewModel->OnCollectionChanged().RemoveAll(this);
	}

	virtual void SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren) override
	{
		OnRebuildChildren = InOnRegenerateChildren;
	}

	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) {}
	virtual void Tick(float DeltaTime) override {}
	virtual bool RequiresTick() const override { return false; }
	virtual bool InitiallyCollapsed() const { return false; }
	
	virtual FName GetName() const  override
	{
		static const FName NiagaraMetadataCustomNodeBuilder("NiagaraMetadataCustomNodeBuilder");
		return NiagaraMetadataCustomNodeBuilder;
	}

	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override
	{
		const TArray<TSharedRef<FNiagaraMetaDataViewModel>>& VariableModels = ViewModel->GetVariableModels();

		FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

		for (const TSharedRef<FNiagaraMetaDataViewModel>& MetadataViewModel : VariableModels)
		{
			IDetailGroup& MetaDataGroup = ChildrenBuilder.AddGroup(MetadataViewModel->GetName(), FText::FromName(MetadataViewModel->GetName()));
			MetaDataGroup.ToggleExpansion(true);
			TSharedRef<FStructOnScope> StructData = MetadataViewModel->GetValueStruct();
			
			auto AddGroupedPropertyRow = [&ChildrenBuilder, &MetadataViewModel, &MetaDataGroup, &StructData](FName PropertyName, bool bShouldAutoExpand, bool bShouldWatchChildProperties)
			{
				IDetailPropertyRow* PropertyRow = ChildrenBuilder.AddExternalStructureProperty(StructData, PropertyName, PropertyName);
				if (PropertyRow)
				{
					if (bShouldAutoExpand)
					{
						PropertyRow->ShouldAutoExpand(true);
					}
					PropertyRow->Visibility(EVisibility::Hidden);
					MetaDataGroup.AddPropertyRow(PropertyRow->GetPropertyHandle()->AsShared());

					TSharedPtr<IPropertyHandle> PropertyHandle = PropertyRow->GetPropertyHandle();
					PropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(MetadataViewModel, &FNiagaraMetaDataViewModel::NotifyMetaDataChanged));
					if (bShouldWatchChildProperties)
					{
						PropertyHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(MetadataViewModel, &FNiagaraMetaDataViewModel::NotifyMetaDataChanged));
					}
				}
			};

			AddGroupedPropertyRow(GET_MEMBER_NAME_CHECKED(FNiagaraVariableMetaData, Description), false, false);
			AddGroupedPropertyRow(GET_MEMBER_NAME_CHECKED(FNiagaraVariableMetaData, CategoryName), false, false);
			AddGroupedPropertyRow(GET_MEMBER_NAME_CHECKED(FNiagaraVariableMetaData, bAdvancedDisplay), false, false);
			AddGroupedPropertyRow(GET_MEMBER_NAME_CHECKED(FNiagaraVariableMetaData, bInlineEditConditionToggle), false, false);
			AddGroupedPropertyRow(GET_MEMBER_NAME_CHECKED(FNiagaraVariableMetaData, EditorSortPriority), false, false);
			AddGroupedPropertyRow(GET_MEMBER_NAME_CHECKED(FNiagaraVariableMetaData, EditCondition), false, true);
			AddGroupedPropertyRow(GET_MEMBER_NAME_CHECKED(FNiagaraVariableMetaData, VisibleCondition), false, true);
			AddGroupedPropertyRow(GET_MEMBER_NAME_CHECKED(FNiagaraVariableMetaData, PropertyMetaData), true, true);
		}
	}

private:
	void OnCollectionViewModelChanged()
	{
		OnRebuildChildren.ExecuteIfBound();
	}

private:
	TSharedRef<FNiagaraMetaDataCollectionViewModel> ViewModel;
	FSimpleDelegate OnRebuildChildren;
};

#undef LOCTEXT_NAMESPACE // "NiagaraMetaDataCustomNodeBuilder"
