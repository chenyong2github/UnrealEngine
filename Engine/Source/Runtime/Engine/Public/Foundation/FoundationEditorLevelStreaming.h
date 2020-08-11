// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Foundation/FoundationTypes.h"
#include "FoundationEditorLevelStreaming.generated.h"

class AFoundationActor;

UCLASS(Transient, MinimalAPI)
class ULevelStreamingFoundationEditor : public ULevelStreamingAlwaysLoaded
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	virtual bool ShowInLevelCollection() const override { return false; }
	AFoundationActor* GetFoundationActor() const;

protected:
	friend class UFoundationSubsystem;

	static ULevelStreamingFoundationEditor* Load(AFoundationActor* FoundationActor);
	static void Unload(ULevelStreamingFoundationEditor* LevelStreaming);
private:
	FFoundationID FoundationID;
#endif
};