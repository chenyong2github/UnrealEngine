// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkPhysics.h"
#include "PhysicsEffectTypes.generated.h"

namespace Chaos
{
	class FSingleParticlePhysicsProxy;
}

UENUM()
enum class EPhysicsEffectQueryType : uint8
{
	None,	// Don't do a query. The response is applied directly to the target particles instead.
	SphereOverlap // sphere overlap
};

UENUM()
enum class EPhysicsEffectResponseType : uint8
{
	Force,
	Impulse,
	Torque
};

USTRUCT(BlueprintType)
struct FPhysicsEffectDef
{
	GENERATED_BODY()

	FPhysicsEffectDef()
		: QueryData(ForceInitToZero)
		, ResponseMagnitude(ForceInitToZero)
	{ }

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	int32 StartFrame = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	int32 EndFrame = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	EPhysicsEffectQueryType QueryType = EPhysicsEffectQueryType::SphereOverlap;

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	FVector QueryData;

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	FTransform QueryRelativeTransform = FTransform::Identity;

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	EPhysicsEffectResponseType ResponseType = EPhysicsEffectResponseType::Impulse;

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	FVector ResponseMagnitude;

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	uint8 TypeID=0;

	void NetSerialize(FArchive& Ar)
	{
		Ar << StartFrame;
		if (StartFrame != INDEX_NONE)
		{
			Ar << EndFrame;
			Ar << QueryType;
			Ar << QueryData;
			Ar << QueryRelativeTransform;
			Ar << ResponseType;
			Ar << ResponseMagnitude;
			Ar << TypeID;
		}
	}

	bool ShouldReconcile(const FPhysicsEffectDef& AuthState) const
	{
		if (StartFrame == INDEX_NONE && StartFrame == AuthState.StartFrame)
		{
			// If start frame is -1 and we both agree, don't worry about the rest of the payload
			return false;
		}

		return 
			StartFrame != AuthState.StartFrame ||
			EndFrame != AuthState.EndFrame ||
			QueryType != AuthState.QueryType ||
			QueryData != AuthState.QueryData ||
			!QueryRelativeTransform.Equals(AuthState.QueryRelativeTransform) ||
			ResponseType != AuthState.ResponseType ||
			ResponseMagnitude != AuthState.ResponseMagnitude ||
			TypeID != AuthState.TypeID;
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("{StartFrame: %d EndFrame: %d QueryType: %d QueryData: %s ResponseType: %d ResponseMagnitude: %s"),
			StartFrame, EndFrame, QueryType, *QueryData.ToString(), ResponseType, *ResponseMagnitude.ToString());
	}
};

USTRUCT(BlueprintType)
struct FPhysicsEffectCmd
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	FPhysicsEffectDef PendingEffect;

	void NetSerialize(FArchive& Ar)
	{
		PendingEffect.NetSerialize(Ar);
	}

	bool ShouldReconcile(const FPhysicsEffectCmd& AuthState) const
	{
		return PendingEffect.ShouldReconcile(AuthState.PendingEffect);
	}

	FString ToString() const
	{
		return PendingEffect.ToString();
	}
};

USTRUCT(BlueprintType)
struct FPhysicsEffectNetState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	FPhysicsEffectDef ActiveEffect;

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	uint8 LastPlayedEffectTypeID = 0;

	UPROPERTY(BlueprintReadWrite, Category="Physics Effect")
	int32 LastPlayedSimFrame = INDEX_NONE;

	void NetSerialize(FArchive& Ar)
	{
		ActiveEffect.NetSerialize(Ar);
		Ar << LastPlayedEffectTypeID;
		Ar << LastPlayedSimFrame;
	}

	bool ShouldReconcile(const FPhysicsEffectNetState& AuthState) const
	{
		return ActiveEffect.ShouldReconcile(AuthState.ActiveEffect) ||
			LastPlayedEffectTypeID != AuthState.LastPlayedEffectTypeID ||
			LastPlayedSimFrame != AuthState.LastPlayedSimFrame;
	}

	FString ToString() const
	{
		return ActiveEffect.ToString();
	}
};

USTRUCT(BlueprintType)
struct FPhysicsEffectLocalState
{
	GENERATED_BODY()

	Chaos::FSingleParticlePhysicsProxy* Proxy = nullptr;

	FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
};