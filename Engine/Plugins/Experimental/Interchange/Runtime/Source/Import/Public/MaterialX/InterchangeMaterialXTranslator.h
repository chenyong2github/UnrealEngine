// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

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
class UInterchangeShaderNode;

UCLASS(BlueprintType, Experimental)
class UInterchangeMaterialXTranslator : public UInterchangeTranslatorBase, public IInterchangeTexturePayloadInterface
{
	GENERATED_BODY()

public:

	UInterchangeMaterialXTranslator();

	/** Begin UInterchangeTranslatorBase API*/
	virtual TArray<FString> GetSupportedFormats() const override;

	/**
	 * Translate the associated source data into a node hold by the specified nodes container.
	 *
	 * @param BaseNodeContainer - The unreal objects descriptions container where to put the translated source data.
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
	 * @param NodeContainer - The unreal objects descriptions container where to put the translated source data.
	 * @param StandardSurfaceNode - The <standard_surface> in the MaterialX file
	 */
	void ProcessStandardSurface(UInterchangeBaseNodeContainer & NodeContainer, MaterialX::NodePtr StandardSurfaceNode, MaterialX::DocumentPtr Document) const;

	/**
	 * Connect an ouput in the NodeGraph to the ShaderGraph
	 * 
	 * @param InputToNodeGraph - The input from the standard surface to retrieve the output in the NodeGraph
	 * @param ShaderNode - The Interchange shader node to connect the MaterialX's node graph to
	 * @param ParentInputName - The name of the input of the shader node to which we want the node graph to be connected to
	 * @param NamesToShaderNodes - Map of the shader nodes already created
	 * @param NodeContainer - The unreal objects descriptions container where to put the translated source data.
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
	 * @param NodeContainer - The unreal objects descriptions container where to put the translated source data.
	 * 
	 * @return true if a shader node has been successfully created and is connected to the given input
	 */
	bool ConnectNodeOutputToInput(MaterialX::NodePtr Node, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName, TMap<FString, UInterchangeShaderNode*>& NamesToShaderNodes, UInterchangeBaseNodeContainer& NodeContainer) const;

	/**
	 * Helper function to create an InterchangeShaderNode
	 * 
	 * @param NodeName - The name of the shader node
	 * @param ShaderType - The shader node's type we want to create
	 * @param ParentNode - The parent node of the created node
	 * @param NamesToShaderNodes - Map of the shader nodes already created
	 * @param NodeContainer - The unreal objects descriptions container where to put the translated source data.
	 * 
	 * @return The shader node that was created
	 */
	UInterchangeShaderNode* CreateShaderNode(const FString & NodeName, const FString & ShaderType, UInterchangeShaderNode * ParentNode, TMap<FString, UInterchangeShaderNode*>& NamesToShaderNodes, UInterchangeBaseNodeContainer & NodeContainer) const;

	/**
	 * Helper function to create an InterchangeTextureNode
	 *
	 * @param Node - The MaterialX node, it should be of the category <image> no test is done on it
	 * @param NodeContainer - The unreal objects descriptions container where to put the translated source data.
	 * 
	 * @return The texture node that was created
	 */
	UInterchangeTextureNode* CreateTextureNode(MaterialX::NodePtr Node, UInterchangeBaseNodeContainer& NodeContainer) const;

	/**
	 * Get the UE corresponding name of MaterialX input of a material
	 * 
	 * @param Input - MaterialX input
	 * 
	 * @return The matched name of the input else empty string
	 */
	const FString& GetMatchedInputName(MaterialX::InputPtr Input) const;

	/**
	 * Rename the inputs names of a node to correspond to the one used by UE, it will keep the old inputs names under the attribute "oldname"
	 * 
	 * @param Node - Look up to all inputs of Node and rename them to match UE names
	 */
	void RenameInputsNames(MaterialX::NodePtr Node) const;

	/**
	 * Helper function to retrieve an Input in a Node from its old name (after a renaming)
	 * 
	 * @param Node - The node with the Inputs to look up
	 * @param OldNameAttribute - The previous name of an Input
	 * 
	 * @return The found Input, nullptr otherwise
	 */
	MaterialX::InputPtr GetInputFromOldName(MaterialX::NodePtr Node, const char * OldNameAttribute) const;

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

private:

	TMap<FString, FString> InputNamesMaterialX2UE;
	TMap<FString, FString> NodeNamesMaterialX2UE; //given a MaterialX node category, return the UE category
	TSet<FString> UEInputs;
#endif // WITH_EDITOR
};