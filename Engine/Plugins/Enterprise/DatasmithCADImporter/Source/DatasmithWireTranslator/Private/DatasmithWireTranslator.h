// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CADLibraryOptions.h"
#include "DatasmithImportOptions.h"
#include "Translators/DatasmithTranslator.h"
#include "UObject/ObjectMacros.h"

class FWireTranslatorImpl;
class AlDagNode;


class FDatasmithWireTranslator : public IDatasmithTranslator
{
public:
	FDatasmithWireTranslator();

	virtual FName GetFName() const override { return "DatasmithWireTranslator"; };

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;

	virtual bool IsSourceSupported(const FDatasmithSceneSource& Source) override;

	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;
	virtual void UnloadScene() override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;

	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;

private:
	TSharedPtr<FWireTranslatorImpl> Translator;
	FDatasmithTessellationOptions TessellationOptions;

	const float MetricUnit = 0.001; // As CT don`t take care of unit input data, metric unit is set with default value
	const float ScaleFactor = 1; // Wire internal unit is cm so no scale needed
};
