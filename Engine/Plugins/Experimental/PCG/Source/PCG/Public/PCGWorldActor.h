// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PCGWorldActor.generated.h"

UCLASS(MinimalAPI, NotBlueprintable, NotPlaceable)
class APCGWorldActor : public AActor
{
	GENERATED_BODY()

public:
	APCGWorldActor(const FObjectInitializer& ObjectInitializer);

	//~Begin AActor Interface
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR	
	virtual bool CanChangeIsSpatiallyLoadedFlag() const { return false; }
	virtual bool IsUserManaged() const override { return false; }
	//~End AActor Interface

	static APCGWorldActor* CreatePCGWorldActor(UWorld* InWorld);
#endif

private:
	void RegisterToSubsystem();
	void UnregisterFromSubsystem();
};
