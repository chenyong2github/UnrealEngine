// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Containers/Map.h"
#include "Misc/MemStack.h"
#include "Templates/RefCounting.h"
#include "RHIDefinitions.h"
#include "HLSLTree/HLSLTree.h"

class FMaterial;
class FMaterialCompilationOutput;
struct FSharedShaderCompilerEnvironment;
struct FMaterialCompileTargetParameters;
class UMaterial;
class UMaterialFunctionInterface;
class UMaterialExpression;
class UMaterialExpressionFunctionInput;
class UMaterialExpressionFunctionOutput;
struct FFunctionExpressionInput;
class ITargetPlatform;
enum class EMaterialGenerateHLSLStatus : uint8;

namespace UE
{
namespace HLSLTree
{
struct FSwizzleParameters;
class FExpressionConstant;
class FExpressionExternalInput;
class FExpressionSwizzle;
class FExpressionCast;
class FFunctionCall;
}
}

/**
 * MaterialHLSLGenerator is a bridge between a material, and HLSLTree.  It facilitates generating HLSL source code for a given material, using HLSLTree
 */
class FMaterialHLSLGenerator
{
public:
	FMaterialHLSLGenerator(UMaterial* InTargetMaterial, const FMaterialCompileTargetParameters& InCompilerTarget,
		UE::HLSLTree::FTree& InOutTree);
	FMaterialHLSLGenerator(UMaterialFunctionInterface* InTargetMaterialFunction, const FMaterialCompileTargetParameters& InCompilerTarget,
		UE::HLSLTree::FTree& InOutTree);

	const FMaterialCompileTargetParameters& GetCompileTarget() const { return CompileTarget; }

	//bool GenerateHLSL(FMaterial& InMaterial,
	//	FMaterialCompilationOutput& OutCompilationOutput,
	//	TRefCountPtr<FSharedShaderCompilerEnvironment>& OutMaterialEnvironment);

	/** Retrieve the compile errors from the generator */
	void AcquireErrors(TArray<FString>& OutCompileErrors, TArray<UMaterialExpression*>& OutErrorExpressions);

	EMaterialGenerateHLSLStatus Error(const FString& Message);

	template <typename FormatType, typename... ArgTypes>
	inline EMaterialGenerateHLSLStatus Errorf(const FormatType& Format, ArgTypes... Args)
	{
		return Error(FString::Printf(Format, Args...));
	}

	UE::HLSLTree::FTree& GetTree() const { return *HLSLTree; }

	UE::HLSLTree::FStatement* NewResult(UE::HLSLTree::FScope& Scope);

	UE::HLSLTree::FExpressionConstant* NewConstant(UE::HLSLTree::FScope& Scope, const UE::Shader::FValue& Value);
	UE::HLSLTree::FExpressionExternalInput* NewTexCoord(UE::HLSLTree::FScope& Scope, int32 Index);
	UE::HLSLTree::FExpressionSwizzle* NewSwizzle(UE::HLSLTree::FScope& Scope, const UE::HLSLTree::FSwizzleParameters& Params, UE::HLSLTree::FExpression* Input);
	UE::HLSLTree::FExpression* NewCast(UE::HLSLTree::FScope& Scope, UE::Shader::EValueType Type, UE::HLSLTree::FExpression* Input, UE::HLSLTree::ECastFlags Flags = UE::HLSLTree::ECastFlags::None);

	UE::HLSLTree::FExpression* NewFunctionInput(UE::HLSLTree::FScope& Scope, int32 InputIndex, UMaterialExpressionFunctionInput* MaterialFunctionInput);

	UE::HLSLTree::FLocalDeclaration* AcquireLocalDeclaration(UE::HLSLTree::FScope& Scope, UE::Shader::EValueType Type, const FName& Name);
	UE::HLSLTree::FParameterDeclaration* AcquireParameterDeclaration(UE::HLSLTree::FScope& Scope, const FName& Name, const UE::Shader::FValue& DefaultValue);

	/** Returns a declaration to access the given texture, with no parameter */
	UE::HLSLTree::FTextureParameterDeclaration* AcquireTextureDeclaration(UE::HLSLTree::FScope& Scope, const UE::HLSLTree::FTextureDescription& Value);

	/** Returns a declaration to access the given texture parameter */
	UE::HLSLTree::FTextureParameterDeclaration* AcquireTextureParameterDeclaration(UE::HLSLTree::FScope& Scope, const FName& Name, const UE::HLSLTree::FTextureDescription& DefaultValue);

	UE::HLSLTree::FFunctionCall* AcquireFunctionCall(UE::HLSLTree::FScope& Scope, UMaterialFunctionInterface* Function, TArrayView<UE::HLSLTree::FExpression*> Inputs);

	/**
	 * Returns the appropriate HLSLNode representing the given UMaterialExpression.
	 * The node will be created if it doesn't exist. Otherwise, the tree will be updated to ensure the node is visible in the given scope
	 * Note that a given UMaterialExpression may only support 1 of these node types, attempting to access an invalid node type will generate an error
	 */
	UE::HLSLTree::FExpression* AcquireExpression(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex);
	UE::HLSLTree::FTextureParameterDeclaration* AcquireTextureDeclaration(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex);
	UE::HLSLTree::FStatement* AcquireStatement(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression);

private:
	struct FExpressionKey
	{
		explicit FExpressionKey(UMaterialExpression* InExpression, int32 InOutputIndex = INDEX_NONE) : Expression(InExpression), OutputIndex(InOutputIndex) {}

		UMaterialExpression* Expression;
		int32 OutputIndex;

		friend inline uint32 GetTypeHash(const FExpressionKey& Value)
		{
			return HashCombine(GetTypeHash(Value.Expression), GetTypeHash(Value.OutputIndex));
		}

		friend inline bool operator==(const FExpressionKey& Lhs, const FExpressionKey& Rhs)
		{
			return Lhs.Expression == Rhs.Expression && Lhs.OutputIndex == Rhs.OutputIndex;
		}
	};

	struct FFunctionCallKey
	{
		UMaterialFunctionInterface* Function;
		FSHAHash InputHash;

		friend inline uint32 GetTypeHash(const FFunctionCallKey& Value)
		{
			return HashCombine(GetTypeHash(Value.Function), GetTypeHash(Value.InputHash));
		}

		friend inline bool operator==(const FFunctionCallKey& Lhs, const FFunctionCallKey& Rhs)
		{
			return Lhs.Function == Rhs.Function && Lhs.InputHash == Rhs.InputHash;
		}
	};

	const FMaterialCompileTargetParameters& CompileTarget;
	UMaterial* TargetMaterial;
	UMaterialFunctionInterface* TargetMaterialFunction;

	UE::HLSLTree::FTree* HLSLTree;
	TArray<FExpressionKey> ExpressionStack;
	TArray<FString> CompileErrors;
	TArray<UMaterialExpression*> ErrorExpressions;
	TMap<FName, UE::HLSLTree::FLocalDeclaration*> LocalDeclarationMap;
	TMap<FName, UE::HLSLTree::FParameterDeclaration*> ParameterDeclarationMap;
	TMap<UE::HLSLTree::FTextureDescription, UE::HLSLTree::FTextureParameterDeclaration*> TextureDeclarationMap;
	TMap<FName, UE::HLSLTree::FTextureParameterDeclaration*> TextureParameterDeclarationMap;
	TMap<FFunctionCallKey, UE::HLSLTree::FFunctionCall*> FunctionCallMap;
	TMap<FExpressionKey, UE::HLSLTree::FExpression*> ExpressionMap;
	TMap<UMaterialExpression*, UE::HLSLTree::FStatement*> StatementMap;
	bool bGeneratedResult;
};

#endif // WITH_EDITOR
