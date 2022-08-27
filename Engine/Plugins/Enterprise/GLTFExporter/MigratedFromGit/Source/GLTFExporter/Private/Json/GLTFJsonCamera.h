// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonUtility.h"
#include "Serialization/JsonSerializer.h"

struct GLTFEXPORTER_API FGLTFJsonOrthographic
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

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteValue(TEXT("xmag"), XMag);
		JsonWriter.WriteValue(TEXT("ymag"), YMag);
		JsonWriter.WriteValue(TEXT("zfar"), ZFar);
		JsonWriter.WriteValue(TEXT("znear"), ZNear);

		JsonWriter.WriteObjectEnd();
	}
};

struct GLTFEXPORTER_API FGLTFJsonPerspective
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

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		if (AspectRatio != 0)
		{
			JsonWriter.WriteValue(TEXT("aspectRatio"), AspectRatio);
		}

		JsonWriter.WriteValue(TEXT("yfov"), YFov);

		if (ZFar != 0)
		{
			JsonWriter.WriteValue(TEXT("zfar"), ZFar);
		}

		JsonWriter.WriteValue(TEXT("znear"), ZNear);

		JsonWriter.WriteObjectEnd();
	}
};

struct GLTFEXPORTER_API FGLTFJsonCamera
{
	FString Name;

	EGLTFJsonCameraType Type;

	union {
		FGLTFJsonOrthographic Orthographic;
		FGLTFJsonPerspective  Perspective;
	};

	FGLTFJsonCamera()
		: Type(EGLTFJsonCameraType::None)
		, Orthographic()
		, Perspective()
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("name"), Name);
		}

		JsonWriter.WriteValue(TEXT("type"), FGLTFJsonUtility::ToString(Type));

		switch (Type)
		{
			case EGLTFJsonCameraType::Orthographic:
				JsonWriter.WriteIdentifierPrefix(TEXT("orthographic"));
				Orthographic.WriteObject(JsonWriter, Extensions);
				break;

			case EGLTFJsonCameraType::Perspective:
				JsonWriter.WriteIdentifierPrefix(TEXT("perspective"));
				Perspective.WriteObject(JsonWriter, Extensions);
				break;

			default:
				break;
		}

		JsonWriter.WriteObjectEnd();
	}
};
