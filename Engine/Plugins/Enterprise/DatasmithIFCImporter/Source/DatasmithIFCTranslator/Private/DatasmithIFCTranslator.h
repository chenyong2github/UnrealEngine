// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Translators/DatasmithTranslator.h"
#include "DatasmithIFCImporter.h"
#include "DatasmithIFCImportOptions.h"
#include "CoreMinimal.h"

class FDatasmithIFCTranslator : public IDatasmithTranslator
{
public:
	virtual FName GetFName() const override { return "DatasmithIFCTranslator"; };

	// IDatasmithTranslator interface
	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;
	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;
	virtual bool LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	//~ End IDatasmithTranslator interface

private:
    TStrongObjectPtr<UDatasmithIFCImportOptions> ImportOptions;
    TSharedPtr<class FDatasmithIFCImporter> Importer;
};
