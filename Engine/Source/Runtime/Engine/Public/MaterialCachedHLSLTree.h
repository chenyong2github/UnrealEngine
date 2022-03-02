// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "HLSLTree/HLSLTree.h"
#include "RHIDefinitions.h"
#include "SceneTypes.h"
#include "Misc/MemStack.h"

class UMaterial;
class UMaterialExpressionCustomOutput;
struct FMaterialLayersFunctions;

class FMaterialCachedHLSLTree
{
public:
	static const FMaterialCachedHLSLTree EmptyTree;

	ENGINE_API FMaterialCachedHLSLTree();
	ENGINE_API ~FMaterialCachedHLSLTree();

	SIZE_T GetAllocatedSize() const;

	ENGINE_API bool GenerateTree(UMaterial* Material, const FMaterialLayersFunctions* LayerOverrides, UMaterialExpression* PreviewExpression);

	UE::Shader::FStructTypeRegistry& GetTypeRegistry() { return TypeRegistry; }
	//UE::HLSLTree::FTree& GetTree() { return *HLSLTree; }

	const UE::Shader::FStructTypeRegistry& GetTypeRegistry() const { return TypeRegistry; }
	UE::HLSLTree::FTree& GetTree() const { return *HLSLTree; }

	UE::HLSLTree::FExpression* GetResultExpression() const { return ResultExpression; }
	UE::HLSLTree::FStatement* GetResultStatement() const { return ResultStatement; }

	const UE::Shader::FStructType* GetMaterialAttributesType() const { return MaterialAttributesType; }
	const UE::Shader::FValue& GetMaterialAttributesDefaultValue() const { return MaterialAttributesDefaultValue; }

	void SetRequestedFields(EShaderFrequency ShaderFrequency, UE::HLSLTree::FRequestedType& OutRequestedType) const;
	void EmitSharedCode(FStringBuilderBase& OutCode) const;

	bool IsAttributeUsed(UE::HLSLTree::FEmitContext& Context,
		UE::HLSLTree::FEmitScope& Scope,
		const UE::HLSLTree::FPreparedType& ResultType,
		EMaterialProperty Property) const;

private:
	FMemStackBase Allocator;
	UE::Shader::FStructTypeRegistry TypeRegistry;
	UE::HLSLTree::FTree* HLSLTree = nullptr;
	UE::HLSLTree::FExpression* ResultExpression = nullptr;
	UE::HLSLTree::FStatement* ResultStatement = nullptr;

	TArray<UMaterialExpressionCustomOutput*> MaterialCustomOutputs;
	const UE::Shader::FStructType* MaterialAttributesType = nullptr;
	UE::Shader::FValue MaterialAttributesDefaultValue;

	friend class FMaterialHLSLGenerator;
};

#endif // WITH_EDITOR
