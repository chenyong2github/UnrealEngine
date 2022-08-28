// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonEnums.h"

struct FGLTFJsonUtility
{
	template <typename EnumType>
	static int32 ToInteger(EnumType Value)
	{
		return static_cast<int32>(Value);
	}

	static const TCHAR* ToString(EGLTFJsonExtension Value)
	{
		switch (Value)
		{
			case EGLTFJsonExtension::KHR_LightsPunctual:     return TEXT("KHR_lights_punctual");
			case EGLTFJsonExtension::KHR_MaterialsUnlit:     return TEXT("KHR_materials_unlit");
			case EGLTFJsonExtension::KHR_MaterialsClearCoat: return TEXT("KHR_materials_clearcoat");
			case EGLTFJsonExtension::KHR_MeshQuantization:   return TEXT("KHR_mesh_quantization");
			default:                                         return TEXT("unknown");
		}
	}

	static const TCHAR* ToString(EGLTFJsonAlphaMode Value)
	{
		switch (Value)
		{
			case EGLTFJsonAlphaMode::Opaque: return TEXT("OPAQUE");
			case EGLTFJsonAlphaMode::Blend:  return TEXT("BLEND");
			case EGLTFJsonAlphaMode::Mask:   return TEXT("MASK");
			default:                         return TEXT("UNKNOWN");
		}
	}

	static const TCHAR* ToString(EGLTFJsonMimeType Value)
	{
		switch (Value)
		{
			case EGLTFJsonMimeType::PNG:  return TEXT("image/png");
			case EGLTFJsonMimeType::JPEG: return TEXT("image/jpeg");
			default:                      return TEXT("unknown");
		}
	}

	static const TCHAR* ToString(EGLTFJsonAccessorType Value)
	{
		switch (Value)
		{
			case EGLTFJsonAccessorType::Scalar: return TEXT("SCALAR");
			case EGLTFJsonAccessorType::Vec2:   return TEXT("VEC2");
			case EGLTFJsonAccessorType::Vec3:   return TEXT("VEC3");
			case EGLTFJsonAccessorType::Vec4:   return TEXT("VEC4");
			case EGLTFJsonAccessorType::Mat2:   return TEXT("MAT2");
			case EGLTFJsonAccessorType::Mat3:   return TEXT("MAT3");
			case EGLTFJsonAccessorType::Mat4:   return TEXT("MAT4");
			default:                            return TEXT("UNKNOWN");
		}
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	static void WriteExactValue(TJsonWriter<CharType, PrintPolicy>& JsonWriter, float Value)
	{
		FString ExactStringRepresentation = FString::Printf(TEXT("%.9g"), Value);
		JsonWriter.WriteRawJSONValue(ExactStringRepresentation);
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	static void WriteExactValue(TJsonWriter<CharType, PrintPolicy>& JsonWriter, const FString& Identifier, float Value)
	{
		FString ExactStringRepresentation = FString::Printf(TEXT("%.9g"), Value);
		JsonWriter.WriteRawJSONValue(Identifier, ExactStringRepresentation);
	}

	template <class WriterType, class ElementType, class AllocatorType>
	static void WriteObjectArray(WriterType& JsonWriter, const FString& Identifier, const TArray<ElementType, AllocatorType>& ObjectArray)
	{
		if (ObjectArray.Num() > 0)
		{
			JsonWriter.WriteArrayStart(Identifier);
			for (const ElementType& Object : ObjectArray)
			{
				Object.WriteObject(JsonWriter);
			}
			JsonWriter.WriteArrayEnd();
		}
	}
};
