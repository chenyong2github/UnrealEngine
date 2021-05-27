// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"

#include "LensDistortionModelHandlerBase.h"

#include "LiveLinkLensController.generated.h"

/**
 * LiveLink Controller for the LensRole to drive lens distortion data 
 */
UCLASS()
class LIVELINKLENS_API ULiveLinkLensController : public ULiveLinkControllerBase
{
	GENERATED_BODY()

public:
	ULiveLinkLensController();

	//~ Begin ULiveLinkControllerBase interface
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData) override;
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) override;
	virtual TSubclassOf<UActorComponent> GetDesiredComponentClass() const override;
	virtual void Cleanup() override;
	//~ End ULiveLinkControllerBase interface

	//~ Begin UObject Interface
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostEditImport() override;
	//~ End UObject Interface

protected:
	/** Cached distortion handler associated with attached camera component */
	UPROPERTY(Transient)
	ULensDistortionModelHandlerBase* LensDistortionHandler = nullptr;

	/** Unique identifier representing the source of distortion data */
	UPROPERTY(DuplicateTransient)
	FGuid DistortionProducerID;
};