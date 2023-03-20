// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/Nodes/MovieGraphRenderLayerNode.h"

#include "Graph/MovieGraphConfig.h"
#include "Styling/AppStyle.h"

UMovieGraphRenderLayerNode::UMovieGraphRenderLayerNode()
{
	LayerName = TEXT("beauty");
}

TArray<FMovieGraphPinProperties> UMovieGraphRenderLayerNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphRenderLayerNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

TArray<FPropertyBagPropertyDesc> UMovieGraphRenderLayerNode::GetDynamicPropertyDescriptions() const
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
FText UMovieGraphRenderLayerNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText RenderLayerNodeName = NSLOCTEXT("MovieGraphNodes", "NodeName_RenderLayer", "Render Layer");
	static const FText RenderLayerNodeDescription = NSLOCTEXT("MovieGraphNodes", "NodeDescription_RenderLayer", "Render Layer\n{0}");

	if (bGetDescriptive && !LayerName.IsEmpty())
	{
		return FText::Format(RenderLayerNodeDescription, FText::FromString(LayerName));
	}
	
	return RenderLayerNodeName;
}

FText UMovieGraphRenderLayerNode::GetMenuCategory() const
{
	return NSLOCTEXT("MovieGraphNodes", "RenderLayerGraphNode_Category", "Rendering");
}

FLinearColor UMovieGraphRenderLayerNode::GetNodeTitleColor() const
{
	static const FLinearColor RenderLayerNodeColor = FLinearColor(0.192f, 0.258f, 0.615f);
	return RenderLayerNodeColor;
}

FSlateIcon UMovieGraphRenderLayerNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon RenderLayerIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.DataLayers");

	OutColor = FLinearColor::White;
	return RenderLayerIcon;
}

void UMovieGraphRenderLayerNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMovieGraphRenderLayerNode, LayerName))
	{
		OnNodeChangedDelegate.Broadcast(this);
	}
}
#endif // WITH_EDITOR