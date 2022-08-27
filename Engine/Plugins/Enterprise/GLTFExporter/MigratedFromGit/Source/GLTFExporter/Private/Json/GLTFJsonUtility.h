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

	// TODO: replace all default cases (i.e. "unknown") with checkNoEntry() in each ToString

	static const TCHAR* ToString(EGLTFJsonExtension Value)
	{
		switch (Value)
		{
			case EGLTFJsonExtension::KHR_LightsPunctual:       return TEXT("KHR_lights_punctual");
			case EGLTFJsonExtension::KHR_MaterialsUnlit:       return TEXT("KHR_materials_unlit");
			case EGLTFJsonExtension::KHR_MaterialsClearCoat:   return TEXT("KHR_materials_clearcoat");
			case EGLTFJsonExtension::KHR_MeshQuantization:     return TEXT("KHR_mesh_quantization");
			case EGLTFJsonExtension::EPIC_OrbitalCameras:      return TEXT("EPIC_orbital_cameras");
			case EGLTFJsonExtension::EPIC_HDRIBackdrops:       return TEXT("EPIC_hdri_backdrops");
			case EGLTFJsonExtension::EPIC_InteractionHotspots: return TEXT("EPIC_interaction_hotspots");
			case EGLTFJsonExtension::EPIC_LightmapTextures:    return TEXT("EPIC_lightmap_textures");
			case EGLTFJsonExtension::EPIC_ReflectionCaptures:  return TEXT("EPIC_reflection_captures");
			case EGLTFJsonExtension::EPIC_LevelVariantSets:    return TEXT("EPIC_level_variant_sets");
			case EGLTFJsonExtension::EPIC_TextureHDREncoding:  return TEXT("EPIC_texture_hdr_encoding");
			case EGLTFJsonExtension::EPIC_AnimationPlayData:   return TEXT("EPIC_animation_play_data");
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

	static const TCHAR* ToString(EGLTFJsonHDREncoding Value)
	{
		switch (Value)
		{
			case EGLTFJsonHDREncoding::RGBE: return TEXT("RGBE");
			case EGLTFJsonHDREncoding::RGBM: return TEXT("RGBM");
			default:                         return TEXT("UNKNOWN");
		}
	}

	static const TCHAR* ToString(EGLTFJsonCubeFace Value)
	{
		switch (Value)
		{
			case EGLTFJsonCubeFace::PosX: return TEXT("PosX");
			case EGLTFJsonCubeFace::NegX: return TEXT("NegX");
			case EGLTFJsonCubeFace::PosY: return TEXT("PosY");
			case EGLTFJsonCubeFace::NegY: return TEXT("NegY");
			case EGLTFJsonCubeFace::PosZ: return TEXT("PosZ");
			case EGLTFJsonCubeFace::NegZ: return TEXT("NegZ");
			default:                      return TEXT("Unknown");
		}
	}

	static const TCHAR* ToString(EGLTFJsonCameraType Value)
	{
		switch (Value)
		{
			case EGLTFJsonCameraType::Perspective:  return TEXT("perspective");
			case EGLTFJsonCameraType::Orthographic: return TEXT("orthographic");
			default:                                return TEXT("unknown");
		}
	}

	static const TCHAR* ToString(EGLTFJsonLightType Value)
	{
		switch (Value)
		{
			case EGLTFJsonLightType::Directional: return TEXT("directional");
			case EGLTFJsonLightType::Point:       return TEXT("point");
			case EGLTFJsonLightType::Spot:        return TEXT("spot");
			default:                              return TEXT("unknown");
		}
	}

	static const TCHAR* ToString(EGLTFJsonInterpolation Value)
	{
		switch (Value)
		{
			case EGLTFJsonInterpolation::Linear:      return TEXT("LINEAR");
			case EGLTFJsonInterpolation::Step:        return TEXT("STEP");
			case EGLTFJsonInterpolation::CubicSpline: return TEXT("CUBICSPLINE");
			default:                                  return TEXT("UNKNOWN");
		}
	}

	static const TCHAR* ToString(EGLTFJsonTargetPath Value)
	{
		switch (Value)
		{
			case EGLTFJsonTargetPath::Translation: return TEXT("translation");
			case EGLTFJsonTargetPath::Rotation:    return TEXT("rotation");
			case EGLTFJsonTargetPath::Scale:       return TEXT("scale");
			case EGLTFJsonTargetPath::Weights:     return TEXT("weights");
			default:                               return TEXT("unknown");
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

	template <class WriterType, class ElementType, SIZE_T Size>
	static void WriteFixedArray(WriterType& JsonWriter, const FString& Identifier, const ElementType (&Array)[Size])
	{
		JsonWriter.WriteArrayStart(Identifier);
		for (const ElementType& Element : Array)
		{
			JsonWriter.WriteValue(Element);
		}
		JsonWriter.WriteArrayEnd();
	}
};
