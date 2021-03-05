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

	TStrongObjectPtr<UDatasmithCommonTessellationOptions> CommonTessellationOptionsPtr = Datasmith::MakeOptions<UDatasmithCommonTessellationOptions>();
	check(CommonTessellationOptionsPtr.IsValid());
	InitCommonTessellationOptions(CommonTessellationOptionsPtr->Options);

	Options.Add(CommonTessellationOptionsPtr);
}

void FCoreTechTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
	for (const TStrongObjectPtr<UDatasmithOptionsBase>& OptionPtr : Options)
	{
		if (UDatasmithCommonTessellationOptions* TessellationOptionsObject = Cast<UDatasmithCommonTessellationOptions>(OptionPtr.Get()))
		{
			CommonTessellationOptions = TessellationOptionsObject->Options;
		}
	}
}




