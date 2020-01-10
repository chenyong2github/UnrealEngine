// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Translators/DatasmithTranslator.h"
#include "DatasmithC4DImporter.h"
#include "DatasmithC4DImportOptions.h"
#include "CoreMinimal.h"

class FDatasmithC4DTranslator : public IDatasmithTranslator
{
public:
	virtual FName GetFName() const override { return "DatasmithC4DTranslator"; };

	// IDatasmithTranslator interface
#ifndef _MELANGE_SDK_
	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override { OutCapabilities.bIsEnabled = false; }
#else

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;
	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;
	virtual bool LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	//~ End IDatasmithTranslator interface

private:
	TStrongObjectPtr<UDatasmithC4DImportOptions> ImportOptions;
	TSharedPtr<class FDatasmithC4DImporter> Importer;
#endif
};
