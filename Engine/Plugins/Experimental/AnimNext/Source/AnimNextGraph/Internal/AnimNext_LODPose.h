// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interface/InterfaceTypes.h"
#include "Interface/IAnimNextInterface.h"
#include "Interface/InterfaceContext.h"
#include "ReferencePose.h"
#include "LODPose.h"
#include "AnimNext_LODPose.generated.h"

USTRUCT(BlueprintType, meta = (DisplayName = "ReferencePose"))
struct FAnimNextGraphReferencePose
{
	GENERATED_BODY()

	FAnimNextGraphReferencePose() = default;

	explicit FAnimNextGraphReferencePose(const UE::AnimNext::FReferencePose* InReferencePose)
		: ReferencePose(InReferencePose)
	{
	}

	const UE::AnimNext::FReferencePose* ReferencePose = nullptr;
};

USTRUCT(BlueprintType, meta = (DisplayName = "LODPose"))
struct FAnimNextGraphLODPose
{
	GENERATED_BODY()

	FAnimNextGraphLODPose() = default;

	explicit FAnimNextGraphLODPose(const UE::AnimNext::FLODPose& InLODPose)
		: LODPose(InLODPose)
	{
	}
	explicit FAnimNextGraphLODPose(UE::AnimNext::FLODPose&& InLODPose)
		: LODPose(MoveTemp(InLODPose))
	{
	}

	UE::AnimNext::FLODPose LODPose;
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
