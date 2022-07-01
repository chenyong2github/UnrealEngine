// Copyright Epic Games, Inc. All Rights Reserved.

/*================================================================================================
VirtualShadowMapDefinitions.h: used in virtual shadow map shaders and C++ code to define common constants
!!! Changing this file requires recompilation of the engine !!!
=================================================================================================*/

#pragma once

#define VIRTUAL_SHADOW_MAP_VISUALIZE_NONE						0
#define VIRTUAL_SHADOW_MAP_VISUALIZE_SHADOW_FACTOR				(1 << 0)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_CLIPMAP_OR_MIP				(1 << 1)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_VIRTUAL_PAGE				(1 << 2)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_CACHED_PAGE				(1 << 3)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_SMRT_RAY_COUNT				(1 << 4)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_CLIPMAP_VIRTUAL_SPACE		(1 << 5)
#define VIRTUAL_SHADOW_MAP_VISUALIZE_GENERAL_DEBUG				(1 << 6)


#define VSM_PROJ_FLAG_CURRENT_DISTANT_LIGHT (1U << 0)
