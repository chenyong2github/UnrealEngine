// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonEnums.h"

struct FGLTFJsonUtility
{
	template <typename EnumType, typename = typename TEnableIf<TIsEnum<EnumType>::Value>::Type>
	static int32 GetValue(EnumType Enum)
	{
		return static_cast<int32>(Enum);
	}

	static const TCHAR* GetValue(EGLTFJsonExtension Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonExtension::KHR_LightsPunctual:      return TEXT("KHR_lights_punctual");
			case EGLTFJsonExtension::KHR_MaterialsClearCoat:  return TEXT("KHR_materials_clearcoat");
			case EGLTFJsonExtension::KHR_MaterialsUnlit:      return TEXT("KHR_materials_unlit");
			case EGLTFJsonExtension::KHR_MaterialsVariants:   return TEXT("KHR_materials_variants");
			case EGLTFJsonExtension::KHR_MeshQuantization:    return TEXT("KHR_mesh_quantization");
			case EGLTFJsonExtension::KHR_TextureTransform:    return TEXT("KHR_texture_transform");
			case EGLTFJsonExtension::EPIC_AnimationHotspots:  return TEXT("EPIC_animation_hotspots");
			case EGLTFJsonExtension::EPIC_AnimationPlayback:  return TEXT("EPIC_animation_playback");
			case EGLTFJsonExtension::EPIC_BlendModes:         return TEXT("EPIC_blend_modes");
			case EGLTFJsonExtension::EPIC_CameraControls:     return TEXT("EPIC_camera_controls");
			case EGLTFJsonExtension::EPIC_HDRIBackdrops:      return TEXT("EPIC_hdri_backdrops");
			case EGLTFJsonExtension::EPIC_LevelVariantSets:   return TEXT("EPIC_level_variant_sets");
			case EGLTFJsonExtension::EPIC_LightmapTextures:   return TEXT("EPIC_lightmap_textures");
			case EGLTFJsonExtension::EPIC_SkySpheres:         return TEXT("EPIC_sky_spheres");
			case EGLTFJsonExtension::EPIC_TextureHDREncoding: return TEXT("EPIC_texture_hdr_encoding");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonAlphaMode Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonAlphaMode::Opaque: return TEXT("OPAQUE");
			case EGLTFJsonAlphaMode::Blend:  return TEXT("BLEND");
			case EGLTFJsonAlphaMode::Mask:   return TEXT("MASK");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonBlendMode Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonBlendMode::Additive:       return TEXT("ADDITIVE");
			case EGLTFJsonBlendMode::Modulate:       return TEXT("MODULATE");
			case EGLTFJsonBlendMode::AlphaComposite: return TEXT("ALPHACOMPOSITE");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonMimeType Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonMimeType::PNG:  return TEXT("image/png");
			case EGLTFJsonMimeType::JPEG: return TEXT("image/jpeg");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonAccessorType Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonAccessorType::Scalar: return TEXT("SCALAR");
			case EGLTFJsonAccessorType::Vec2:   return TEXT("VEC2");
			case EGLTFJsonAccessorType::Vec3:   return TEXT("VEC3");
			case EGLTFJsonAccessorType::Vec4:   return TEXT("VEC4");
			case EGLTFJsonAccessorType::Mat2:   return TEXT("MAT2");
			case EGLTFJsonAccessorType::Mat3:   return TEXT("MAT3");
			case EGLTFJsonAccessorType::Mat4:   return TEXT("MAT4");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonHDREncoding Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonHDREncoding::RGBM: return TEXT("RGBM");
			case EGLTFJsonHDREncoding::RGBE: return TEXT("RGBE");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonCubeFace Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonCubeFace::PosX: return TEXT("PosX");
			case EGLTFJsonCubeFace::NegX: return TEXT("NegX");
			case EGLTFJsonCubeFace::PosY: return TEXT("PosY");
			case EGLTFJsonCubeFace::NegY: return TEXT("NegY");
			case EGLTFJsonCubeFace::PosZ: return TEXT("PosZ");
			case EGLTFJsonCubeFace::NegZ: return TEXT("NegZ");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonCameraType Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonCameraType::Perspective:  return TEXT("perspective");
			case EGLTFJsonCameraType::Orthographic: return TEXT("orthographic");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonLightType Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonLightType::Directional: return TEXT("directional");
			case EGLTFJsonLightType::Point:       return TEXT("point");
			case EGLTFJsonLightType::Spot:        return TEXT("spot");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonInterpolation Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonInterpolation::Linear:      return TEXT("LINEAR");
			case EGLTFJsonInterpolation::Step:        return TEXT("STEP");
			case EGLTFJsonInterpolation::CubicSpline: return TEXT("CUBICSPLINE");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonTargetPath Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonTargetPath::Translation: return TEXT("translation");
			case EGLTFJsonTargetPath::Rotation:    return TEXT("rotation");
			case EGLTFJsonTargetPath::Scale:       return TEXT("scale");
			case EGLTFJsonTargetPath::Weights:     return TEXT("weights");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonCameraControlMode Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonCameraControlMode::FreeLook: return TEXT("freeLook");
			case EGLTFJsonCameraControlMode::Orbital:  return TEXT("orbital");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static const TCHAR* GetValue(EGLTFJsonShadingModel Enum)
	{
		switch (Enum)
		{
			case EGLTFJsonShadingModel::Default:   return TEXT("Default");
			case EGLTFJsonShadingModel::Unlit:     return TEXT("Unlit");
			case EGLTFJsonShadingModel::ClearCoat: return TEXT("ClearCoat");
			default:
				checkNoEntry();
				return TEXT("");
		}
	}

	static int32 GetComponentCount(EGLTFJsonAccessorType AccessorType)
	{
		switch (AccessorType)
		{
			case EGLTFJsonAccessorType::Scalar: return 1;
			case EGLTFJsonAccessorType::Vec2:   return 2;
			case EGLTFJsonAccessorType::Vec3:   return 3;
			case EGLTFJsonAccessorType::Vec4:   return 4;
			case EGLTFJsonAccessorType::Mat2:   return 4;
			case EGLTFJsonAccessorType::Mat3:   return 9;
			case EGLTFJsonAccessorType::Mat4:   return 16;
			default:                            return 0;
		}
	}

	static int32 GetComponentSize(EGLTFJsonComponentType ComponentType)
	{
		switch (ComponentType)
		{
			case EGLTFJsonComponentType::S8:
			case EGLTFJsonComponentType::U8:
				return 1;
			case EGLTFJsonComponentType::S16:
			case EGLTFJsonComponentType::U16:
				return 2;
			case EGLTFJsonComponentType::S32:
			case EGLTFJsonComponentType::U32:
			case EGLTFJsonComponentType::F32:
				return 4;
			default:
				return 0;
		}
	}

	static EGLTFJsonComponentType ParseComponentType(int32 NumericComponentType)
	{
		const EGLTFJsonComponentType ComponentType = static_cast<EGLTFJsonComponentType>(NumericComponentType);
		switch (ComponentType)
		{
			case EGLTFJsonComponentType::S8:
			case EGLTFJsonComponentType::U8:
			case EGLTFJsonComponentType::S16:
			case EGLTFJsonComponentType::U16:
			case EGLTFJsonComponentType::S32:
			case EGLTFJsonComponentType::U32:
			case EGLTFJsonComponentType::F32:
				return ComponentType;
			default:
				return EGLTFJsonComponentType::None;
		}
	}

	static EGLTFJsonAccessorType ParseAccessorType(const FString& AccessorType)
	{
		if (AccessorType.Equals(TEXT("SCALAR")))
		{
			return EGLTFJsonAccessorType::Scalar;
		}
		if (AccessorType.Equals(TEXT("VEC2")))
		{
			return EGLTFJsonAccessorType::Vec2;
		}
		if (AccessorType.Equals(TEXT("VEC3")))
		{
			return EGLTFJsonAccessorType::Vec3;
		}
		if (AccessorType.Equals(TEXT("VEC4")))
		{
			return EGLTFJsonAccessorType::Vec4;
		}
		if (AccessorType.Equals(TEXT("MAT2")))
		{
			return EGLTFJsonAccessorType::Mat2;
		}
		if (AccessorType.Equals(TEXT("MAT3")))
		{
			return EGLTFJsonAccessorType::Mat3;
		}
		if (AccessorType.Equals(TEXT("MAT4")))
		{
			return EGLTFJsonAccessorType::Mat4;
		}

		return EGLTFJsonAccessorType::None;
	}

	static EGLTFJsonBufferTarget ParseBufferTarget(int32 NumericBufferTarget)
	{
		const EGLTFJsonBufferTarget BufferTarget = static_cast<EGLTFJsonBufferTarget>(NumericBufferTarget);
		switch (BufferTarget)
		{
			case EGLTFJsonBufferTarget::ArrayBuffer:
			case EGLTFJsonBufferTarget::ElementArrayBuffer:
				return BufferTarget;
			default:
				return EGLTFJsonBufferTarget::None;
		}
	}

	static EGLTFJsonMimeType ParseMimeType(const FString& MimeType)
	{
		if (MimeType.Equals(TEXT("image/jpeg")))
		{
			return EGLTFJsonMimeType::JPEG;
		}
		if (MimeType.Equals(TEXT("image/png")))
		{
			return EGLTFJsonMimeType::PNG;
		}

		return EGLTFJsonMimeType::None;
	}
};
