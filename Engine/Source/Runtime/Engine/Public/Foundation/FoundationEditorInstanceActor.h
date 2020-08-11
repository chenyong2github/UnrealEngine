// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Foundation/FoundationTypes.h"
#include "FoundationEditorInstanceActor.generated.h"

class AFoundationActor;
class ULevel;

/**
 * Editor Only Actor that is spawned inside every Foundation Instance Level so that we can update its Actor Transforms through the AFoundationActor's root component(UFoundationComponent)
 * @see UFoundationComponent
 */
UCLASS(transient, notplaceable)
class ENGINE_API AFoundationEditorInstanceActor : public AActor
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
public:
	static AFoundationEditorInstanceActor* Create(AFoundationActor* FoundationActor, ULevel* LoadedLevel);
	
	void SetFoundationID(const FFoundationID& InFoundationID) { FoundationID = InFoundationID; }
	const FFoundationID& GetFoundationID() const { return FoundationID; }
	
	virtual bool IsSelectionParentOfAttachedActors() const override { return true; }
	virtual bool IsSelectionChild() const override { return true; }
	virtual AActor* GetSelectionParent() const override;

private:

	FFoundationID FoundationID;
#endif
};