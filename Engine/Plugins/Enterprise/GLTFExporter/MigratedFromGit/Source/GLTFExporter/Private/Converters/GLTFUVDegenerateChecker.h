// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFUVDegenerateChecker final : public TGLTFConverter<bool, const FMeshDescription*, int32, int32>
{
	virtual void Sanitize(const FMeshDescription*& Description, int32& SectionIndex, int32& TexCoord) override;

	virtual bool Convert(const FMeshDescription* Description, int32 SectionIndex, int32 TexCoord) override;
};
