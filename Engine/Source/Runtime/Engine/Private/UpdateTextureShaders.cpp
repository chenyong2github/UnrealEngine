// Copyright Epic Games, Inc. All Rights Reserved.

#include "UpdateTextureShaders.h"

IMPLEMENT_SHADER_TYPE4_WITH_TEMPLATE_PREFIX(, ENGINE_API, FUpdateTexture2DSubresouceCS, SF_Compute);
IMPLEMENT_SHADER_TYPE4_WITH_TEMPLATE_PREFIX(, ENGINE_API, FUpdateTexture3DSubresouceCS, SF_Compute);

IMPLEMENT_SHADER_TYPE(,FCopyTexture2DCS,TEXT("/Engine/Private/UpdateTextureShaders.usf"),TEXT("CopyTexture2DCS"),SF_Compute);

IMPLEMENT_SHADER_TYPE(template<>, TCopyTexture2DCS<1u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TCopyTexture2DCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TCopyTexture2DCS<2u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TCopyTexture2DCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TCopyTexture2DCS<4u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TCopyTexture2DCS"), SF_Compute);
typedef TCopyTexture2DCS< 3u, float > FCopyTexture2DCS_Float3;
IMPLEMENT_SHADER_TYPE(template<>, FCopyTexture2DCS_Float3, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TCopyTexture2DCS"), SF_Compute);

IMPLEMENT_SHADER_TYPE4_WITH_TEMPLATE_PREFIX(template<>, ENGINE_API, TUpdateTexture2DSubresouceCS<1u>, SF_Compute);
IMPLEMENT_SHADER_TYPE4_WITH_TEMPLATE_PREFIX(template<>, ENGINE_API, TUpdateTexture2DSubresouceCS<2u>, SF_Compute);
IMPLEMENT_SHADER_TYPE4_WITH_TEMPLATE_PREFIX(template<>, ENGINE_API, TUpdateTexture2DSubresouceCS<3u>, SF_Compute);
IMPLEMENT_SHADER_TYPE4_WITH_TEMPLATE_PREFIX(template<>, ENGINE_API, TUpdateTexture2DSubresouceCS<4u>, SF_Compute);

IMPLEMENT_SHADER_TYPE4_WITH_TEMPLATE_PREFIX(template<>, ENGINE_API, TCopyDataCS<2>, SF_Compute);
IMPLEMENT_SHADER_TYPE4_WITH_TEMPLATE_PREFIX(template<>, ENGINE_API, TCopyDataCS<1>, SF_Compute);