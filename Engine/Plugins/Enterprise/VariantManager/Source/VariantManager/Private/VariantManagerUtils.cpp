// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VariantManagerUtils.h"

#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Components/LightComponent.h"
#include "Atmosphere/AtmosphericFogComponent.h"

FArrayProperty* FVariantManagerUtils::OverrideMaterialsProperty = nullptr;
FStructProperty* FVariantManagerUtils::RelativeLocationProperty = nullptr;
FStructProperty* FVariantManagerUtils::RelativeRotationProperty = nullptr;
FStructProperty* FVariantManagerUtils::RelativeScale3DProperty = nullptr;
FBoolProperty* FVariantManagerUtils::VisiblityProperty = nullptr;
FStructProperty* FVariantManagerUtils::LightColorProperty = nullptr;
FStructProperty* FVariantManagerUtils::DefaultLightColorProperty = nullptr;
FDelegateHandle FVariantManagerUtils::OnHotReloadHandle;

void FVariantManagerUtils::RegisterForHotReload()
{
	OnHotReloadHandle = FCoreUObjectDelegates::RegisterClassForHotReloadReinstancingDelegate.AddStatic(&FVariantManagerUtils::InvalidateCache);
}

void FVariantManagerUtils::UnregisterForHotReload()
{
	FCoreUObjectDelegates::RegisterClassForHotReloadReinstancingDelegate.Remove(OnHotReloadHandle);
	OnHotReloadHandle.Reset();
}

bool FVariantManagerUtils::IsBuiltInStructProperty(const FProperty* Property)
{
	bool bIsBuiltIn = false;

		const FStructProperty* StructProp = CastField<const FStructProperty>(Property);
	if (StructProp && StructProp->Struct)
	{
		FName StructName = StructProp->Struct->GetFName();

		bIsBuiltIn =
			StructName == NAME_Rotator ||
			StructName == NAME_Color ||
			StructName == NAME_LinearColor ||
			StructName == NAME_Vector ||
			StructName == NAME_Quat ||
			StructName == NAME_Vector4 ||
			StructName == NAME_Vector2D ||
			StructName == NAME_IntPoint;
	}

	return bIsBuiltIn;
}

FArrayProperty* FVariantManagerUtils::GetOverrideMaterialsProperty()
{
	if (!OverrideMaterialsProperty)
	{
		OverrideMaterialsProperty = FindField<FArrayProperty>( UMeshComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( UMeshComponent, OverrideMaterials ) );
	}

	return OverrideMaterialsProperty;
}

FStructProperty* FVariantManagerUtils::GetRelativeLocationProperty()
{
	if (!RelativeLocationProperty)
	{
		RelativeLocationProperty = FindField<FStructProperty>( USceneComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( USceneComponent, USceneComponent::GetRelativeLocationPropertyName() ) );
	}

	return RelativeLocationProperty;
}

FStructProperty* FVariantManagerUtils::GetRelativeRotationProperty()
{
	if (!RelativeRotationProperty)
	{
		RelativeRotationProperty = FindField<FStructProperty>( USceneComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( USceneComponent, USceneComponent::GetRelativeRotationPropertyName() ) );
	}

	return RelativeRotationProperty;
}

FStructProperty* FVariantManagerUtils::GetRelativeScale3DProperty()
{
	if (!RelativeScale3DProperty)
	{
		RelativeScale3DProperty = FindField<FStructProperty>( USceneComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( USceneComponent, USceneComponent::GetRelativeScale3DPropertyName() ) );
	}

	return RelativeScale3DProperty;
}

FBoolProperty* FVariantManagerUtils::GetVisibilityProperty()
{
	if (!VisiblityProperty)
	{
		VisiblityProperty = FindField<FBoolProperty>( USceneComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( USceneComponent, USceneComponent::GetVisiblePropertyName() ) );
	}

	return VisiblityProperty;
}

FStructProperty* FVariantManagerUtils::GetLightColorProperty()
{
	if (!LightColorProperty)
	{
		LightColorProperty = FindField<FStructProperty>( ULightComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( ULightComponent, LightColor ) );
	}

	return LightColorProperty;
}

FStructProperty* FVariantManagerUtils::GetDefaultLightColorProperty()
{
	if (!DefaultLightColorProperty)
	{
		DefaultLightColorProperty = FindField<FStructProperty>( UAtmosphericFogComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( UAtmosphericFogComponent, DefaultLightColor) );
	}

	return DefaultLightColorProperty;
}

void FVariantManagerUtils::InvalidateCache(UClass* OldClass, UClass* NewClass, EHotReloadedClassFlags Flags)
{
	OverrideMaterialsProperty = nullptr;
	RelativeLocationProperty = nullptr;
	RelativeRotationProperty = nullptr;
	RelativeScale3DProperty = nullptr;
	VisiblityProperty = nullptr;
	LightColorProperty = nullptr;
	DefaultLightColorProperty = nullptr;
}