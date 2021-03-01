// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"
#include "Controllers/LiveLinkTransformController.h"
#include "Engine/EngineTypes.h"
#include "LiveLinkCameraController.generated.h"

class ULensFile;


/**
 */
UCLASS()
class LIVELINKCAMERA_API ULiveLinkCameraController : public ULiveLinkControllerBase
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FComponentReference ComponentToControl_DEPRECATED;

	UPROPERTY()
	FLiveLinkTransformControllerData TransformData_DEPRECATED;
#endif

	/** Asset containing encoder and fiz mapping */
	UPROPERTY(EditAnywhere, Category = "Lens")
	ULensFile* LensFile;

	/** Whether to apply lens distortion to the binded camera component */
	UPROPERTY(EditAnywhere, Category = "Lens")
	bool bApplyLensDistortion = false;

private:
	
	/** Whether incoming data requires encoder mapping */
	bool bIsEncoderMappingNeeded = false;

	/** Timestamp when we made the last warning log. Intervals to avoid log spamming */
	double LastInvalidLoggingLoggedTimestamp = 0.0f;
	static constexpr float TimeBetweenLoggingSeconds = 2.0f;

public:
	//~ Begin ULiveLinkControllerBase interface
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData) override;
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) override;
	virtual TSubclassOf<UActorComponent> GetDesiredComponentClass() const override;
	virtual void OnEvaluateRegistered() override;
	//~ End ULiveLinkControllerBase interface

	//~ Begin UObject interface
	virtual void PostLoad() override;
	//~ End UObject interface

	/** Returns true if encoder mapping is required for FIZ data */
	bool IsEncoderMappingNeeded() const { return bIsEncoderMappingNeeded; }
};