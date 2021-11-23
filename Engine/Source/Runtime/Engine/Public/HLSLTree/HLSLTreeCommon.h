// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HLSLTree/HLSLTree.h"

enum class EMaterialParameterType : uint8;

namespace UE
{

namespace Shader
{
enum class EPreshaderOpcode : uint8;
} // namespace Shader

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
	FBinaryOpDescription();
	FBinaryOpDescription(const TCHAR* InName, const TCHAR* InOperator, Shader::EPreshaderOpcode InOpcode);

	const TCHAR* Name;
	const TCHAR* Operator;
	Shader::EPreshaderOpcode PreshaderOpcode;
};

FBinaryOpDescription GetBinaryOpDesription(EBinaryOp Op);

class FExpressionConstant : public FExpression
{
public:
	explicit FExpressionConstant(const FConstantValue& InValue)
		: Value(InValue)
	{}

	FConstantValue Value;

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const override;
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

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const override;
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

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override { SetType(Context, EExpressionEvaluationType::Shader, GetInputExpressionType(InputType)); }
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
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

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
};

class FExpressionGetStructField : public FExpression
{
public:
	FExpressionGetStructField() {}

	const FStructType* StructType;
	const TCHAR* FieldName;
	FExpression* StructExpression;

	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(StructExpression);
		}
		return Result;
	}

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
};

class FExpressionSetStructField : public FExpression
{
public:
	FExpressionSetStructField() {}

	const FStructType* StructType;
	const TCHAR* FieldName;
	FExpression* StructExpression;
	FExpression* FieldExpression;
	
	virtual ENodeVisitResult Visit(FNodeVisitor& Visitor) override
	{
		const ENodeVisitResult Result = FExpression::Visit(Visitor);
		if (ShouldVisitDependentNodes(Result))
		{
			Visitor.VisitNode(StructExpression);
			Visitor.VisitNode(FieldExpression);
		}
		return Result;
	}

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
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

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const override;
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

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const override;
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

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const override;
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

	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const override;
};

class FExpressionReflectionVector : public FExpression
{
public:
	virtual void PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType) override;
	virtual void EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const override;
};

class FStatementReturn : public FStatement
{
public:
	//static constexpr bool MarkScopeLiveRecursive = true;
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

	virtual void PrepareValues(FEmitContext& Context) const override;
	virtual void EmitHLSL(FEmitContext& Context) const override;
};

class FStatementBreak : public FStatement
{
public:
	//static constexpr bool MarkScopeLive = true;

	virtual void PrepareValues(FEmitContext& Context) const override;
	virtual void EmitHLSL(FEmitContext& Context) const override;
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

	virtual void PrepareValues(FEmitContext& Context) const override;
	virtual void EmitHLSL(FEmitContext& Context) const override;
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

	virtual void PrepareValues(FEmitContext& Context) const override;
	virtual void EmitHLSL(FEmitContext& Context) const override;
};

}
}
