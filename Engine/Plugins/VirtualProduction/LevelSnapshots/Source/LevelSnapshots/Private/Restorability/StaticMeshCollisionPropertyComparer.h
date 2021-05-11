// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LevelSnapshotsModule.h"
#include "Restorability/IPropertyComparer.h"

/**
 * UStaticMeshComponents uses the property bUseDefaultCollision to determine whether to use the settings from the
 * UStaticMesh asset or the ones specified in UPrimitiveComponent::BodyInstance. If bUseDefaultCollision = true, the
 * component should not show up as modified
 */
class FStaticMeshCollisionPropertyComparer : public IPropertyComparer
{
public:
	
	static void Register(FLevelSnapshotsModule& Module);

	//~ Begin IPropertyComparer Interface
	virtual EPropertyComparison ShouldConsiderPropertyEqual(const FPropertyComparisonParams& Params) const override;
	//~ End IPropertyComparer Interface

private:

	// Hidden so we can assume this class is only ever registered for UStaticMeshComponent
	FStaticMeshCollisionPropertyComparer() = default;
	
	const FProperty* BodyInstanceProperty;
	const FProperty* UseDefaultCollisionProperty;
};
