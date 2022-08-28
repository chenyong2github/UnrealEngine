// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonExtensions.h"

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
			case EGLTFJsonExtension::KHR_LightsPunctual:       return TEXT("KHR_lights_punctual");
			case EGLTFJsonExtension::KHR_MaterialsUnlit:       return TEXT("KHR_materials_unlit");
			case EGLTFJsonExtension::KHR_MaterialsClearCoat:   return TEXT("KHR_materials_clearcoat");
			case EGLTFJsonExtension::KHR_MeshQuantization:     return TEXT("KHR_mesh_quantization");
			case EGLTFJsonExtension::EPIC_CameraOrbital:       return TEXT("EPIC_camera_orbital");
			case EGLTFJsonExtension::EPIC_HDRIBackdrops:       return TEXT("EPIC_hdri_backdrops");
			case EGLTFJsonExtension::EPIC_InteractionHotspots: return TEXT("EPIC_interaction_hotspots");
			case EGLTFJsonExtension::EPIC_LightmapTextures:    return TEXT("EPIC_lightmap_textures");
			case EGLTFJsonExtension::EPIC_ReflectionCaptures:  return TEXT("EPIC_reflection_captures");
			case EGLTFJsonExtension::EPIC_LevelVariantSets:    return TEXT("EPIC_level_variant_sets");
			case EGLTFJsonExtension::EPIC_TextureHDREncoding:  return TEXT("EPIC_texture_hdr_encoding");
			default:                                           return TEXT("unknown");
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

	template <class WriterType, class ContainerType>
    static void WriteObjectArray(WriterType& JsonWriter, const FString& Identifier, const ContainerType& Container, FGLTFJsonExtensions& Extensions, bool bWriteIfEmpty = false)
	{
		if (Container.Num() > 0 || bWriteIfEmpty)
		{
			JsonWriter.WriteArrayStart(Identifier);
			for (const auto& Element : Container)
			{
				Element.WriteObject(JsonWriter, Extensions);
			}
			JsonWriter.WriteArrayEnd();
		}
	}

	template <class WriterType, class ContainerType>
    static void WriteStringArray(WriterType& JsonWriter, const FString& Identifier, const ContainerType& Container, bool bWriteIfEmpty = false)
	{
		if (Container.Num() > 0 || bWriteIfEmpty)
		{
			JsonWriter.WriteArrayStart(Identifier);
			for (const auto& Element : Container)
			{
				JsonWriter.WriteValue(ToString(Element));
			}
			JsonWriter.WriteArrayEnd();
		}
	}
};
