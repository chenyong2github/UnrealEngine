// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonVector4.h"

struct FGLTFJsonOrbitCamera
{
	FGLTFJsonNodeIndex Focus;
	float              MaxDistance;
	float              MinDistance;
	float              MaxAngle;
	float              MinAngle;
	float              DistanceSensitivity;
	float              OrbitSensitivity;
	float              OrbitInertia;
	float              DollyDuration;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteValue(TEXT("focus"), Focus);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("maxDistance"), MaxDistance);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("minDistance"), MinDistance);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("maxAngle"), MaxAngle);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("minAngle"), MinAngle);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("distanceSensitivity"), DistanceSensitivity);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("orbitSensitivity"), OrbitSensitivity);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("orbitInertia"), OrbitInertia);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("dollyDuration"), DollyDuration);

		JsonWriter.WriteObjectEnd();
	}
};
