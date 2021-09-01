// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HLSLTree/HLSLTree.h"

enum class EMaterialParameterType : uint8;

namespace UE
{
namespace HLSLTree
{

enum class EBinaryOp : uint8
{
	None,
	Add,
	Sub,
	Mul,
	Div,
	Less,
};

struct FBinaryOpDescription
{
	FBinaryOpDescription()
		: Name(nullptr), Operator(nullptr)
	{}

	FBinaryOpDescription(const TCHAR* InName, const TCHAR* InOperator)
		: Name(InName), Operator(InOperator)
	{}

	const TCHAR* Name;
	const TCHAR* Operator;
};

FBinaryOpDescription GetBinaryOpDesription(EBinaryOp Op);

class FExpressionConstant : public FExpression
{
public:
	explicit FExpressionConstant(const Shader::FValue& InValue)
		: Value(InValue)
	{}

	Shader::FValue Value;

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionMaterialParameter : public FExpression
{
public:
	explicit FExpressionMaterialParameter(EMaterialParameterType InType, const FName& InName, const Shader::FValue& InDefaultValue)
		: ParameterName(InName), DefaultValue(InDefaultValue), ParameterType(InType)
	{}

	FName ParameterName;
	Shader::FValue DefaultValue;
	EMaterialParameterType ParameterType;

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
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
inline Shader::EValueType GetInputExpressionType(EExternalInputType Type)
{
	return Shader::EValueType::Float2;
}
inline EExternalInputType MakeInputTexCoord(int32 Index)
{
	check(Index >= 0 && Index < 8);
	return (EExternalInputType)((int32)EExternalInputType::TexCoord0 + Index);
}

class FExpressionExternalInput : public FExpression
{
public:
	FExpressionExternalInput(EExternalInputType InInputType) : InputType(InInputType) {}

	EExternalInputType InputType;

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionTextureSample : public FExpression
{
public:
	FExpressionTextureSample(FTextureParameterDeclaration* InDeclaration, FExpression* InTexCoordExpression)
		: Declaration(InDeclaration)
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

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionDefaultMaterialAttributes : public FExpression
{
public:
	FExpressionDefaultMaterialAttributes() {}

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionSetMaterialAttribute : public FExpression
{
public:
	FExpressionSetMaterialAttribute() {}

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

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionSelect : public FExpression
{
public:
	FExpressionSelect(FExpression* InCondition, FExpression* InTrue, FExpression* InFalse)
		: ConditionExpression(InCondition)
		, TrueExpression(InTrue)
		, FalseExpression(InFalse)
	{}

	FExpression* ConditionExpression;
	FExpression* TrueExpression;
	FExpression* FalseExpression;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(ConditionExpression);
			Visitor.VisitNode(TrueExpression);
			Visitor.VisitNode(FalseExpression);
		}
		return Result;
	}

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionBinaryOp : public FExpression
{
public:
	FExpressionBinaryOp(EBinaryOp InOp, FExpression* InLhs, FExpression* InRhs)
		: Op(InOp)
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

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
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
		: Parameters(InParams)
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

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionAppend : public FExpression
{
public:
	FExpressionAppend(FExpression* InLhs, FExpression* InRhs)
		: Lhs(InLhs)
		, Rhs(InRhs)
	{}

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

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionCast : public FExpression
{
public:
	FExpressionCast(Shader::EValueType InType, FExpression* InInput, ECastFlags InFlags = ECastFlags::None)
		: Type(InType)
		, Input(InInput)
		, Flags(InFlags)
	{}

	Shader::EValueType Type;
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

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionReflectionVector : public FExpression
{
public:
	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
};

class FExpressionFunctionInput : public FExpression
{
public:
	FExpressionFunctionInput(const FName& InName, Shader::EValueType InType, int32 InIndex)
		: Name(InName), Type(InType), InputIndex(InIndex)
	{}

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;

	FName Name;
	Shader::EValueType Type;
	int32 InputIndex;
};

class FExpressionFunctionOutput : public FExpression
{
public:
	FExpressionFunctionOutput(FFunctionCall* InFunctionCall, int32 InIndex)
		: FunctionCall(InFunctionCall)
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

	virtual bool EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const override;
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

	virtual bool EmitHLSL(FEmitContext& Context) const override;
};

class FStatementBreak : public FStatement
{
public:
	virtual bool EmitHLSL(FEmitContext& Context) const override;
};

class FStatementIf : public FStatement
{
public:
	FExpression* ConditionExpression;
	FScope* ThenScope;
	FScope* ElseScope;
	FScope* NextScope;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FStatement::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(ConditionExpression);
			Visitor.VisitNode(ThenScope);
			Visitor.VisitNode(ElseScope);
			Visitor.VisitNode(NextScope);
		}
		return Result;
	}

	virtual bool EmitHLSL(FEmitContext& Context) const override;
};

class FStatementLoop : public FStatement
{
public:
	FScope* LoopScope;
	FScope* NextScope;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FStatement::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(LoopScope);
			Visitor.VisitNode(NextScope);
		}
		return Result;
	}

	virtual bool EmitHLSL(FEmitContext& Context) const override;
};

}
}
