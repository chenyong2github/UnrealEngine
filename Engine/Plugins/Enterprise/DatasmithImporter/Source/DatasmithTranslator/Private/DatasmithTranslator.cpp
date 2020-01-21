// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithTranslator.h"
#include "DatasmithTranslatorManager.h"

#define LOCTEXT_NAMESPACE "DatasmithTranslator"

namespace Datasmith
{

	void Details::RegisterTranslatorImpl(const FTranslatorRegisterInformation& Info)
	{
		FDatasmithTranslatorManager::Get().Register(Info);
	}

	void Details::UnregisterTranslatorImpl(const FTranslatorRegisterInformation& Info)
	{
		FDatasmithTranslatorManager::Get().Unregister(Info.TranslatorName);
	}

} // ns Datasmith


#undef LOCTEXT_NAMESPACE
