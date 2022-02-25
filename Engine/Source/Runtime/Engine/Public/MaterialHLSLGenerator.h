// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Containers/Map.h"
#include "Misc/MemStack.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/RefCounting.h"
#include "RHIDefinitions.h"
#include "Materials/MaterialLayersFunctions.h"
#include "HLSLTree/HLSLTree.h"

class FMaterial;
class FMaterialCompilationOutput;
struct FSharedShaderCompilerEnvironment;
struct FMaterialCompileTargetParameters;
struct FMaterialParameterMetadata;
class UMaterial;
class UMaterialFunctionInterface;
class UMaterialExpression;
class UMaterialExpressionFunctionInput;
class UMaterialExpressionFunctionOutput;
class UMaterialExpressionCustomOutput;
struct FFunctionExpressionInput;
class ITargetPlatform;
enum class EMaterialParameterType : uint8;

namespace UE
{
namespace HLSLTree
{
struct FSwizzleParameters;
}
namespace Shader
{
struct FStructType;
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

class FMaterialHLSLErrorHandler : public UE::HLSLTree::FErrorHandlerInterface
{
public:
	explicit FMaterialHLSLErrorHandler(FMaterial& InOutMaterial);

	virtual void AddErrorInternal(UObject* InOwner, FStringView InError) override;

private:
	FMaterial* Material;
};

/**
 * MaterialHLSLGenerator is a bridge between a material, and HLSLTree.  It facilitates generating HLSL source code for a given material, using HLSLTree
 */
class FMaterialHLSLGenerator
{
public:
	FMaterialHLSLGenerator(const FMaterialCompileTargetParameters& InCompilerTarget,
		const FStaticParameterSet& InStaticParameters,
		FMaterial& InOutMaterial,
		UE::Shader::FStructTypeRegistry& InOutTypeRegistry,
		UE::HLSLTree::FTree& InOutTree);

	const FMaterialCompileTargetParameters& GetCompileTarget() const { return CompileTarget; }
	const FStaticParameterSet& GetStaticParameters() const { return StaticParameters; }
	
	bool Generate();

	UE::HLSLTree::FTree& GetTree() const { return *HLSLTree; }
	UE::Shader::FStructTypeRegistry& GetTypeRegistry() const { return *TypeRegistry; }
	FMaterialHLSLErrorHandler& GetErrors() { return Errors; }

	UE::HLSLTree::FExpression* GetResultExpression() { return ResultExpression; }
	UE::HLSLTree::FStatement* GetResultStatement() { return ResultStatement; }

	void SetRequestedFields(EShaderFrequency ShaderFrequency, UE::HLSLTree::FRequestedType& OutRequestedType);

	void EmitSharedCode(FStringBuilderBase& OutCode) const;

	bool GenerateResult(UE::HLSLTree::FScope& Scope);

	UE::HLSLTree::FScope* NewScope(UE::HLSLTree::FScope& Scope, EMaterialNewScopeFlag Flags = EMaterialNewScopeFlag::None);
	UE::HLSLTree::FScope* NewOwnedScope(UE::HLSLTree::FStatement& Owner);

	UE::HLSLTree::FScope* NewJoinedScope(UE::HLSLTree::FScope& Scope);

	UE::HLSLTree::FExpression* NewConstant(const UE::Shader::FValue& Value);
	UE::HLSLTree::FExpression* NewTexCoord(int32 Index);
	UE::HLSLTree::FExpression* NewSwizzle(const UE::HLSLTree::FSwizzleParameters& Params, UE::HLSLTree::FExpression* Input);

	const UE::Shader::FTextureValue* AcquireTextureValue(const UE::Shader::FTextureValue& Value);

	/**
	 * Returns the appropriate HLSLNode representing the given UMaterialExpression.
	 * The node will be created if it doesn't exist. Otherwise, the tree will be updated to ensure the node is visible in the given scope
	 * Note that a given UMaterialExpression may only support 1 of these node types, attempting to access an invalid node type will generate an error
	 */
	UE::HLSLTree::FExpression* AcquireExpression(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex);
	UE::HLSLTree::FExpression* AcquireFunctionInputExpression(UE::HLSLTree::FScope& Scope, const UMaterialExpressionFunctionInput* MaterialExpression);

	bool GenerateStatements(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression);

	UE::HLSLTree::FExpression* GenerateMaterialParameter(EMaterialParameterType InType, FName InParameterName, const UE::Shader::FValue& InDefaultValue);

	UE::HLSLTree::FExpression* GenerateFunctionCall(UE::HLSLTree::FScope& Scope,
		UMaterialFunctionInterface* Function,
		EMaterialParameterAssociation ParameterAssociation,
		int32 ParameterIndex,
		TArrayView<UE::HLSLTree::FExpression*> ConnectedInputs,
		int32 OutputIndex);

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

	const UE::Shader::FStructType* GetMaterialAttributesType() const { return MaterialAttributesType; }
	const UE::Shader::FValue& GetMaterialAttributesDefaultValue() const { return MaterialAttributesDefaultValue; }

	bool GetParameterOverrideValueForCurrentFunction(EMaterialParameterType ParameterType, FName ParameterName, FMaterialParameterMetadata& OutResult) const;
	FMaterialParameterInfo GetParameterInfo(const FName& ParameterName) const;

private:
	static constexpr int32 MaxNumPreviousScopes = UE::HLSLTree::MaxNumPreviousScopes;
	
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

	using FFunctionInputArray = TArray<const UMaterialExpressionFunctionInput*, TInlineAllocator<4>>;
	using FFunctionOutputArray = TArray<const UMaterialExpressionFunctionOutput*, TInlineAllocator<4>>;
	using FConnectedInputArray = TArray<UE::HLSLTree::FExpression*, TInlineAllocator<4>>;

	struct FFunctionCallEntry
	{
		UMaterialFunctionInterface* MaterialFunction = nullptr;
		UE::HLSLTree::FFunction* HLSLFunction = nullptr;
		FFunctionInputArray FunctionInputs;
		FFunctionOutputArray FunctionOutputs;
		FConnectedInputArray ConnectedInputs;
		EMaterialParameterAssociation ParameterAssociation = GlobalParameter;
		int32 ParameterIndex = INDEX_NONE;
		bool bGeneratedResult = false;
	};

	struct FStatementEntry
	{
		UE::HLSLTree::FScope* PreviousScope[MaxNumPreviousScopes];
		int32 NumInputs = 0;
	};

	void InternalRegisterExpressionData(const FName& Type, const UMaterialExpression* MaterialExpression, void* Data);
	void* InternalFindExpressionData(const FName& Type, const UMaterialExpression* MaterialExpression);

	const FMaterialCompileTargetParameters& CompileTarget;
	const FStaticParameterSet& StaticParameters;
	UMaterial* TargetMaterial = nullptr;
	FMaterialHLSLErrorHandler Errors;

	TArray<UMaterialExpressionCustomOutput*> MaterialCustomOutputs;
	const UE::Shader::FStructType* MaterialAttributesType = nullptr;
	UE::Shader::FValue MaterialAttributesDefaultValue;

	UE::HLSLTree::FTree* HLSLTree;
	UE::Shader::FStructTypeRegistry* TypeRegistry = nullptr;
	UE::HLSLTree::FExpression* ResultExpression = nullptr;
	UE::HLSLTree::FStatement* ResultStatement = nullptr;

	TArray<FFunctionCallEntry*, TInlineAllocator<8>> FunctionCallStack;
	TArray<UE::HLSLTree::FScope*> JoinedScopeStack;
	TMap<FXxHash64, const UE::Shader::FTextureValue*> TextureValueMap;
	TMap<FSHAHash, FFunctionCallEntry*> FunctionCallMap;
	TMap<UMaterialFunctionInterface*, UE::HLSLTree::FFunction*> FunctionMap;
	TMap<UMaterialExpression*, FStatementEntry> StatementMap;
	TMap<FExpressionDataKey, void*> ExpressionDataMap;
	bool bGeneratedResult;
};

#endif // WITH_EDITOR
