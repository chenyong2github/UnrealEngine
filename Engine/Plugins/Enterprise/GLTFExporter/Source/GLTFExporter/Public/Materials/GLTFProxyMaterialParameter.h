// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Materials/GLTFProxyMaterialParameterInfo.h"

template <typename ParameterType, typename MaterialType, typename = typename TEnableIf<TIsDerivedFrom<MaterialType, UMaterialInterface>::Value>::Type>
class TGLTFProxyMaterialParameterBase
{
public:

	TGLTFProxyMaterialParameterBase(const TGLTFProxyMaterialParameterInfo<ParameterType>& ParameterInfo, MaterialType* Material)
		: ParameterInfo(ParameterInfo)
		, Material(Material)
	{
	}

	bool Get(ParameterType& OutValue, bool NonDefaultOnly = false) const
	{
		return ParameterInfo.Get(Material, OutValue, NonDefaultOnly);
	}

	ParameterType Get() const
	{
		ParameterType Value;
		if (!ParameterInfo.Get(Material, Value))
		{
			checkNoEntry();
		}
		return Value;
	}

protected:

	const TGLTFProxyMaterialParameterInfo<ParameterType> ParameterInfo;
	MaterialType* Material;
};

template <typename ParameterType, typename MaterialType, typename = typename TEnableIf<TIsDerivedFrom<MaterialType, UMaterialInterface>::Value>::Type>
class TGLTFProxyMaterialParameter: public TGLTFProxyMaterialParameterBase<ParameterType, MaterialType, void>
{
public:

	using TGLTFProxyMaterialParameterBase<ParameterType, MaterialType, void>::TGLTFProxyMaterialParameterBase;
};

template <typename ParameterType>
class TGLTFProxyMaterialParameter<ParameterType, UMaterialInstanceDynamic, void>: public TGLTFProxyMaterialParameterBase<ParameterType, UMaterialInstanceDynamic, void>
{
public:

	using TGLTFProxyMaterialParameterBase<ParameterType, UMaterialInstanceDynamic, void>::TGLTFProxyMaterialParameterBase;

	void Set(const ParameterType& Value, bool NonDefaultOnly = false) const
	{
		this->ParameterInfo.Set(this->Material, Value, NonDefaultOnly);
	}
};

template <typename ParameterType>
class TGLTFProxyMaterialParameter<ParameterType, UMaterialInstanceConstant, void>: public TGLTFProxyMaterialParameterBase<ParameterType, UMaterialInstanceConstant, void>
{
public:

	using TGLTFProxyMaterialParameterBase<ParameterType, UMaterialInstanceConstant, void>::TGLTFProxyMaterialParameterBase;

#if WITH_EDITOR
	void Set(const ParameterType& Value, bool NonDefaultOnly = false) const
	{
		this->ParameterInfo.Set(this->Material, Value, NonDefaultOnly);
	}
#endif
};

template <typename MaterialType, typename = typename TEnableIf<TIsDerivedFrom<MaterialType, UMaterialInstance>::Value>::Type>
class TGLTFProxyMaterialTextureParameter
{
public:

	TGLTFProxyMaterialTextureParameter(const FGLTFProxyMaterialTextureParameterInfo& ParameterInfo, MaterialType* Material)
		: Texture(ParameterInfo.Texture, Material)
		, UVIndex(ParameterInfo.UVIndex, Material)
		, UVOffset(ParameterInfo.UVOffset, Material)
		, UVScale(ParameterInfo.UVScale, Material)
		, UVRotation(ParameterInfo.UVRotation, Material)
	{
	}

	TGLTFProxyMaterialParameter<UTexture*, MaterialType> Texture;
	TGLTFProxyMaterialParameter<float, MaterialType> UVIndex;
	TGLTFProxyMaterialParameter<FLinearColor, MaterialType> UVOffset;
	TGLTFProxyMaterialParameter<FLinearColor, MaterialType> UVScale;
	TGLTFProxyMaterialParameter<float, MaterialType> UVRotation;
};
