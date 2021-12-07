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
class FExpressionLocalPHI;
class FRequestedType;
struct FEmitShaderValue;
struct FShaderValue;

static constexpr int32 MaxNumPreviousScopes = 2;

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

using FEmitShaderValueDependencies = TArray<FEmitShaderValue*, TInlineAllocator<8>>;

struct FEmitShaderValueContext
{
	FEmitShaderValueDependencies Dependencies;
};

/** Tracks shared state while emitting HLSL code */
class FEmitContext
{
public:
	explicit FEmitContext(FMemStackBase& InAllocator, const Shader::FStructTypeRegistry& InTypeRegistry);
	~FEmitContext();

	void Finalize();

	/** Get a unique local variable name */
	const TCHAR* AcquireLocalDeclarationCode();

	FEmitShaderValue* AcquireShader(FScope* Scope, const FShaderValue& Shader, TArrayView<FEmitShaderValue*> Dependencies);
	FEmitShaderValue* AcquirePreshader(const FRequestedType& RequestedType, FScope* Scope, FExpression* Expression);

	FEmitShaderValue* CastShaderValue(FNode* Node, FScope* Scope, FEmitShaderValue* ShaderValue, const Shader::FType& DestType);

	FMemStackBase* Allocator = nullptr;
	const Shader::FStructTypeRegistry* TypeRegistry = nullptr;
	TMap<FSHAHash, FEmitShaderValue*> ShaderValueMap;
	TArray<const FExpressionLocalPHI*> LocalPHIs;
	FErrors Errors;

	// TODO - remove preshader material dependency
	const FMaterial* Material = nullptr;
	const FStaticParameterSet* StaticParameters = nullptr;
	FMaterialCompilationOutput* MaterialCompilationOutput = nullptr;
	TMap<Shader::FValue, uint32> DefaultUniformValues;
	TMap<FSHAHash, FEmitShaderValue*> Preshaders;
	TArray<FScope*, TInlineAllocator<16>> ScopeStack;
	TArray<FEmitShaderValueContext, TInlineAllocator<16>> ShaderValueStack;
	uint32 UniformPreshaderOffset = 0u;
	bool bReadMaterialNormal = false;

	int32 NumExpressionLocals = 0;
	int32 NumTexCoords = 0;
};

/** Root class of the HLSL AST */
class FNode
{
public:
	virtual ~FNode() {}
	virtual void Reset() {}
	FNode* NextNode = nullptr;
};

/**
 * Represents an HLSL statement.  This is a piece of code that doesn't evaluate to any value, but instead should be executed sequentially, and likely has side-effects.
 * Examples include assigning a value, or various control flow structures (if, for, while, etc)
 * This is an abstract base class, with derived classes representing various types of statements
 */
class FStatement : public FNode
{
public:
	virtual void Reset() override;
	virtual void Prepare(FEmitContext& Context) const = 0;
	virtual void EmitShader(FEmitContext& Context) const = 0;

	FScope* ParentScope = nullptr;
	bool bEmitShader = false;
};

/**
 * Like Shader::FType, but tracks which individual components are needed
 */
class FRequestedType
{
public:
	FRequestedType() = default;
	FRequestedType(int32 NumComponents, bool bDefaultRequest = true) : RequestedComponents(bDefaultRequest, NumComponents) {}
	FRequestedType(const Shader::FType& InType, bool bDefaultRequest = true);
	FRequestedType(const Shader::EValueType& InType, bool bDefaultRequest = true);
	
	const TCHAR* GetName() const { return GetType().GetName(); }
	bool IsStruct() const { return StructType != nullptr; }
	const Shader::FStructType* GetStructType() const { return StructType; }
	const Shader::FType GetType() const;

	int32 GetNumComponents() const;
	bool IsComponentRequested(int32 Index) const { return RequestedComponents.IsValidIndex(Index) ? (bool)RequestedComponents[Index] : false; }
	bool IsVoid() const { return RequestedComponents.Find(true) == INDEX_NONE; }

	bool Merge(const FRequestedType& OtherType);

	void Reset()
	{
		StructType = nullptr;
		ValueComponentType = Shader::EValueComponentType::Void;
		RequestedComponents.Reset();
	}

	void SetComponentRequested(int32 Index, bool bRequested = true)
	{
		if (bRequested)
		{
			RequestedComponents.PadToNum(Index + 1, false);
		}
		if (RequestedComponents.IsValidIndex(Index))
		{
			RequestedComponents[Index] = bRequested;
		}
	}

	/** Marks the given field as requested (or not) */
	void SetFieldRequested(const Shader::FStructField* Field, bool bRequested = true)
	{
		RequestedComponents.SetRange(Field->ComponentIndex, Field->GetNumComponents(), bRequested);
	}

	void ClearFieldRequested(const Shader::FStructField* Field)
	{
		SetFieldRequested(Field, false);
	}

	/** Marks the given field as requested, based on the input request type (which should match the field type) */
	void SetField(const Shader::FStructField* Field, const FRequestedType& InRequest)
	{
		ensure(InRequest.GetNumComponents() == Field->GetNumComponents());
		RequestedComponents.SetRangeFromRange(Field->ComponentIndex, InRequest.GetNumComponents(), InRequest.RequestedComponents, 0);
	}

	/** Returns the requested type of the given field */
	FRequestedType GetField(const Shader::FStructField* Field) const
	{
		FRequestedType Result(Field->Type);
		Result.RequestedComponents.SetRangeFromRange(0, Field->GetNumComponents(), RequestedComponents, Field->ComponentIndex);
		return Result;
	}

	/**
	 * If either StructType or ValueComponentType are set, then the request is for an explicit type
	 * Otherwise, the request is for any type with the given components
	 */
	const Shader::FStructType* StructType = nullptr;
	Shader::EValueComponentType ValueComponentType = Shader::EValueComponentType::Void;

	/** 1 bit per component, a value of 'true' means the specified component is requsted */
	TBitArray<> RequestedComponents;
};

/**
 * Like FRequestedType, but tracks an EExpressionEvaluationType per component, rather than a simple requested flag
 */
class FPreparedType
{
public:
	FPreparedType() = default;
	FPreparedType(Shader::EValueComponentType InComponentType) : ValueComponentType(InComponentType) {}
	FPreparedType(const Shader::FStructType* InStructType) : StructType(InStructType) {}
	FPreparedType(const Shader::FType& InType);

	void SetEvaluationType(EExpressionEvaluationType EvaluationType);

	void SetField(const Shader::FStructField* Field, const FPreparedType& FieldType);
	FPreparedType GetFieldType(const Shader::FStructField* Field) const;

	int32 GetNumComponents() const;
	FRequestedType GetRequestedType() const;
	Shader::FType GetType() const;
	bool IsStruct() const { return !IsVoid() && StructType != nullptr; }
	bool IsNumeric() const { return !IsVoid() && ValueComponentType != Shader::EValueComponentType::Void; }
	bool IsInitialized() const { return StructType != nullptr || ValueComponentType != Shader::EValueComponentType::Void; }
	bool IsVoid() const;
	EExpressionEvaluationType GetEvaluationType(const FRequestedType& RequestedType) const;

	EExpressionEvaluationType GetComponentEvaluationType(int32 Index) const
	{
		return ComponentEvaluationType.IsValidIndex(Index) ? ComponentEvaluationType[Index] : EExpressionEvaluationType::None;
	}

	void SetComponentEvaluationType(int32 Index, EExpressionEvaluationType EvaluationType);

	/** Unlike FRequestedType, one of these should be set */
	const Shader::FStructType* StructType = nullptr;
	Shader::EValueComponentType ValueComponentType = Shader::EValueComponentType::Void;

	/** Evaluation type for each component, may be 'None' for components that are unused */
	TArray<EExpressionEvaluationType, TInlineAllocator<16>> ComponentEvaluationType;
};

FPreparedType MergePreparedTypes(const FPreparedType& Lhs, const FPreparedType& Rhs);

struct FShaderValue
{
	explicit FShaderValue(FStringBuilderBase& InCode) : Code(InCode) {}

	FStringBuilderBase& Code;
	Shader::FType Type;
	bool bInline = false;
};

class FPrepareValueResult
{
public:
	const FPreparedType& GetPreparedType() const { return PreparedType; }

	void SetType(FEmitContext& Context, const FRequestedType& RequestedType, EExpressionEvaluationType EvaluationType, const Shader::FType& Type);
	void SetType(FEmitContext& Context, const FRequestedType& RequestedType, EExpressionEvaluationType EvaluationType, Shader::EValueComponentType ComponentType);
	void SetType(FEmitContext& Context, const FRequestedType& RequestedType, const FPreparedType& Type);
	void SetForwardValue(FEmitContext& Context, const FRequestedType& RequestedType, FExpression* InValue);

private:
	bool TryMergePreparedType(FEmitContext& Context, const Shader::FStructType* StructType, Shader::EValueComponentType ComponentType);

	FExpression* ForwardValue = nullptr;
	FPreparedType PreparedType;

	friend class FExpression;
};

struct FEmitShaderValue
{
	FEmitShaderValue(FScope* InScope, const Shader::FType& InType) : Scope(InScope), Type(InType) {}

	inline bool IsInline() const { return Value == nullptr; }

	FScope* Scope = nullptr;
	const TCHAR* Reference = nullptr;
	const TCHAR* Value = nullptr;
	Shader::FType Type;
	TArrayView<FEmitShaderValue*> Dependencies;
	FSHAHash Hash;
};

/**
 * Represents an HLSL expression.  This is a piece of code that evaluates to a value, but has no side effects.
 * Unlike statements, expressions are not expected to execute in any particular order.  They may be cached (or not) in generated code, without the underlying implementation needing to care.
 * Examples include constant literals, variable accessors, and various types of math operations
 * This is an abstract base class, with derived classes representing various types of expression
 */
class FExpression : public FNode
{
public:
	FRequestedType GetRequestedType() const { return PrepareValueResult.PreparedType.GetRequestedType(); }
	Shader::FType GetType() const { return PrepareValueResult.PreparedType.GetType(); }
	EExpressionEvaluationType GetEvaluationType(const FRequestedType& RequestedType) const { return PrepareValueResult.PreparedType.GetEvaluationType(RequestedType); }

	virtual void Reset() override;

	friend const FPreparedType& PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression, const FRequestedType& RequestedType);

	const TCHAR* GetValueShader(FEmitContext& Context, const FRequestedType& RequestedType);
	void GetValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader);
	Shader::FValue GetValueConstant(FEmitContext& Context, const FRequestedType& RequestedType);

	const TCHAR* GetValueShader(FEmitContext& Context);

protected:
	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const = 0;
	virtual void EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const;
	virtual void EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const;

private:
	FRequestedType CurrentRequestedType;
	FPrepareValueResult PrepareValueResult;
	bool bReentryFlag = false;

	friend class FEmitContext;
};


/**
 * Represents a phi node (see various topics on single static assignment)
 * A phi node takes on a value based on the previous scope that was executed.
 * In practice, this means the generated HLSL code will declare a local variable before all the previous scopes, then assign that variable the proper value from within each scope
 */
class FExpressionLocalPHI final : public FExpression
{
public:
	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const override;

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
	virtual void Reset() override;

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

	void AddPreviousScope(FScope& Scope);

	friend bool PrepareScope(FEmitContext& Context, FScope* InScope);

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

	void EmitScope(FEmitContext& Context, FScope* NestedScope)
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

	enum class ENextScopeFormat : uint8
	{
		None,
		Unscoped,
		Scoped,
	};

	struct FCodeEntry
	{
		FCodeEntry* Next;
		FScope* Scope;
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

	void InternalEmitCode(FEmitContext& Context, FCodeList& List, ENextScopeFormat ScopeFormat, FScope* Scope, const TCHAR* String, int32 Length);

	template<typename FormatType, typename... Types>
	void InternalEmitCodef(FEmitContext& Context, FCodeList& List, ENextScopeFormat ScopeFormat, FScope* Scope, const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		String.Appendf(Format, Forward<Types>(Args)...);
		InternalEmitCode(Context, List, ScopeFormat, Scope, String.ToString(), String.Len());
	}

	FStatement* OwnerStatement = nullptr;
	FScope* ParentScope = nullptr;
	FStatement* ContainedStatement = nullptr;
	FScope* PreviousScope[MaxNumPreviousScopes];
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

inline void MarkScopeLive(FScope* InScope)
{
	if (InScope)
	{
		InScope->MarkLive();
	}
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

	void ResetNodes();

	bool EmitShader(FEmitContext& Context, FStringBuilderBase& OutCode) const;

	FScope& GetRootScope() const { return *RootScope; }

	template<typename T, typename... ArgTypes>
	inline T* NewExpression(ArgTypes&&... Args)
	{
		T* Expression = NewNode<T>(Forward<ArgTypes>(Args)...);
		RegisterExpression(Expression);
		return Expression;
	}

	template<typename T, typename... ArgTypes>
	inline T* NewStatement(FScope& Scope, ArgTypes&&... Args)
	{
		T* Statement = NewNode<T>(Forward<ArgTypes>(Args)...);
		RegisterStatement(Scope, Statement);
		return Statement;
	}

	FScope* NewScope(FScope& Scope);
	FScope* NewOwnedScope(FStatement& Owner);

	FTextureParameterDeclaration* NewTextureParameterDeclaration(const FName& Name, const FTextureDescription& DefaultValue);

private:
	template<typename T, typename... ArgTypes>
	inline T* NewNode(ArgTypes&&... Args)
	{
		T* Node = new(*Allocator) T(Forward<ArgTypes>(Args)...);
		Node->NextNode = Nodes;
		Nodes = Node;
		return Node;
	}

	void RegisterExpression(FExpression* Expression);
	void RegisterStatement(FScope& Scope, FStatement* Statement);

	FMemStackBase* Allocator = nullptr;
	FNode* Nodes = nullptr;
	FScope* RootScope = nullptr;
};

//Shader::EValueType RequestExpressionType(FEmitContext& Context, FExpression* InExpression, int8 InRequestedNumComponents); // friend of FExpression
//EExpressionEvaluationType PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression); // friend of FExpression
//void PrepareScopeValues(FEmitContext& Context, const FScope* InScope); // friend of FScope

} // namespace HLSLTree
} // namespace UE
