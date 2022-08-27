// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

class UCameraComponent;

typedef TGLTFConverter<FGLTFJsonCamera*, const UCameraComponent*> IGLTFCameraConverter;

class FGLTFCameraConverter final : public FGLTFBuilderContext, public IGLTFCameraConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonCamera* Convert(const UCameraComponent* CameraComponent) override;
};
