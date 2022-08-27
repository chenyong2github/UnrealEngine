// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Exporters/GLTFExporter.h"
#include "GLTFAnimSequenceExporter.generated.h"

UCLASS()
class GLTFEXPORTER_API UGLTFAnimSequenceExporter final : public UGLTFExporter
{
public:

	GENERATED_BODY()

	UGLTFAnimSequenceExporter(const FObjectInitializer& ObjectInitializer = FObjectInitializer());

	bool Add(FGLTFContainerBuilder& Builder, const UObject* Object) override;
};
