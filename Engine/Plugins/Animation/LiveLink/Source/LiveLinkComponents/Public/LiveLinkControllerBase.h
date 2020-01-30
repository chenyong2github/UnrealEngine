// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

#include "Components/ActorComponent.h"
#include "ILiveLinkClient.h"

#include "LiveLinkControllerBase.generated.h"

class AActor;

/**
 */
UCLASS(Abstract, ClassGroup=(LiveLink), editinlinenew)
class LIVELINKCOMPONENTS_API ULiveLinkControllerBase : public UObject
{
	GENERATED_BODY()

public:
	/** Initialize the controller at the first tick of his owner component. */
	virtual void OnEvaluateRegistered() { }

	UE_DEPRECATED(4.25, "This function is deprecated. Use Tick function that received evaluated data instead.")
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectRepresentation& SubjectRepresentation) { }

	/**
	 * Function called every frame with the data evaluated by the component.
	 */
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData) { }

	/**
	 * Can it support a specific role.
	 * This is called on the default object before creating an instance.
	 */
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) { return false; }

	/**
	 * Returns the component class that this controller wants to control
	 */
	virtual TSubclassOf<UActorComponent> GetDesiredComponentClass() const { return UActorComponent::StaticClass(); }

	/**
	 * Sets the component this controller is driving
	 */
	virtual void SetAttachedComponent(UActorComponent* ActorComponent);

#if WITH_EDITOR
	virtual void InitializeInEditor() {}
#endif

protected:
	AActor* GetOuterActor() const;

public:

	UE_DEPRECATED(4.25, "This function is deprecated. Use GetControllersForRole instead and use first element to have the same result.")
	static TSubclassOf<ULiveLinkControllerBase> GetControllerForRole(const TSubclassOf<ULiveLinkRole>& RoleToSupport);

	/**
	 * Returns the list of ULiveLinkControllerBase classes that support the given role
	 */
	static TArray<TSubclassOf<ULiveLinkControllerBase>> GetControllersForRole(const TSubclassOf<ULiveLinkRole>& RoleToSupport);

protected:
	TWeakObjectPtr<UActorComponent> AttachedComponent;
};

