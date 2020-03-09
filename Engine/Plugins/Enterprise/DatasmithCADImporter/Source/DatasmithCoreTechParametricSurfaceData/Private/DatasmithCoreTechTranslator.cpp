// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCoreTechTranslator.h"

#include "DatasmithImportOptions.h"

void FDatasmithCoreTechTranslator::GetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
	Options.Add(GetCommonTessellationOptionsPtr());
}

void FDatasmithCoreTechTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
	for (const TStrongObjectPtr<UDatasmithOptionsBase>& OptionPtr : Options)
	{
		if (UDatasmithCommonTessellationOptions* TessellationOptionsObject = Cast<UDatasmithCommonTessellationOptions>(OptionPtr.Get()))
		{
			CommonTessellationOptionsPtr.Reset(TessellationOptionsObject);
		}
	}
}

TStrongObjectPtr<UDatasmithCommonTessellationOptions>& FDatasmithCoreTechTranslator::GetCommonTessellationOptionsPtr()
{
	if (!CommonTessellationOptionsPtr.IsValid())
	{
		CommonTessellationOptionsPtr = Datasmith::MakeOptions<UDatasmithCommonTessellationOptions>();
		check(CommonTessellationOptionsPtr.IsValid());
		InitCommonTessellationOptions(CommonTessellationOptionsPtr->Options);
	}

	return CommonTessellationOptionsPtr;
}




