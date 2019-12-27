// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Translators/DatasmithTranslator.h"
#include "DatasmithGLTFImporter.h"
#include "DatasmithGLTFImportOptions.h"
#include "CoreMinimal.h"

class FDatasmithGLTFTranslator : public IDatasmithTranslator
{
public:
	// IDatasmithTranslator interface
	virtual FName GetFName() const override { return "DatasmithGLTFTranslator"; };
	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;
	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;
	virtual bool LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	//~ End IDatasmithTranslator interface

private:
    TStrongObjectPtr<UDatasmithGLTFImportOptions> ImportOptions;
    TSharedPtr<class FDatasmithGLTFImporter> Importer;
};
