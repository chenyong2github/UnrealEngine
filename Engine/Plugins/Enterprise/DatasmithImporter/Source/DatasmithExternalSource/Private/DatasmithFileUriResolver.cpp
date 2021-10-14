// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFileUriResolver.h"

#include "DatasmithFileExternalSource.h"
#include "DatasmithSceneSource.h"
#include "DatasmithTranslatorManager.h"

namespace UE::DatasmithImporter
{
	TSharedPtr<FExternalSource> FDatasmithFileUriResolver::GetOrCreateExternalSource(const FSourceUri& Uri) const
	{
		if (CanResolveUri(Uri))
		{
			return MakeShared<FDatasmithFileExternalSource>(Uri);
		}

		return nullptr;
	}

	bool FDatasmithFileUriResolver::CanResolveUri(const FSourceUri& Uri) const
	{
		if (Uri.HasScheme(FSourceUri::GetFileScheme()))
		{
			FDatasmithSceneSource DatasmithSceneSource;
			DatasmithSceneSource.SetSourceFile(FString(Uri.GetPath()));
			return FDatasmithTranslatorManager::Get().SelectFirstCompatible(DatasmithSceneSource).IsValid();
		}

		return false;
	}

	FName FDatasmithFileUriResolver::GetScheme() const
	{
		return FName(FSourceUri::GetFileScheme());
	}
}