// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "OVR_Avatar.h"
#include "Components/ActorComponent.h"
#include "Runtime/Engine/Classes/Components/SceneComponent.h"
#include "OvrAvatarGazeTarget.generated.h"

UENUM(BlueprintType, Category = OculusAvatar)
enum class OculusAvatarGazeTargetType : uint8 {
	AvatarHead,
	AvatarHand,
	Object,
	ObjectStatic
};

UCLASS(ClassGroup = Custom, BlueprintType, meta = (BlueprintSpawnableComponent))
class OCULUSAVATAR_API UOvrAvatarGazeTarget : public UActorComponent
{
	GENERATED_BODY()

public:


	UOvrAvatarGazeTarget();

	virtual void BeginPlay() override;
	virtual	void BeginDestroy() override;
	virtual void TickComponent(
		float DeltaTime, 
		enum ELevelTick TickType, 
		FActorComponentTickFunction *ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category = OculusAvatar)
	OculusAvatarGazeTargetType TargetType = OculusAvatarGazeTargetType::ObjectStatic;

	void EnableGazeTarget(bool DoEnable);

	void SetGazeTransform(USceneComponent* sceneComp) { GazeTransform = sceneComp; }
	void SetGazeTargetType(OculusAvatarGazeTargetType type) { TargetType = type; }
	void SetAvatarHeadTransform(const FTransform& trans) { AvatarHeadTransform = trans; }
private:
	const FTransform& GetGazeTransform() const;
	ovrAvatarGazeTargetType ConvertEditorTypeToNativeType() const;

	TWeakObjectPtr<USceneComponent> GazeTransform = nullptr;
	ovrAvatarGazeTargetSet NativeTarget;
	FTransform AvatarHeadTransform = FTransform::Identity;
	bool IsEnabled = false;
};
