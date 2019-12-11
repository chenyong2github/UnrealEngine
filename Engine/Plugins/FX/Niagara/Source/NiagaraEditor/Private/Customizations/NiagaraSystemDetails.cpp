// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemDetails.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "NiagaraEditorModule.h"

TSharedRef<IDetailCustomization> FNiagaraSystemDetails::MakeInstance()
{
	return MakeShared<FNiagaraSystemDetails>();
}

void FNiagaraSystemDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	if (GbShowFastPathOptions <= 0)
	{
		InDetailLayout.EditCategory("Script Fast Path").SetCategoryVisibility(false);
	}
}

