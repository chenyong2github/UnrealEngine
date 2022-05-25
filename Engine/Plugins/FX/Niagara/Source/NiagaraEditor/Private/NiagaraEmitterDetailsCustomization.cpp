// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterDetailsCustomization.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorModule.h"
#include "Toolkits/NiagaraSystemToolkit.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/TNiagaraViewModelManager.h"

TSharedRef<IDetailCustomization> FNiagaraEmitterDetails::MakeInstance()
{
	return MakeShared<FNiagaraEmitterDetails>();
}

void FNiagaraEmitterDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	InDetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	
	if (UNiagaraEmitter* EmitterBeingCustomized = CastChecked<UNiagaraEmitter>(ObjectsBeingCustomized[0]))
	{
		TArray<TSharedPtr<FNiagaraEmitterViewModel>> ExistingViewModels;
		TNiagaraViewModelManager<UNiagaraEmitter, FNiagaraEmitterViewModel>::GetAllViewModelsForObject(EmitterBeingCustomized, ExistingViewModels);
		FVersionedNiagaraEmitter VersionedNiagaraEmitter = ExistingViewModels[0]->GetEmitter();
		FVersionedNiagaraEmitterData* EmitterData = VersionedNiagaraEmitter.GetEmitterData();

		for (FProperty* ChildProperty : TFieldRange<FProperty>(FVersionedNiagaraEmitterData::StaticStruct()))
		{
			if (ChildProperty->HasAllPropertyFlags(CPF_Edit))
			{
				FName Category = FName(ChildProperty->GetMetaData(TEXT("Category")));
				IDetailCategoryBuilder& CategoryBuilder = InDetailLayout.EditCategory(Category);
				
				TSharedPtr<FStructOnScope> StructData = MakeShareable(new FStructOnScope(FVersionedNiagaraEmitterData::StaticStruct(), reinterpret_cast<uint8*>(EmitterData)));
				if (IDetailPropertyRow* PropertyRow = CategoryBuilder.AddExternalStructureProperty(StructData, ChildProperty->GetFName()))
				{
					PropertyRow->GetPropertyHandle()->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([VersionedNiagaraEmitter, ChildProperty]()
					{
						FPropertyChangedEvent ChangeEvent(ChildProperty);
						VersionedNiagaraEmitter.Emitter->PostEditChangeVersionedProperty(ChangeEvent, VersionedNiagaraEmitter.Version);
					}));
				}
			}
		}
	}

	// we display the scalability category within scalability mode, which is why we hide it here
	InDetailLayout.HideCategory("Scalability");
}

TSharedRef<IDetailCustomization> FNiagaraEmitterScalabilityDetails::MakeInstance()
{
	return MakeShared<FNiagaraEmitterScalabilityDetails>();
}

void FNiagaraEmitterScalabilityDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	InDetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	
	if (UNiagaraEmitter* EmitterBeingCustomized = CastChecked<UNiagaraEmitter>(ObjectsBeingCustomized[0]))
	{
		TArray<TSharedPtr<FNiagaraEmitterViewModel>> ExistingViewModels;
		TNiagaraViewModelManager<UNiagaraEmitter, FNiagaraEmitterViewModel>::GetAllViewModelsForObject(EmitterBeingCustomized, ExistingViewModels);
		FVersionedNiagaraEmitter VersionedNiagaraEmitter = ExistingViewModels[0]->GetEmitter();
		FVersionedNiagaraEmitterData* EmitterData = VersionedNiagaraEmitter.GetEmitterData();

		for (FProperty* ChildProperty : TFieldRange<FProperty>(FVersionedNiagaraEmitterData::StaticStruct()))
		{
			if (ChildProperty->HasAllPropertyFlags(CPF_Edit) && ChildProperty->HasMetaData(TEXT("DisplayInScalabilityContext")))
			{
				FName Category = FName(ChildProperty->GetMetaData(TEXT("Category")));
				IDetailCategoryBuilder& CategoryBuilder = InDetailLayout.EditCategory(FName(Category));
				
				TSharedPtr<FStructOnScope> StructData = MakeShareable(new FStructOnScope(FVersionedNiagaraEmitterData::StaticStruct(), reinterpret_cast<uint8*>(EmitterData)));
				if (IDetailPropertyRow* PropertyRow = CategoryBuilder.AddExternalStructureProperty(StructData, ChildProperty->GetFName()))
				{
					PropertyRow->GetPropertyHandle()->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([VersionedNiagaraEmitter, ChildProperty]()
					{
						FPropertyChangedEvent ChangeEvent(ChildProperty);
						VersionedNiagaraEmitter.Emitter->PostEditChangeVersionedProperty(ChangeEvent, VersionedNiagaraEmitter.Version);
					}));
				}
			}
		}
	}
}
