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

	/**
	 * Function called every frame.
	 * Will only tick the when ShouldTickController returned true.
	 */
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectRepresentation& SubjectRepresentation) { }

	/**
	 * Can it support a specific role.
	 * This is called on the default object before creating an instance.
	 */
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) { return false; }

#if WITH_EDITOR
	virtual void InitializeInEditor() {}
#endif

protected:
	AActor* GetOuterActor() const;

public:
	static TSubclassOf<ULiveLinkControllerBase> GetControllerForRole(const TSubclassOf<ULiveLinkRole>& RoleToSupport);
};