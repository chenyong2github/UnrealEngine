// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFConvertBuilder.h"

class FGLTFContainerBuilder : public FGLTFConvertBuilder
{
public:

	FGLTFContainerBuilder(bool bSelectedActorsOnly)
        : FGLTFConvertBuilder(bSelectedActorsOnly)
	{
	}
};
