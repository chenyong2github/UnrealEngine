// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithSceneSource.h"

#include "Translators/DatasmithTranslator.h"
#include "Translators/DatasmithTranslatorManager.h"

#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "DatasmithSceneSource"

void FDatasmithSceneSource::SetSourceFile(const FString& InFilePath)
{
	FilePath = InFilePath;
	SceneDeducedName = FPaths::GetBaseFilename(FilePath);
	FileExtension = FPaths::GetExtension(FilePath);
	if (FCString::IsNumeric(*FileExtension))
	{
		FileExtension = FPaths::GetExtension(SceneDeducedName) + TEXT(".") + FileExtension;
		SceneDeducedName = FPaths::GetBaseFilename(SceneDeducedName);
	}
}

void FDatasmithSceneSource::SetSceneName(const FString& InSceneName)
{
	SceneOverrideName = InSceneName;
}

const FString& FDatasmithSceneSource::GetSceneName() const
{
	return SceneOverrideName.IsEmpty() ? SceneDeducedName : SceneOverrideName;
}

#undef LOCTEXT_NAMESPACE
