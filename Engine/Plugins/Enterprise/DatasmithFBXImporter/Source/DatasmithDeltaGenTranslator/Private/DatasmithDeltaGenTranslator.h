// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Translators/DatasmithTranslator.h"
#include "DatasmithDeltaGenImporter.h"
#include "DatasmithDeltaGenImportOptions.h"
#include "CoreMinimal.h"

class FDatasmithDeltaGenTranslator : public IDatasmithTranslator
{
public:
	virtual FName GetFName() const override { return "DatasmithDeltaGenTranslator"; };

	// IDatasmithTranslator interface
	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;
	virtual bool IsSourceSupported(const FDatasmithSceneSource& Source);

	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;
	virtual bool LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	//~ End IDatasmithTranslator interface

private:
    TStrongObjectPtr<UDatasmithDeltaGenImportOptions> ImportOptions;
    TSharedPtr<class FDatasmithDeltaGenImporter> Importer;
};
