// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Graph/MovieGraphNode.h"
#include "MovieGraphRenderLayerNode.generated.h"


UCLASS()
class UMovieGraphRenderLayerNode : public UMovieGraphNode
{
	GENERATED_BODY()
public:
	UMovieGraphRenderLayerNode()
	{
		LayerName = TEXT("beauty");
	}

	FString GetRenderLayerName() const { return LayerName; }

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

	virtual TArray<FPropertyBagPropertyDesc> GetDynamicPropertyDescriptions() const
	{
		TArray<FPropertyBagPropertyDesc> Properties;
		FPropertyBagPropertyDesc FloatEditConProperty = FPropertyBagPropertyDesc("bOverride_TestPropName", EPropertyBagPropertyType::Bool);
		FPropertyBagPropertyDesc FloatProperty = FPropertyBagPropertyDesc("TestPropName", EPropertyBagPropertyType::Float);
#if WITH_EDITOR
		FloatEditConProperty.MetaData.Add(FPropertyBagPropertyDescMetaData("InlineEditConditionToggle", "true"));
		FloatProperty.MetaData.Add(FPropertyBagPropertyDescMetaData("EditCondition", "bOverride_TestPropName"));
#endif
		
		Properties.Add(FloatEditConProperty);
		Properties.Add(FloatProperty);
		return Properties;
	}
#if WITH_EDITOR
	virtual FText GetMenuDescription() const override
	{
		return NSLOCTEXT("MovieGraphNodes", "RenderLayerGraphNode_Description", "Render Layer");
	}
	
	virtual FText GetMenuCategory() const override
	{
		return NSLOCTEXT("MovieGraphNodes", "RenderLayerGraphNode_Category", "Rendering");
	}
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FString LayerName;
};