// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Graph/MovieGraphNode.h"
#include "MovieGraphRenderPassNode.generated.h"

// Forward Declare
struct FMovieGraphRenderPassSetupData;
struct FMovieGraphTimeStepData;

UCLASS(Abstract)
class UMovieGraphRenderPassNode : public UMovieGraphNode
{
	GENERATED_BODY()
public:
	UMovieGraphRenderPassNode()
	{
	}

	// UMovieGraphRenderPassNode interface
	FString GetRendererName() const { return GetRendererNameImpl(); }
	void Setup(const FMovieGraphRenderPassSetupData& InSetupData) { SetupImpl(InSetupData); }
	void Teardown() { TeardownImpl(); }
	void Render(const FMovieGraphTimeStepData& InTimeData) { RenderImpl(InTimeData); }
	// ~UMovieGraphRenderPassNode interface

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override
	{
		TArray<FMovieGraphPinProperties> Properties;
		Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
		return Properties;
	}

	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override
	{
		TArray<FMovieGraphPinProperties> Properties;
		Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
		return Properties;
	}

#if WITH_EDITOR
	virtual FText GetMenuCategory() const override
	{
		return NSLOCTEXT("MovieGraphNodes", "RenderPassGraphNode_Category", "Rendering");
	}
#endif

protected:
	virtual FString GetRendererNameImpl() const { return TEXT("UnnamedRenderPass"); }
	virtual void SetupImpl(const FMovieGraphRenderPassSetupData& InSetupData) {}
	virtual void TeardownImpl() {}
	virtual void RenderImpl(const FMovieGraphTimeStepData& InTimeData) {}
};