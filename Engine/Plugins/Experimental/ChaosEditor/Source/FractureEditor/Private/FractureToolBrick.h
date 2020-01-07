// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolBrick.generated.h"


/** Brick Projection Directions*/
UENUM()
enum class EFractureBrickProjection: uint8
{
	X UMETA(DisplayName = "X"),

	Y UMETA(DisplayName = "Y"),

	Z UMETA(DisplayName = "Z")
};

/** Brick Projection Directions*/
UENUM()
enum class EFractureBrickBond : uint8
{
	Stretcher UMETA(DisplayName = "Stretcher"),

	Stack UMETA(DisplayName = "Stack"),

	English  UMETA(DisplayName = "English"),

	Header UMETA(DisplayName = "Header"),

	Flemish UMETA(DisplayName = "Flemish")
};


UCLASS()
class UFractureBrickSettings
	: public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	UFractureBrickSettings() 
	: Bond(EFractureBrickBond::Stretcher)
	, Forward(EFractureBrickProjection::X)
	, Up(EFractureBrickProjection::Z)
	, BrickLength(194.f)
	, BrickHeight(57.f)
	, BrickDepth(92.f)
	{}

	/** Forward Direction to project brick pattern. */
	UPROPERTY(EditAnywhere, Category = Brick)
	EFractureBrickBond Bond;

	/** Forward Direction to project brick pattern. */
	UPROPERTY(EditAnywhere, Category = Brick)
	EFractureBrickProjection Forward;

	/** Up Direction for vertical brick slices. */
	UPROPERTY(EditAnywhere, Category = Brick)
	EFractureBrickProjection Up;

	/** Brick length */
	UPROPERTY(EditAnywhere, Category = Brick)
	float BrickLength;

	/** Brick Height */
	UPROPERTY(EditAnywhere, Category = Brick)
	float BrickHeight;

	/** Brick Height */
	UPROPERTY(EditAnywhere, Category = Brick)
	float BrickDepth;

	UPROPERTY()
	UFractureTool *OwnerTool;

};


UCLASS(DisplayName="Brick", Category="FractureTools")
class UFractureToolBrick : public UFractureTool
{
public:
	GENERATED_BODY()

	UFractureToolBrick(const FObjectInitializer& ObjInit);//  : Super(ObjInit) {}

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext );
	
	virtual TArray<UObject*> GetSettingsObjects() const override;// { return TArray<UObject*>(); }

	void GenerateBrickTransforms(const FBox& Bounds);
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif


	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	virtual void ExecuteFracture(const FFractureContext& FractureContext) override;
	virtual bool CanExecuteFracture() const override;

private:
	UPROPERTY()
	UFractureBrickSettings* BrickSettings;

	void AddBoxEdges(const FVector& Min, const FVector& Max);

	TArray<FTransform> BrickTransforms;
	TArray<TTuple<FVector, FVector>> Edges;
	TArray<TTuple<FVector, FVector>> Boxes;
};