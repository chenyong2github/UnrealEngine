// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "ClearReplacementShaders.h"
#include "ShaderParameterUtils.h"
#include "RendererInterface.h"

IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementVS				            , SF_Vertex);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementVS_Bounds                  , SF_Vertex);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementVS_Depth		            , SF_Vertex);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementPS                         , SF_Pixel);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementPS_128                     , SF_Pixel);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementPS_Zero                    , SF_Pixel);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Buffer_Uint_Bounds      , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Texture2D_Float4_Bounds , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Buffer_Uint_Zero        , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Texture2DArray_Uint_Zero, SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Buffer_Uint             , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Texture2DArray_Uint     , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Texture3D_Float4        , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Texture2D_Float4        , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Texture2DArray_Float4   , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Texture3D_Uint4         , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Texture2D_Uint4         , SF_Compute);
IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<>, FClearReplacementCS_Texture2DArray_Uint4    , SF_Compute);
