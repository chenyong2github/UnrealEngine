// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "Hash/xxhash.h"
#include "HLSLTree/HLSLTreeTypes.h"

class FMaterial;
class FMaterialCompilationOutput;
struct FStaticParameterSet;

namespace UE
{
namespace HLSLTree
{

class FErrorHandlerInterface;
class FNode;
class FScope;
class FExpression;
class FFunction;
class FRequestedType;
class FEmitShaderScope;
class FEmitShaderExpression;
class FEmitShaderStatement;

struct FEmitShaderScopeEntry
{
	FEmitShaderScopeEntry() = default;
	FEmitShaderScopeEntry(FEmitShaderScope* InScope, int32 InIndent, FStringBuilderBase& InCode) : Scope(InScope), Code(&InCode), Indent(InIndent) {}

	FEmitShaderScope* Scope = nullptr;
	FStringBuilderBase* Code = nullptr;
	int32 Indent = 0;
};
using FEmitShaderScopeStack = TArray<FEmitShaderScopeEntry, TInlineAllocator<16>>;

class FEmitShaderNode
{
public:
	virtual void EmitShaderCode(FEmitShaderScopeStack& Stack, int32 Indent, FStringBuilderBase& OutString) = 0;
	virtual FEmitShaderExpression* AsExpression() { return nullptr; }
	virtual FEmitShaderStatement* AsStatement() { return nullptr; }

	FEmitShaderNode(FEmitShaderScope& InScope, TArrayView<FEmitShaderNode*> InDependencies);

	FEmitShaderScope* Scope = nullptr;
	FEmitShaderNode* NextScopedNode = nullptr;
	TArrayView<FEmitShaderNode*> Dependencies;
};
using FEmitShaderDependencies = TArray<FEmitShaderNode*, TInlineAllocator<8>>;

class FEmitShaderExpression final : public FEmitShaderNode
{
public:
	FEmitShaderExpression(FEmitShaderScope& InScope, TArrayView<FEmitShaderNode*> InDependencies, const Shader::FType& InType, FXxHash64 InHash)
		: FEmitShaderNode(InScope, InDependencies)
		, Type(InType)
		, Hash(InHash)
	{}

	virtual void EmitShaderCode(FEmitShaderScopeStack& Stack, int32 Indent, FStringBuilderBase& OutString) override;
	virtual FEmitShaderExpression* AsExpression() override { return this; }
	inline bool IsInline() const { return Value == nullptr; }

	const TCHAR* Reference = nullptr;
	const TCHAR* Value = nullptr;
	Shader::FType Type;
	FXxHash64 Hash;
};

enum class EEmitScopeFormat : uint8
{
	None,
	Unscoped,
	Scoped,
};

class FEmitShaderStatement final : public FEmitShaderNode
{
public:
	FEmitShaderStatement(FEmitShaderScope& InScope, TArrayView<FEmitShaderNode*> InDependencies)
		: FEmitShaderNode(InScope, InDependencies)
	{}

	virtual void EmitShaderCode(FEmitShaderScopeStack& Stack, int32 Indent, FStringBuilderBase& OutString) override;
	virtual FEmitShaderStatement* AsStatement() override { return this; }

	FEmitShaderScope* NestedScopes[2] = { nullptr };
	FStringView Code[2];
	EEmitScopeFormat ScopeFormat;
	int8 NumEntries = 0;
};

enum class EFormatArgType : uint8
{
	Void,
	ShaderValue,
	String,
	Int,
};

struct FFormatArgVariant
{
	FFormatArgVariant() {}
	FFormatArgVariant(FEmitShaderExpression* InValue) : Type(EFormatArgType::ShaderValue), ShaderValue(InValue) { check(InValue); }
	FFormatArgVariant(const TCHAR* InValue) : Type(EFormatArgType::String), String(InValue) { check(InValue); }
	FFormatArgVariant(int32 InValue) : Type(EFormatArgType::Int), Int(InValue) {}

	EFormatArgType Type = EFormatArgType::Void;
	union
	{
		FEmitShaderExpression* ShaderValue;
		const TCHAR* String;
		int32 Int;
	};
};

using FFormatArgList = TArray<FFormatArgVariant, TInlineAllocator<8>>;

namespace Private
{
inline void BuildFormatArgList(FFormatArgList&) {}

template<typename Type, typename... Types>
inline void BuildFormatArgList(FFormatArgList& OutList, Type Arg, Types... Args)
{
	OutList.Add(Arg);
	BuildFormatArgList(OutList, Forward<Types>(Args)...);
}

void InternalFormatStrings(FStringBuilderBase* OutString0,
	FStringBuilderBase* OutString1,
	FEmitShaderDependencies& OutDependencies,
	FStringView Format0,
	FStringView Format1,
	const FFormatArgList& ArgList);
} // namespace Private

template<typename FormatType, typename... Types>
void FormatString(FStringBuilderBase& OutString, FEmitShaderDependencies& OutDependencies, const FormatType& Format, Types... Args)
{
	FFormatArgList ArgList;
	Private::BuildFormatArgList(ArgList, Forward<Types>(Args)...);
	Private::InternalFormatStrings(&OutString, nullptr, OutDependencies, Format, FStringView(), ArgList);
}

template<typename FormatType0, typename FormatType1, typename... Types>
void FormatStrings(FStringBuilderBase& OutString0, FStringBuilderBase& OutString1, FEmitShaderDependencies& OutDependencies, const FormatType0& Format0, const FormatType1& Format1, Types... Args)
{
	FFormatArgList ArgList;
	Private::BuildFormatArgList(ArgList, Forward<Types>(Args)...);
	Private::InternalFormatStrings(&OutString0, &OutString1, OutDependencies, Format0, Format1, ArgList);
}

class FEmitShaderScope
{
public:
	static FEmitShaderScope* FindSharedParent(FEmitShaderScope* Lhs, FEmitShaderScope* Rhs);

	void EmitShaderCode(FEmitShaderScopeStack& Stack);

	FEmitShaderScope* ParentScope = nullptr;
	FEmitShaderNode* FirstNode;
	int32 NestedLevel = 0;
};

/** Tracks shared state while emitting HLSL code */
class FEmitContext
{
public:
	explicit FEmitContext(FMemStackBase& InAllocator, FErrorHandlerInterface& InErrors, const Shader::FStructTypeRegistry& InTypeRegistry);
	~FEmitContext();

	template<typename T>
	static TArrayView<FEmitShaderNode*> MakeDependencies(T*& Dependency)
	{
		return Dependency ? TArrayView<FEmitShaderNode*>(&Dependency, 1) : TArrayView<FEmitShaderNode*>();
	}

	void Finalize();

	/** Get a unique local variable name */
	const TCHAR* AcquireLocalDeclarationCode();

	FEmitShaderExpression* EmitExpressionInternal(FEmitShaderScope& Scope, TArrayView<FEmitShaderNode*> Dependencies, bool bInline, const Shader::FType& Type, FStringView Code);

	template<typename FormatType, typename... Types>
	FEmitShaderExpression* EmitExpressionWithDependencies(FEmitShaderScope& Scope, TArrayView<FEmitShaderNode*> Dependencies, const Shader::FType& Type, const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		FEmitShaderDependencies LocalDependencies(Dependencies);
		FormatString(String, LocalDependencies, Format, Forward<Types>(Args)...);
		return EmitExpressionInternal(Scope, LocalDependencies, false, Type, String.ToView());
	}

	template<typename FormatType, typename... Types>
	FEmitShaderExpression* EmitInlineExpressionWithDependencies(FEmitShaderScope& Scope, TArrayView<FEmitShaderNode*> Dependencies, const Shader::FType& Type, const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		FEmitShaderDependencies LocalDependencies(Dependencies);
		FormatString(String, LocalDependencies, Format, Forward<Types>(Args)...);
		return EmitExpressionInternal(Scope, LocalDependencies, true, Type, String.ToView());
	}

	template<typename FormatType, typename... Types>
	FEmitShaderExpression* EmitInlineExpressionWithDependency(FEmitShaderScope& Scope, FEmitShaderNode* Dependency, const Shader::FType& Type, const FormatType& Format, Types... Args)
	{
		return EmitInlineExpressionWithDependencies(Scope, MakeDependencies(Dependency), Type, Format, Forward<Types>(Args)...);
	}

	template<typename FormatType, typename... Types>
	FEmitShaderExpression* EmitExpression(FEmitShaderScope& Scope, const Shader::FType& Type, const FormatType& Format, Types... Args)
	{
		return EmitExpressionWithDependencies(Scope, TArrayView<FEmitShaderNode*>(), Type, Format, Forward<Types>(Args)...);
	}

	template<typename FormatType, typename... Types>
	FEmitShaderExpression* EmitInlineExpression(FEmitShaderScope& Scope, const Shader::FType& Type, const FormatType& Format, Types... Args)
	{
		return EmitInlineExpressionWithDependencies(Scope, TArrayView<FEmitShaderNode*>(), Type, Format, Forward<Types>(Args)...);
	}

	FEmitShaderStatement* EmitStatementInternal(FEmitShaderScope& Scope, TArrayView<FEmitShaderNode*> Dependencies, EEmitScopeFormat ScopeFormat, FEmitShaderScope* NestedScope0, FEmitShaderScope* NestedScope1, FStringView Code0, FStringView Code1);

	template<typename FormatType0, typename FormatType1, typename... Types>
	FEmitShaderStatement* EmitFormatStatementInternal(FEmitShaderScope& Scope,
		TArrayView<FEmitShaderNode*> Dependencies,
		EEmitScopeFormat ScopeFormat,
		FEmitShaderScope* NestedScope0,
		FEmitShaderScope* NestedScope1,
		const FormatType0& Format0,
		const FormatType1& Format1,
		Types... Args)
	{
		TStringBuilder<1024> String0;
		TStringBuilder<1024> String1;
		FEmitShaderDependencies LocalDependencies(Dependencies);
		FormatStrings(String0, String1, LocalDependencies, Format0, Format1, Forward<Types>(Args)...);
		return EmitStatementInternal(Scope, LocalDependencies, ScopeFormat, NestedScope0, NestedScope1, String0.ToView(), String1.ToView());
	}

	template<typename FormatType, typename... Types>
	FEmitShaderStatement* EmitStatementWithDependencies(FEmitShaderScope& Scope, TArrayView<FEmitShaderNode*> Dependencies, const FormatType& Format, Types... Args)
	{
		return EmitFormatStatementInternal(Scope, Dependencies, EEmitScopeFormat::None, nullptr, nullptr, Format, FStringView(), Forward<Types>(Args)...);
	}

	template<typename FormatType, typename... Types>
	FEmitShaderStatement* EmitStatementWithDependency(FEmitShaderScope& Scope, FEmitShaderNode* Dependency, const FormatType& Format, Types... Args)
	{
		return EmitStatementWithDependencies(Scope, MakeDependencies(Dependency), Format, Forward<Types>(Args)...);
	}

	template<typename FormatType, typename... Types>
	FEmitShaderStatement* EmitStatement(FEmitShaderScope& Scope, const FormatType& Format, Types... Args)
	{
		return EmitStatementWithDependencies(Scope, TArrayView<FEmitShaderNode*>(), Format, Forward<Types>(Args)...);
	}

	FEmitShaderStatement* EmitNextScopeWithDependency(FEmitShaderScope& Scope, FEmitShaderNode* Dependency, FScope* NextScope)
	{
		FEmitShaderScope* EmitScope = AcquireEmitScope(NextScope, &Scope);
		return EmitStatementInternal(Scope, MakeDependencies(Dependency), EEmitScopeFormat::Unscoped, EmitScope, nullptr, FStringView(), FStringView());
	}

	FEmitShaderStatement* EmitNextScope(FEmitShaderScope& Scope, FScope* NextScope)
	{
		return EmitNextScopeWithDependency(Scope, nullptr, NextScope);
	}

	template<typename FormatType, typename... Types>
	FEmitShaderStatement* EmitNestedScope(FEmitShaderScope& Scope, FScope* NestedScope, const FormatType& Format, Types... Args)
	{
		FEmitShaderScope* EmitScope = AcquireEmitScope(NestedScope, &Scope);
		return EmitFormatStatementInternal(Scope, TArrayView<FEmitShaderNode*>(), EEmitScopeFormat::Scoped, EmitScope, nullptr, Format, FStringView(), Forward<Types>(Args)...);
	}

	template<typename FormatType0, typename FormatType1, typename... Types>
	FEmitShaderStatement* EmitNestedScopes(FEmitShaderScope& Scope, FScope* NestedScope0, FScope* NestedScope1, const FormatType0& Format0, const FormatType1& Format1, Types... Args)
	{
		FEmitShaderScope* EmitScope0 = AcquireEmitScope(NestedScope0, &Scope);
		FEmitShaderScope* EmitScope1 = AcquireEmitScope(NestedScope1, &Scope);
		return EmitFormatStatementInternal(Scope, TArrayView<FEmitShaderNode*>(), EEmitScopeFormat::Scoped, EmitScope0, EmitScope1, Format0, Format1, Forward<Types>(Args)...);
	}

	FEmitShaderScope* AcquireEmitScope(FScope* Scope, FEmitShaderScope* OverrideParent = nullptr);
	FEmitShaderScope* FindEmitScope(FScope* Scope) const;

	FEmitShaderExpression* EmitPreshaderOrConstant(FEmitShaderScope& Scope, const FRequestedType& RequestedType, FExpression* Expression);
	FEmitShaderExpression* EmitConstantZero(FEmitShaderScope& Scope, const Shader::FType& Type);
	FEmitShaderExpression* EmitCast(FEmitShaderScope& Scope, FEmitShaderExpression* ShaderValue, const Shader::FType& DestType);

	FMemStackBase* Allocator = nullptr;
	FErrorHandlerInterface* Errors = nullptr;
	const Shader::FStructTypeRegistry* TypeRegistry = nullptr;

	TArray<FEmitShaderNode*> EmitNodes;
	TMap<const FScope*, FEmitShaderScope*> EmitScopeMap;
	TMap<const FExpression*, FEmitShaderExpression*> EmitLocalPHIMap;
	TMap<FXxHash64, FEmitShaderExpression*> EmitExpressionMap;
	TMap<FXxHash64, FEmitShaderExpression*> EmitPreshaderMap;
	TMap<const FFunction*, FEmitShaderNode*> EmitFunctionMap;

	// TODO - remove preshader material dependency
	const FMaterial* Material = nullptr;
	const FStaticParameterSet* StaticParameters = nullptr;
	FMaterialCompilationOutput* MaterialCompilationOutput = nullptr;
	TMap<Shader::FValue, uint32> DefaultUniformValues;
	uint32 UniformPreshaderOffset = 0u;
	bool bReadMaterialNormal = false;

	int32 NumExpressionLocals = 0;
	int32 NumExpressionLocalPHIs = 0;
	int32 NumTexCoords = 0;
};

} // namespace HLSLTree
} // namespace UE
