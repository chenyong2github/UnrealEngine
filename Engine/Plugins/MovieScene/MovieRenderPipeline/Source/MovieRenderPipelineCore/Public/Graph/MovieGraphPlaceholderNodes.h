// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieGraphNode.h"

#include "MovieGraphPlaceholderNodes.generated.h"

// TODO: This should probably be called UMovieGraphCollectionNode instead
/** A graph node which creates collections that are available for use in other nodes. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMoviePipelineCollectionNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMoviePipelineCollectionNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

	virtual TArray<FPropertyBagPropertyDesc> GetDynamicPropertyDescriptions() const override
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
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	//~ Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
#endif

	/** The name of this collection, which is used to reference this collection in the graph. */
	UPROPERTY(EditAnywhere, Category = "General")
	FString CollectionName;
};

/** A node which modifies part of the world being rendered. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphModifierNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMovieGraphModifierNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	//~ Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
#endif

	// TODO: Temporary - The modifier name will come from the specific specialized modifier node type (eg, MaterialModifier) in the future
	/** The name of this modifier. */
	UPROPERTY(EditAnywhere, Category = "General")
	FString ModifierName;

	/** The name of the collection being modified. */
	UPROPERTY(EditAnywhere, Category = "General")
	FString ModifiedCollectionName;
};

/** A node which configures the global game overrides. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphGlobalGameOverridesNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMovieGraphGlobalGameOverridesNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
#endif
};

/** A node which represents a path traced renderer. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphPathTracedRendererNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMovieGraphPathTracedRendererNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
#endif
};

/** A node which generates an EXR image sequence. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphEXRSequenceNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMovieGraphEXRSequenceNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
#endif
};

/** A node which creates a branching condition. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphBranchNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMovieGraphBranchNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
#endif
};

// TODO: Currently this node is restricted to just accepting string-based options.
/** A node which creates a condition that selects from a set of input branches. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphSelectNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMovieGraphSelectNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	//~ Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
#endif

	/** The values of options which can be selected. */
	UPROPERTY(EditAnywhere, Category = "General")
	TArray<FString> SelectOptions;

	/** The value of the option which has been selected. */
	UPROPERTY(EditAnywhere, Category = "General")
	FString SelectedOption;

	/** A description of what this select is doing. */
	UPROPERTY(EditAnywhere, Category = "General")
	FString Description;
};

/** A node which configures output settings. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphOutputSettingsNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMovieGraphOutputSettingsNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
#endif
};

/** A node which configures anti-aliasing settings. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphAntiAliasingNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMovieGraphAntiAliasingNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

#if WITH_EDITOR
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const override;
	virtual FText GetMenuCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
#endif
};