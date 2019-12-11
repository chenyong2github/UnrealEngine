// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EHotReloadedClassFlags;

class FVariantManagerUtils
{
public:
	// Invalidate our cached pointers whenever a hot reload happens, as the
	// classes that own those UPropertys might be reinstanced
	static void RegisterForHotReload();
	static void UnregisterForHotReload();

	// Returns true if Property is a UStructProperty with a Struct
	// of type FVector, FColor, FRotator, FQuat, etc
	static bool IsBuiltInStructProperty(const UProperty* Property);

	// Returns the OverrideMaterials property of the UMeshComponent class
	static UArrayProperty* GetOverrideMaterialsProperty();

	// Returns the RelativeLocation property of the USceneComponent class
	static UStructProperty* GetRelativeLocationProperty();

	// Returns the RelativeRotation property of the USceneComponent class
	static UStructProperty* GetRelativeRotationProperty();

	// Returns the RelativeScale3D property of the USceneComponent class
	static UStructProperty* GetRelativeScale3DProperty();

	// Returns the bVisible property of the USceneComponent class
	static UBoolProperty* GetVisibilityProperty();

	// Returns the LightColor property of the ULightComponent class
	static UStructProperty* GetLightColorProperty();

	// Returns the DefaultLightColor property of the UAtmosphericFogComponent class
	static UStructProperty* GetDefaultLightColorProperty();

private:
	// Invalidates all of our cached UProperty pointers
	static void InvalidateCache(UClass* OldClass, UClass* NewClass, EHotReloadedClassFlags Flags);

	static UArrayProperty* OverrideMaterialsProperty;
	static UStructProperty* RelativeLocationProperty;
	static UStructProperty* RelativeRotationProperty;
	static UStructProperty* RelativeScale3DProperty;
	static UBoolProperty* VisiblityProperty;
	static UStructProperty* LightColorProperty;
	static UStructProperty* DefaultLightColorProperty;

	static FDelegateHandle OnHotReloadHandle;
};