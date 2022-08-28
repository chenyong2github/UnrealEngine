// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EGLTFJsonExtension
{
	KHR_LightsPunctual,
	KHR_MaterialsUnlit,
	KHR_MaterialsClearCoat,
	KHR_MeshQuantization,
	KHR_TextureTransform,
	EPIC_OrbitalCameras,
	EPIC_HDRIBackdrops,
	EPIC_AnimationHotspots,
	EPIC_LightmapTextures,
	EPIC_ReflectionCaptures,
	EPIC_LevelVariantSets,
	EPIC_TextureHDREncoding,
    EPIC_AnimationPlayback,
	EPIC_SkySpheres
};

enum class EGLTFJsonShadingModel
{
	None = -1,
	Default,
	Unlit,
	ClearCoat
};

enum class EGLTFJsonHDREncoding
{
	None = -1,
	RGBE,
	RGBM
};

enum class EGLTFJsonCubeFace
{
	None = -1,
	PosX,
	NegX,
	PosY,
	NegY,
	PosZ,
	NegZ
};

enum class EGLTFJsonAccessorType
{
	None = -1,
	Scalar,
	Vec2,
	Vec3,
	Vec4,
	Mat2,
	Mat3,
	Mat4
};

enum class EGLTFJsonComponentType
{
	None = -1,
	S8 = 5120, // signed byte
	U8 = 5121, // unsigned byte
	S16 = 5122, // signed short
	U16 = 5123, // unsigned short
	S32 = 5124, // unused
	U32 = 5125, // unsigned int -- only valid for indices, not attributes
	F32 = 5126  // float
};

enum class EGLTFJsonBufferTarget
{
	None = -1,
	ArrayBuffer = 34962,
	ElementArrayBuffer = 34963
};

enum class EGLTFJsonPrimitiveMode
{
	// unsupported
	Points = 0,
	Lines = 1,
	LineLoop = 2,
	LineStrip = 3,
	// initially supported
	Triangles = 4,
	// will be supported prior to release
	TriangleStrip = 5,
	TriangleFan = 6
};

enum class EGLTFJsonAlphaMode
{
	Opaque,
	Mask,
	Blend
};

enum class EGLTFJsonMimeType
{
	None = -1,
	PNG,
	JPEG
};

enum class EGLTFJsonTextureFilter
{
	None = -1,
	// valid for Min & Mag
	Nearest = 9728,
	Linear = 9729,
	// valid for Min only
	NearestMipmapNearest = 9984,
	LinearMipmapNearest = 9985,
	NearestMipmapLinear = 9986,
	LinearMipmapLinear = 9987
};

enum class EGLTFJsonTextureWrap
{
	Repeat = 10497,
	MirroredRepeat = 33648,
	ClampToEdge = 33071
};

enum class EGLTFJsonInterpolation
{
	Linear,
	Step,
	CubicSpline,
};

enum class EGLTFJsonTargetPath
{
	None = -1,
	Translation,
	Rotation,
	Scale,
	Weights
};

enum class EGLTFJsonCameraType
{
	None = -1,
	Orthographic,
	Perspective
};

enum class EGLTFJsonLightType
{
	None = -1,
	Directional,
	Point,
	Spot
};
