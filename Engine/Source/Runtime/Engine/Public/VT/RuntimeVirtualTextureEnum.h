// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RuntimeVirtualTextureEnum.generated.h"

/** Maximum number of texture layers we will have in a runtime virtual texture. Increase if we add a ERuntimeVirtualTextureMaterialType with more layers. */
namespace RuntimeVirtualTexture { enum { MaxTextureLayers = 3 }; }

/** 
 * Enumeration of virtual texture stack layouts to support. 
 * Extend this enumeration with other layouts as required. For example we will probably want to add a displacement texture option.
 * This "fixed function" approach will probably break down if we end up needing to support some complex set of attribute combinations but it is OK to begin with.
 */
UENUM()
enum class ERuntimeVirtualTextureMaterialType : uint8
{
	BaseColor UMETA(DisplayName = "Base Color"),
	BaseColor_Normal_DEPRECATED UMETA(Hidden),
	BaseColor_Normal_Specular UMETA(DisplayName = "Base Color, Normal, Roughness, Specular"),
	BaseColor_Normal_Specular_YCoCg UMETA(DisplayName = "YCoCg Base Color, Normal, Roughness, Specular", ToolTip = "Base Color is stored in YCoCg space. This requires more memory but may provide better quality."),
	BaseColor_Normal_Specular_Mask_YCoCg UMETA(DisplayName = "YCoCg Base Color, Normal, Roughness, Specular, Mask", ToolTip="Base Color is stored in YCoCg space. This requires more memory but may provide better quality."),
	WorldHeight UMETA(DisplayName = "World Height"),

	Count UMETA(Hidden),
};

namespace RuntimeVirtualTexture { enum { MaterialType_NumBits = 3 }; }
static_assert((uint32)ERuntimeVirtualTextureMaterialType::Count <= (1 << (uint32)RuntimeVirtualTexture::MaterialType_NumBits), "NumBits is too small");

/** Enumeration of main pass behaviors when rendering to a runtime virtual texture. */
UENUM()
enum class ERuntimeVirtualTextureMainPassType : uint8
{
	/** If there is no valid virtual texture target we will not render at all. Use this for items that we don't mind removing if there is no virtual texture support. */
	Never UMETA(DisplayName = "Virtual Texture Only"),
	/** If and only if there is no valid virtual texture target we will render to the main pass. Use this for items that we must have whether virtual texture is supported or not. */
	Exclusive UMETA(DisplayName = "Virtual Texture OR Main Pass"),
	/** We will render to any valid virtual texture target AND the main pass. Use this for items that need to both read and write the virtual texture. For example, some landscape setups need this. */
	Always UMETA(DisplayName = "Virtual Texture AND Main Pass"),
};

/** Enumeration of runtime virtual texture debug modes. */
UENUM()
enum class ERuntimeVirtualTextureDebugType
{
	None,
	Debug
};
