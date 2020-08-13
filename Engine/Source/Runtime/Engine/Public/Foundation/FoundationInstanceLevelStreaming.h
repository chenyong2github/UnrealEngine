// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Foundation/FoundationTypes.h"
#include "FoundationInstanceLevelStreaming.generated.h"

class AFoundationActor;

UCLASS(Transient)
class ENGINE_API ULevelStreamingFoundationInstance : public ULevelStreamingDynamic
{
	GENERATED_UCLASS_BODY()

public:
	AFoundationActor* GetFoundationActor() const;

#if WITH_EDITOR
	virtual bool ShowInLevelCollection() const override { return false; }
	FBox GetBounds() const;
#endif
	
protected:
	static ULevelStreamingFoundationInstance* LoadInstance(AFoundationActor* FoundationActor);
	static void UnloadInstance(ULevelStreamingFoundationInstance* LevelStreaming);

	friend class UFoundationSubsystem;

private:
	FFoundationID FoundationID;
};