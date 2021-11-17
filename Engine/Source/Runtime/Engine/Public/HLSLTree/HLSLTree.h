// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
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

	bool AddError(const FNode* InNode, FStringView InError);

	template<typename FormatType, typename... Types>
	bool AddErrorf(const FNode* InNode, const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		String.Appendf(Format, Forward<Types>(Args)...);
		return AddError(InNode, FStringView(String.GetData(), String.Len()));
	}

private:
	FMemStackBase* Allocator = nullptr;
	const FError* FirstError = nullptr;
	int32 NumErrors = 0;
};

class FUpdateTypeContext
{
public:
	explicit FUpdateTypeContext(FErrors& InErrors) : Errors(InErrors) {}

	FErrors& Errors;
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

	const TCHAR* CastShaderValue(const FNode* Node, const TCHAR* Code, Shader::EValueType SourceType, Shader::EValueType DestType, ECastFlags Flags);

	void AddPreshader(Shader::EValueType Type, const Shader::FPreshaderData& Preshader, FStringBuilderBase& OutCode);

	FMemStackBase* Allocator = nullptr;
	FErrors Errors;

	// TODO - remove preshader material dependency
	const FMaterial* Material = nullptr;
	const FStaticParameterSet* StaticParameters = nullptr;
	FMaterialCompilationOutput* MaterialCompilationOutput = nullptr;
	TMap<Shader::FValue, uint32> DefaultUniformValues;
	uint32 UniformPreshaderOffset = 0u;

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

	virtual void RequestTypes(FUpdateTypeContext& Context) const = 0;
	virtual void EmitHLSL(FEmitContext& Context) const = 0;

	FScope* ParentScope = nullptr;
	bool bEmitHLSL = false;
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
	Shader::EValueType GetValueType() const { return ValueType; }

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	friend Shader::EValueType RequestExpressionType(FUpdateTypeContext& Context, FExpression* InExpression, int8 InRequestedNumComponents);
	friend EExpressionEvaluationType PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression);

	const TCHAR* GetValueShader(FEmitContext& Context);
	const TCHAR* GetValueShader(FEmitContext& Context, Shader::EValueType Type);
	void GetValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader);
	Shader::FValue GetValueConstant(FEmitContext& Context);

	FScope* ParentScope = nullptr;

protected:
	virtual bool UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents) = 0;
	virtual bool PrepareValue(FEmitContext& Context) = 0;

	bool SetType(FUpdateTypeContext& Context, Shader::EValueType InType) { ValueType = InType; return true; }

	bool InternalSetValueShader(FEmitContext& Context, const TCHAR* InCode, int32 InLength, bool bInline);

	bool SetValuePreshader(FEmitContext& Context, Shader::FPreshaderData& InPreshader);
	bool SetValueConstant(FEmitContext& Context, const Shader::FValue& InValue);
	bool SetValueForward(FEmitContext& Context, FExpression* Source);

	bool SetValuePreshader(FEmitContext& Context, EExpressionEvaluationType InEvaluationType, Shader::FPreshaderData& InPreshader);

	template<typename FormatType, typename... Types>
	bool SetValueShaderf(FEmitContext& Context, const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		String.Appendf(Format, Forward<Types>(Args)...);
		return InternalSetValueShader(Context, String.GetData(), String.Len(), false);
	}

	template<typename FormatType, typename... Types>
	bool SetValueInlineShaderf(FEmitContext& Context, const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		String.Appendf(Format, Forward<Types>(Args)...);
		return InternalSetValueShader(Context, String.GetData(), String.Len(), true);
	}

	inline bool SetValueShader(FEmitContext& Context, const FStringBuilderBase& String) { return InternalSetValueShader(Context, String.GetData(), String.Len(), false); }
	inline bool SetValueInlineShader(FEmitContext& Context, const FStringBuilderBase& String) { return InternalSetValueShader(Context, String.GetData(), String.Len(), true); }

private:
	const TCHAR* LocalVariableName = nullptr;
	const TCHAR* Code = nullptr;
	const Shader::FPreshaderData* Preshader = nullptr;
	Shader::FValue ConstantValue;
	EExpressionEvaluationType EvaluationType = EExpressionEvaluationType::None;
	Shader::EValueType ValueType = Shader::EValueType::Void;
	int8 RequestedNumComponents = 0;
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
	virtual bool UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents) override;
	virtual bool PrepareValue(FEmitContext& Context) override;

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

/**
 * Represents an HLSL scope.  A scope contains a single statement, along with any expressions required by that statement
 */
class FScope final : public FNode
{
public:
	static FScope* FindSharedParent(FScope* Lhs, FScope* Rhs);

	inline FScope* GetParentScope() const { return ParentScope; }
	inline bool IsLive() const { return bLive; }

	inline TArrayView<FScope*> GetPreviousScopes() const
	{
		// const_cast needed, otherwise type of array view is 'FScope*const' which doesn't make sense
		return MakeArrayView(const_cast<FScope*>(this)->PreviousScope, NumPreviousScopes);
	}

	bool HasParentScope(const FScope& ParentScope) const;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	void AddPreviousScope(FScope& Scope);

	void UseExpression(FExpression* Expression);

	friend void RequestScopeTypes(FUpdateTypeContext& Context, const FScope* InScope);

	template<typename FormatType, typename... Types>
	void EmitDeclarationf(FEmitContext& Context, const FormatType& Format, Types... Args)
	{
		InternalEmitCodef(Context, Declarations, nullptr, Format, Forward<Types>(Args)...);
	}

	template<typename FormatType, typename... Types>
	void EmitStatementf(FEmitContext& Context, const FormatType& Format, Types... Args)
	{
		InternalEmitCodef(Context, Statements, nullptr, Format, Forward<Types>(Args)...);
	}

	void EmitNestedScope(FEmitContext& Context, FScope* NestedScope)
	{
		InternalEmitCode(Context, Statements, NestedScope, nullptr, 0);
	}

	template<typename FormatType, typename... Types>
	void EmitNestedScopef(FEmitContext& Context, FScope* NestedScope, const FormatType& Format, Types... Args)
	{
		InternalEmitCodef(Context, Statements, NestedScope, Format, Forward<Types>(Args)...);
	}

	void MarkLive();
	void MarkLiveRecursive();

	void WriteHLSL(int32 Indent, FStringBuilderBase& OutString) const;

private:
	friend class FTree;
	friend class FExpression;
	friend class FNodeVisitor_MoveToScope;

	struct FCodeEntry
	{
		FCodeEntry* Next;
		FScope* NestedScope;
		int32 Length;
		TCHAR String[1];
	};

	struct FCodeList
	{
		FCodeEntry* First = nullptr;
		FCodeEntry* Last = nullptr;
		int32 Num = 0;
	};

	void InternalEmitCode(FEmitContext& Context, FCodeList& List, FScope* NestedScope, const TCHAR* String, int32 Length);

	template<typename FormatType, typename... Types>
	void InternalEmitCodef(FEmitContext& Context, FCodeList& List, FScope* NestedScope, const FormatType& Format, Types... Args)
	{
		TStringBuilder<2048> String;
		String.Appendf(Format, Forward<Types>(Args)...);
		InternalEmitCode(Context, List, NestedScope, String.GetData(), String.Len());
	}

	FScope* ParentScope = nullptr;
	FStatement* Statement = nullptr;
	FScope* PreviousScope[MaxNumPreviousScopes];
	TMap<FSHAHash, const TCHAR*> ExpressionCodeMap;
	FCodeList Declarations;
	FCodeList Statements;
	int32 NumPreviousScopes = 0;
	int32 NestedLevel = 0;
	bool bLive = false;
};

/**
 * The HLSL AST.  Basically a wrapper around the root scope, with some helper methods
 */
class FTree
{
public:
	static FTree* Create(FMemStackBase& Allocator);
	static void Destroy(FTree* Tree);

	FMemStackBase& GetAllocator() { return *Allocator; }

	bool EmitHLSL(FEmitContext& Context, FStringBuilderBase& Writer) const;

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
	FScope* RootScope = nullptr;
	FStatement* ResultStatement = nullptr;
};

Shader::EValueType RequestExpressionType(FUpdateTypeContext& Context, FExpression* InExpression, int8 InRequestedNumComponents); // friend of FExpression
EExpressionEvaluationType PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression); // friend of FExpression
void RequestScopeTypes(FUpdateTypeContext& Context, const FScope* InScope); // friend of FScope

} // namespace HLSLTree
} // namespace UE
