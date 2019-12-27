// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "ILiveLinkClient.h"
#include "LiveLinkComponentController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLiveLinkTickSignature, float, DeltaTime);

class ULiveLinkControllerBase;

UCLASS( ClassGroup=(LiveLink), meta=(DisplayName="LiveLink Controller", BlueprintSpawnableComponent) )
class LIVELINKCOMPONENTS_API ULiveLinkComponentController : public UActorComponent
{
	GENERATED_BODY()
	
public:
	ULiveLinkComponentController();

public:
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category="LiveLink")
	FLiveLinkSubjectRepresentation SubjectRepresentation;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="LiveLink", Instanced, NoClear, meta=(ShowOnlyInnerProperties))
	ULiveLinkControllerBase* Controller;

	UPROPERTY(EditAnywhere, Category="LiveLink", AdvancedDisplay)
	bool bUpdateInEditor;

public:
	virtual void OnRegister() override;
	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// This Event is triggered any time new LiveLink data is available, including in the editor
	UPROPERTY(BlueprintAssignable, Category = "LiveLink")
	FLiveLinkTickSignature OnLiveLinkUpdated;

public:
	//~ UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
	//~ End UObject interface

private:
	// Record whether we have been recently registered
	bool bIsDirty;
};