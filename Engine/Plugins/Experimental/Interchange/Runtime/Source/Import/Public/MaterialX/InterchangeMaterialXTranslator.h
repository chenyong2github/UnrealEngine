// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "InterchangeShaderGraphNode.h"
#include "InterchangeTranslatorBase.h"
#include "Texture/InterchangeTexturePayloadInterface.h"

#if WITH_EDITOR
#if defined(PRAGMA_DISABLE_MISSING_BRACES_WARNINGS)
PRAGMA_DISABLE_MISSING_BRACES_WARNINGS
#endif
#include "MaterialXCore/Document.h"
#if defined(PRAGMA_ENABLE_MISSING_BRACES_WARNINGS)
PRAGMA_ENABLE_MISSING_BRACES_WARNINGS
#endif
#endif

#include "InterchangeMaterialXTranslator.generated.h"

class UInterchangeTextureNode;
class UInterchangeBaseLightNode;
class UInterchangeSceneNode;

UCLASS(BlueprintType, Experimental)
class UInterchangeMaterialXTranslator : public UInterchangeTranslatorBase, public IInterchangeTexturePayloadInterface
{
	GENERATED_BODY()

public:

	UInterchangeMaterialXTranslator();

	/** Begin UInterchangeTranslatorBase API*/

	virtual EInterchangeTranslatorType GetTranslatorType() const override;

	virtual bool DoesSupportAssetType(EInterchangeTranslatorAssetType AssetType) const override;

	virtual TArray<FString> GetSupportedFormats() const override;

	/**
	 * Translate the associated source data into a node hold by the specified nodes container.
	 *
	 * @param BaseNodeContainer - The container where to add the translated Interchange nodes.
	 * @return true if the translator can translate the source data, false otherwise.
	 */
	virtual bool Translate( UInterchangeBaseNodeContainer& BaseNodeContainer ) const override;
	/** End UInterchangeTranslatorBase API*/

	virtual TOptional<UE::Interchange::FImportImage> GetTexturePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayloadKey) const override;

#if WITH_EDITOR
protected:

	/**
	 * Process Autodesk's standard surface shader
	 * 
	 * @param NodeContainer - The container where to add the translated Interchange nodes.
	 * @param StandardSurfaceNode - The <standard_surface> in the MaterialX file
	 * @param Document - The MaterialX Document that contains all the definitions of the loaded libraries
	 */
	void ProcessStandardSurface(UInterchangeBaseNodeContainer & NodeContainer, MaterialX::NodePtr StandardSurfaceNode, MaterialX::DocumentPtr Document) const;

	/**
	 * Process light shader, MaterialX doesn't standardized lights, but defines the 3 common ones, directional, point and spot
	 * 
	 * @param The container where to add the translated Interchange nodes.
	 * @param LightShaderNode - a MaterialX light shader node
	 * @param Document - The MaterialX Document that contains all the definitions of the loaded libraries
	 */
	void ProcessLightShader(UInterchangeBaseNodeContainer& NodeContainer, MaterialX::NodePtr LightShaderNode, MaterialX::DocumentPtr Document) const;

	/**
	 * Create a directional light node and set the proper transform in the scene node
	 *
	 * @param DirectionalLightShaderNode - The MaterialX node related to a <directional_light>
	 * @param SceneNode - The interchange scene node, to which the node will be attached to
	 * @param The container where to add the translated Interchange nodes.
	 * @param Document - The MaterialX Document that contains all the definitions of the loaded libraries
	 */
	UInterchangeBaseLightNode* CreateDirectionalLightNode(MaterialX::NodePtr DirectionalLightShaderNode, UInterchangeSceneNode* SceneNode, UInterchangeBaseNodeContainer& NodeContainer, MaterialX::DocumentPtr Document) const;

	/**
	 * Create a point  light node and set the proper transform in the scene node
	 *
	 * @param PointLightShaderNode - The MaterialX node related to a <point_light>
	 * @param SceneNode - The interchange scene node, to which the node will be attached to
	 * @param The container where to add the translated Interchange nodes.
	 * @param Document - The MaterialX Document that contains all the definitions of the loaded libraries
	 */
	UInterchangeBaseLightNode* CreatePointLightNode(MaterialX::NodePtr PointLightShaderNode, UInterchangeSceneNode* SceneNode, UInterchangeBaseNodeContainer& NodeContainer, MaterialX::DocumentPtr Document) const;
	
	/**
	 * Create a spot light node and set the proper transform in the scene node
	 *
	 * @param SpotLightShaderNode - The MaterialX node related to a <spot_light>
	 * @param SceneNode - The interchange scene node, to which the node will be attached to
	 * @param The container where to add the translated Interchange nodes.
	 * @param Document - The MaterialX Document that contains all the definitions of the loaded libraries
	 */
	UInterchangeBaseLightNode* CreateSpotLightNode(MaterialX::NodePtr SpotLightShaderNode, UInterchangeSceneNode* SceneNode, UInterchangeBaseNodeContainer& NodeContainer, MaterialX::DocumentPtr Document) const;
	/**
	 * Connect an ouput in the NodeGraph to the ShaderGraph
	 * 
	 * @param InputToNodeGraph - The input from the standard surface to retrieve the output in the NodeGraph
	 * @param ShaderNode - The Interchange shader node to connect the MaterialX's node graph to
	 * @param ParentInputName - The name of the input of the shader node to which we want the node graph to be connected to
	 * @param NamesToShaderNodes - Map of the shader nodes already created
	 * @param The container where to add the translated Interchange nodes.
	 * 
	 * @return true if the given input is attached to one of the outputs of a node graph
	 */
	bool ConnectNodeGraphOutputToInput(MaterialX::InputPtr InputToNodeGraph, UInterchangeShaderNode * ShaderNode, const FString& ParentInputName, TMap<FString, UInterchangeShaderNode*> & NamesToShaderNodes, UInterchangeBaseNodeContainer & NodeContainer) const;

	/**
	 * Create and Connect the output of a node to a shader node
	 * 
	 * @param Node - The MaterialX node of a given type used to create the appropriate shader node
	 * @param ParentShaderNode - The shader node to connect to
	 * @param InputChannelName - The input of the ParentShaderNode to connect to
	 * @param NamesToShaderNodes - Map of the shader nodes already created
	 * @param The container where to add the translated Interchange nodes.
	 * 
	 * @return true if a shader node has been successfully created and is connected to the given input
	 */
	bool ConnectNodeOutputToInput(MaterialX::NodePtr Node, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName, TMap<FString, UInterchangeShaderNode*>& NamesToShaderNodes, UInterchangeBaseNodeContainer& NodeContainer) const;

	/**
	 * Helper template function to create an InterchangeShaderNode
	 * 
	 * @param NodeName - The name of the shader node
	 * @param ShaderType - The shader node's type we want to create
	 * @param ParentNode - The parent node of the created node
	 * @param NamesToShaderNodes - Map of the shader nodes already created
	 * @param The container where to add the translated Interchange nodes.
	 * 
	 * @return The shader node that was created
	 */
	template<typename ShaderNodeType>
	ShaderNodeType* CreateShaderNode(const FString& NodeName, const FString& ShaderType, const FString & ParentNodeUID, TMap<FString, UInterchangeShaderNode*>& NamesToShaderNodes, UInterchangeBaseNodeContainer& NodeContainer) const
	{
		static_assert(std::is_convertible_v<ShaderNodeType*, UInterchangeShaderNode*>, "CreateShaderNode only accepts type that derived from UInterchangeShaderNode");

		ShaderNodeType* Node;

		const FString NodeUID = UInterchangeShaderNode::MakeNodeUid(NodeName, ParentNodeUID);
		
		//Test directly in the NodeContainer, because the NamesToShaderNodes can be altered during the node graph either by the parent (dot/normalmap),
		//or by putting an intermediary node between the child and the parent (tiledimage)
		if(Node = const_cast<ShaderNodeType*>(Cast<ShaderNodeType>(NodeContainer.GetNode(NodeUID))); !Node)
		{
			Node = NewObject<ShaderNodeType>(&NodeContainer);
			Node->InitializeNode(NodeUID, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
			NodeContainer.AddNode(Node);
			if constexpr(std::is_same_v<ShaderNodeType, UInterchangeShaderGraphNode>)
			{
				NodeContainer.SetNodeParentUid(NodeUID, ParentNodeUID);
			}
			Node->SetCustomShaderType(ShaderType);

			NamesToShaderNodes.Add(NodeName, Node);
		}

		return Node;
	}

	/**
	 * Helper function to create an InterchangeTextureNode
	 *
	 * @param Node - The MaterialX node, it should be of the category <image> no test is done on it
	 * @param The container where to add the translated Interchange nodes.
	 * 
	 * @return The texture node that was created
	 */
	UInterchangeTextureNode* CreateTextureNode(MaterialX::NodePtr Node, UInterchangeBaseNodeContainer& NodeContainer) const;

	/**
	 * Get the UE corresponding name of a MaterialX Node category and input for a material
	 * 
	 * @param Input - MaterialX input
	 * 
	 * @return The matched name of the Node/Input else empty string
	 */
	const FString& GetMatchedInputName(MaterialX::NodePtr Node, MaterialX::InputPtr Input) const;
	
	/**
	 * Rename the inputs names of a node to correspond to the one used by UE, it will keep the old inputs names under the attribute "oldname"
	 * 
	 * @param Node - Look up to all inputs of Node and rename them to match UE names
	 */
	void RenameNodeInputs(MaterialX::NodePtr Node) const;

	/**
	 * Rename the input name, it will keep the original input name under the attribute MaterialX::Attributes::OriginalName.
	 * It will keep the uniqueness of the name inside the MaterialX Document
	 *
	 * @param Input - The input to rename
	 * @param NewName - the new name of the input
	 */
	void RenameInput(MaterialX::InputPtr Input, const char * NewName) const;

	/**
	 * Helper function to retrieve an Input in a Node from its original name (after a renaming)
	 * 
	 * @param Node - The node with the Inputs to look up
	 * @param OriginalNameAttribute - The previous name of an Input
	 * 
	 * @return The found Input, nullptr otherwise
	 */
	MaterialX::InputPtr GetInputFromOriginalName(MaterialX::NodePtr Node, const char * OriginalNameAttribute) const;

	/**
	 * Get the input name, use this function instead of getName, because a renaming may have occured and we ensure to have the proper name that will be used by UE inputs
	 * 
	 * @param Input - The input to retrieve the name from
	 * 
	 * @return The input name
	 */
	FString GetInputName(MaterialX::InputPtr Input) const;

	/**
	 * Retrieve the input from a standard_surface node, or take the default input from the library, 
	 * this function should only be called after testing the MaterialX libraries have been successfully imported, meaning the node definition of the standard_surface 
	 * should always be valid
	 * 
	 * @param StandardSurface - the <standard_surface> node
	 * @param InputName - the input name to retrieve
	 * @param Document - the MaterialX library which has the node definition of the standard_surface
	 * 
	 * @return the input from the given name
	 */
	MaterialX::InputPtr GetStandardSurfaceInput(MaterialX::NodePtr StandardSurface, const char* InputName, MaterialX::DocumentPtr Document) const;

	/**
	 * Retrieve the input from a point_light node, or take the default input from the library,
	 * this function should only be called after testing the MaterialX libraries have been successfully imported, meaning the node definition of the point_light
	 * should always be valid
	 *
	 * @param PointLight - the <point_light> node
	 * @param InputName - the input name to retrieve
	 * @param Document - the MaterialX library which has the node definition of the standard_surface
	 *
	 * @return the input from the given name
	 */
	MaterialX::InputPtr GetPointLightInput(MaterialX::NodePtr PointLight, const char* InputName, MaterialX::DocumentPtr Document) const;

	/**
	 * Retrieve the input from a directional_light node, or take the default input from the library,
	 * this function should only be called after testing the MaterialX libraries have been successfully imported, meaning the node definition of the directional_light
	 * should always be valid
	 *
	 * @param DirectionalLight - the <directional_light> node
	 * @param InputName - the input name to retrieve
	 * @param Document - the MaterialX library which has the node definition of the standard_surface
	 *
	 * @return the input from the given name
	 */
	MaterialX::InputPtr GetDirectionalLightInput(MaterialX::NodePtr DirectionalLight, const char* InputName, MaterialX::DocumentPtr Document) const;

	/**
	 * Retrieve the input from a spot_light node, or take the default input from the library,
	 * this function should only be called after testing the MaterialX libraries have been successfully imported, meaning the node definition of the spot_light
	 * should always be valid
	 *
	 * @param SpotLight - the <spot_light > node
	 * @param InputName - the input name to retrieve
	 * @param Document - the MaterialX library which has the node definition of the standard_surface
	 *
	 * @return the input from the given name
	 */
	MaterialX::InputPtr GetSpotLightInput(MaterialX::NodePtr SpotLight, const char* InputName, MaterialX::DocumentPtr Document) const;

	/**
	 * Add an attribute to a shader node, only floats and linear colors are supported for the moment
	 * 
	 * @param Input - The MaterialX input to retrieve and add the value from, must be of type float/color/vector
	 * @param InputChannelName - The name of the shader node's input to add the attribute
	 * @param ShaderNode - The shader node to which we want to add the attribute
	 * 
	 * @return true if the attribute was successfully added
	 */
	bool AddAttribute(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode) const;

	/**
	 * Add a float attribute to a shader node only if its value taken from the input is not equal to its default value. Return false if the attribute does not exist or if we cannot add it
	 *
	 * @param Input - The MaterialX input to retrieve and add the value from, must be of type float/color/vector
	 * @param InputChannelName - The name of the shader node's input to add the attribute
	 * @param ShaderNode - The shader node to which we want to add the attribute
	 * @param DefaultValue - the default value to test the input against
	 *
	 * @return true if the attribute was successfully added
	 */
	bool AddFloatAttribute(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode, float DefaultValue) const;

	/**
	 * Add a FLinearColor attribute to a shader node only if its value taken from the input is not equal to its default value. Return false if the attribute does not exist or if we cannot add it
	 *
	 * @param Input - The MaterialX input to retrieve and add the value from, must be of type float/color/vector
	 * @param InputChannelName - The name of the shader node's input to add the attribute
	 * @param ShaderNode - The shader node to which we want to add the attribute
	 * @param DefaultValue - the default value to test the input against
	 *
	 * @return true if the attribute was successfully added
	 */
	bool AddLinearColorAttribute(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode, const FLinearColor& DefaultValue) const;

	/**
	 * Return the innermost file prefix of an element in the current scope, if it has none, it will take the one from its parents
	 * 
	 * @param Element - the Element to retrieve the file prefix from (can be anything, an input, a node, a nodegraph, etc.)
	 * 
	 * @return a file prefix or an empty string
	 * 
	 */
	FString GetFilePrefix(MaterialX::ElementPtr Element) const;

	/**
	 * Return the innermost color space of an element in the current scope, if it has none, it will take the one from its parents
	 *
	 * @param Element - the Element to retrieve the color space from (can be anything, an input, a node, a nodegraph, etc.)
	 *
	 * @return a color space or an empty string
	 *
	 */
	FString GetColorSpace(MaterialX::ElementPtr Element) const;

	/**
	 * Helper function that returns a color after a color space conversion, the function makes no assumption on the input, and it should have a value of Color3 type
	 * 
	 * @param Input - The input that has a Color3 value in it
	 * 
	 * @return The linear color after color space conversion
	 */
	FLinearColor MakeLinearColorFromColor3(MaterialX::InputPtr Input) const;

	/**
	 * Helper function that returns a color after a color space conversion, the function makes no assumption on the input, and it should have a value of Color3 type
	 *
	 * @param Input - The input that has a Color3 value in it
	 *
	 * @return The linear color after color space conversion
	 */
	FLinearColor MakeLinearColorFromColor4(MaterialX::InputPtr Input) const;

private:

	TMap<TPair<FString, FString>, FString> InputNamesMaterialX2UE; //given a MaterialX node (category - input), return the UE/Interchange input name.
	TMap<FString, FString> NodeNamesMaterialX2UE; //given a MaterialX node category, return the UE category
	TSet<FString> UEInputs;
#endif // WITH_EDITOR
};