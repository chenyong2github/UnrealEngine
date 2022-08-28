// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

class UCameraComponent;

typedef TGLTFConverter<FGLTFJsonCameraIndex, const UCameraComponent*> IGLTFCameraConverter;

class FGLTFCameraConverter final : public FGLTFBuilderContext, public IGLTFCameraConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonCameraIndex Convert(const UCameraComponent* CameraComponent) override;
};
