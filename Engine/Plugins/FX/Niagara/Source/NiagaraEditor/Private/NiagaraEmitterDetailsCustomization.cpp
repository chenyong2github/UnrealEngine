// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterDetailsCustomization.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorModule.h"

TSharedRef<IDetailCustomization> FNiagaraEmitterDetails::MakeInstance()
{
	return MakeShared<FNiagaraEmitterDetails>();
}

void FNiagaraEmitterDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	TSharedPtr<IPropertyHandle> EventHandlersPropertyHandle = InDetailLayout.GetProperty(UNiagaraEmitter::PrivateMemberNames::EventHandlerScriptProps);
	EventHandlersPropertyHandle->MarkHiddenByCustomization();

	if (GbShowFastPathOptions <= 0)
	{
		InDetailLayout.EditCategory("Script Fast Path").SetCategoryVisibility(false);
	}
}

