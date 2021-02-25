// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "HLSLTree/HLSLTreeTypes.h"

class FMaterial;
class FMaterialCompilationOutput;
class FMaterialPreshaderData;

/**
 * The HLSLTree module contains classes to build an HLSL AST (abstract syntax tree)
 * This allows C++ to procedurally define an HLSL program.  The structure of the tree is designed to be flexible, to facilitate incremental generation from a material node graph
 * Once the tree is complete, HLSL source code may be generated
 */
namespace UE
{
namespace HLSLTree
{

/** Allows building a string incrementally, with some additional features to support code generation, such as indent handling */
class FCodeWriter
{
public:
	static FCodeWriter* Create(FMemStackBase& Allocator);

	const FStringBuilderBase& GetStringBuilder() const { return *StringBuilder; }

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

	explicit FCodeWriter(FStringBuilderBase* InStringBuilder)
		: StringBuilder(InStringBuilder)
		, IndentLevel(0)
	{}

	void WriteIndent();

	FStringBuilderBase* StringBuilder;
	int32 IndentLevel;
};

/** Tracks shared state while emitting HLSL code */
class FEmitContext
{
public:
	/** Returns a snippet of HLSL source code that references the given expression */
	const TCHAR* AcquireHLSLReference(FExpression* Expression);

	/** Returns a snippet of HLSL source code that references the given local variablew */
	const TCHAR* AcquireHLSLReference(FLocalDeclaration* Declaration);

	struct FScopeEntry
	{
		const FScope* Scope;
		FCodeWriter* ExpressionCodeWriter;
	};

	struct FDeclarationEntry
	{
		const TCHAR* Definition;
	};

	FScopeEntry* FindScope(FScope* Scope);

	TArray<FScopeEntry> ScopeStack;
	TMap<FNode*, FDeclarationEntry> DeclarationMap;
	FMemStackBase* Allocator = nullptr;
	FMaterial* Material = nullptr; // TODO - remove preshader material dependency
	FMaterialCompilationOutput* MaterialCompilationOutput = nullptr;
	int32 NumExpressionLocals = 0;
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
	virtual ENodeVisitResult OnLocalDeclaration(FLocalDeclaration& Declaration) { return ENodeVisitResult::VisitDependentNodes; }
	virtual ENodeVisitResult OnParameterDeclaration(FParameterDeclaration& Declaration) { return ENodeVisitResult::VisitDependentNodes; }
	virtual ENodeVisitResult OnTextureParameterDeclaration(FTextureParameterDeclaration& Declaration) { return ENodeVisitResult::VisitDependentNodes; }
};

/** Root class of the HLSL AST */
class FNode
{
public:
	virtual ~FNode() {}

	static bool ShouldVisitDependentNodes(ENodeVisitResult Result) { return Result == ENodeVisitResult::VisitDependentNodes; }

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) = 0;

	FScope* ParentScope = nullptr;
};

/**
 * Represents an HLSL statement.  This is a piece of code that doesn't evaluate to any value, but instead should be executed sequentially, and likely has side-effects.
 * Examples include assigning a value, or various control flow structures (if, for, while, etc)
 * This is an abstract base class, with derived classes representing various types of statements
 */
class FStatement : public FNode
{
public:
	/** Emits HLSL code for the statement. The generated code should include any required semi-colons and newlines */
	virtual void EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const = 0;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	FStatement* NextStatement = nullptr;
};

/**
 * Represents an HLSL expression.  This is a piece of code that evaluates to a value, but has no side effects.
 * Unlike statements, expressions are not expected to execute in any particular order.  They may be cached (or not) in generated code, without the underlying implementation needing to care.
 * Expressions track the outer-most scope in which they're accessed. The generated HLSL code will ensure they are defined in that scope.
 * Examples include constant literals, variable accessors, and various types of math operations
 * This is an abstract base class, with derived classes representing various types of expression
 * Derived classes are expected to implement at least one of EmitHLSL or EmitPreshader
 */
class FExpression : public FNode
{
public:
	inline bool IsInline() const { return bInline; }

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	/* Emits HLSL code for the expression. The code should NOT include any newlines or semi-colons. The returned string may be assigned to a temporary variable, or embedded in another HLSL string */
	virtual void EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const;

	/* Emits bytecode for the preshader virtual machine */
	virtual void EmitPreshader(FEmitContext& Context, FMaterialPreshaderData& OutPreshader) const;

	/** The HLSL type of the expression (float, int, etc) */
	EExpressionType Type;

	EExpressionEvaluationType EvaluationType;
	bool bInline;

protected:
	explicit FExpression(EExpressionType InType,
		EExpressionEvaluationType InEvaluationType = EExpressionEvaluationType::Shader)
		: Type(InType)
		, EvaluationType(InEvaluationType)
		, bInline(false)
	{}
};

/**
 * Represents an HLSL local variable.  This is used by statements/expressions to refer to a particular variable
 */
class FLocalDeclaration final : public FNode
{
public:
	FLocalDeclaration(const FName& InName, EExpressionType InType) : Name(InName), Type(InType) {}

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	FName Name;
	EExpressionType Type;
};

/**
 * Represents an HLSL parameter.  This is a uniform constant passed to the generated shader
 */
class FParameterDeclaration final : public FNode
{
public:
	FParameterDeclaration(const FName& InName, const FConstant& InDefaultValue) : Name(InName), DefaultValue(InDefaultValue) {}

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	FName Name;
	FConstant DefaultValue;
};

/**
 * Represents an HLSL texture parameter.
 */
class FTextureParameterDeclaration : public FNode
{
public:
	FTextureParameterDeclaration(const FName& InName, const FTextureDescription& InDescription) : Name(InName), Description(InDescription) {}

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;

	FName Name;
	FTextureDescription Description;
};

/**
 * Represents an HLSL scope.  This is an ordered list of statements (enclosed by an {} pair in HLSL)
 * All HLSL nodes track which scopes they are accessed from
 */
class FScope final : public FNode
{
public:
	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override;
	void EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const;

	void AddDeclaration(FLocalDeclaration* Declaration);
	void AddExpression(FExpression* Expression);
	void AddStatement(FStatement* Statement);

	void UseDeclaration(FLocalDeclaration* Declaration);
	void UseExpression(FExpression* Expression);
	bool TryMoveStatement(FStatement* Statement);

private:
	friend class FTree;
	friend class FNodeVisitor_MoveToScope;

	void UseNode(FNode* Node);

	FScope* LinkedScope = nullptr;
	FStatement* FirstStatement = nullptr;
	FStatement* LastStatement = nullptr;
	int32 NestedLevel = 0;
};

/**
 * The HLSL AST.  Basically a wrapper around the root scope, with some helper methods
 */
class FTree
{
public:
	static FTree* Create(FMemStackBase& Allocator);

	void EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const;

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

	/**
	 * 2 scopes may be linked if they are both logically part of the same control structure (an 'if' scope and an 'else' scope are linked for example)
	 * Linked scopes always share the same parent scope
	 * Attempting to move a statement from a scope to its linked scope will instead move that statement into the parent scope
	 * When translating node graphs, a chain of statements may be generated when translating an 'if' scope for example
	 * Then when translating the 'else' scope, the flow may eventually hit a statement that was previously translated from the 'if' scope
	 * At this point, that statement connected to both 'if' and 'else' scope should become the next statement after the if/else block,
	 * which means it should be moved to the parent scope (the scope that contains the linked if/else scopes)
	 * It's not clear if this pattern will be useful outside if/else block
	 */
	FScope* NewLinkedScope(FScope& Scope);
	
	FLocalDeclaration* NewLocalDeclaration(FScope& Scope, EExpressionType Type, const FName& Name);
	FParameterDeclaration* NewParameterDeclaration(FScope& Scope, const FName& Name, const FConstant& DefaultValue);
	FTextureParameterDeclaration* NewTextureParameterDeclaration(FScope& Scope, const FName& Name, const FTextureDescription& DefaultValue);
private:
	template<typename T, typename... ArgTypes>
	inline T* NewNode(ArgTypes&&... Args)
	{
		T* Node = new(*Allocator) T(Forward<ArgTypes>(Args)...);
		return Node;
	}

	FMemStackBase* Allocator;
	FScope* RootScope;
};

} // namespace HLSLTree
} // namespace UE
