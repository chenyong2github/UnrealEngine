// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithImportOptions.h"
#include "Translators/DatasmithTranslator.h"

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

class FOpenNurbsTranslatorImpl;

class FDatasmithOpenNurbsTranslator : public IDatasmithTranslator
{
public:
	FDatasmithOpenNurbsTranslator();

	virtual FName GetFName() const override { return "DatasmithOpenNurbsTranslator"; };

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;

	virtual bool IsSourceSupported(const FDatasmithSceneSource& Source) override;

	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;

private:
	TSharedPtr<FOpenNurbsTranslatorImpl> Translator;
	FDatasmithTessellationOptions TessellationOptions;
};

