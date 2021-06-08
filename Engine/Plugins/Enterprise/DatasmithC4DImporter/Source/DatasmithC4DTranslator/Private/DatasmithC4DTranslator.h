// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "DatasmithC4DImportOptions.h"
#include "DatasmithTranslator.h"
#include "DatasmithC4DImporter.h"
#include "IDatasmithC4DImporter.h"

#include "CoreMinimal.h"

class FDatasmithC4DTranslator : public IDatasmithTranslator
{
public:
	virtual FName GetFName() const override { return "DatasmithC4DTranslator"; };

#if !defined(_MELANGE_SDK_) && !defined(_CINEWARE_SDK_)
	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override { OutCapabilities.bIsEnabled = false; }
#else

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;
	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;
	virtual bool LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options) override;

	/** Event for when a C4D document is about to be opened for translation */
	/*DECLARE_EVENT_TwoParams(FDatasmithC4DTranslator, FPreTranslateEvent, melange::BaseDocument*, const FString&)
	static FPreTranslateEvent& OnPreTranslate() { return PreTranslateEvent; }*/
	
private:
	TStrongObjectPtr<UDatasmithC4DImportOptions>& GetOrCreateC4DImportOptions();

private:
	/** -1: try load dynamic initially; 0: dynamic cannot be loaded; 1: dynamic successful loaded */
	TStrongObjectPtr<UDatasmithC4DImportOptions> ImportOptions;
	TSharedPtr<class IDatasmithC4DImporter> Importer;
#endif
};
