// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithCoreTechTranslator.h"

#include "CADOptions.h"
#include "DatasmithDispatcher.h"
#include "DatasmithMeshBuilder.h"
#include "DatasmithSceneGraphBuilder.h"

#include "UObject/ObjectMacros.h"

class IDatasmithMeshElement;

class FDatasmithCADTranslator : public FDatasmithCoreTechTranslator
{
public:
	virtual FName GetFName() const override { return "DatasmithCADTranslator"; };

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;

	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;

	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;

	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options) override;

private:
	TMap<uint32, FString> CADFileToUE4GeomMap;

	CADLibrary::FImportParameters ImportParameters;

	TUniquePtr<FDatasmithMeshBuilder> MeshBuilderPtr;
};

