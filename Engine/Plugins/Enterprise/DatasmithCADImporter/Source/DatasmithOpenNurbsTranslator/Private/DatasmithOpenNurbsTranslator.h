// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithTranslator.h"
#include "DatasmithOpenNurbsImportOptions.h"

#include "CoreMinimal.h"

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

	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;

protected:
	const FDatasmithTessellationOptions& GetCommonTessellationOptions()
	{
		return OpenNurbsImportOptions->Options;
	}

private:

	TSharedPtr<FOpenNurbsTranslatorImpl> Translator;

	// Temporarily store this here for UE-81278 so that we can trigger the recreation of
	// static meshes if we're reimporting with new materials that haven't been assigned yet
	FDatasmithImportBaseOptions BaseOptions;

	TStrongObjectPtr<UDatasmithOpenNurbsImportOptions> OpenNurbsImportOptions;
};

