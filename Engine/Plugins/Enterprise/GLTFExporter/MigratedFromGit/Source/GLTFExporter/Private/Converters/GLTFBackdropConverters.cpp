// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFBackdropConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFActorUtility.h"
#include "Builders/GLTFContainerBuilder.h"

FGLTFJsonBackdropIndex FGLTFBackdropConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const AActor* Actor)
{
	const UBlueprint* Blueprint = FGLTFActorUtility::GetBlueprintFromActor(Actor);
	if (!FGLTFActorUtility::IsHDRIBackdropBlueprint(Blueprint))
	{
		return FGLTFJsonBackdropIndex(INDEX_NONE);
	}

	FGLTFJsonBackdrop JsonBackdrop;
	JsonBackdrop.Name = Name;

	const UStaticMesh* Mesh;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("Mesh"), Mesh))
	{
		// TODO: avoid exporting the mesh material (use gltf default material)
		JsonBackdrop.Mesh = Builder.GetOrAddMesh(Mesh);
	}
	else
	{
		// TODO: report error
	}

	const UTextureCube* Cubemap;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("Cubemap"), Cubemap))
	{
		// TODO: add a proper custom gltf extension with its own converters for cubemaps

		for (int32 CubeFaceIndex = 0; CubeFaceIndex < CubeFace_MAX; ++CubeFaceIndex)
		{
			const ECubeFace CubeFace = static_cast<ECubeFace>(CubeFaceIndex);
			const EGLTFJsonCubeFace JsonCubeFace = FGLTFConverterUtility::ConvertCubeFace(CubeFace);
			const int32 JsonCubeFaceIndex = static_cast<int32>(JsonCubeFace);

			JsonBackdrop.Cubemap[JsonCubeFaceIndex] = Builder.GetOrAddTexture(Cubemap, CubeFace, Cubemap->GetName() + TEXT("_") + FGLTFJsonUtility::ToString(JsonCubeFace));
		}
	}
	else
	{
		// TODO: report error
	}

	float Intensity;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("Intensity"), Intensity))
	{
		JsonBackdrop.Intensity = Intensity;
	}
	else
	{
		// TODO: report error
	}

	float Size;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("Size"), Size))
	{
		JsonBackdrop.Size = Size;
	}
	else
	{
		// TODO: report error
	}

	FVector ProjectionCenter;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("ProjectionCenter"), ProjectionCenter))
	{
		JsonBackdrop.ProjectionCenter = FGLTFConverterUtility::ConvertPosition(ProjectionCenter);
	}
	else
	{
		// TODO: report error
	}

	float LightingDistanceFactor;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("LightingDistanceFactor"), LightingDistanceFactor))
	{
		JsonBackdrop.LightingDistanceFactor = LightingDistanceFactor;
	}
	else
	{
		// TODO: report error
	}

	bool UseCameraProjection;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("UseCameraProjection"), UseCameraProjection))
	{
		JsonBackdrop.UseCameraProjection = UseCameraProjection;
	}
	else
	{
		// TODO: report error
	}

	return Builder.AddBackdrop(JsonBackdrop);
}
