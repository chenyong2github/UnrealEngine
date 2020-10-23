// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechTranslator.h"

#include "DatasmithImportOptions.h"

void FCoreTechTranslator::GetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
	FString Extension = GetSource().GetSourceFileExtension();
	if (Extension == "cgr" || Extension == "3dxml")
	{
		return;
	}

	Options.Add(GetCommonTessellationOptionsPtr());
}

void FCoreTechTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
	for (const TStrongObjectPtr<UDatasmithOptionsBase>& OptionPtr : Options)
	{
		if (UDatasmithCommonTessellationOptions* TessellationOptionsObject = Cast<UDatasmithCommonTessellationOptions>(OptionPtr.Get()))
		{
			CommonTessellationOptionsPtr.Reset(TessellationOptionsObject);
		}
	}
}

TStrongObjectPtr<UDatasmithCommonTessellationOptions>& FCoreTechTranslator::GetCommonTessellationOptionsPtr()
{
	if (!CommonTessellationOptionsPtr.IsValid())
	{
		CommonTessellationOptionsPtr = Datasmith::MakeOptions<UDatasmithCommonTessellationOptions>();
		check(CommonTessellationOptionsPtr.IsValid());
		InitCommonTessellationOptions(CommonTessellationOptionsPtr->Options);
	}

	return CommonTessellationOptionsPtr;
}




