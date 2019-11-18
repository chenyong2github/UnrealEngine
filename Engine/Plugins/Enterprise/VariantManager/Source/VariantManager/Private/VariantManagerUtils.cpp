// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VariantManagerUtils.h"

#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Components/LightComponent.h"
#include "Atmosphere/AtmosphericFogComponent.h"

UArrayProperty* FVariantManagerUtils::OverrideMaterialsProperty = nullptr;
UStructProperty* FVariantManagerUtils::RelativeLocationProperty = nullptr;
UStructProperty* FVariantManagerUtils::RelativeRotationProperty = nullptr;
UStructProperty* FVariantManagerUtils::RelativeScale3DProperty = nullptr;
UBoolProperty* FVariantManagerUtils::VisiblityProperty = nullptr;
UStructProperty* FVariantManagerUtils::LightColorProperty = nullptr;
UStructProperty* FVariantManagerUtils::DefaultLightColorProperty = nullptr;
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

bool FVariantManagerUtils::IsBuiltInStructProperty(const UProperty* Property)
{
	bool bIsBuiltIn = false;

	const UStructProperty* StructProp = Cast<const UStructProperty>(Property);
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

UArrayProperty* FVariantManagerUtils::GetOverrideMaterialsProperty()
{
	if (!OverrideMaterialsProperty)
	{
		OverrideMaterialsProperty = FindField<UArrayProperty>( UMeshComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( UMeshComponent, OverrideMaterials ) );
	}

	return OverrideMaterialsProperty;
}

UStructProperty* FVariantManagerUtils::GetRelativeLocationProperty()
{
	if (!RelativeLocationProperty)
	{
		RelativeLocationProperty = FindField<UStructProperty>(USceneComponent::StaticClass(), *USceneComponent::GetRelativeLocationPropertyName().ToString());
	}

	return RelativeLocationProperty;
}

UStructProperty* FVariantManagerUtils::GetRelativeRotationProperty()
{
	if (!RelativeRotationProperty)
	{
		RelativeRotationProperty = FindField<UStructProperty>(USceneComponent::StaticClass(), *USceneComponent::GetRelativeRotationPropertyName().ToString());
	}

	return RelativeRotationProperty;
}

UStructProperty* FVariantManagerUtils::GetRelativeScale3DProperty()
{
	if (!RelativeScale3DProperty)
	{
		RelativeScale3DProperty = FindField<UStructProperty>(USceneComponent::StaticClass(), *USceneComponent::GetRelativeScale3DPropertyName().ToString());
	}

	return RelativeScale3DProperty;
}

UBoolProperty* FVariantManagerUtils::GetVisibilityProperty()
{
	if (!VisiblityProperty)
	{
		VisiblityProperty = FindField<UBoolProperty>(USceneComponent::StaticClass(), *USceneComponent::GetVisiblePropertyName().ToString());
	}

	return VisiblityProperty;
}

UStructProperty* FVariantManagerUtils::GetLightColorProperty()
{
	if (!LightColorProperty)
	{
		LightColorProperty = FindField<UStructProperty>( ULightComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( ULightComponent, LightColor ) );
	}

	return LightColorProperty;
}

UStructProperty* FVariantManagerUtils::GetDefaultLightColorProperty()
{
	if (!DefaultLightColorProperty)
	{
		DefaultLightColorProperty = FindField<UStructProperty>( UAtmosphericFogComponent::StaticClass(), GET_MEMBER_NAME_CHECKED( UAtmosphericFogComponent, DefaultLightColor) );
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