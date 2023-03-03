// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "MovieGraphPin.h"
#include "InstancedStruct.h"
#include "PropertyBag.h"
#include "MovieGraphNode.generated.h"


// Forward Declares
class UMovieGraphPin;
class UMovieGraphVariable;

#if WITH_EDITOR
class UEdGraphNode;
#endif

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovieGraphNodeChanged, UMovieGraphNode*);

/**
* This is a base class for all nodes that can exist in the UMovieGraphConfig network.
* In the editor, each node in the network will have an editor-only representation too 
* which contains data about it's visual position in the graph, comments, etc.
*/
UCLASS(Abstract)
class MOVIERENDERPIPELINECORE_API UMovieGraphNode : public UObject
{
	GENERATED_BODY()

	friend class UMovieGraphConfig;
	friend class UMovieGraphEdge;
	
public:
	UMovieGraphNode();

	const TArray<TObjectPtr<UMovieGraphPin>>& GetInputPins() const { return InputPins; }
	const TArray<TObjectPtr<UMovieGraphPin>>& GetOutputPins() const { return OutputPins; }
	
	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const
	{
		return TArray<FMovieGraphPinProperties>();
	}
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const
	{
		return TArray<FMovieGraphPinProperties>();
	}

	virtual TArray<FPropertyBagPropertyDesc> GetDynamicPropertyDescriptions() const
	{
		return TArray<FPropertyBagPropertyDesc>();
	}

	virtual TArray<FName> GetExposedDynamicProperties() const
	{ 
		return ExposedDynamicPropertyNames;
	}

	/** Promotes the property with the given name to a pin on the node via a dynamic property. */
	virtual void PromoteDynamicPropertyToPin(const FName& PropertyName);

	void UpdatePins();
	void UpdateDynamicProperties();
	class UMovieGraphConfig* GetGraph() const;
	UMovieGraphPin* GetInputPin(const FName& InPinLabel) const;
	UMovieGraphPin* GetOutputPin(const FName& InPinLabel) const;

	/** Gets the GUID which uniquely identifies this node. */
	const FGuid& GetGuid() const { return Guid; }

#if WITH_EDITOR
	int32 GetNodePosX() const { return NodePosX; }
	int32 GetNodePosY() const { return NodePosY; }

	void SetNodePosX(const int32 InNodePosX) { NodePosX = InNodePosX; }
	void SetNodePosY(const int32 InNodePosY) { NodePosY = InNodePosY; }
#endif

public:
	FOnMovieGraphNodeChanged OnNodeChangedDelegate;

#if WITH_EDITORONLY_DATA
	/** Editor Node Graph representation. Not strongly typed to avoid circular dependency between editor/runtime modules. */
	UPROPERTY()
	TObjectPtr<UEdGraphNode>	GraphNode;

	class UEdGraphNode* GetGraphNode() const;
#endif

#if WITH_EDITOR
	virtual FText GetMenuDescription() const PURE_VIRTUAL(UMovieGraphNode::GetMenuDescription, return FText(););
	virtual FText GetMenuCategory() const PURE_VIRTUAL(UMovieGraphNode::GetMenuCategory, return FText(); );
#endif

protected:
	virtual TArray<FMovieGraphPinProperties> GetExposedDynamicPinProperties() const;

protected:
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphPin>> InputPins;
	
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphPin>> OutputPins;

	UPROPERTY(EditAnywhere, meta=(FixedLayout), Category = "Node")
	FInstancedPropertyBag DynamicProperties;

	UPROPERTY()
	TArray<FName> ExposedDynamicPropertyNames;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	int32 NodePosX = 0;

	UPROPERTY()
	int32 NodePosY = 0;
#endif

	/** A GUID which uniquely identifies this node. */
	UPROPERTY()
	FGuid Guid;
};

// Dummy test nodes
UCLASS()
class UMoviePipelineCollectionNode : public UMovieGraphNode
{
	GENERATED_BODY()
public:
	UMoviePipelineCollectionNode()
	{
	}

	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override
	{
		TArray<FMovieGraphPinProperties> Properties;
		Properties.Add(FMovieGraphPinProperties(TEXT("Input"), false));
		return Properties;
	}

	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override
	{
		TArray<FMovieGraphPinProperties> Properties;
		Properties.Add(FMovieGraphPinProperties(TEXT("Output"), false));
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
		return NSLOCTEXT("debug", "collection nodename", "Component Collection");
	}
	
	virtual FText GetMenuCategory() const override
	{
		return NSLOCTEXT("debug", "collection cat", "Rendering");
	}
#endif
};

UCLASS()
class UMoviePipelineRenderLayerNode : public UMovieGraphNode
{
	GENERATED_BODY()
	
public:
	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override
	{
		TArray<FMovieGraphPinProperties> Properties;
		Properties.Add(FMovieGraphPinProperties(TEXT("Test Input 3"), false));
		Properties.Add(FMovieGraphPinProperties(TEXT("Test Input 4"), false));
		Properties.Add(FMovieGraphPinProperties(TEXT("Test Input 5"), false));
		Properties.Add(FMovieGraphPinProperties(TEXT("Test Input 6"), false));
		return Properties;
	}

	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override
	{
		TArray<FMovieGraphPinProperties> Properties;
		Properties.Add(FMovieGraphPinProperties(TEXT("Test Output 2"), false));
		return Properties;
	}

#if WITH_EDITOR
	virtual FText GetMenuDescription() const override
	{
		return NSLOCTEXT("debug", "collection nodename2", "Deferred Render Layer");
	}
	
	virtual FText GetMenuCategory() const override
	{
		return NSLOCTEXT("debug", "collection cat", "Rendering");
	}
#endif
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings2")
	FString LayerName;
};


UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphOutputNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override
	{
		TArray<FMovieGraphPinProperties> Properties;
		Properties.Add(FMovieGraphPinProperties(TEXT("Output"), false));
		return Properties;
	}

#if WITH_EDITOR
	virtual FText GetMenuDescription() const override { return NSLOCTEXT("MovieGraphNodes", "OutputNode_Description", "Output"); }
	virtual FText GetMenuCategory() const override { return NSLOCTEXT("MovieGraphNodes", "OutputNode_Category", "Input/Output"); }
#endif
};

UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphInputNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override
	{
		TArray<FMovieGraphPinProperties> Properties;
		Properties.Add(FMovieGraphPinProperties(TEXT("Input"), false));
		return Properties;
	}


#if WITH_EDITOR
	virtual FText GetMenuDescription() const override { return NSLOCTEXT("MovieGraphNodes", "InputNode_Description", "Input"); }
	virtual FText GetMenuCategory() const override { return NSLOCTEXT("MovieGraphNodes", "InputNode_Category", "Input/Output"); }
#endif
};

/** A node which gets the value of a variable which has been defined on the graph. */
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphVariableNode : public UMovieGraphNode
{
	GENERATED_BODY()

public:
	UMovieGraphVariableNode() = default;

	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;

	/** Gets the variable that this node represents. */
	UMovieGraphVariable* GetVariable() const { return GraphVariable; }

	/** Sets the variable that this node represents. */
	void SetVariable(UMovieGraphVariable* InVariable);

#if WITH_EDITOR
	virtual FText GetMenuDescription() const override;
	virtual FText GetMenuCategory() const override;
#endif

private:
	/** Updates the output pin on the node to match the provided variable. */
	void UpdateOutputPin(UMovieGraphVariable* ChangedVariable);

private:
	/** The underlying graph variable this node represents. */
	UPROPERTY()
	TObjectPtr<UMovieGraphVariable> GraphVariable = nullptr;

	/** The properties for the output pin on this node. */
	UPROPERTY(Transient)
	FMovieGraphPinProperties OutputPin;
};