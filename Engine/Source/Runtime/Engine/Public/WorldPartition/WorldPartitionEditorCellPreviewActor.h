// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/PostProcessVolume.h"
#include "WorldPartitionEditorCellPreviewActor.generated.h"

UCLASS(NotPlaceable)
class AWorldPartitionEditorCellPreview : public AActor
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
public:
	//~ Begin AActor interface
	virtual EActorGridPlacement GetDefaultGridPlacement() const override;
	virtual bool IsSelectable() const override;
	virtual void GetActorBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors = false) const override;
	virtual void PostRegisterAllComponents() override;
	//~ End AActor interface

	void SetVisibility(bool bVisible);
	void SetCellBounds(const FBox& InCellBounds) { CellBounds = InCellBounds; }

private:
	bool bVisible;
	FBox CellBounds;
#endif
};

UCLASS(NotPlaceable, Transient)
class AWorldPartitionUnloadedCellPreviewPostProcessVolume : public APostProcessVolume
{
	GENERATED_UCLASS_BODY()
};
