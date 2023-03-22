// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieGraphPlaceholderNodes.h"

#include "MovieGraphConfig.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "MovieGraphNode"

static const FText NodeCategory_Utility = LOCTEXT("NodeCategory_Utility", "Utility");
static const FText NodeCategory_Globals = LOCTEXT("NodeCategory_Globals", "Globals");
static const FText NodeCategory_Renderers = LOCTEXT("NodeCategory_Renderers", "Renderers");
static const FText NodeCategory_OutputType = LOCTEXT("NodeCategory_OutputType", "Output Type");
static const FText NodeCategory_Conditionals = LOCTEXT("NodeCategory_Conditionals", "Conditionals");
static const FText NodeCategory_Settings = LOCTEXT("NodeCategory_Settings", "Settings");

TArray<FMovieGraphPinProperties> UMoviePipelineCollectionNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMoviePipelineCollectionNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMoviePipelineCollectionNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText CollectionNodeName = LOCTEXT("NodeName_Collection", "Collection");
	static const FText CollectionNodeDescription = LOCTEXT("NodeDescription_Collection", "Collection\n{0}");

	if (bGetDescriptive && !CollectionName.IsEmpty())
	{
		return FText::Format(CollectionNodeDescription, FText::FromString(CollectionName));
	}

	return CollectionNodeName;
}

FText UMoviePipelineCollectionNode::GetMenuCategory() const
{
	return NodeCategory_Utility;
}

FLinearColor UMoviePipelineCollectionNode::GetNodeTitleColor() const
{
	static const FLinearColor CollectionNodeColor = FLinearColor(0.047f, 0.501f, 0.654f);
	return CollectionNodeColor;
}

FSlateIcon UMoviePipelineCollectionNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon CollectionIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "SceneOutliner.NewFolderIcon");

	OutColor = FLinearColor::White;
	return CollectionIcon;
}

void UMoviePipelineCollectionNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMoviePipelineCollectionNode, CollectionName))
	{
		OnNodeChangedDelegate.Broadcast(this);
	}
}
#endif // WITH_EDITOR

TArray<FMovieGraphPinProperties> UMovieGraphModifierNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphModifierNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphModifierNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText ModifierNodeName = LOCTEXT("NodeName_Modifier", "Modifier");
	static const FText ModifierNodeDescription = LOCTEXT("NodeDescription_Modifier", "Modifier\n{0} - {1}");

	if (bGetDescriptive && !ModifiedCollectionName.IsEmpty() && !ModifierName.IsEmpty())
	{
		return FText::Format(ModifierNodeDescription, FText::FromString(ModifiedCollectionName), FText::FromString(ModifierName));
	}

	return ModifierNodeName;
}

FText UMovieGraphModifierNode::GetMenuCategory() const
{
	return NodeCategory_Utility;
}

FLinearColor UMovieGraphModifierNode::GetNodeTitleColor() const
{
	static const FLinearColor ModifierNodeColor = FLinearColor(0.6f, 0.113f, 0.113f);
	return ModifierNodeColor;
}

FSlateIcon UMovieGraphModifierNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon ModifierIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ContentBrowser.ReferenceViewer");

	OutColor = FLinearColor::White;
	return ModifierIcon;
}

void UMovieGraphModifierNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if ((PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMovieGraphModifierNode, ModifierName)) ||
		(PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMovieGraphModifierNode, ModifiedCollectionName)))
	{
		OnNodeChangedDelegate.Broadcast(this);
	}
}
#endif // WITH_EDITOR

TArray<FMovieGraphPinProperties> UMovieGraphGlobalGameOverridesNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphGlobalGameOverridesNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphGlobalGameOverridesNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText GlobalGameOverridesNodeName = LOCTEXT("NodeName_GlobalGameOverrides", "Global Game Overrides");
	return GlobalGameOverridesNodeName;
}

FText UMovieGraphGlobalGameOverridesNode::GetMenuCategory() const
{
	return NodeCategory_Globals;
}

FLinearColor UMovieGraphGlobalGameOverridesNode::GetNodeTitleColor() const
{
	static const FLinearColor GlobalGameOverridesNodeColor = FLinearColor(0.549f, 0.f, 0.250f);
	return GlobalGameOverridesNodeColor;
}

FSlateIcon UMovieGraphGlobalGameOverridesNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon GlobalGameOverridesIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Launcher.TabIcon");

	OutColor = FLinearColor::White;
	return GlobalGameOverridesIcon;
}
#endif // WITH_EDITOR

TArray<FMovieGraphPinProperties> UMovieGraphPathTracedRendererNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphPathTracedRendererNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphPathTracedRendererNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText PathTracedRendererNodeName = LOCTEXT("NodeName_PathTracedRenderer", "Path Traced Renderer");
	return PathTracedRendererNodeName;
}

FText UMovieGraphPathTracedRendererNode::GetMenuCategory() const
{
	return NodeCategory_Renderers;
}

FLinearColor UMovieGraphPathTracedRendererNode::GetNodeTitleColor() const
{
	static const FLinearColor DeferredRendererNodeColor = FLinearColor(0.572f, 0.274f, 1.f);
	return DeferredRendererNodeColor;
}

FSlateIcon UMovieGraphPathTracedRendererNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon DeferredRendererIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "SequenceRecorder.TabIcon");

	OutColor = FLinearColor::White;
	return DeferredRendererIcon;
}
#endif // WITH_EDITOR

TArray<FMovieGraphPinProperties> UMovieGraphEXRSequenceNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphEXRSequenceNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphEXRSequenceNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText EXRSequenceNodeName = LOCTEXT("NodeName_EXRSequence", "EXR Sequence");
	return EXRSequenceNodeName;
}

FText UMovieGraphEXRSequenceNode::GetMenuCategory() const
{
	return NodeCategory_OutputType;
}

FLinearColor UMovieGraphEXRSequenceNode::GetNodeTitleColor() const
{
	static const FLinearColor ImageSequenceNodeColor = FLinearColor(0.047f, 0.654f, 0.537f);
	return ImageSequenceNodeColor;
}

FSlateIcon UMovieGraphEXRSequenceNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon ImageSequenceIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Texture2D");

	OutColor = FLinearColor::White;
	return ImageSequenceIcon;
}
#endif // WITH_EDITOR

TArray<FMovieGraphPinProperties> UMovieGraphBranchNode::GetInputPinProperties() const
{
	static const FName TrueBranch("True");
	static const FName FalseBranch("False");
	static const FName Condition("Condition");
	
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(TrueBranch, EMovieGraphMemberType::Branch, false));
	Properties.Add(FMovieGraphPinProperties(FalseBranch, EMovieGraphMemberType::Branch, false));
	Properties.Add(FMovieGraphPinProperties(Condition, EMovieGraphMemberType::Bool, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphBranchNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphBranchNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText BranchNodeName = LOCTEXT("NodeName_Branch", "Branch");
	return BranchNodeName;
}

FText UMovieGraphBranchNode::GetMenuCategory() const
{
	return NodeCategory_Conditionals;
}

FLinearColor UMovieGraphBranchNode::GetNodeTitleColor() const
{
	static const FLinearColor BranchNodeColor = FLinearColor(0.266f, 0.266f, 0.266f);
	return BranchNodeColor;
}

FSlateIcon UMovieGraphBranchNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon BranchIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Merge");

	OutColor = FLinearColor::White;
	return BranchIcon;
}
#endif // WITH_EDITOR

TArray<FMovieGraphPinProperties> UMovieGraphSelectNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;

	// Generate branch pins for each option
	for (const FString& SelectOption : SelectOptions)
	{
		Properties.Add(FMovieGraphPinProperties(FName(SelectOption), EMovieGraphMemberType::Branch, false));
	}

	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::String, false));
	
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphSelectNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphSelectNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText SelectNodeName = LOCTEXT("NodeName_Select", "Select");
	static const FText SelectNodeDescription = LOCTEXT("NodeDescription_Select", "Select\n{0}");

	if (bGetDescriptive && !Description.IsEmpty())
	{
		return FText::Format(SelectNodeDescription, FText::FromString(Description));
	}

	return SelectNodeName;
}

FText UMovieGraphSelectNode::GetMenuCategory() const
{
	return NodeCategory_Conditionals;
}

FLinearColor UMovieGraphSelectNode::GetNodeTitleColor() const
{
	static const FLinearColor SelectNodeColor = FLinearColor(0.266f, 0.266f, 0.266f);
	return SelectNodeColor;
}

FSlateIcon UMovieGraphSelectNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon SelectIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Merge");

	OutColor = FLinearColor::White;
	return SelectIcon;
}

void UMovieGraphSelectNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMovieGraphSelectNode, SelectOptions))
	{
		UpdatePins();
	}
	
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMovieGraphSelectNode, Description))
	{
		OnNodeChangedDelegate.Broadcast(this);
	}
}
#endif // WITH_EDITOR

TArray<FMovieGraphPinProperties> UMovieGraphOutputSettingsNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphOutputSettingsNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphOutputSettingsNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText OutputSettingsNodeName = LOCTEXT("NodeName_OutputSettings", "Output Settings");
	return OutputSettingsNodeName;
}

FText UMovieGraphOutputSettingsNode::GetMenuCategory() const
{
	return NodeCategory_Settings;
}

FLinearColor UMovieGraphOutputSettingsNode::GetNodeTitleColor() const
{
	static const FLinearColor OutputSettingsColor = FLinearColor(0.854f, 0.509f, 0.039f);
	return OutputSettingsColor;
}

FSlateIcon UMovieGraphOutputSettingsNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon SettingsIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings");

	OutColor = FLinearColor::White;
	return SettingsIcon;
}
#endif // WITH_EDITOR

TArray<FMovieGraphPinProperties> UMovieGraphAntiAliasingNode::GetInputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

TArray<FMovieGraphPinProperties> UMovieGraphAntiAliasingNode::GetOutputPinProperties() const
{
	TArray<FMovieGraphPinProperties> Properties;
	Properties.Add(FMovieGraphPinProperties(NAME_None, EMovieGraphMemberType::Branch, false));
	return Properties;
}

#if WITH_EDITOR
FText UMovieGraphAntiAliasingNode::GetNodeTitle(const bool bGetDescriptive) const
{
	static const FText AntiAliasingNodeName = LOCTEXT("NodeName_AntiAliasing", "Anti-Aliasing");
	return AntiAliasingNodeName;
}

FText UMovieGraphAntiAliasingNode::GetMenuCategory() const
{
	return NodeCategory_Settings;
}

FLinearColor UMovieGraphAntiAliasingNode::GetNodeTitleColor() const
{
	static const FLinearColor AntiAliasingColor = FLinearColor(0.043f, 0.219f, 0.356f);
	return AntiAliasingColor;
}

FSlateIcon UMovieGraphAntiAliasingNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon SettingsIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings");

	OutColor = FLinearColor::White;
	return SettingsIcon;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE