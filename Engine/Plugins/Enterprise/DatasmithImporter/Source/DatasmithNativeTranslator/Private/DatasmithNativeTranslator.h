// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Translators/DatasmithTranslator.h"

class FDatasmithNativeTranslator : public IDatasmithTranslator
{
public:
	virtual FName GetFName() const override { return "DatasmithNativeTranslator"; };

	virtual void Initialize(FDatasmithTranslatorCapabilities& OutCapabilities) override;

	virtual bool LoadScene(TSharedRef<IDatasmithScene> OutScene) override;

	virtual bool LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload) override;

	virtual bool LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload) override;
};

