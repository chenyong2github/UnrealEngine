// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonVector4.h"

struct FGLTFJsonCameraControl
{
	EGLTFJsonCameraControlMode Mode;
	FGLTFJsonNodeIndex         Focus;
	float                      MaxDistance;
	float                      MinDistance;
	float                      MaxPitch;
	float                      MinPitch;
	float                      MaxYaw;
	float                      MinYaw;
	float                      RotationSensitivity;
	float                      RotationInertia;
	float                      DollySensitivity;
	float                      DollyDuration;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteValue(TEXT("mode"), FGLTFJsonUtility::ToString(Mode));
		JsonWriter.WriteValue(TEXT("focus"), Focus);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("maxDistance"), MaxDistance);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("minDistance"), MinDistance);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("maxPitch"), MaxPitch);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("minPitch"), MinPitch);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("maxYaw"), MaxYaw);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("minYaw"), MinYaw);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("rotationSensitivity"), RotationSensitivity);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("rotationInertia"), RotationInertia);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("dollySensitivity"), DollySensitivity);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("dollyDuration"), DollyDuration);

		JsonWriter.WriteObjectEnd();
	}
};
