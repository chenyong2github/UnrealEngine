// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithCoreTechTranslator.h"

#include "DatasmithImportOptions.h"

void FDatasmithCoreTechTranslator::GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
	Options.Add(GetCommonTessellationOptionsPtr());
}

void FDatasmithCoreTechTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
	for (const TStrongObjectPtr<UObject>& OptionPtr : Options)
	{
		if (UObject* Option = OptionPtr.Get())
		{
			if (UDatasmithCommonTessellationOptions* TessellationOptionsObject = Cast<UDatasmithCommonTessellationOptions>(Option))
			{
				GetCommonTessellationOptionsPtr().Reset(TessellationOptionsObject);
			}
		}
	}
}

TStrongObjectPtr<UDatasmithCommonTessellationOptions>& FDatasmithCoreTechTranslator::GetCommonTessellationOptionsPtr()
{
	if(!CommonTessellationOptionsPtr.IsValid())
	{
		CommonTessellationOptionsPtr = Datasmith::MakeOptions<UDatasmithCommonTessellationOptions>();
		check(CommonTessellationOptionsPtr.IsValid());
		InitCommonTessellationOptions(CommonTessellationOptionsPtr->Options);
	}

	return CommonTessellationOptionsPtr;
}




