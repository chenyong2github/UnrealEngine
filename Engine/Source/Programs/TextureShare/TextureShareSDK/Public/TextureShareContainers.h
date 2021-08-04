// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef TEXTURE_SHARE_SDK_DLL
// Unsigned base types.
typedef unsigned char       uint8;      // 8-bit  unsigned.
typedef unsigned int        uint32;     // 32-bit unsigned.

#endif


#include "TextureShareCoreGenericContainers.h"

struct FTextureShareSDKVector
{
public:
	/** Vector's X component. */
	float X;

	/** Vector's Y component. */
	float Y;

	/** Vector's Z component. */
	float Z;
};

struct FTextureShareSDKRotator
{
public:
	/** Rotation around the right axis (around Y axis), Looking up and down (0=Straight Ahead, +Up, -Down) */
	float Pitch;

	/** Rotation around the up axis (around Z axis), Running in circles 0=East, +North, -South. */
	float Yaw;

	/** Rotation around the forward axis (around X axis), Tilting your head, 0=Straight, +Clockwise, -CCW. */
	float Roll;
};

struct FTextureShareSDKMatrix
{
public:
	union
	{
		float M[4][4];
	};
};

struct FTextureShareSDKAdditionalData
{
	// Frame info
	uint32 FrameNumber;

	// Projection matrix
	FTextureShareSDKMatrix PrjMatrix;

	// View info
	FTextureShareSDKMatrix ViewMatrix;

	FTextureShareSDKVector  ViewLocation;
	FTextureShareSDKRotator ViewRotation;
	FTextureShareSDKVector  ViewScale;

	//@todo: add more frame data
};

struct FTextureShareSDKCustomProjectionData
{
	// Projection matrix
	FTextureShareSDKMatrix PrjMatrix;

	FTextureShareSDKVector  ViewLocation;
	FTextureShareSDKRotator ViewRotation;
	FTextureShareSDKVector  ViewScale;
};
