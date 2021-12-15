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
struct FEmitShaderCode;
struct FEmitShaderValues;

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

struct FEmitShaderCode
{
	FEmitShaderCode(FScope* InScope, const Shader::FType& InType) : Scope(InScope), Type(InType) {}

	inline bool IsInline() const { return Value == nullptr; }

	FScope* Scope = nullptr;
	const TCHAR* Reference = nullptr;
	const TCHAR* Value = nullptr;
	Shader::FType Type;
	TArrayView<FEmitShaderCode*> Dependencies;
	FSHAHash Hash;
};

struct FEmitShaderValues
{
	FEmitShaderCode* Code = nullptr;
	FEmitShaderCode* CodeDdx = nullptr;
	FEmitShaderCode* CodeDdy = nullptr;

	bool HasDerivatives() const { return (bool)CodeDdx && (bool)CodeDdy; }
};

using FEmitShaderValueDependencies = TArray<FEmitShaderCode*, TInlineAllocator<8>>;

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
	FFormatArgVariant(FEmitShaderCode* InValue) : Type(EFormatArgType::ShaderValue), ShaderValue(InValue) { check(InValue); }
	FFormatArgVariant(const TCHAR* InValue) : Type(EFormatArgType::String), String(InValue) { check(InValue); }
	FFormatArgVariant(int32 InValue) : Type(EFormatArgType::Int), Int(InValue) {}

	EFormatArgType Type = EFormatArgType::Void;
	union
	{
		FEmitShaderCode* ShaderValue;
		const TCHAR* String;
		int32 Int;
	};
};

using FFormatArgList = TArray<FFormatArgVariant, TInlineAllocator<8>>;

inline void BuildFormatArgList(FFormatArgList&) {}

template<typename Type, typename... Types>
inline void BuildFormatArgList(FFormatArgList& OutList, Type Arg, Types... Args)
{
	OutList.Add(Arg);
	BuildFormatArgList(OutList, Forward<Types>(Args)...);
}

/** Tracks shared state while emitting HLSL code */
class FEmitContext
{
public:
	explicit FEmitContext(FMemStackBase& InAllocator, const Shader::FStructTypeRegistry& InTypeRegistry);
	~FEmitContext();

	void Finalize();

	/** Get a unique local variable name */
	const TCHAR* AcquireLocalDeclarationCode();

	FEmitShaderCode* EmitCodeInternal(const Shader::FType& Type, FStringView Code, bool bInline, TArrayView<FEmitShaderCode*> Dependencies);
	FEmitShaderCode* EmitFormatCodeInternal(const Shader::FType& Type, const TCHAR* Format, bool bInline, const FFormatArgList& ArgList);

	template<typename FormatType, typename... Types>
	FEmitShaderCode* EmitCode(const Shader::FType& Type, const FormatType& Format, Types... Args)
	{
		FFormatArgList ArgList;
		BuildFormatArgList(ArgList, Forward<Types>(Args)...);
		return EmitFormatCodeInternal(Type, Format, false, ArgList);
	}

	template<typename FormatType, typename... Types>
	FEmitShaderCode* EmitInlineCode(const Shader::FType& Type, const FormatType& Format, Types... Args)
	{
		FFormatArgList ArgList;
		BuildFormatArgList(ArgList, Forward<Types>(Args)...);
		return EmitFormatCodeInternal(Type, Format, true, ArgList);
	}

	
	FEmitShaderCode* EmitPreshaderOrConstant(const FRequestedType& RequestedType, FExpression* Expression);
	FEmitShaderCode* EmitConstantZero(const Shader::FType& Type);

	FEmitShaderCode* EmitCast(FEmitShaderCode* ShaderValue, const Shader::FType& DestType);
	FEmitShaderValues EmitCast(FEmitShaderValues ShaderValue, const Shader::FType& DestType);

	FEmitShaderCode* EmitUnaryOp(EUnaryOp Op, FEmitShaderCode* Input);
	FEmitShaderValues EmitUnaryOp(EUnaryOp Op, FEmitShaderValues Input, EExpressionDerivative Derivative);

	FEmitShaderCode* EmitBinaryOp(EBinaryOp Op, FEmitShaderCode* Lhs, FEmitShaderCode* Rhs);
	FEmitShaderValues EmitBinaryOp(EBinaryOp Op, FEmitShaderValues Lhs, FEmitShaderValues Rhs, EExpressionDerivative Derivative);

	template<typename T> inline T EmitNeg(T Input) { return EmitUnaryOp(EUnaryOp::Neg, Input); }
	template<typename T> inline T EmitRcp(T Input) { return EmitUnaryOp(EUnaryOp::Rcp, Input); }

	template<typename T> inline T EmitAdd(T Lhs, T Rhs) { return EmitBinaryOp(EBinaryOp::Add, Lhs, Rhs); }
	template<typename T> inline T EmitSub(T Lhs, T Rhs) { return EmitBinaryOp(EBinaryOp::Sub, Lhs, Rhs); }
	template<typename T> inline T EmitMul(T Lhs, T Rhs) { return EmitBinaryOp(EBinaryOp::Mul, Lhs, Rhs); }
	template<typename T> inline T EmitDiv(T Lhs, T Rhs) { return EmitBinaryOp(EBinaryOp::Div, Lhs, Rhs); }

	FMemStackBase* Allocator = nullptr;
	const Shader::FStructTypeRegistry* TypeRegistry = nullptr;
	TArray<FScope*, TInlineAllocator<16>> ScopeStack;
	TMap<FSHAHash, FEmitShaderCode*> ShaderValueMap;
	TMap<FSHAHash, FEmitShaderCode*> PreshaderValueMap;
	TArray<const FExpressionLocalPHI*> LocalPHIs;
	FErrors Errors;

	// TODO - remove preshader material dependency
	const FMaterial* Material = nullptr;
	const FStaticParameterSet* StaticParameters = nullptr;
	FMaterialCompilationOutput* MaterialCompilationOutput = nullptr;
	TMap<Shader::FValue, uint32> DefaultUniformValues;
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

enum class EComponentRequest : uint8
{
	None,
	Requested,
	RequestedWithDerivative,
};
inline bool IsRequested(EComponentRequest InRequest) { return InRequest != EComponentRequest::None; }
inline bool IsDerivativeRequested(EComponentRequest InRequest) { return InRequest == EComponentRequest::RequestedWithDerivative; }

/**
 * Like Shader::FType, but tracks which individual components are needed
 */
class FRequestedType
{
public:
	FRequestedType() = default;
	FRequestedType(int32 NumComponents, EComponentRequest DefaultRequest = EComponentRequest::Requested);
	FRequestedType(const Shader::FType& InType, EComponentRequest DefaultRequest = EComponentRequest::Requested);
	FRequestedType(const Shader::EValueType& InType, EComponentRequest DefaultRequest = EComponentRequest::Requested);
	
	const TCHAR* GetName() const { return GetType().GetName(); }
	bool IsStruct() const { return StructType != nullptr; }
	const Shader::FStructType* GetStructType() const { return StructType; }
	const Shader::FType GetType() const;

	int32 GetNumComponents() const;
	bool IsComponentRequested(int32 Index) const { return RequestedComponents.IsValidIndex(Index) ? (bool)RequestedComponents[Index] : false; }
	bool IsComponentDerivativeRequested(int32 Index) const { return RequestedComponentDerivatives.IsValidIndex(Index) ? (bool)RequestedComponentDerivatives[Index] : false; }
	bool IsVoid() const { return RequestedComponents.Find(true) == INDEX_NONE; }

	EComponentRequest GetComponentRequest(int32 Index) const;
	void SetComponentRequest(int32 Index, EComponentRequest Request = EComponentRequest::Requested);

	void Reset()
	{
		StructType = nullptr;
		ValueComponentType = Shader::EValueComponentType::Void;
		RequestedComponents.Reset();
		RequestedComponentDerivatives.Reset();
	}

	/** Marks the given field as requested (or not) */
	void SetFieldRequested(const Shader::FStructField* Field, EComponentRequest Request = EComponentRequest::Requested);
	void ClearFieldRequested(const Shader::FStructField* Field)
	{
		SetFieldRequested(Field, EComponentRequest::None);
	}

	/** Marks the given field as requested, based on the input request type (which should match the field type) */
	void SetField(const Shader::FStructField* Field, const FRequestedType& InRequest);

	/** Returns the requested type of the given field */
	FRequestedType GetField(const Shader::FStructField* Field) const;

	/**
	 * If either StructType or ValueComponentType are set, then the request is for an explicit type
	 * Otherwise, the request is for any type with the given components
	 */
	const Shader::FStructType* StructType = nullptr;
	Shader::EValueComponentType ValueComponentType = Shader::EValueComponentType::Void;

	/** 1 bit per component, a value of 'true' means the specified component is requsted */
	TBitArray<> RequestedComponents;
	TBitArray<> RequestedComponentDerivatives;
};

FRequestedType MakeRequestedType(Shader::EValueComponentType ComponentType, const FRequestedType& RequestedComponents);

struct FPreparedData
{
	FPreparedData() = default;

	FPreparedData(EExpressionEvaluation InEvaluation, EExpressionDerivative InDerivative = EExpressionDerivative::Invalid)
		: Evaluation(InEvaluation)
	{
		switch (InEvaluation)
		{
		case EExpressionEvaluation::None:
			Derivative = EExpressionDerivative::None;
			break;
		case EExpressionEvaluation::Constant:
		case EExpressionEvaluation::Preshader:
			Derivative = EExpressionDerivative::Zero;
			break;
		default:
			Derivative = InDerivative;
			break;
		}
	}

	bool IsValid() const { return Evaluation != EExpressionEvaluation::None; }

	EComponentRequest GetRequest() const
	{
		if (Evaluation != EExpressionEvaluation::None)
		{
			if (Derivative == EExpressionDerivative::Valid || Derivative == EExpressionDerivative::Zero)
			{
				return  EComponentRequest::RequestedWithDerivative;
			}
			return  EComponentRequest::Requested;
		}
		return EComponentRequest::None;
	}

	EExpressionEvaluation Evaluation = EExpressionEvaluation::None;
	EExpressionDerivative Derivative = EExpressionDerivative::None;
};

inline FPreparedData CombinePreparedData(const FPreparedData& Lhs, const FPreparedData& Rhs)
{
	return FPreparedData(CombineEvaluations(Lhs.Evaluation, Rhs.Evaluation), CombineDerivatives(Lhs.Derivative, Rhs.Derivative));
}

/**
 * Like FRequestedType, but tracks an EExpressionEvaluation per component, rather than a simple requested flag
 */
class FPreparedType
{
public:
	FPreparedType() = default;
	FPreparedType(Shader::EValueComponentType InComponentType) : ValueComponentType(InComponentType) {}
	FPreparedType(const Shader::FStructType* InStructType) : StructType(InStructType) {}
	FPreparedType(const Shader::FType& InType);

	void SetEvaluation(EExpressionEvaluation Evaluation);
	void SetDerivative(EExpressionDerivative Derivative);

	void SetField(const Shader::FStructField* Field, const FPreparedType& FieldType);
	FPreparedType GetFieldType(const Shader::FStructField* Field) const;

	int32 GetNumComponents() const;
	FRequestedType GetRequestedType() const;
	Shader::FType GetType() const;
	bool IsStruct() const { return !IsVoid() && StructType != nullptr; }
	bool IsNumeric() const { return !IsVoid() && ValueComponentType != Shader::EValueComponentType::Void; }
	bool IsInitialized() const { return StructType != nullptr || ValueComponentType != Shader::EValueComponentType::Void; }
	bool IsVoid() const;

	FPreparedData GetData() const;
	FPreparedData GetData(const FRequestedType& RequestedType) const;
	FPreparedData GetFieldData(int32 ComponentIndex, int32 NumComponents) const;
	FPreparedData GetComponentData(int32 Index) const;

	inline EExpressionEvaluation GetEvaluation(const FRequestedType& RequestedType) const { return GetData(RequestedType).Evaluation; }
	inline EExpressionDerivative GetDerivative(const FRequestedType& RequestedType) const { return GetData(RequestedType).Derivative; }

	void SetComponentData(int32 Index, const FPreparedData& Data);
	void MergeComponentData(int32 Index, EComponentRequest Request, const FPreparedData& Data);

	/** Unlike FRequestedType, one of these should be set */
	const Shader::FStructType* StructType = nullptr;
	Shader::EValueComponentType ValueComponentType = Shader::EValueComponentType::Void;

	/** Evaluation type for each component, may be 'None' for components that are unused */
	TArray<FPreparedData, TInlineAllocator<16>> PreparedComponents;
};

FPreparedType MergePreparedTypes(const FPreparedType& Lhs, const FPreparedType& Rhs);

class FPrepareValueResult
{
public:
	const FPreparedType& GetPreparedType() const { return PreparedType; }

	void SetType(FEmitContext& Context, const FRequestedType& RequestedType, const FPreparedData& Data, const Shader::FType& Type);
	void SetType(FEmitContext& Context, const FRequestedType& RequestedType, const FPreparedType& Type);

	void SetTypeWithDerivative(FEmitContext& Context, const FRequestedType& RequestedType, EExpressionEvaluation Evaluation, const Shader::FType& Type)
	{
		SetType(Context, RequestedType, FPreparedData(Evaluation, EExpressionDerivative::Valid), Type);
	}

	void SetForwardValue(FEmitContext& Context, const FRequestedType& RequestedType, FExpression* InValue);

private:
	bool TryMergePreparedType(FEmitContext& Context, const Shader::FStructType* StructType, Shader::EValueComponentType ComponentType);

	FExpression* ForwardValue = nullptr;
	FPreparedType PreparedType;

	friend class FExpression;
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
	const FPreparedType& GetPreparedType() const { return PrepareValueResult.PreparedType; }
	FRequestedType GetRequestedType() const { return PrepareValueResult.PreparedType.GetRequestedType(); }
	Shader::FType GetType() const { return PrepareValueResult.PreparedType.GetType(); }
	EExpressionEvaluation GetEvaluation(const FRequestedType& RequestedType) const { return PrepareValueResult.PreparedType.GetEvaluation(RequestedType); }
	EExpressionDerivative GetDerivative(const FRequestedType& RequestedType) const { return PrepareValueResult.PreparedType.GetDerivative(RequestedType); }

	virtual void Reset() override;

	friend const FPreparedType& PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression, const FRequestedType& RequestedType);

	FEmitShaderValues GetValueShader(FEmitContext& Context, const FRequestedType& RequestedType, const Shader::FType& ResultType);
	void GetValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader);
	Shader::FValue GetValueConstant(FEmitContext& Context, const FRequestedType& RequestedType);

	FEmitShaderValues GetValueShader(FEmitContext& Context, const FRequestedType& RequestedType);
	FEmitShaderValues GetValueShader(FEmitContext& Context);

protected:
	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const = 0;
	virtual void EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const;
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
	virtual void EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FEmitShaderValues& OutResult) const override;

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
//EExpressionEvaluation PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression); // friend of FExpression
//void PrepareScopeValues(FEmitContext& Context, const FScope* InScope); // friend of FScope

} // namespace HLSLTree
} // namespace UE
