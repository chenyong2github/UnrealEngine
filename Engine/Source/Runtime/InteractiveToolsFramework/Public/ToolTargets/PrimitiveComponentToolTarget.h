// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargets/ToolTarget.h"

#include "PrimitiveComponentToolTarget.generated.h"

/** 
 * An abstract tool target to share some reusable code for tool targets that are
 * backed by primitive components. 
 */
UCLASS(Transient, Abstract)
class INTERACTIVETOOLSFRAMEWORK_API UPrimitiveComponentToolTarget : public UToolTarget, public IPrimitiveComponentBackedTarget
{
	GENERATED_BODY()
public:
	virtual bool IsValid() const override;

	// IPrimitiveComponentBackedTarget implementation
	virtual UPrimitiveComponent* GetOwnerComponent() const override;
	virtual AActor* GetOwnerActor() const override;
	virtual void SetOwnerVisibility(bool bVisible) const override;
	virtual FTransform GetWorldTransform() const override;
	virtual bool HitTestComponent(const FRay& WorldRay, FHitResult& OutHit) const override;

protected:
	UPrimitiveComponent* Component;
};