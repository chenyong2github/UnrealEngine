// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EGLTFJsonExtension
{
	KHR_LightsPunctual,
	KHR_MaterialsUnlit,
	KHR_MaterialsClearCoat,
	KHR_MeshQuantization
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
	None = -1,
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
	None = -1,
	Opaque,
	Mask,
	Blend
};

enum class EGLTFJsonSamplerFilter
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

enum class EGLTFJsonSamplerWrap
{
	None = -1,
	Repeat = 10497,
	MirroredRepeat = 33648,
	ClampToEdge = 33071
};

enum class EGLTFJsonAnimationInterpolation
{
	None = -1,
	Linear,
	Step,
	CubicSpline,
};

enum class EGLTFJsonAnimationPath
{
	None = -1,
	Translation,
	Rotation,
	Scale,
	Weights
};
