// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Containers/Map.h"
#include "Misc/MemStack.h"
#include "Misc/EnumClassFlags.h"
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
class FExpressionLocalPHI;
}
}

enum class EMaterialNewScopeFlag : uint8
{
	None = 0u,
	NoPreviousScope = (1u << 0),
};
ENUM_CLASS_FLAGS(EMaterialNewScopeFlag);

template<typename T>
struct TMaterialHLSLGeneratorType;

#define DECLARE_MATERIAL_HLSLGENERATOR_DATA(T) \
	template<> struct TMaterialHLSLGeneratorType<T> { static const FName& GetTypeName() { static const FName Name(TEXT(#T)); return Name; } }

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

	bool Finalize();

	/** Retrieve the compile errors from the generator */
	void AcquireErrors(TArray<FString>& OutCompileErrors, TArray<UMaterialExpression*>& OutErrorExpressions);

	EMaterialGenerateHLSLStatus Error(const FString& Message);

	template <typename FormatType, typename... ArgTypes>
	inline EMaterialGenerateHLSLStatus Errorf(const FormatType& Format, ArgTypes... Args)
	{
		return Error(FString::Printf(Format, Args...));
	}

	UE::HLSLTree::FTree& GetTree() const { return *HLSLTree; }

	bool GenerateResult(UE::HLSLTree::FScope& Scope);

	UE::HLSLTree::FScope* NewScope(UE::HLSLTree::FScope& Scope, EMaterialNewScopeFlag Flags = EMaterialNewScopeFlag::None);

	UE::HLSLTree::FScope* NewJoinedScope(UE::HLSLTree::FScope& Scope);

	UE::HLSLTree::FExpressionConstant* NewConstant(const UE::Shader::FValue& Value);
	UE::HLSLTree::FExpressionExternalInput* NewTexCoord(UE::HLSLTree::FScope& Scope, int32 Index);
	UE::HLSLTree::FExpressionSwizzle* NewSwizzle(UE::HLSLTree::FScope& Scope, const UE::HLSLTree::FSwizzleParameters& Params, UE::HLSLTree::FExpression* Input);
	UE::HLSLTree::FExpression* NewCast(UE::HLSLTree::FScope& Scope, UE::Shader::EValueType Type, UE::HLSLTree::FExpression* Input, UE::HLSLTree::ECastFlags Flags = UE::HLSLTree::ECastFlags::None);

	UE::HLSLTree::FExpression* NewFunctionInput(UE::HLSLTree::FScope& Scope, int32 InputIndex, UMaterialExpressionFunctionInput* MaterialFunctionInput);

	/** Returns a declaration to access the given texture, with no parameter */
	UE::HLSLTree::FTextureParameterDeclaration* AcquireTextureDeclaration(const UE::HLSLTree::FTextureDescription& Value);

	/** Returns a declaration to access the given texture parameter */
	UE::HLSLTree::FTextureParameterDeclaration* AcquireTextureParameterDeclaration(const FName& Name, const UE::HLSLTree::FTextureDescription& DefaultValue);

	UE::HLSLTree::FFunctionCall* AcquireFunctionCall(UE::HLSLTree::FScope& Scope, UMaterialFunctionInterface* Function, TArrayView<UE::HLSLTree::FExpression*> Inputs);

	bool GenerateAssignLocal(UE::HLSLTree::FScope& Scope, const FName& LocalName, UE::HLSLTree::FExpression* Value);
	UE::HLSLTree::FExpression* AcquireLocalValue(UE::HLSLTree::FScope& Scope, const FName& LocalName);

	/**
	 * Returns the appropriate HLSLNode representing the given UMaterialExpression.
	 * The node will be created if it doesn't exist. Otherwise, the tree will be updated to ensure the node is visible in the given scope
	 * Note that a given UMaterialExpression may only support 1 of these node types, attempting to access an invalid node type will generate an error
	 */
	UE::HLSLTree::FExpression* AcquireExpression(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex);
	UE::HLSLTree::FTextureParameterDeclaration* AcquireTextureDeclaration(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex);
	bool GenerateStatements(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression);

	template<typename T, typename... ArgTypes>
	T* NewExpressionData(const UMaterialExpression* MaterialExpression, ArgTypes... Args)
	{
		T* Data = new T(Forward<ArgTypes>(Args)...);
		InternalRegisterExpressionData(TMaterialHLSLGeneratorType<T>::GetTypeName(), MaterialExpression, Data);
		return Data;
	}

	template<typename T>
	T* FindExpressionData(const UMaterialExpression* MaterialExpression)
	{
		return static_cast<T*>(InternalFindExpressionData(TMaterialHLSLGeneratorType<T>::GetTypeName(), MaterialExpression));
	}

	template<typename T>
	T* AcquireGlobalData()
	{
		T* Data = static_cast<T*>(InternalFindExpressionData(TMaterialHLSLGeneratorType<T>::GetTypeName(), nullptr));
		if (!Data)
		{
			Data = new T();
			InternalRegisterExpressionData(TMaterialHLSLGeneratorType<T>::GetTypeName(), nullptr, Data);
		}
		return Data;
	}

private:
	static constexpr int32 MaxNumPreviousScopes = UE::HLSLTree::MaxNumPreviousScopes;
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

	struct FLocalKey
	{
		FLocalKey(UE::HLSLTree::FScope* InScope, const FName& InName) : Scope(InScope), Name(InName) {}

		UE::HLSLTree::FScope* Scope;
		FName Name;

		friend inline uint32 GetTypeHash(const FLocalKey& Value)
		{
			return HashCombine(GetTypeHash(Value.Scope), GetTypeHash(Value.Name));
		}

		friend inline bool operator==(const FLocalKey& Lhs, const FLocalKey& Rhs)
		{
			return Lhs.Scope == Rhs.Scope && Lhs.Name == Rhs.Name;
		}
	};

	struct FExpressionDataKey
	{
		FExpressionDataKey(const FName& InTypeName, const UMaterialExpression* InMaterialExpression) : MaterialExpression(InMaterialExpression), TypeName(InTypeName) {}

		const UMaterialExpression* MaterialExpression;
		FName TypeName;

		friend inline uint32 GetTypeHash(const FExpressionDataKey& Value)
		{
			return HashCombine(GetTypeHash(Value.MaterialExpression), GetTypeHash(Value.TypeName));
		}

		friend inline bool operator==(const FExpressionDataKey& Lhs, const FExpressionDataKey& Rhs)
		{
			return Lhs.MaterialExpression == Rhs.MaterialExpression && Lhs.TypeName == Rhs.TypeName;
		}
	};

	struct FStatementEntry
	{
		UE::HLSLTree::FScope* PreviousScope[MaxNumPreviousScopes];
		int32 NumInputs = 0;
	};

	UE::HLSLTree::FExpression* InternalAcquireLocalValue(UE::HLSLTree::FScope& Scope, const FName& LocalName);

	void InternalRegisterExpressionData(const FName& Type, const UMaterialExpression* MaterialExpression, void* Data);
	void* InternalFindExpressionData(const FName& Type, const UMaterialExpression* MaterialExpression);

	const FMaterialCompileTargetParameters& CompileTarget;
	UMaterial* TargetMaterial;
	UMaterialFunctionInterface* TargetMaterialFunction;

	UE::HLSLTree::FTree* HLSLTree;
	TArray<FExpressionKey> ExpressionStack;
	TArray<UE::HLSLTree::FScope*> JoinedScopeStack;
	TArray<UE::HLSLTree::FExpressionLocalPHI*> PHIExpressions;
	TArray<FString> CompileErrors;
	TArray<UMaterialExpression*> ErrorExpressions;
	TMap<UE::HLSLTree::FTextureDescription, UE::HLSLTree::FTextureParameterDeclaration*> TextureDeclarationMap;
	TMap<FName, UE::HLSLTree::FTextureParameterDeclaration*> TextureParameterDeclarationMap;
	TMap<FFunctionCallKey, UE::HLSLTree::FFunctionCall*> FunctionCallMap;
	TMap<FLocalKey, UE::HLSLTree::FExpression*> LocalMap;
	TMap<FExpressionKey, UE::HLSLTree::FExpression*> ExpressionMap;
	TMap<UMaterialExpression*, FStatementEntry> StatementMap;
	TMap<FExpressionDataKey, void*> ExpressionDataMap;
	bool bGeneratedResult;
};

#endif // WITH_EDITOR
