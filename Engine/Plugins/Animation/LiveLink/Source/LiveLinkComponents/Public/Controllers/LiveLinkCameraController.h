// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"
#include "LiveLinkTransformController.h"
#include "Engine/EngineTypes.h"
#include "LiveLinkCameraController.generated.h"


/**
 */
UCLASS()
class LIVELINKCOMPONENTS_API ULiveLinkCameraController : public ULiveLinkControllerBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="LiveLink", meta=(UseComponentPicker, AllowedClasses="CameraComponent"))
	FComponentReference ComponentToControl;

	UPROPERTY(EditAnywhere, Category = "LiveLink", meta = (ShowOnlyInnerProperties))
	FLiveLinkTransformControllerData TransformData;

public:
	virtual void OnEvaluateRegistered() override;
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectRepresentation& SubjectRepresentation) override;
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) override;

#if WITH_EDITOR
	virtual void InitializeInEditor() override;
#endif
};