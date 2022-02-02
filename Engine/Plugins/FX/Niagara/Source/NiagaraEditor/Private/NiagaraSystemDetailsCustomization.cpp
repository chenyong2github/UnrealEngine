// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemDetailsCustomization.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "NiagaraSystem.h"
#include "Toolkits/NiagaraSystemToolkit.h"

TSharedRef<IDetailCustomization> FNiagaraSystemDetails::MakeInstance()
{
	return MakeShared<FNiagaraSystemDetails>();
}

void FNiagaraSystemDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	if(GNiagaraScalabilityModeEnabled)
	{
		InDetailLayout.HideCategory("Scalability");
		InDetailLayout.HideProperty("EffectType");
	}
}
