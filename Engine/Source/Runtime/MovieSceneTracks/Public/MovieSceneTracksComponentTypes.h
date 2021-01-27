// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Transform.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "Engine/EngineTypes.h"
#include "EulerTransform.h"
#include "TransformData.h"
#include "MovieSceneTracksComponentTypes.generated.h"

class UMovieSceneLevelVisibilitySection;
struct FMovieSceneObjectBindingID;

/** Component data for the level visibility system */
USTRUCT()
struct FLevelVisibilityComponentData
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UMovieSceneLevelVisibilitySection> Section = nullptr;
};

namespace UE
{
namespace MovieScene
{

/** Intermediate type used for applying partially animated transforms. Saves us from repteatedly recomposing quaternions from euler angles */
struct FIntermediate3DTransform
{
	float T_X, T_Y, T_Z, R_X, R_Y, R_Z, S_X, S_Y, S_Z;

	FIntermediate3DTransform()
		: T_X(0.f), T_Y(0.f), T_Z(0.f), R_X(0.f), R_Y(0.f), R_Z(0.f), S_X(0.f), S_Y(0.f), S_Z(0.f)
	{}

	FIntermediate3DTransform(float InT_X, float InT_Y, float InT_Z, float InR_X, float InR_Y, float InR_Z, float InS_X, float InS_Y, float InS_Z)
		: T_X(InT_X), T_Y(InT_Y), T_Z(InT_Z), R_X(InR_X), R_Y(InR_Y), R_Z(InR_Z), S_X(InS_X), S_Y(InS_Y), S_Z(InS_Z)
	{}

	FIntermediate3DTransform(const FVector& InLocation, const FRotator& InRotation, const FVector& InScale)
		: T_X(InLocation.X), T_Y(InLocation.Y), T_Z(InLocation.Z)
		, R_X(InRotation.Roll), R_Y(InRotation.Pitch), R_Z(InRotation.Yaw)
		, S_X(InScale.X), S_Y(InScale.Y), S_Z(InScale.Z)
	{}

	float operator[](int32 Index) const
	{
		check(Index >= 0 && Index < 9);
		return (&T_X)[Index];
	}

	FVector GetTranslation() const
	{
		return FVector(T_X, T_Y, T_Z);
	}
	FRotator GetRotation() const
	{
		return FRotator(R_Y, R_Z, R_X);
	}
	FVector GetScale() const
	{
		return FVector(S_X, S_Y, S_Z);
	}

	MOVIESCENETRACKS_API void ApplyTo(USceneComponent* SceneComponent) const;
};

struct FComponentAttachParamsDestination
{
	FName SocketName    = NAME_None;
	FName ComponentName = NAME_None;

	MOVIESCENETRACKS_API USceneComponent* ResolveAttachment(AActor* InParentActor) const;
};

struct FComponentAttachParams
{
	EAttachmentRule AttachmentLocationRule  = EAttachmentRule::KeepRelative;
	EAttachmentRule AttachmentRotationRule  = EAttachmentRule::KeepRelative;
	EAttachmentRule AttachmentScaleRule     = EAttachmentRule::KeepRelative;

	MOVIESCENETRACKS_API void ApplyAttach(USceneComponent* NewAttachParent, USceneComponent* ChildComponentToAttach, const FName& SocketName) const;
};

struct FComponentDetachParams
{
	EDetachmentRule DetachmentLocationRule  = EDetachmentRule::KeepRelative;
	EDetachmentRule DetachmentRotationRule  = EDetachmentRule::KeepRelative;
	EDetachmentRule DetachmentScaleRule     = EDetachmentRule::KeepRelative;

	MOVIESCENETRACKS_API void ApplyDetach(USceneComponent* NewAttachParent, USceneComponent* ChildComponentToAttach, const FName& SocketName) const;
};

struct FAttachmentComponent
{
	FComponentAttachParamsDestination Destination;

	FComponentAttachParams AttachParams;
	FComponentDetachParams DetachParams;
};

MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediate3DTransform& In, FEulerTransform& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FEulerTransform& In, FIntermediate3DTransform& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediate3DTransform& In, FTransform& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FTransform& In, FIntermediate3DTransform& Out);

struct MOVIESCENETRACKS_API FMovieSceneTracksComponentTypes
{
	~FMovieSceneTracksComponentTypes();

	TPropertyComponents<bool> Bool;
	TPropertyComponents<float> Float;
	TPropertyComponents<FTransform, FIntermediate3DTransform> Transform;
	TPropertyComponents<FEulerTransform, FIntermediate3DTransform> EulerTransform;
	TPropertyComponents<FIntermediate3DTransform> ComponentTransform;
	TComponentTypeID<FSourceFloatChannel> QuaternionRotationChannel[3];

	TComponentTypeID<USceneComponent*> AttachParent;
	TComponentTypeID<FAttachmentComponent> AttachComponent;
	TComponentTypeID<FMovieSceneObjectBindingID> AttachParentBinding;

	struct
	{
		TCustomPropertyRegistration<bool> Bool;
		TCustomPropertyRegistration<float> Float;
		TCustomPropertyRegistration<FIntermediate3DTransform> ComponentTransform;
	} Accessors;

	TComponentTypeID<FLevelVisibilityComponentData> LevelVisibility;

	static void Destroy();

	static FMovieSceneTracksComponentTypes* Get();

private:
	FMovieSceneTracksComponentTypes();
};


} // namespace MovieScene
} // namespace UE
