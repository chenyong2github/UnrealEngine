// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CADLibraryOptions.h"
#include "DatasmithCADTranslatorImpl.h"
#include "DatasmithCoreTechTranslator.h"
#include "UObject/ObjectMacros.h"


class FDatasmithCADTranslator : public FDatasmithCoreTechTranslator
{
public:
	FDatasmithCADTranslator();

	// Begin IDatasmithTranslator overrides
	virtual FName GetFName() const override { return "DatasmithCADTranslator"; };

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;

	virtual bool IsSourceSupported(const FDatasmithSceneSource& Source) override;

	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;
	// End IDatasmithTranslator overrides

	// Begin ADatasmithCoreTechTranslator overrides
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	// End ADatasmithCoreTechTranslator overrides

private:
	TSharedPtr<FDatasmithCADTranslatorImpl> Translator;
};

