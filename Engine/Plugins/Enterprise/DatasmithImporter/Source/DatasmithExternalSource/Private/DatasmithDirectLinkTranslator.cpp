// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithDirectLinkTranslator.h"

namespace UE::DatasmithImporter
{
	void FDatasmithDirectLinkTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
	{
		OutCapabilities.SupportedFileFormats.Emplace(TEXT("directlink"), TEXT("DirectLink stream"));
		OutCapabilities.bParallelLoadStaticMeshSupported = true;
	}
}