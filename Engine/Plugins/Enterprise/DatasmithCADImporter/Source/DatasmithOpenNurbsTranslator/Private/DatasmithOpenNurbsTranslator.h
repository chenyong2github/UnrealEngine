// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithCoreTechTranslator.h"
#include "DatasmithImportOptions.h"

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

class FOpenNurbsTranslatorImpl;

class FDatasmithOpenNurbsTranslator : public FDatasmithCoreTechTranslator
{
public:
	FDatasmithOpenNurbsTranslator();

	// Begin IDatasmithTranslator overrides
	virtual FName GetFName() const override { return "DatasmithOpenNurbsTranslator"; };

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;

	virtual bool IsSourceSupported(const FDatasmithSceneSource& Source) override;

	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;
	// End IDatasmithTranslator overrides

	// Begin ADatasmithCoreTechTranslator overrides
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;

protected:
	virtual void InitCommonTessellationOptions(FDatasmithTessellationOptions& TessellationOptions) override
	{
		TessellationOptions.StitchingTechnique = EDatasmithCADStitchingTechnique::StitchingNone;
	}
	// End ADatasmithCoreTechTranslator overrides

private:

	TSharedPtr<FOpenNurbsTranslatorImpl> Translator;

	// Temporarily store this here for UE-81278 so that we can trigger the recreation of
	// static meshes if we're reimporting with new materials that haven't been assigned yet
	FDatasmithImportBaseOptions BaseOptions;
};

