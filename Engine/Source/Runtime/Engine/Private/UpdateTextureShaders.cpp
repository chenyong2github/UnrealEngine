// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UpdateTextureShaders.h"

IMPLEMENT_SHADER_TYPE(,FUpdateTexture2DSubresouceCS,TEXT("/Engine/Private/UpdateTextureShaders.usf"),TEXT("UpdateTexture2DSubresourceCS"),SF_Compute);
IMPLEMENT_SHADER_TYPE(,FUpdateTexture3DSubresouceCS, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("UpdateTexture3DSubresourceCS"), SF_Compute);

IMPLEMENT_SHADER_TYPE(,FCopyTexture2DCS,TEXT("/Engine/Private/UpdateTextureShaders.usf"),TEXT("CopyTexture2DCS"),SF_Compute);

IMPLEMENT_SHADER_TYPE(template<>, TCopyTexture2DCS<1u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TCopyTexture2DCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TCopyTexture2DCS<2u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TCopyTexture2DCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TCopyTexture2DCS<4u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TCopyTexture2DCS"), SF_Compute);
typedef TCopyTexture2DCS< 3u, float > FCopyTexture2DCS_Float3;
IMPLEMENT_SHADER_TYPE(template<>, FCopyTexture2DCS_Float3, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TCopyTexture2DCS"), SF_Compute);

IMPLEMENT_SHADER_TYPE(template<>, TUpdateTexture2DSubresouceCS<1u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TUpdateTexture2DSubresourceCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TUpdateTexture2DSubresouceCS<2u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TUpdateTexture2DSubresourceCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TUpdateTexture2DSubresouceCS<3u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TUpdateTexture2DSubresourceCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TUpdateTexture2DSubresouceCS<4u>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("TUpdateTexture2DSubresourceCS"), SF_Compute);

IMPLEMENT_SHADER_TYPE(template<>, TCopyDataCS<2>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("CopyData2CS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TCopyDataCS<1>, TEXT("/Engine/Private/UpdateTextureShaders.usf"), TEXT("CopyData1CS"), SF_Compute);
