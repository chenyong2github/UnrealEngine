// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithCoreTechTranslator.h"

#ifdef CAD_LIBRARY
#include "CADOptions.h"
#include "DatasmithDispatcher.h"
#include "DatasmithMeshBuilder.h"
#include "DatasmithSceneGraphBuilder.h"
#endif // CAD_LIBRARY

#include "UObject/ObjectMacros.h"

class IDatasmithMeshElement;

class FDatasmithCADTranslator : public FDatasmithCoreTechTranslator
{
public:
	virtual FName GetFName() const override { return "DatasmithCADTranslator"; };

#ifndef CAD_LIBRARY
	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override { OutCapabilities.bIsEnabled = false; }
#else
	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;

	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;

	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;

	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;

private:
	TMap<FString, FString> CADFileToUE4GeomMap;

	CADLibrary::FImportParameters ImportParameters;

	TUniquePtr<FDatasmithMeshBuilder> MeshBuilderPtr;
#endif
};

