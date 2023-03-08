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

	explicit FAnimNextGraphReferencePose(const UE::AnimNext::Interface::FAnimationReferencePose* InReferencePose)
		: ReferencePose(InReferencePose)
	{
	}

	const UE::AnimNext::Interface::FAnimationReferencePose* ReferencePose = nullptr;
};
DECLARE_ANIM_NEXT_INTERFACE_PARAM_TYPE(ANIMNEXTINTERFACEGRAPH_API, FAnimNextGraphReferencePose, FAnimNextGraphReferencePose);

USTRUCT(BlueprintType, meta = (DisplayName = "LODPose"))
struct FAnimNextGraphLODPose
{
	GENERATED_BODY()

	FAnimNextGraphLODPose() = default;

	explicit FAnimNextGraphLODPose(const UE::AnimNext::Interface::FAnimationLODPose& InLODPose)
		: LODPose(InLODPose)
	{
	}
	explicit FAnimNextGraphLODPose(UE::AnimNext::Interface::FAnimationLODPose&& InLODPose)
		: LODPose(MoveTemp(InLODPose))
	{
	}

	UE::AnimNext::Interface::FAnimationLODPose LODPose;
};
DECLARE_ANIM_NEXT_INTERFACE_PARAM_TYPE(ANIMNEXTINTERFACEGRAPH_API, FAnimNextGraphLODPose, FAnimNextGraphLODPose);


UCLASS()
class UAnimNextInterfaceGraphLODPose : public UObject, public IAnimNextInterface
{
	GENERATED_BODY()

	virtual FName GetReturnTypeNameImpl() const final override
	{
		static const FName Name = TNameOf<FAnimNextGraphLODPose>::GetName();
		return Name;
	}

	virtual bool GetDataImpl(const UE::AnimNext::Interface::FContext& Context) const override
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
	virtual bool GetDataImpl(const UE::AnimNext::Interface::FContext& Context) const final override
	{
		Context.SetResult(Value);
		return true;
	}
	
	UPROPERTY(EditAnywhere, Category = "Parameters")
	FAnimNextGraphLODPose Value;
};
