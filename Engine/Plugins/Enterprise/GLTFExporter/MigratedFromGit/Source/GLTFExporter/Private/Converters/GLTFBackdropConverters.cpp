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
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export Mesh for HDRIBackdrop %s"), *Actor->GetName()));
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
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export Cubemap for HDRIBackdrop %s"), *Actor->GetName()));
	}

	float Intensity;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("Intensity"), Intensity))
	{
		JsonBackdrop.Intensity = Intensity;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export Intensity for HDRIBackdrop %s"), *Actor->GetName()));
	}

	float Size;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("Size"), Size))
	{
		JsonBackdrop.Size = Size;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export Size for HDRIBackdrop %s"), *Actor->GetName()));
	}

	FVector ProjectionCenter;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("ProjectionCenter"), ProjectionCenter))
	{
		JsonBackdrop.ProjectionCenter = FGLTFConverterUtility::ConvertPosition(ProjectionCenter, Builder.ExportOptions->ExportScale);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export ProjectionCenter for HDRIBackdrop %s"), *Actor->GetName()));
	}

	float LightingDistanceFactor;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("LightingDistanceFactor"), LightingDistanceFactor))
	{
		JsonBackdrop.LightingDistanceFactor = LightingDistanceFactor;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export LightingDistanceFactor for HDRIBackdrop %s"), *Actor->GetName()));
	}

	bool UseCameraProjection;
	if (FGLTFActorUtility::TryGetPropertyValue(Actor, TEXT("UseCameraProjection"), UseCameraProjection))
	{
		JsonBackdrop.UseCameraProjection = UseCameraProjection;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export UseCameraProjection for HDRIBackdrop %s"), *Actor->GetName()));
	}

	return Builder.AddBackdrop(JsonBackdrop);
}
