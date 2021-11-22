// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
#include "Containers/BitArray.h"
#include "Containers/List.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "HLSLTree/HLSLTreeTypes.h"

class FMaterial;
class FMaterialCompilationOutput;
struct FStaticParameterSet;

namespace UE
{

namespace Shader
{
class FPreshaderData;
}

/**
 * The HLSLTree module contains classes to build an HLSL AST (abstract syntax tree)
 * This allows C++ to procedurally define an HLSL program.  The structure of the tree is designed to be flexible, to facilitate incremental generation from a material node graph
 * Once the tree is complete, HLSL source code may be generated
 */
namespace HLSLTree
{

class FNode;
class FScope;

static constexpr int32 MaxNumPreviousScopes = 2;

struct FType
{
	FType() : StructType(nullptr), ValueType(Shader::EValueType::Void) {}
	FType(Shader::EValueType InValueType) : StructType(nullptr), ValueType(InValueType) {}
	FType(const FStructType* InStruct) : StructType(InStruct), ValueType(Shader::EValueType::Struct) { check(InStruct); }

	const TCHAR* GetName() const;
	bool IsStruct() const { return (bool)StructType; }
	int32 GetNumComponents() const;
	bool Merge(const FType& OtherType);

	inline operator Shader::EValueType() const { return ValueType; }
	inline operator bool() const { return ValueType != Shader::EValueType::Void; }
	inline bool operator!() const { return ValueType == Shader::EValueType::Void; }

	const FStructType* StructType;
	Shader::EValueType ValueType;
};

inline bool operator==(const FType& Lhs, const FType& Rhs)
{
	if (Lhs.ValueType != Rhs.ValueType) return false;
	if (Lhs.ValueType == Shader::EValueType::Struct && Lhs.StructType != Rhs.StructType) return false;
	return true;
}
inline bool operator!=(const FType& Lhs, const FType& Rhs)
{
	return !operator==(Lhs, Rhs);
}

struct FConstantValue
{
	FConstantValue() = default;
	FConstantValue(const Shader::FValue& InValue);

	FType Type;

	/**
	 * For basic types, this will include 1-4 components
	 * For struct types, will match the flattened list of struct component types
	 */
	TArray<Shader::FValueComponent, TInlineAllocator<16>> Component;
};

struct FError
{
	const FError* Next;
	const FNode* Node;
	int32 MessageLength;
	TCHAR Message[1];
};

class FErrors
{
public:
	explicit FErrors(FMemStackBase& InAllocator);

	int32 Num() const { return NumErrors; }

	void AddError(const FNode* InNode, FStringView InError);

	template<typename FormatType, typename... Types>
	void AddErrorf(const FNode* InNode, const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		String.Appendf(Format, Forward<Types>(Args)...);
		AddError(InNode, FStringView(String.ToString(), String.Len()));
	}

private:
	FMemStackBase* Allocator = nullptr;
	const FError* FirstError = nullptr;
	int32 NumErrors = 0;
};

enum class ECastFlags : uint32
{
	None = 0u,
	ReplicateScalar = (1u << 0),
	AllowTruncate = (1u << 1),
	AllowAppendZeroes = (1u << 2),

	ValidCast = ReplicateScalar | AllowTruncate,
};
ENUM_CLASS_FLAGS(ECastFlags);

/** Tracks shared state while emitting HLSL code */
class FEmitContext
{
public:
	explicit FEmitContext(FMemStackBase& InAllocator);
	~FEmitContext();

	void Finalize();

	/** Get a unique local variable name */
	const TCHAR* AcquireLocalDeclarationCode();

	const TCHAR* CastShaderValue(const FNode* Node, const TCHAR* Code, const FType& SourceType, const FType& DestType, ECastFlags Flags);

	void AddPreshader(Shader::EValueType Type, const Shader::FPreshaderData& Preshader, FStringBuilderBase& OutCode);

	FMemStackBase* Allocator = nullptr;
	FErrors Errors;

	// TODO - remove preshader material dependency
	const FMaterial* Material = nullptr;
	const FStaticParameterSet* StaticParameters = nullptr;
	FMaterialCompilationOutput* MaterialCompilationOutput = nullptr;
	TMap<Shader::FValue, uint32> DefaultUniformValues;
	uint32 UniformPreshaderOffset = 0u;
	bool bReadMaterialNormal = false;

	int32 NumExpressionLocals = 0;
	int32 NumLocalPHIs = 0;
	int32 NumTexCoords = 0;
};

enum class ENodeVisitResult
{
	VisitDependentNodes,
	SkipDependentNodes,
};

/** Should be overriden to inspect the nodes of an HLSLTree */
class FNodeVisitor
{
public:
	virtual ~FNodeVisitor() {}

	void VisitNode(FNode* Node);

	virtual ENodeVisitResult OnScope(FScope& Scope) { return ENodeVisitResult::VisitDependentNodes; }
	virtual ENodeVisitResult OnStatement(FStatement& Statement) { return ENodeVisitResult::VisitDependentNodes; }
	virtual ENodeVisitResult OnExpression(FExpression& Expression) { return ENodeVisitResult::VisitDependentNodes; }
	virtual ENodeVisitResult OnTextureParameterDeclaration(FTextureParameterDeclaration& Declaration) { return ENodeVisitResult::VisitDependentNodes; }
};

/** Root class of the HLSL AST */
class FNode
{
public:
	virtual ~FNode() {}

	static bool ShouldVisitDependentNodes(ENodeVisitResult Result) { return Result == ENodeVisitResult::VisitDependentNodes; }

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) = 0;

	FNode* NextNode = nullptr;
};

struct FStructField
{
	const TCHAR* Name;
	FType Type;
	int32 ComponentIndex;
};

struct FStructFieldRef
{
	FStructFieldRef() = default;
	FStructFieldRef(const FType& InType, int32 InIndex, int32 InNum) : Type(InType), ComponentIndex(InIndex), ComponentNum(InNum) {}

	FType Type;
	int32 ComponentIndex = INDEX_NONE;
	int32 ComponentNum = 0;

	inline operator bool() const { return ComponentNum > 0; }
	inline bool operator!() const { return ComponentNum <= 0; }
};

class FStructType : public FNode
{
public:
	FStructType* NextType;
	const TCHAR* Name;
	TArrayView<const FStructField> Fields;

	/**
	 * Most code working with HLSLTree views struct types as a flat list of components
	 * Fields with basic types are represented directly. Fields with struct types are recursively flattened into this list
	 */
	TArrayView<const Shader::EValueComponentType> ComponentTypes;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override { return ENodeVisitResult::VisitDependentNodes; }

	FStructFieldRef FindFieldByName(const TCHAR* InName) const;

	void WriteHLSL(FStringBuilderBase& OutWriter) const;
};

struct FStructFieldInitializer
{
	FStructFieldInitializer() = default;
	FStructFieldInitializer(const FStringView& InName, const FType& InType) : Name(InName), Type(InType) {}

	FStringView Name;
	FType Type;
};

struct FStructTypeInitializer
{
	FStringView Name;
	TArrayView<const FStructFieldInitializer> Fields;
};

/**
 * Represents an HLSL statement.  This is a piece of code that doesn't evaluate to any value, but instead should be executed sequentially, and likely has side-effects.
 * Examples include assigning a value, or various control flow structures (if, for, while, etc)
 * This is an abstract base class, with derived classes representing various types of statements
 */
class FStatement : public FNode
{
public:
	static constexpr bool MarkScopeLive = false;
	static constexpr bool MarkScopeLiveRecursive = false;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	virtual void PrepareValues(FEmitContext& Context) const = 0;
	virtual void EmitHLSL(FEmitContext& Context) const = 0;

	FScope* ParentScope = nullptr;
	bool bEmitHLSL = false;
};

class FRequestedType
{
public:
	FRequestedType() = default;
	FRequestedType(int32 NumComponents, bool bDefaultRequest = true) : RequestedComponents(bDefaultRequest, NumComponents) {}
	FRequestedType(const FType& InType, bool bDefaultRequest = true) : StructType(InType.StructType), RequestedComponents(bDefaultRequest, InType.GetNumComponents()) {}

	bool IsStruct() const { return (bool)StructType; }
	const FStructType* GetStructType() const { return StructType; }
	int32 GetNumComponents() const { return RequestedComponents.Num(); }
	int32 GetRequestedNumComponents() const { const int32 LastIndex = RequestedComponents.FindLast(true); return LastIndex + 1; }
	bool IsComponentRequested(int32 Index) const { return RequestedComponents.IsValidIndex(Index) ? (bool)RequestedComponents[Index] : false; }
	bool IsVoid() const { return RequestedComponents.Find(true) == INDEX_NONE; }

	void Reset()
	{
		StructType = nullptr;
		RequestedComponents.Reset();
	}

	/** Returns a type that represents access of FieldRef from a struct on given type, using 'this' */
	FRequestedType MakeFieldAccess(const FStructType* InStructType, const FStructFieldRef& FieldRef) const
	{
		FRequestedType Result(InStructType, false);
		Result.RequestedComponents.SetRangeFromRange(FieldRef.ComponentIndex, FieldRef.ComponentNum, RequestedComponents, 0);
		return Result;
	}

	/** Returns copy of 'this', with request for given field removed */
	FRequestedType CopyWithFieldRemoved(const FStructFieldRef& FieldRef) const
	{
		FRequestedType Result(*this);
		Result.RequestedComponents.SetRange(FieldRef.ComponentIndex, FieldRef.ComponentNum, false);
		return Result;
	}

	/** Returns a type that represents access of the given field from 'this' */
	FRequestedType GetField(const FStructFieldRef& FieldRef) const
	{
		FRequestedType Result(FieldRef.Type);
		Result.RequestedComponents.SetRangeFromRange(0, FieldRef.ComponentNum, RequestedComponents, FieldRef.ComponentIndex);
		return Result;
	}

	/** The struct type we're requesting, or nullptr if we're requesting a basic type */
	const FStructType* StructType = nullptr;

	/** 1 bit per component, a value of 'true' means the specified component is requsted */
	TBitArray<> RequestedComponents;
};

struct FShaderValue
{
	explicit FShaderValue(FStringBuilderBase& InCode) : Code(InCode) {}

	FStringBuilderBase& Code;
	bool bInline = false;
	bool bHasDependencies = false;
};

struct FPrepareValueResult
{
	FType Type;
	EExpressionEvaluationType EvaluationType = EExpressionEvaluationType::None;
	Shader::FValue ConstantValue;

	inline operator bool() const { return EvaluationType != EExpressionEvaluationType::None; }
	inline bool operator!() const { return EvaluationType == EExpressionEvaluationType::None; }
};

/**
 * Represents an HLSL expression.  This is a piece of code that evaluates to a value, but has no side effects.
 * Unlike statements, expressions are not expected to execute in any particular order.  They may be cached (or not) in generated code, without the underlying implementation needing to care.
 * Expressions track the outer-most scope in which they're accessed. The generated HLSL code will ensure they are defined in that scope.
 * Examples include constant literals, variable accessors, and various types of math operations
 * This is an abstract base class, with derived classes representing various types of expression
 */
class FExpression : public FNode
{
public:
	const FType& GetType() const { return PrepareValueResult.Type; }
	EExpressionEvaluationType GetEvaluationType() const { return PrepareValueResult.EvaluationType; }

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	friend FPrepareValueResult PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression, const FRequestedType& RequestedType);

	const TCHAR* GetValueShader(FEmitContext& Context);
	const TCHAR* GetValueShader(FEmitContext& Context, const FType& InType);
	void GetValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader);
	Shader::FValue GetValueConstant(FEmitContext& Context);

	FScope* ParentScope = nullptr;

protected:
	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) = 0;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const;
	virtual void EmitShaderDependencies(FEmitContext& Context, const FShaderValue& Shader) const;
	virtual void EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const;

	void SetType(FEmitContext& Context, EExpressionEvaluationType InEvaluationType, const FType& InType);
	void SetForwardValue(FEmitContext& Context, FExpression* InForwardValue, const FRequestedType& RequestedType);

private:
	const TCHAR* LocalVariableName = nullptr;
	const TCHAR* Code = nullptr;
	FRequestedType CurrentRequestedType;
	FPrepareValueResult PrepareValueResult;
	FExpression* ForwardedValue = nullptr;
	bool bReentryFlag = false;
};


/**
 * Represents a phi node (see various topics on single static assignment)
 * A phi node takes on a value based on the previous scope that was executed.
 * In practice, this means the generated HLSL code will declare a local variable before all the previous scopes, then assign that variable the proper value from within each scope
 */
class FExpressionLocalPHI final : public FExpression
{
public:
	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
	virtual void EmitShaderDependencies(FEmitContext& Context, const FShaderValue& Shader) const override;

	FName LocalName;
	FScope* Scopes[MaxNumPreviousScopes];
	FExpression* Values[MaxNumPreviousScopes];
	int32 NumValues;
};

/**
 * Represents an HLSL texture parameter.
 */
class FTextureParameterDeclaration final : public FNode
{
public:
	FTextureParameterDeclaration(const FName& InName, const FTextureDescription& InDescription) : Name(InName), Description(InDescription) {}

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	FName Name;
	FTextureDescription Description;
};

enum class EScopeState : uint8
{
	Uninitialized,
	Live,
	Dead,
};

/**
 * Represents an HLSL scope.  A scope contains a single statement, along with any expressions required by that statement
 */
class FScope final : public FNode
{
public:
	static FScope* FindSharedParent(FScope* Lhs, FScope* Rhs);

	inline FScope* GetParentScope() const { return ParentScope; }
	inline bool IsLive() const { return State == EScopeState::Live; }
	inline bool IsDead() const { return State == EScopeState::Dead; }

	inline TArrayView<FScope*> GetPreviousScopes() const
	{
		// const_cast needed, otherwise type of array view is 'FScope*const' which doesn't make sense
		return MakeArrayView(const_cast<FScope*>(this)->PreviousScope, NumPreviousScopes);
	}

	bool HasParentScope(const FScope& ParentScope) const;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	void AddPreviousScope(FScope& Scope);

	void UseExpression(FExpression* Expression);

	friend void PrepareScopeValues(FEmitContext& Context, const FScope* InScope);

	template<typename FormatType, typename... Types>
	void EmitDeclarationf(FEmitContext& Context, const FormatType& Format, Types... Args)
	{
		InternalEmitCodef(Context, Declarations, ENextScopeFormat::None, nullptr, Format, Forward<Types>(Args)...);
	}

	template<typename FormatType, typename... Types>
	void EmitStatementf(FEmitContext& Context, const FormatType& Format, Types... Args)
	{
		InternalEmitCodef(Context, Statements, ENextScopeFormat::None, nullptr, Format, Forward<Types>(Args)...);
	}

	void EmitNextScope(FEmitContext& Context, FScope* NestedScope)
	{
		InternalEmitCode(Context, Statements, ENextScopeFormat::Unscoped, NestedScope, nullptr, 0);
	}

	template<typename FormatType, typename... Types>
	void EmitNestedScopef(FEmitContext& Context, FScope* NestedScope, const FormatType& Format, Types... Args)
	{
		InternalEmitCodef(Context, Statements, ENextScopeFormat::Scoped, NestedScope, Format, Forward<Types>(Args)...);
	}

	void MarkLive();
	void MarkLiveRecursive();
	void MarkDead();

	void WriteHLSL(int32 Indent, FStringBuilderBase& OutString) const;

private:
	friend class FTree;
	friend class FExpression;
	friend class FNodeVisitor_MoveToScope;

	enum class ENextScopeFormat : uint8
	{
		None,
		Unscoped,
		Scoped,
	};

	struct FCodeEntry
	{
		FCodeEntry* Next;
		FScope* NextScope;
		int32 Length;
		ENextScopeFormat ScopeFormat;
		TCHAR String[1];
	};

	struct FCodeList
	{
		FCodeEntry* First = nullptr;
		FCodeEntry* Last = nullptr;
		int32 Num = 0;
	};

	void InternalEmitCode(FEmitContext& Context, FCodeList& List, ENextScopeFormat ScopeFormat, FScope* NextScope, const TCHAR* String, int32 Length);

	template<typename FormatType, typename... Types>
	void InternalEmitCodef(FEmitContext& Context, FCodeList& List, ENextScopeFormat ScopeFormat, FScope* NextScope, const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		String.Appendf(Format, Forward<Types>(Args)...);
		InternalEmitCode(Context, List, ScopeFormat, NextScope, String.ToString(), String.Len());
	}

	FScope* ParentScope = nullptr;
	FStatement* Statement = nullptr;
	FScope* PreviousScope[MaxNumPreviousScopes];
	TMap<FSHAHash, const TCHAR*> ExpressionCodeMap;
	FCodeList Declarations;
	FCodeList Statements;
	int32 NumPreviousScopes = 0;
	int32 NestedLevel = 0;
	EScopeState State = EScopeState::Uninitialized;
};

inline bool IsScopeLive(const FScope* InScope)
{
	return (bool)InScope && InScope->IsLive();
}

inline void MarkScopeDead(FScope* InScope)
{
	if (InScope)
	{
		InScope->MarkDead();
	}
}

/**
 * The HLSL AST.  Basically a wrapper around the root scope, with some helper methods
 */
class FTree
{
public:
	static FTree* Create(FMemStackBase& Allocator);
	static void Destroy(FTree* Tree);

	FMemStackBase& GetAllocator() { return *Allocator; }

	bool EmitHLSL(FEmitContext& Context,
		FStringBuilderBase& OutDeclarations,
		FStringBuilderBase& OutCode) const;

	FScope& GetRootScope() const { return *RootScope; }

	template<typename T, typename... ArgTypes>
	inline T* NewExpression(FScope& Scope, ArgTypes&&... Args)
	{
		T* Expression = NewNode<T>(Forward<ArgTypes>(Args)...);
		RegisterExpression(Scope, Expression);
		return Expression;
	}

	template<typename T, typename... ArgTypes>
	inline T* NewStatement(FScope& Scope, ArgTypes&&... Args)
	{
		T* Statement = NewNode<T>(Forward<ArgTypes>(Args)...);
		RegisterStatement(Scope, Statement);
		if constexpr (T::MarkScopeLiveRecursive)
		{
			Scope.MarkLiveRecursive();
		}
		else if constexpr (T::MarkScopeLive)
		{
			Scope.MarkLive();
		}

		return Statement;
	}

	FScope* NewScope(FScope& Scope);
	FTextureParameterDeclaration* NewTextureParameterDeclaration(const FName& Name, const FTextureDescription& DefaultValue);

	const FStructType* NewStructType(const FStructTypeInitializer& Initializer);

	void SetResult(FStatement& InResult);

private:
	template<typename T, typename... ArgTypes>
	inline T* NewNode(ArgTypes&&... Args)
	{
		T* Node = new(*Allocator) T(Forward<ArgTypes>(Args)...);
		Node->NextNode = Nodes;
		Nodes = Node;
		return Node;
	}

	void RegisterExpression(FScope& Scope, FExpression* Expression);
	void RegisterStatement(FScope& Scope, FStatement* Statement);

	FMemStackBase* Allocator = nullptr;
	FNode* Nodes = nullptr;
	FStructType* StructTypes = nullptr;
	FScope* RootScope = nullptr;
	FStatement* ResultStatement = nullptr;
};

//Shader::EValueType RequestExpressionType(FEmitContext& Context, FExpression* InExpression, int8 InRequestedNumComponents); // friend of FExpression
//EExpressionEvaluationType PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression); // friend of FExpression
//void PrepareScopeValues(FEmitContext& Context, const FScope* InScope); // friend of FScope

} // namespace HLSLTree
} // namespace UE
