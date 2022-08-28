// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFDegenerateUVSectionsChecker final : public TGLTFConverter<const TArray<int32>*, const FMeshDescription*, int32>
{
	TArray<TUniquePtr<TArray<int32>>> Outputs;

	virtual void Sanitize(const FMeshDescription*& Description, int32& TexCoord) override;

	virtual const TArray<int32>* Convert(const FMeshDescription* Description, int32 TexCoord) override;
};
