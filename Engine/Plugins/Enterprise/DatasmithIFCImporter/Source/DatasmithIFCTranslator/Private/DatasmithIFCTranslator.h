// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithIFCImporter.h"
#include "DatasmithIFCImportOptions.h"

#include "CoreMinimal.h"
#include "DatasmithTranslator.h"

class FDatasmithIFCTranslator : public IDatasmithTranslator
{
public:
	virtual FName GetFName() const override { return "DatasmithIFCTranslator"; };

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;
	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;
	virtual bool LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options) override;

private:
    TStrongObjectPtr<UDatasmithIFCImportOptions> ImportOptions;
    TSharedPtr<class FDatasmithIFCImporter> Importer;
};
