// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCoreTechTranslator.h"

#include "DatasmithImportOptions.h"

void FDatasmithCoreTechTranslator::GetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
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

void FDatasmithCoreTechTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
	for (const TStrongObjectPtr<UDatasmithOptionsBase>& OptionPtr : Options)
	{
		if (UDatasmithCommonTessellationOptions* TessellationOptionsObject = Cast<UDatasmithCommonTessellationOptions>(OptionPtr.Get()))
		{
			CommonTessellationOptions = TessellationOptionsObject->Options;
		}
	}
}

bool FDatasmithCoreTechTranslator::IsSourceSupported(const FDatasmithSceneSource& Source)
{
	if (Source.GetSourceFileExtension() != TEXT("xml"))
	{
		return true;
	}

	return Datasmith::CheckXMLFileSchema(Source.GetSourceFile(), TEXT("XPDMXML"), TEXT("ns3:Uos"));
}



