// Copyright Epic Games, Inc. All Rights Reserved.
 
#pragma once
 
#include "Nodes/InterchangeBaseNode.h"
#include "UObject/Object.h"
 
#include "InterchangeShaderGraphNode.generated.h"
 
/**
 * The Shader Ports API manages a set of inputs and outputs attributes.
 * This API can be used over any InterchangeBaseNode that wants to support shader ports as attributes.
 */
UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeShaderPortsAPI : public UObject
{
	GENERATED_BODY()
 
public:
	/**
	 * Makes an attribute key to represent a node being connected to an input (ie: Inputs:InputName:Connect).
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	static FString MakeInputConnectionKey(const FString& InputName);
 
	/**
	 * Makes an attribute key to represent a value being given to an input (ie: Inputs:InputName:Value).
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	static FString MakeInputValueKey(const FString& InputName);
 
	/**
	 * From an attribute key associated with an input (ie: Inputs:InputName:Value), retrieves the input name from it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	static FString MakeInputName(const FString& InputKey);
	
	/**
	 * Returns true if the attribute key is associated with an input (starts with "Inputs:").
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	static bool IsAnInput(const FString& AttributeKey);
 
	/**
	 * Checks if a particular input exists on a given node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	static bool HasInput(const UInterchangeBaseNode* InterchangeNode, const FName& InInputName);
 
	/**
	 * Retrieves the names of all the inputs for a given node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	static void GatherInputs(const UInterchangeBaseNode* InterchangeNode, TArray<FString>& OutInputNames);
 
	/**
	 * Adds an input connection attribute.
	 * @param InterchangeNode	The Node to create the input on.
	 * @param InputName			The name to give to the input.
	 * @param ExpressionUid		The unique id of the node to connect to the input.
	 * @return					true if the input connection was succesfully added to the node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	static bool ConnectDefaultOuputToInput(UInterchangeBaseNode* InterchangeNode, const FString& InputName, const FString& ExpressionUid);
 
	/**
	 * Adds an input connection attribute.
	 * @param InterchangeNode	The Node to create the input on.
	 * @param InputName			The name to give to the input.
	 * @param ExpressionUid		The unique id of the node to connect to the input.
	 * @param OutputName		The name of the ouput from ExpressionUid to connect to the input.
	 * @return					true if the input connection was succesfully added to the node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	static bool ConnectOuputToInput(UInterchangeBaseNode* InterchangeNode, const FString& InputName, const FString& ExpressionUid, const FString& OutputName);
 
	/**
	 * Retrieves the node unique id and the ouputname connected to a given input, if any.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	static bool GetInputConnection(const UInterchangeBaseNode* InterchangeNode, const FString& InputName, FString& OutExpressionUid, FString& OutputName);
	
	/**
	 * For an input with a value, returns the type of the stored value.
	 */
	static UE::Interchange::EAttributeTypes GetInputType(const UInterchangeBaseNode* InterchangeNode, const FString& InputName);
 
private:
	static const TCHAR* InputPrefix;
	static const TCHAR* InputSeparator;
};
 
/**
 * A shader node is a named set of inputs and outputs. It can be connected to other shader nodes and finally to a shader graph input.
 */
UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeShaderNode : public UInterchangeBaseNode
{
	GENERATED_BODY()
 
public:
	virtual FString GetTypeName() const override;
 
public:
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetCustomShaderType(FString& AttributeValue) const;
 
	/**
	 * Sets which type of shader this nodes represents. Can be arbitrary or one of the predefined shader types.
	 * The material pipeline handling the shader node should be aware of the shader type that is being set here.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetCustomShaderType(const FString& AttributeValue);
 
private:
	const UE::Interchange::FAttributeKey Macro_CustomShaderTypeKey = UE::Interchange::FAttributeKey(TEXT("ShaderType"));
};
 
/**
 * A shader graph has its own set of inputs on which shader nodes can be connected to.
 */
UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeShaderGraphNode : public UInterchangeShaderNode
{
	GENERATED_BODY()
 
public:
	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

public:
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetCustomTwoSided(bool& AttributeValue) const;
 
	/**
	 * Sets if this shader graph should be rendered two sided or not. Defaults to off.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetCustomTwoSided(const bool& AttributeValue);

private:
	const UE::Interchange::FAttributeKey Macro_CustomTwoSidedKey = UE::Interchange::FAttributeKey(TEXT("TwoSided"));
};