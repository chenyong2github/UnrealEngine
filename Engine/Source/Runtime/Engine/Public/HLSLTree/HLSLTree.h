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

class FExpressionLocalPHI;

static constexpr int32 MaxNumPreviousScopes = 2;

/** Allows building a string incrementally, with some additional features to support code generation, such as indent handling */
class FCodeWriter
{
public:
	static FCodeWriter* Create(FMemStackBase& Allocator);

	const FStringBuilderBase& GetStringBuilder() const { return *StringBuilder; }

	FSHAHash GetCodeHash() const;

	void IncreaseIndent();
	void DecreaseIndent();

	template <typename TextType>
	inline void Write(const TextType& Text)
	{
		StringBuilder->Append(Text);
	}

	template <typename FormatType, typename... ArgTypes>
	inline void Writef(const FormatType& Format, ArgTypes... Args)
	{
		StringBuilder->Appendf(Format, Args...);
	}

	template <typename TextType>
	inline void WriteLine(const TextType& Text)
	{
		WriteIndent();
		StringBuilder->Append(Text);
		StringBuilder->Append('\n');
	}

	template <typename FormatType, typename... ArgTypes>
	inline void WriteLinef(const FormatType& Format, ArgTypes... Args)
	{
		WriteIndent();
		StringBuilder->Appendf(Format, Args...);
		StringBuilder->Append('\n');
	}

	void WriteConstant(const Shader::FValue& Value);

	explicit FCodeWriter(FStringBuilderBase* InStringBuilder)
		: StringBuilder(InStringBuilder)
		, IndentLevel(0)
	{}

	void WriteIndent();

	void Reset();

	void Append(const FCodeWriter& InWriter);

	FStringBuilderBase* StringBuilder;
	int32 IndentLevel;
};

class FScope;
class FEmitScope;

class FEmitDeclaration : public TIntrusiveLinkedList<FEmitDeclaration>
{
public:
	const TCHAR* Declaration;
	const TCHAR* Value;
	Shader::EValueType Type;
};

class FEmitAssignment : public TIntrusiveLinkedList<FEmitAssignment>
{
public:
	const TCHAR* Declaration;
	FExpression* Expression;
};

class FEmitStatement : public TIntrusiveLinkedList<FEmitStatement>
{
public:
	FStringView Code;
};

class FEmitScopeLink : public TIntrusiveLinkedList<FEmitScopeLink>
{
public:
	FEmitScope* NextScope;
	FStringView Code;
};

class FEmitScope
{
public:
	FEmitScope* ParentScope = nullptr;
	FEmitDeclaration* FirstDeclaration = nullptr;
	FEmitAssignment* FirstAssignment = nullptr;
	FEmitStatement* FirstStatement = nullptr;
	FEmitStatement* LastStatement = nullptr;
	FEmitScopeLink* FirstLink = nullptr;
	FEmitScopeLink* LastLink = nullptr;
	const FScope* SourceScope = nullptr;
	TMap<FSHAHash, const TCHAR*> ExpressionMap;
};

class FEmitValue
{
public:
	EExpressionEvaluationType GetEvaluationType() const { return EvaluationType; }
	Shader::EValueType GetExpressionType() const { return ExpressionType; }
	const Shader::FValue& GetConstantValue() const { return ConstantValue; }

private:
	mutable const TCHAR* Code = nullptr;
	const Shader::FPreshaderData* Preshader = nullptr;
	EExpressionEvaluationType EvaluationType = EExpressionEvaluationType::None;
	Shader::EValueType ExpressionType = Shader::EValueType::Void;
	Shader::FValue ConstantValue;

	friend class FEmitContext;
};

/** Tracks shared state while emitting HLSL code */
class FEmitContext
{
public:
	FEmitContext();
	~FEmitContext();

	/** Returns a value that references the given expression */
	const FEmitValue* AcquireValue(FExpression* Expression);

	/** Returns value that references the given local function output */
	const FEmitValue* AcquireValue(FFunctionCall* FunctionCall, int32 OutputIndex);

	/** Get a unique local variable name */
	const TCHAR* AcquireLocalDeclarationCode();

	FStringView InternalAcquireInternedString(const TCHAR* String, int32 Length);

	FStringView AcquireInternedString(const TCHAR* Format, ...);

	template<int32 ArrayLength>
	FStringView AcquireInternedString(TCHAR (&String)[ArrayLength])
	{
		return InternalAcquireInternedString(String, ArrayLength);
	}

	/** Gets HLSL code that references the given value */
	const TCHAR* GetCode(const FEmitValue* Value) const;

	/** Append preshader bytecode that represents the given value */
	void AppendPreshader(const FEmitValue* Value, Shader::FPreshaderData& InOutPreshader) const;

	FEmitScope* FindScope(const FScope& Scope);
	FEmitScope* AcquireScope(const FScope& Scope);

	FEmitScope& GetCurrentScope();

	void InternalWriteStatementToScope(FEmitScope& EmitScope, FStringView InternedCode);
	bool InternalWriteScope(const FScope& Scope, FStringView InternedCode);
	FEmitScopeLink* InternalWriteScopeLink(FStringView InternedCode);

	template<typename StringType>
	void WriteStatementToScope(FEmitScope& EmitScope, const StringType& String)
	{
		InternalWriteStatementToScope(EmitScope, AcquireInternedString(String));
	}

	template<typename FormatType, typename... Types>
	void WriteStatementToScopef(FEmitScope& EmitScope, const FormatType& Format, Types... Args)
	{
		InternalWriteStatementToScope(EmitScope, AcquireInternedString(Format, Forward<Types>(Args)...));
	}

	template<typename StringType>
	void WriteStatement(const StringType& String)
	{
		InternalWriteStatementToScope(GetCurrentScope(), AcquireInternedString(String));
	}

	template<typename FormatType, typename... Types>
	void WriteStatementf(const FormatType& Format, Types... Args)
	{
		InternalWriteStatementToScope(GetCurrentScope(), AcquireInternedString(Format, Forward<Types>(Args)...));
	}

	bool WriteScope(const FScope& Scope)
	{
		return InternalWriteScope(Scope, FStringView());
	}

	template<typename FormatType, typename... Types>
	bool WriteScopef(const FScope& Scope, const FormatType& Format, Types... Args)
	{
		return InternalWriteScope(Scope, AcquireInternedString(Format, Forward<Types>(Args)...));
	}

	template<typename FormatType, typename... Types>
	void WriteScopeTerminatorf(const FormatType& Format, Types... Args)
	{
		InternalWriteScopeLink(AcquireInternedString(Format, Forward<Types>(Args)...));
	}

	void WriteDeclaration(FEmitScope& EmitScope, Shader::EValueType Type, const TCHAR* Declaration, const TCHAR* Value = nullptr);

	bool WriteAssignment(FEmitScope& EmitScope, const TCHAR* Declaration, FExpression* Expression, Shader::EValueType& InOutType);

	bool FinalizeScope(FEmitScope& EmitScope);

	struct FDeclarationEntry
	{
		FEmitValue Value;
	};

	struct FFunctionCallEntry
	{
		const FEmitValue* OutputValues;
		int32 NumOutputs;
	};

	struct FFunctionStackEntry
	{
		FFunctionCall* FunctionCall = nullptr;

		TMap<FNode*, FDeclarationEntry*> DeclarationMap;
		TMap<FFunctionCall*, FFunctionCallEntry*> FunctionCallMap;
	};

	TArray<FEmitScope*> ScopeStack;
	TMap<const FScope*, FEmitScope*> ScopeMap;

	TArray<FFunctionStackEntry> FunctionStack;
	TArray<Shader::FPreshaderData*> TempPreshaders;
	TSet<const FExpression*> PendingEmitValueExpressions;
	FMemStackBase* Allocator = nullptr;
	const FMaterial* Material = nullptr; // TODO - remove preshader material dependency
	const FStaticParameterSet* StaticParameters = nullptr;
	FMaterialCompilationOutput* MaterialCompilationOutput = nullptr;
	int32 TotalCodeLength = 0;
	int32 NumExpressionLocals = 0;
	int32 NumTexCoords = 0;
};

struct FExpressionEmitResult
{
	FExpressionEmitResult(FCodeWriter& InWriter, Shader::FPreshaderData& InPreshader)
		: Writer(InWriter)
		, Preshader(InPreshader)
		, EvaluationType(EExpressionEvaluationType::None)
		, Type(Shader::EValueType::Void)
		, bInline(false)
	{}

	void ForwardValue(FEmitContext& Context, const FEmitValue* InValue);

	FCodeWriter& Writer;
	Shader::FPreshaderData& Preshader;
	EExpressionEvaluationType EvaluationType;
	Shader::EValueType Type;
	bool bInline;
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
	virtual ENodeVisitResult OnParameterDeclaration(FParameterDeclaration& Declaration) { return ENodeVisitResult::VisitDependentNodes; }
	virtual ENodeVisitResult OnTextureParameterDeclaration(FTextureParameterDeclaration& Declaration) { return ENodeVisitResult::VisitDependentNodes; }
	virtual ENodeVisitResult OnFunctionCall(FFunctionCall& FunctionCall) { return ENodeVisitResult::VisitDependentNodes; }
};

/** Root class of the HLSL AST */
class FNode
{
public:
	virtual ~FNode() {}

	static bool ShouldVisitDependentNodes(ENodeVisitResult Result) { return Result == ENodeVisitResult::VisitDependentNodes; }

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) = 0;

	FScope* ParentScope = nullptr;
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
	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	/** Emits HLSL code for the statement. The generated code should include any required semi-colons and newlines */
	virtual bool EmitHLSL(FEmitContext& Context) const = 0;

	FStatement* NextStatement = nullptr;
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
	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	/** Emits code for the given expression, either HLSL code or preshader bytecode */
	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const = 0;
};

/**
 * Represents a phi node (see various topics on single static assignment)
 * A phi node takes on a value based on the previous scope that was executed.
 * In practice, this means the generated HLSL code will declare a local variable before all the previous scopes, then assign that variable the proper value from within each scope
 */
class FExpressionLocalPHI final : public FExpression
{
public:
	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;

	FName LocalName;
	FScope* Scopes[MaxNumPreviousScopes];
	FExpression* Values[MaxNumPreviousScopes];
	int32 NumValues;
};

/**
 * Represents an HLSL parameter.  This is a uniform constant passed to the generated shader
 */
class FParameterDeclaration final : public FNode
{
public:
	FParameterDeclaration(const FName& InName, const Shader::FValue& InDefaultValue) : Name(InName), DefaultValue(InDefaultValue) {}

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	FName Name;
	Shader::FValue DefaultValue;
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
 * Represents a call to an HLSL function from a separate tree
 */
class FFunctionCall final : public FNode
{
public:
	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	/** Root scope of the function to call. Note that this scope will be from a separate (external) tree */
	const FScope* FunctionScope;

	/** Outputs are expressions from the function's scope */
	FExpression* const* Outputs;

	/** Inputs are connected to the calling scope */
	FExpression* const* Inputs;
	
	int32 NumInputs;
	int32 NumOutputs;
};

/**
 * Represents an HLSL scope.  This is an ordered list of statements (enclosed by an {} pair in HLSL)
 * All HLSL nodes track which scopes they are accessed from
 */
class FScope final : public FNode
{
public:
	static FScope* FindSharedParent(FScope* Lhs, FScope* Rhs);

	inline TArrayView<FScope*> GetPreviousScopes() const
	{
		// const_cast needed, otherwise type of array view is 'FScope*const' which doesn't make sense
		return MakeArrayView(const_cast<FScope*>(this)->PreviousScope, NumPreviousScopes);
	}

	bool HasParentScope(const FScope& ParentScope) const;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;
	bool EmitHLSL(FEmitContext& Context, FEmitScope& Scope) const;

	void AddPreviousScope(FScope& Scope);

	void AddExpression(FExpression* Expression);
	void AddStatement(FStatement* Statement);

	void UseFunctionCall(FFunctionCall* FunctionCall);
	void UseExpression(FExpression* Expression);
private:
	friend class FTree;
	friend class FNodeVisitor_MoveToScope;

	void UseNode(FNode* Node);

	FScope* PreviousScope[MaxNumPreviousScopes];
	FStatement* FirstStatement = nullptr;
	FStatement* LastStatement = nullptr;
	int32 NumPreviousScopes = 0;
	int32 NestedLevel = 0;
};

/**
 * The HLSL AST.  Basically a wrapper around the root scope, with some helper methods
 */
class FTree
{
public:
	static FTree* Create(FMemStackBase& Allocator);
	static void Destroy(FTree* Tree);

	bool EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const;

	FScope& GetRootScope() const { return *RootScope; }

	template<typename T, typename... ArgTypes>
	inline T* NewExpression(FScope& Scope, ArgTypes&&... Args)
	{
		T* Expression = NewNode<T>(Forward<ArgTypes>(Args)...);
		Scope.AddExpression(Expression);
		return Expression;
	}

	template<typename T, typename... ArgTypes>
	inline T* NewStatement(FScope& Scope, ArgTypes&&... Args)
	{
		T* Statement = NewNode<T>(Forward<ArgTypes>(Args)...);
		Scope.AddStatement(Statement);
		return Statement;
	}

	FScope* NewScope(FScope& Scope);
	FParameterDeclaration* NewParameterDeclaration(FScope& Scope, const FName& Name, const Shader::FValue& DefaultValue);
	FTextureParameterDeclaration* NewTextureParameterDeclaration(FScope& Scope, const FName& Name, const FTextureDescription& DefaultValue);

	FFunctionCall* NewFunctionCall(FScope& Scope,
		const FScope& FunctionScope,
		FExpression* const* Inputs,
		FExpression* const* Outputs,
		int32 NumInputs,
		int32 NumOutputs);

private:
	template<typename T, typename... ArgTypes>
	inline T* NewNode(ArgTypes&&... Args)
	{
		T* Node = new(*Allocator) T(Forward<ArgTypes>(Args)...);
		Node->NextNode = Nodes;
		Nodes = Node;
		return Node;
	}

	FMemStackBase* Allocator;
	FNode* Nodes;
	FScope* RootScope;
};

} // namespace HLSLTree
} // namespace UE
