// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Containers/Map.h"
#include "Misc/MemStack.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"
#include "RHIDefinitions.h"
#include "Materials/MaterialLayersFunctions.h"
#include "HLSLTree/HLSLTree.h"

class FMaterial;
class FMaterialCachedHLSLTree;
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

namespace UE::HLSLTree
{
struct FSwizzleParameters;
}
namespace UE::HLSLTree::Material
{
enum class EExternalInput : uint8;
}
namespace UE::Shader
{
struct FStructType;
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
	FMaterialHLSLGenerator(UMaterial* Material,
		const FMaterialLayersFunctions* InLayerOverrides,
		UMaterialExpression* InPreviewExpression,
		FMaterialCachedHLSLTree& OutCachedTree);

	const FMaterialLayersFunctions* GetLayerOverrides() const { return LayerOverrides; }

	UE::HLSLTree::FTree& GetTree() const;
	UE::Shader::FStructTypeRegistry& GetTypeRegistry() const;
	const UE::Shader::FStructType* GetMaterialAttributesType() const;
	const UE::Shader::FValue& GetMaterialAttributesDefaultValue() const;

	template<typename StringType>
	inline bool Error(const StringType& InError)
	{
		return InternalError(FStringView(InError));
	}

	template<typename FormatType, typename... Types>
	inline bool Errorf(const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		String.Appendf(Format, Forward<Types>(Args)...);
		return InternalError(FStringView(String.ToString(), String.Len()));
	}
	
	bool Generate();

	bool GenerateResult(UE::HLSLTree::FScope& Scope);

	UE::HLSLTree::FScope* NewScope(UE::HLSLTree::FScope& Scope, EMaterialNewScopeFlag Flags = EMaterialNewScopeFlag::None);
	UE::HLSLTree::FScope* NewOwnedScope(UE::HLSLTree::FStatement& Owner);

	UE::HLSLTree::FScope* NewJoinedScope(UE::HLSLTree::FScope& Scope);

	const UE::HLSLTree::FExpression* NewConstant(const UE::Shader::FValue& Value);
	const UE::HLSLTree::FExpression* NewTexCoord(int32 Index);
	const UE::HLSLTree::FExpression* NewExternalInput(UE::HLSLTree::Material::EExternalInput Input);
	const UE::HLSLTree::FExpression* NewSwizzle(const UE::HLSLTree::FSwizzleParameters& Params, const UE::HLSLTree::FExpression* Input);

	const UE::Shader::FTextureValue* AcquireTextureValue(const UE::Shader::FTextureValue& Value);

	/**
	 * Returns the appropriate HLSLNode representing the given UMaterialExpression.
	 * The node will be created if it doesn't exist. Otherwise, the tree will be updated to ensure the node is visible in the given scope
	 * Note that a given UMaterialExpression may only support 1 of these node types, attempting to access an invalid node type will generate an error
	 */
	const UE::HLSLTree::FExpression* AcquireExpression(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex);
	const UE::HLSLTree::FExpression* AcquireFunctionInputExpression(UE::HLSLTree::FScope& Scope, const UMaterialExpressionFunctionInput* MaterialExpression);

	bool GenerateStatements(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression);

	const UE::HLSLTree::FExpression* GenerateMaterialParameter(FName InParameterName, const FMaterialParameterMetadata& InParameterMeta, const UE::Shader::FValue& InDefaultValue);

	const UE::HLSLTree::FExpression* GenerateFunctionCall(UE::HLSLTree::FScope& Scope,
		UMaterialFunctionInterface* Function,
		EMaterialParameterAssociation ParameterAssociation,
		int32 ParameterIndex,
		TArrayView<const UE::HLSLTree::FExpression*> ConnectedInputs,
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
	using FConnectedInputArray = TArray<const UE::HLSLTree::FExpression*, TInlineAllocator<4>>;

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

	bool InternalError(FStringView ErrorMessage);

	void InternalRegisterExpressionData(const FName& Type, const UMaterialExpression* MaterialExpression, void* Data);
	void* InternalFindExpressionData(const FName& Type, const UMaterialExpression* MaterialExpression);

	UMaterial* TargetMaterial;
	const FMaterialLayersFunctions* LayerOverrides;
	UMaterialExpression* PreviewExpression;
	FMaterialCachedHLSLTree& CachedTree;
	FString CurrentErrorMessage;

	FFunctionCallEntry RootFunctionCallEntry;
	TArray<FFunctionCallEntry*, TInlineAllocator<8>> FunctionCallStack;
	TArray<UE::HLSLTree::FScope*> JoinedScopeStack;
	TMap<FXxHash64, const UE::Shader::FTextureValue*> TextureValueMap;
	TMap<FXxHash64, TUniquePtr<FFunctionCallEntry>> FunctionCallMap;
	TMap<UMaterialFunctionInterface*, UE::HLSLTree::FFunction*> FunctionMap;
	TMap<UMaterialExpression*, FStatementEntry> StatementMap;
	TMap<FExpressionDataKey, void*> ExpressionDataMap;
	const UE::HLSLTree::FExpression* PreviewExpressionResult = nullptr;
	bool bGeneratedResult;
};

#endif // WITH_EDITOR
