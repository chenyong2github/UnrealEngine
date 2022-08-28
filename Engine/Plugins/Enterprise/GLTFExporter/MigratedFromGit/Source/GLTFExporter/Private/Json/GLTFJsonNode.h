// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonVector.h"
#include "Json/GLTFJsonQuaternion.h"

struct FGLTFJsonNode : IGLTFJsonObject
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

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (!Translation.IsNearlyEqual(FGLTFJsonVector3::Zero, Writer.DefaultTolerance))
		{
			Writer.Write(TEXT("translation"), Translation);
		}

		if (!Rotation.IsNearlyEqual(FGLTFJsonQuaternion::Identity, Writer.DefaultTolerance))
		{
			Writer.Write(TEXT("rotation"), Rotation);
		}

		if (!Scale.IsNearlyEqual(FGLTFJsonVector3::One, Writer.DefaultTolerance))
		{
			Writer.Write(TEXT("scale"), Scale);
		}

		if (Camera != INDEX_NONE)
		{
			Writer.Write(TEXT("camera"), Camera);
		}

		if (Skin != INDEX_NONE)
		{
			Writer.Write(TEXT("skin"), Skin);
		}

		if (Mesh != INDEX_NONE)
		{
			Writer.Write(TEXT("mesh"), Mesh);
		}

		if (Backdrop != INDEX_NONE || Hotspot != INDEX_NONE || Light != INDEX_NONE || LightMap != INDEX_NONE || SkySphere != INDEX_NONE)
		{
			Writer.StartExtensions();

			if (Backdrop != INDEX_NONE)
			{
				Writer.StartExtension(EGLTFJsonExtension::EPIC_HDRIBackdrops);
				Writer.Write(TEXT("backdrop"), Backdrop);
				Writer.EndExtension();
			}

			if (Hotspot != INDEX_NONE)
			{
				Writer.StartExtension(EGLTFJsonExtension::EPIC_AnimationHotspots);
				Writer.Write(TEXT("hotspot"), Hotspot);
				Writer.EndExtension();
			}

			if (Light != INDEX_NONE)
			{
				Writer.StartExtension(EGLTFJsonExtension::KHR_LightsPunctual);
				Writer.Write(TEXT("light"), Light);
				Writer.EndExtension();
			}

			if (LightMap != INDEX_NONE)
			{
				Writer.StartExtension(EGLTFJsonExtension::EPIC_LightmapTextures);
				Writer.Write(TEXT("lightmap"), LightMap);
				Writer.EndExtension();
			}

			if (SkySphere != INDEX_NONE)
			{
				Writer.StartExtension(EGLTFJsonExtension::EPIC_SkySpheres);
				Writer.Write(TEXT("skySphere"), SkySphere);
				Writer.EndExtension();
			}

			Writer.EndExtensions();
		}

		if (Children.Num() > 0)
		{
			Writer.Write(TEXT("children"), Children);
		}
	}
};
