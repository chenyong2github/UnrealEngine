// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"
#include "FractureToolVoronoiBase.generated.h"


UCLASS(Abstract, DisplayName="Voronoi Base", Category="FractureTools")
class UFractureToolVoronoiBase : public UFractureTool
{
public:
	GENERATED_BODY()

	UFractureToolVoronoiBase(const FObjectInitializer& ObjInit);//  : Super(ObjInit) {}
	virtual ~UFractureToolVoronoiBase();

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
	
	void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	virtual void FractureContextChanged() override;
	virtual void ExecuteFracture(const FFractureContext& FractureContext) override;

protected:
	
	virtual void GenerateVoronoiSites(const FFractureContext &Context, TArray<FVector>& Sites) {}

private:
	TArray<int32> CellMember;
	TArray<TTuple<FVector,FVector>> VoronoiEdges;
	TArray<FVector> VoronoiSites;
	TArray<FLinearColor> Colors;
};