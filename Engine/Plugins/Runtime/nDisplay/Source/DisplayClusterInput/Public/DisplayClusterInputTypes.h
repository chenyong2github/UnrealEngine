// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "DisplayClusterInputTypes.generated.h"


// VRPN Keyboard reflection modes
UENUM(Blueprintable)
enum class EDisplayClusterInputKeyboardReflectionMode : uint8
{
	None     UMETA(DisplayName = "No reflection"),
	nDisplay UMETA(DisplayName = "nDisplay buttons only"),
	Core     UMETA(DisplayName = "UE core keyboard events"),
	All      UMETA(DisplayName = "Both nDisplay and UE4 core")
};


// Binding description. Maps VRPN device channel to an UE4 FKey
USTRUCT(BlueprintType)
struct FDisplayClusterInputBinding
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DisplayCluster|Input")
	int32 VrpnChannel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DisplayCluster|Input")
	FKey Target;

	FDisplayClusterInputBinding(const FKey InKey = EKeys::Invalid)
		: VrpnChannel(0)
		, Target(InKey)
	{ }
};
