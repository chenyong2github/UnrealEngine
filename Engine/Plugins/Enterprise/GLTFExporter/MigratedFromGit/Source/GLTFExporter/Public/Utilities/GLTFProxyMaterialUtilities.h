// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Json/GLTFJsonEnums.h"
#include "MaterialTypes.h"
#include "UObject/ObjectMacros.h"

class UMaterial;
class UMaterialInstance;
class UMaterialInstanceDynamic;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UTexture;

class GLTFEXPORTER_API FGLTFProxyMaterialUtilities
{
public:

	template <typename MaterialType, typename = typename TEnableIf<TIsDerivedFrom<MaterialType, UMaterialInstance>::Value>::Type>
	static MaterialType* CreateProxyMaterial(EGLTFJsonShadingModel ShadingModel, UObject* Outer = GetTransientPackage(), FName Name = NAME_None, EObjectFlags Flags = RF_NoFlags)
	{
		MaterialType* MaterialInstance = NewObject<MaterialType>(Outer, Name, Flags);
		MaterialInstance->Parent = GetBaseMaterial(ShadingModel);
		return MaterialInstance;
	}

	static bool IsProxyMaterial(const UMaterial* Material);
	static bool IsProxyMaterial(const UMaterialInterface* Material);

	static UMaterial* GetBaseMaterial(EGLTFJsonShadingModel ShadingModel);

	static UMaterialInterface* GetProxyMaterial(UMaterialInterface* OriginalMaterial);
	static void SetProxyMaterial(UMaterialInterface* OriginalMaterial, UMaterialInterface* ProxyMaterial);

	static bool GetParameterValue(const UMaterialInterface* Material, const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue, bool NonDefaultOnly = false);
	static bool GetParameterValue(const UMaterialInterface* Material, const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue, bool NonDefaultOnly = false);
	static bool GetParameterValue(const UMaterialInterface* Material, const FHashedMaterialParameterInfo& ParameterInfo, UTexture*& OutValue, bool NonDefaultOnly = false);

	static void SetParameterValue(UMaterialInstanceDynamic* Material, const FHashedMaterialParameterInfo& ParameterInfo, float Value, bool NonDefaultOnly = false);
	static void SetParameterValue(UMaterialInstanceDynamic* Material, const FHashedMaterialParameterInfo& ParameterInfo, const FLinearColor& Value, bool NonDefaultOnly = false);
	static void SetParameterValue(UMaterialInstanceDynamic* Material, const FHashedMaterialParameterInfo& ParameterInfo, UTexture* Value, bool NonDefaultOnly = false);

#if WITH_EDITOR
	static void SetParameterValue(UMaterialInstanceConstant* Material, const FHashedMaterialParameterInfo& ParameterInfo, float Value, bool NonDefaultOnly = false);
	static void SetParameterValue(UMaterialInstanceConstant* Material, const FHashedMaterialParameterInfo& ParameterInfo, const FLinearColor& Value, bool NonDefaultOnly = false);
	static void SetParameterValue(UMaterialInstanceConstant* Material, const FHashedMaterialParameterInfo& ParameterInfo, UTexture* Value, bool NonDefaultOnly = false);
#endif

	static bool GetTwoSided(const UMaterialInstance* Material, bool& OutValue, bool NonDefaultOnly = false);
	static bool GetBlendMode(const UMaterialInstance* Material, EBlendMode& OutValue, bool NonDefaultOnly = false);
	static bool GetOpacityMaskClipValue(const UMaterialInstance* Material, float& OutValue, bool NonDefaultOnly = false);

	static void SetTwoSided(UMaterialInstance* Material, bool Value, bool NonDefaultOnly = false);
	static void SetBlendMode(UMaterialInstance* Material, EBlendMode Value, bool NonDefaultOnly = false);
	static void SetOpacityMaskClipValue(UMaterialInstance* Material, float Value, bool NonDefaultOnly = false);
};
