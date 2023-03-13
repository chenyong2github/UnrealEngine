// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextInterfaceTypes.h"
#include "IAnimNextInterface.h"
#include "AnimNextInterfaceContext.h"
#include "AnimationReferencePose.h"
#include "AnimNextInterfaceTypes.h"
#include "AnimNextInterface_LODPose.generated.h"

USTRUCT(BlueprintType, meta = (DisplayName = "ReferencePose"))
struct FAnimNextGraphReferencePose
{
	GENERATED_BODY()

	FAnimNextGraphReferencePose() = default;

	explicit FAnimNextGraphReferencePose(const UE::AnimNext::FAnimationReferencePose* InReferencePose)
		: ReferencePose(InReferencePose)
	{
	}

	const UE::AnimNext::FAnimationReferencePose* ReferencePose = nullptr;
};

USTRUCT(BlueprintType, meta = (DisplayName = "LODPose"))
struct FAnimNextGraphLODPose
{
	GENERATED_BODY()

	FAnimNextGraphLODPose() = default;

	explicit FAnimNextGraphLODPose(const UE::AnimNext::FAnimationLODPose& InLODPose)
		: LODPose(InLODPose)
	{
	}
	explicit FAnimNextGraphLODPose(UE::AnimNext::FAnimationLODPose&& InLODPose)
		: LODPose(MoveTemp(InLODPose))
	{
	}

	UE::AnimNext::FAnimationLODPose LODPose;
};

UCLASS()
class UAnimNextInterfaceGraphLODPose : public UObject, public IAnimNextInterface
{
	GENERATED_BODY()

	virtual UE::AnimNext::FParamTypeHandle GetReturnTypeHandleImpl() const final override
	{
		return UE::AnimNext::FParamTypeHandle::GetHandle<FAnimNextGraphLODPose>();
	}

	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const override
	{
		checkf(false, TEXT("UAnimNextInterfaceLODPose::GetDataImpl must be overridden"));
		return false;
	}
};

UCLASS()
class UAnimNextInterface_LODPose_Literal : public UAnimNextInterfaceGraphLODPose
{
	GENERATED_BODY()

	// IAnimNextInterface interface
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override
	{
		Context.SetResult(Value);
		return true;
	}
	
	UPROPERTY(EditAnywhere, Category = "Parameters")
	FAnimNextGraphLODPose Value;
};
