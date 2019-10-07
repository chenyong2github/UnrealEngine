// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithSceneSource.h"

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Wrap a source with an adapted translator.
 * This scopes the lifecycle of a translator, 
 */
struct DATASMITHIMPORTER_API FDatasmithTranslatableSceneSource
{
	FDatasmithTranslatableSceneSource(const FDatasmithSceneSource& Source);
	~FDatasmithTranslatableSceneSource();

	bool IsTranslatable() const;

	bool Translate(TSharedRef< class IDatasmithScene > Scene);

	TSharedPtr<class IDatasmithTranslator> GetTranslator() const;

private:
	/** Translator currently in use (null when not importing) */
	TSharedPtr<class IDatasmithTranslator> Translator;

	/** internal helper to release scene */
	TUniquePtr< class FSceneGuard > SceneGuard;
};



