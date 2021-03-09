// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HLSLTree/HLSLTree.h"

namespace UE
{
namespace HLSLTree
{

enum class EBinaryOp
{
	None,
	Add,
	Sub,
	Mul,
	Div,
};

class FExpressionConstant : public FExpression
{
public:
	explicit FExpressionConstant(const FConstant& InValue)
		: FExpression(InValue.Type), Value(InValue)
	{}

	FConstant Value;

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionLocalVariable : public FExpression
{
public:
	explicit FExpressionLocalVariable(FLocalDeclaration* InDeclaration) : FExpression(InDeclaration->Type), Declaration(InDeclaration) {}

	FLocalDeclaration* Declaration;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(Declaration);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionParameter : public FExpression
{
public:
	FExpressionParameter(FParameterDeclaration* InDeclaration)
		: FExpression(InDeclaration->DefaultValue.Type)
		, Declaration(InDeclaration)
	{}

	FParameterDeclaration* Declaration;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(Declaration);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

enum class EExternalInputType
{
	TexCoord0,
	TexCoord1,
	TexCoord2,
	TexCoord3,
	TexCoord4,
	TexCoord5,
	TexCoord6,
	TexCoord7,
};
inline EExpressionType GetInputExpressionType(EExternalInputType Type)
{
	return EExpressionType::Float2;
}
inline EExternalInputType MakeInputTexCoord(int32 Index)
{
	check(Index >= 0 && Index < 8);
	return (EExternalInputType)((int32)EExternalInputType::TexCoord0 + Index);
}

class FExpressionExternalInput : public FExpression
{
public:
	FExpressionExternalInput(EExternalInputType InInputType) : FExpression(GetInputExpressionType(InInputType)), InputType(InInputType) {}

	EExternalInputType InputType;

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionTextureSample : public FExpression
{
public:
	FExpressionTextureSample(FTextureParameterDeclaration* InDeclaration, FExpression* InTexCoordExpression)
		: FExpression(EExpressionType::Float4)
		, Declaration(InDeclaration)
		, TexCoordExpression(InTexCoordExpression)
		, SamplerSource(SSM_FromTextureAsset)
		, MipValueMode(TMVM_None)
	{}

	FTextureParameterDeclaration* Declaration;
	FExpression* TexCoordExpression;
	ESamplerSourceMode SamplerSource;
	ETextureMipValueMode MipValueMode;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(Declaration);
			Visitor.VisitNode(TexCoordExpression);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionDefaultMaterialAttributes : public FExpression
{
public:
	FExpressionDefaultMaterialAttributes() : FExpression(EExpressionType::MaterialAttributes) {}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionSetMaterialAttribute : public FExpression
{
public:
	FExpressionSetMaterialAttribute() : FExpression(EExpressionType::MaterialAttributes) {}

	FGuid AttributeID;
	FExpression* AttributesExpression;
	FExpression* ValueExpression;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(AttributesExpression);
			Visitor.VisitNode(ValueExpression);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionBinaryOp : public FExpression
{
public:
	FExpressionBinaryOp(EExpressionType ResultType, EBinaryOp InOp, FExpression* InLhs, FExpression* InRhs)
		: FExpression(ResultType)
		, Op(InOp)
		, Lhs(InLhs)
		, Rhs(InRhs)
	{}

	EBinaryOp Op;
	FExpression* Lhs;
	FExpression* Rhs;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(Lhs);
			Visitor.VisitNode(Rhs);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

struct FSwizzleParameters
{
	FSwizzleParameters() : NumComponents(0) { ComponentIndex[0] = ComponentIndex[1] = ComponentIndex[2] = ComponentIndex[3] = INDEX_NONE; }
	FSwizzleParameters(int8 IndexR, int8 IndexG, int8 IndexB, int8 IndexA);

	int8 ComponentIndex[4];
	int32 NumComponents;
};
FSwizzleParameters MakeSwizzleMask(bool bInR, bool bInG, bool bInB, bool bInA);

class FExpressionSwizzle : public FExpression
{
public:
	FExpressionSwizzle(const FSwizzleParameters& InParams, FExpression* InInput)
		: FExpression(MakeExpressionType(InInput->Type, InParams.NumComponents))
		, Parameters(InParams)
		, Input(InInput)
	{}

	FSwizzleParameters Parameters;
	FExpression* Input;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(Input);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionCast : public FExpression
{
public:
	FExpressionCast(EExpressionType InType, FExpression* InInput, ECastFlags InFlags = ECastFlags::None)
		: FExpression(InType)
		, Input(InInput)
		, Flags(InFlags)
	{}

	FExpression* Input;
	ECastFlags Flags;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(Input);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionFunctionInput : public FExpression
{
public:
	FExpressionFunctionInput(const FName& InName, EExpressionType InType, int32 InIndex)
		: FExpression(InType), Name(InName), InputIndex(InIndex)
	{}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;

	FName Name;
	int32 InputIndex;
};

class FExpressionFunctionOutput : public FExpression
{
public:
	FExpressionFunctionOutput(FFunctionCall* InFunctionCall, int32 InIndex)
		: FExpression(InFunctionCall->OutputTypes[InIndex])
		, FunctionCall(InFunctionCall)
		, OutputIndex(InIndex)
	{
		check(InIndex >= 0 && InIndex < InFunctionCall->NumOutputs);
	}

	FFunctionCall* FunctionCall;
	int32 OutputIndex;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(FunctionCall);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FStatementSetFunctionOutput : public FStatement
{
public:
	FExpression* Expression;
	FName Name;
	int32 OutputIndex;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FStatement::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(Expression);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const override;
};

class FStatementReturn : public FStatement
{
public:
	FExpression* Expression;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FStatement::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(Expression);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const override;
};

class FStatementSetLocalVariable : public FStatement
{
public:
	FLocalDeclaration* Declaration;
	FExpression* Expression;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FStatement::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(Declaration);
			Visitor.VisitNode(Expression);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const override;
};

class FStatementIf : public FStatement
{
public:
	FExpression* ConditionExpression;
	FScope* ThenScope;
	FScope* ElseScope;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FStatement::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(ConditionExpression);
			Visitor.VisitNode(ThenScope);
			Visitor.VisitNode(ElseScope);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const override;
};

class FStatementFor : public FStatement
{
public:
	FExpression* StartExpression;
	FExpression* EndExpression;
	FScope* LoopScope;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FStatement::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(StartExpression);
			Visitor.VisitNode(EndExpression);
			Visitor.VisitNode(LoopScope);
		}
		return Result;
	}

	virtual void EmitHLSL(FEmitContext& Context, FCodeWriter& Writer) const override;
};

}
}