// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonCameraControl.h"

struct FGLTFJsonOrthographic : IGLTFJsonObject
{
	float XMag; // horizontal magnification of the view
	float YMag; // vertical magnification of the view
	float ZFar;
	float ZNear;

	FGLTFJsonOrthographic()
		: XMag(0)
		, YMag(0)
		, ZFar(0)
		, ZNear(0)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("xmag"), XMag);
		Writer.Write(TEXT("ymag"), YMag);
		Writer.Write(TEXT("zfar"), ZFar);
		Writer.Write(TEXT("znear"), ZNear);
	}
};

struct FGLTFJsonPerspective : IGLTFJsonObject
{
	float AspectRatio; // aspect ratio of the field of view
	float YFov; // vertical field of view in radians
	float ZFar;
	float ZNear;

	FGLTFJsonPerspective()
		: AspectRatio(0)
		, YFov(0)
		, ZFar(0)
		, ZNear(0)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!FMath::IsNearlyEqual(AspectRatio, 0, Writer.DefaultTolerance))
		{
			Writer.Write(TEXT("aspectRatio"), AspectRatio);
		}

		Writer.Write(TEXT("yfov"), YFov);

		if (!FMath::IsNearlyEqual(ZFar, 0, Writer.DefaultTolerance))
		{
			Writer.Write(TEXT("zfar"), ZFar);
		}

		Writer.Write(TEXT("znear"), ZNear);
	}
};

struct FGLTFJsonCamera : IGLTFJsonObject
{
	FString Name;

	EGLTFJsonCameraType               Type;
	TOptional<FGLTFJsonCameraControl> CameraControl;

	FGLTFJsonOrthographic Orthographic;
	FGLTFJsonPerspective  Perspective;

	FGLTFJsonCamera()
		: Type(EGLTFJsonCameraType::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("type"), Type);

		switch (Type)
		{
			case EGLTFJsonCameraType::Orthographic:
				Writer.Write(TEXT("orthographic"), Orthographic);
				break;

			case EGLTFJsonCameraType::Perspective:
				Writer.Write(TEXT("perspective"), Perspective);
				break;

			default:
				break;
		}

		if (CameraControl.IsSet())
		{
			Writer.StartExtensions();
			Writer.Write(EGLTFJsonExtension::EPIC_CameraControls, CameraControl.GetValue());
			Writer.EndExtensions();
		}
	}
};
