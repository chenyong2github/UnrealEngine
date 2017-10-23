// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "LiveLinkTypes.h"
#include "LiveLinkDrivenComponent.generated.h"

class ILiveLinkClient;
class IModularFeature;

// References the live link client modular feature and handles add/remove
struct FLiveLinkClientReference
{
public:

	FLiveLinkClientReference();
	~FLiveLinkClientReference();

	ILiveLinkClient* GetClient() const { return LiveLinkClient; }

	void InitClient();

private:
	// Handlers for modular features coming and going
	void OnLiveLinkClientRegistered(const FName& Type, class IModularFeature* ModularFeature);
	void OnLiveLinkClientUnregistered(const FName& Type, class IModularFeature* ModularFeature);

	ILiveLinkClient* LiveLinkClient;
};

/** A component that applies data from Live Link to the owning actor */
UCLASS(Blueprintable, ClassGroup = "LiveLink", meta = (BlueprintSpawnableComponent))
class LIVELINK_API ULiveLinkDrivenComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	// The name of the live link subject to take data from
	UPROPERTY(EditAnywhere, Category = "Live Link", meta = (ShowOnlyInnerProperties))
	FLiveLinkSubjectName SubjectName;

	// The name of the bone to drive the actors transform with (if None then we will take the first bone)
	UPROPERTY(EditAnywhere, Category = "Live Link")
	FName ActorTransformBone;

	// Should the actors transform be driven by live link
	UPROPERTY(EditAnywhere, Category = "Live Link")
	bool bModifyActorTransform;

	// Should the transform from live link be treated as relative or world space
	UPROPERTY(EditAnywhere, Category = "Live Link")
	bool bSetRelativeLocation;

	// UActorComponent interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	// End UActorComponent interface

private:

	// Reference to the live link client so that we can get data about our subject
	FLiveLinkClientReference ClientRef;
};