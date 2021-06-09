// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHIShaderPlatformDefinitions.h: Localizable Friendly Names for Shader Platforms
=============================================================================*/

#pragma once

static FText GetFriendlyShaderPlatformName(const EShaderPlatform InShaderPlatform)
{
	switch (InShaderPlatform)
	{
	case SP_PCD3D_SM5:
	case SP_METAL_SM5:
	case SP_VULKAN_SM5:
	{
		static const FText Description = NSLOCTEXT("FriendlyShaderPlatformNames", "Generic_SM5_loc", "SM5");
		return Description;
	}
	break;

	case SP_METAL_SM5_NOTESS:
	case SP_METAL_MRT_MAC:
	case SP_METAL_MRT:
	{
		static const FText Description = NSLOCTEXT("FriendlyShaderPlatformNames", "Metal_SM5_loc", "Metal SM5");
		return Description;
	}
	break;

	case SP_PCD3D_ES3_1:
	case SP_VULKAN_PCES3_1:
	case SP_OPENGL_PCES3_1:
	{
		static const FText Description = NSLOCTEXT("FriendlyShaderPlatformNames", "Generic_ES31_loc", "ES31");
		return Description;
	}
	break;

	case SP_VULKAN_ES3_1_ANDROID:
	{
		static const FText Description = NSLOCTEXT("FriendlyShaderPlatformNames", "Android_Vulkan_ES31_loc", "Android Vulkan ES31");
		return Description;
	}
	break;

	case SP_OPENGL_ES3_1_ANDROID:
	{
		static const FText Description = NSLOCTEXT("FriendlyShaderPlatformNames", "Android_ES31_loc", "Android ES31");
		return Description;
	}
	break;

	case SP_VULKAN_ES3_1_LUMIN:
	{
		static const FText Description = NSLOCTEXT("FriendlyShaderPlatformNames", "Lumin_Vulkan_ES31_loc", "Lumin Vulkan ES31");
		return Description;
	}
	break;

	case SP_METAL:
	case SP_METAL_MACES3_1:
	{
		static const FText Description = NSLOCTEXT("FriendlyShaderPlatformNames", "iOS_Metal_Mace_31_loc", "Metal ES31");
		return Description;
	}
	break;

	case SP_VULKAN_SM5_LUMIN:
	{
		static const FText Description = NSLOCTEXT("FriendlyShaderPlatformNames", "Lumin_Vulkan_SM5_loc", "Lumin Vulkan SM5");
		return Description;
	}
	break;

	case SP_VULKAN_SM5_ANDROID:
	{
		static const FText Description = NSLOCTEXT("FriendlyShaderPlatformNames", "Android_Vulkan_SM5_loc", "Android Vulkan SM5");
		return Description;
	}
	break;

	default:
		if (FStaticShaderPlatformNames::IsStaticPlatform(InShaderPlatform))
		{
			return FDataDrivenShaderPlatformInfo::GetFriendlyName(InShaderPlatform);
		}
		break;
	};

	return FText::GetEmpty();
}
