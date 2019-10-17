// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CADLibraryOptions.h"
#include "DatasmithImportOptions.h"
#include "DatasmithCADTranslatorImpl.h"
#include "Translators/DatasmithTranslator.h"
#include "UObject/ObjectMacros.h"

class FDatasmithCADTranslatorImpl;


class FDatasmithCADTranslator : public IDatasmithTranslator
{
public:
	FDatasmithCADTranslator();

	virtual FName GetFName() const override { return "DatasmithCADTranslator"; };

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;

	virtual bool IsSourceSupported(const FDatasmithSceneSource& Source) override;

	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;

private:
	TSharedPtr<FDatasmithCADTranslatorImpl> Translator;
	FDatasmithTessellationOptions TessellationOptions;
};

