// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMaterialTasks.h"

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Convert(const UMaterialInterface* Material)
{
	if (Material == FGLTFMaterialUtility::GetDefault())
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE); // use default gltf definition
	}

	const FGLTFJsonMaterialIndex MaterialIndex = Builder.AddMaterial();
	Builder.SetupTask<FGLTFMaterialTask>(Builder, Material, MaterialIndex);
	return MaterialIndex;
}
