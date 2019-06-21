// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"
#include "LiveLinkTransformController.h"
#include "Engine/EngineTypes.h"
#include "LiveLinkLightController.generated.h"


/**
 * Controller that uses LiveLink light data to drive a light component. 
 * UPointLightComponent and USpotLightComponent are supported for specific properties
 */
UCLASS()
class LIVELINKCOMPONENTS_API ULiveLinkLightController : public ULiveLinkControllerBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="LiveLink", meta=(UseComponentPicker, AllowedClasses="LightComponent"))
	FComponentReference ComponentToControl;

	UPROPERTY(EditAnywhere, Category = "LiveLink", meta=(ShowOnlyInnerProperties))
	FLiveLinkTransformControllerData TransformData;

public:
	virtual void OnEvaluateRegistered() override;
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectRepresentation& SubjectRepresentation) override;
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) override;

#if WITH_EDITOR
	virtual void InitializeInEditor() override;
#endif
};