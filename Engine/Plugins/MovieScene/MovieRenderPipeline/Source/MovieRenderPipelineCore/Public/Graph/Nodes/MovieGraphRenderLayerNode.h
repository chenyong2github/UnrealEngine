// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Graph/MovieGraphNode.h"
#include "MovieGraphRenderLayerNode.generated.h"

UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphRenderLayerNode : public UMovieGraphNode
{
	GENERATED_BODY()
public:
	UMovieGraphRenderLayerNode();

	FString GetRenderLayerName() const { return LayerName; }

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;
	virtual TArray<FPropertyBagPropertyDesc> GetDynamicPropertyDescriptions() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	//~ Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FString LayerName;
};