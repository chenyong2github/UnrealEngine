// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonNode : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonVector3    Translation;
	FGLTFJsonQuaternion Rotation;
	FGLTFJsonVector3    Scale;

	FGLTFJsonCameraIndex    Camera;
	FGLTFJsonSkinIndex      Skin;
	FGLTFJsonMeshIndex      Mesh;
	FGLTFJsonBackdropIndex  Backdrop;
	FGLTFJsonHotspotIndex   Hotspot;
	FGLTFJsonLightIndex     Light;
	FGLTFJsonLightMapIndex  LightMap;
	FGLTFJsonSkySphereIndex SkySphere;

	FGLTFJsonNodeIndex ComponentNode;

	TArray<FGLTFJsonNodeIndex> Children;

	FGLTFJsonNode()
		: Translation(FGLTFJsonVector3::Zero)
		, Rotation(FGLTFJsonQuaternion::Identity)
		, Scale(FGLTFJsonVector3::One)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
