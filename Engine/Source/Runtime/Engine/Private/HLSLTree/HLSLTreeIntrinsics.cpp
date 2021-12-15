// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTree.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "Misc/MemStackUtility.h"
#include "Shader/ShaderTypes.h"
#include "Shader/Preshader.h"
#include "Misc/LargeWorldRenderPosition.h"

namespace UE
{
namespace HLSLTree
{

FUnaryOpDescription::FUnaryOpDescription()
	: Name(nullptr), Operator(nullptr), PreshaderOpcode(Shader::EPreshaderOpcode::Nop)
{}

FUnaryOpDescription::FUnaryOpDescription(const TCHAR* InName, const TCHAR* InOperator, Shader::EPreshaderOpcode InOpcode)
	: Name(InName), Operator(InOperator), PreshaderOpcode(InOpcode)
{}

FBinaryOpDescription::FBinaryOpDescription()
	: Name(nullptr), Operator(nullptr), PreshaderOpcode(Shader::EPreshaderOpcode::Nop)
{}

FBinaryOpDescription::FBinaryOpDescription(const TCHAR* InName, const TCHAR* InOperator, Shader::EPreshaderOpcode InOpcode)
	: Name(InName), Operator(InOperator), PreshaderOpcode(InOpcode)
{}

FUnaryOpDescription GetUnaryOpDesription(EUnaryOp Op)
{
	switch (Op)
	{
	case EUnaryOp::None: return FUnaryOpDescription(TEXT("None"), TEXT(""), Shader::EPreshaderOpcode::Nop); break;
	case EUnaryOp::Neg: return FUnaryOpDescription(TEXT("Neg"), TEXT("-"), Shader::EPreshaderOpcode::Neg); break;
	case EUnaryOp::Rcp: return FUnaryOpDescription(TEXT("Rcp"), TEXT("/"), Shader::EPreshaderOpcode::Rcp); break;
	default: checkNoEntry(); return FUnaryOpDescription();
	}
}

FBinaryOpDescription GetBinaryOpDesription(EBinaryOp Op)
{
	switch (Op)
	{
	case EBinaryOp::None: return FBinaryOpDescription(TEXT("None"), TEXT(""), Shader::EPreshaderOpcode::Nop); break;
	case EBinaryOp::Add: return FBinaryOpDescription(TEXT("Add"), TEXT("+"), Shader::EPreshaderOpcode::Add); break;
	case EBinaryOp::Sub: return FBinaryOpDescription(TEXT("Subtract"), TEXT("-"), Shader::EPreshaderOpcode::Sub); break;
	case EBinaryOp::Mul: return FBinaryOpDescription(TEXT("Multiply"), TEXT("*"), Shader::EPreshaderOpcode::Mul); break;
	case EBinaryOp::Div: return FBinaryOpDescription(TEXT("Divide"), TEXT("/"), Shader::EPreshaderOpcode::Div); break;
	case EBinaryOp::Less: return FBinaryOpDescription(TEXT("Less"), TEXT("<"), Shader::EPreshaderOpcode::Nop); break;
	default: checkNoEntry(); return FBinaryOpDescription();
	}
}

FEmitShaderCode* FEmitContext::EmitCast(FEmitShaderCode* ShaderValue, const Shader::FType& DestType)
{
	check(ShaderValue);
	check(!DestType.IsVoid());

	if (ShaderValue->Type == DestType)
	{
		return ShaderValue;
	}

	const Shader::FValueTypeDescription SourceTypeDesc = Shader::GetValueTypeDescription(ShaderValue->Type);
	const Shader::FValueTypeDescription DestTypeDesc = Shader::GetValueTypeDescription(DestType);

	TStringBuilder<1024> FormattedCode;
	Shader::FType IntermediateType = DestType;

	if (SourceTypeDesc.NumComponents > 0 && DestTypeDesc.NumComponents > 0)
	{
		const bool bIsSourceLWC = SourceTypeDesc.ComponentType == Shader::EValueComponentType::Double;
		const bool bIsLWC = DestTypeDesc.ComponentType == Shader::EValueComponentType::Double;

		if (bIsLWC != bIsSourceLWC)
		{
			if (bIsLWC)
			{
				// float->LWC
				ShaderValue = EmitCast(ShaderValue, Shader::MakeValueType(Shader::EValueComponentType::Float, DestTypeDesc.NumComponents));
				FormattedCode.Appendf(TEXT("LWCPromote(%s)"), ShaderValue->Reference);
			}
			else
			{
				//LWC->float
				FormattedCode.Appendf(TEXT("LWCToFloat(%s)"), ShaderValue->Reference);
				IntermediateType = Shader::MakeValueType(Shader::EValueComponentType::Float, SourceTypeDesc.NumComponents);
			}
		}
		else
		{
			const bool bReplicateScalar = (SourceTypeDesc.NumComponents == 1);

			int32 NumComponents = 0;
			bool bNeedClosingParen = false;
			if (bIsLWC)
			{
				FormattedCode.Append(TEXT("MakeLWCVector("));
				bNeedClosingParen = true;
			}
			else
			{
				if (SourceTypeDesc.NumComponents == 1 || SourceTypeDesc.NumComponents == DestTypeDesc.NumComponents)
				{
					NumComponents = DestTypeDesc.NumComponents;
					// Cast the scalar to the correct type, HLSL language will replicate the scalar if needed when performing this cast
					FormattedCode.Appendf(TEXT("((%s)%s)"), DestTypeDesc.Name, ShaderValue->Reference);
				}
				else
				{
					NumComponents = FMath::Min(SourceTypeDesc.NumComponents, DestTypeDesc.NumComponents);
					if (NumComponents < DestTypeDesc.NumComponents)
					{
						FormattedCode.Appendf(TEXT("%s("), DestTypeDesc.Name);
						bNeedClosingParen = true;
					}
					if (NumComponents == SourceTypeDesc.NumComponents && SourceTypeDesc.ComponentType == DestTypeDesc.ComponentType)
					{
						// If we're taking all the components from the source, can avoid adding a swizzle
						FormattedCode.Append(ShaderValue->Reference);
					}
					else
					{
						// Use a cast to truncate the source to the correct number of types
						const Shader::EValueType LocalType = Shader::MakeValueType(DestTypeDesc.ComponentType, NumComponents);
						FormattedCode.Appendf(TEXT("((%s)%s)"), Shader::GetValueTypeDescription(LocalType).Name, ShaderValue->Reference);
					}
				}
			}

			if (bNeedClosingParen)
			{
				const Shader::FValue ZeroValue(DestTypeDesc.ComponentType, 1);
				for (int32 ComponentIndex = NumComponents; ComponentIndex < DestTypeDesc.NumComponents; ++ComponentIndex)
				{
					if (ComponentIndex > 0u)
					{
						FormattedCode.Append(TEXT(","));
					}
					if (bIsLWC)
					{
						if (!bReplicateScalar && ComponentIndex >= SourceTypeDesc.NumComponents)
						{
							FormattedCode.Append(TEXT("LWCPromote(0.0f)"));
						}
						else
						{
							FormattedCode.Appendf(TEXT("LWCGetComponent(%s, %d)"), ShaderValue->Reference, bReplicateScalar ? 0 : ComponentIndex);
						}
					}
					else
					{
						// Non-LWC case should only be zero-filling here, other cases should have already been handled
						check(!bReplicateScalar);
						check(ComponentIndex >= SourceTypeDesc.NumComponents);
						ZeroValue.ToString(Shader::EValueStringFormat::HLSL, FormattedCode);
					}
				}
				NumComponents = DestTypeDesc.NumComponents;
				FormattedCode.Append(TEXT(")"));
			}

			check(NumComponents == DestTypeDesc.NumComponents);
		}
	}
	else
	{
		Errors.AddErrorf(nullptr, TEXT("Cannot cast between non-numeric types %s to %s."), SourceTypeDesc.Name, DestTypeDesc.Name);
		FormattedCode.Appendf(TEXT("((%s)0)"), DestType.GetName());
	}

	check(IntermediateType != ShaderValue->Type);
	const bool bInline = true;
	ShaderValue = EmitCodeInternal(IntermediateType, FormattedCode.ToView(), bInline, MakeArrayView(&ShaderValue, 1));
	if (ShaderValue->Type != DestType)
	{
		// May need to cast through multiple intermediate types to reach our destination type
		ShaderValue = EmitCast(ShaderValue, DestType);
	}
	return ShaderValue;
}

FEmitShaderValues FEmitContext::EmitCast(FEmitShaderValues ShaderValue, const Shader::FType& DestType)
{
	FEmitShaderValues Result;
	Result.Code = EmitCast(ShaderValue.Code, DestType);
	if (ShaderValue.HasDerivatives())
	{
		const Shader::FType DerivativeType = DestType.GetDerivativeType();
		Result.CodeDdx = EmitCast(ShaderValue.CodeDdx, DerivativeType);
		Result.CodeDdy = EmitCast(ShaderValue.CodeDdy, DerivativeType);
	}
	return Result;
}

FEmitShaderCode* FEmitContext::EmitUnaryOp(EUnaryOp Op, FEmitShaderCode* Input)
{
	const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(Input->Type);
	const Shader::EValueComponentType InputComponentType = InputTypeDesc.ComponentType;
	const int8 NumComponents = InputTypeDesc.NumComponents;
	Shader::EValueType ResultType = Input->Type;

	FEmitShaderCode* Result = nullptr;
	switch (Op)
	{
	case EUnaryOp::Neg:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			Result = EmitCode(ResultType, TEXT("LWCNegate(%)"), Input);
		}
		else
		{
			Result = EmitInlineCode(ResultType, TEXT("(-%)"), Input);
		}
		break;
	case EUnaryOp::Rcp:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			ResultType = Shader::MakeValueType(Shader::EValueComponentType::Float, NumComponents);
			Result = EmitCode(ResultType, TEXT("LWCRcp(%)"), Input);
		}
		else
		{
			Result = EmitCode(ResultType, TEXT("rcp(%)"), Input);
		}
		break;
	default:
		checkNoEntry();
		break;
	}
	return Result;
}

FEmitShaderValues FEmitContext::EmitUnaryOp(EUnaryOp Op, FEmitShaderValues Input, EExpressionDerivative Derivative)
{
	FEmitShaderValues Result;
	Result.Code = EmitUnaryOp(Op, Input.Code);
	if (Derivative == EExpressionDerivative::Valid && Input.HasDerivatives())
	{
		switch (Op)
		{
		case EUnaryOp::Neg:
			Result.CodeDdx = EmitNeg(Input.CodeDdx);
			Result.CodeDdy = EmitNeg(Input.CodeDdy);
			break;
		case EUnaryOp::Rcp:
		{
			FEmitShaderCode* dFdA = EmitNeg(EmitMul(Result.Code, Result.Code));
			Result.CodeDdx = EmitMul(dFdA, Input.CodeDdx);
			Result.CodeDdy = EmitMul(dFdA, Input.CodeDdy);
			break;
		}
		default:
			checkNoEntry();
			break;
		}
	}
	return Result;
}

FEmitShaderCode* FEmitContext::EmitBinaryOp(EBinaryOp Op, FEmitShaderCode* Lhs, FEmitShaderCode* Rhs)
{
	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(Lhs->Type);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(Rhs->Type);
	const int8 NumComponents = (LhsTypeDesc.NumComponents == 1 || RhsTypeDesc.NumComponents == 1) ? FMath::Max(LhsTypeDesc.NumComponents, RhsTypeDesc.NumComponents) : FMath::Min(LhsTypeDesc.NumComponents, RhsTypeDesc.NumComponents);
	const Shader::EValueComponentType InputComponentType = Shader::CombineComponentTypes(LhsTypeDesc.ComponentType, RhsTypeDesc.ComponentType);
	const Shader::EValueType InputType = Shader::MakeValueType(InputComponentType, NumComponents);
	Shader::EValueType ResultType = InputType;

	FEmitShaderCode* LhsCast = EmitCast(Lhs, InputType);
	FEmitShaderCode* RhsCast = EmitCast(Rhs, InputType);

	FEmitShaderCode* Result = nullptr;
	switch (Op)
	{
	case EBinaryOp::Add:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			Result = EmitCode(ResultType, TEXT("LWCAdd(%, %)"), LhsCast, RhsCast);
		}
		else
		{
			Result = EmitCode(ResultType, TEXT("(% + %)"), LhsCast, RhsCast);
		}
		break;
	case EBinaryOp::Sub:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			Result = EmitCode(ResultType, TEXT("LWCSubtract(%, %)"), LhsCast, RhsCast);
		}
		else
		{
			Result = EmitCode(ResultType, TEXT("(% - %)"), LhsCast, RhsCast);
		}
		break;
	case EBinaryOp::Mul:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			Result = EmitCode(ResultType, TEXT("LWCMultiply(%, %)"), LhsCast, RhsCast);
		}
		else
		{
			Result = EmitCode(ResultType, TEXT("(% * %)"), LhsCast, RhsCast);
		}
		break;
	case EBinaryOp::Div:
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			Result = EmitCode(ResultType, TEXT("LWCDivide(%, %)"), LhsCast, RhsCast);
		}
		else
		{
			Result = EmitCode(ResultType, TEXT("(% / %)"), LhsCast, RhsCast);
		}
		break;
	case EBinaryOp::Less:
		ResultType = Shader::MakeValueType(Shader::EValueComponentType::Bool, NumComponents);
		if (InputComponentType == Shader::EValueComponentType::Double)
		{
			Result = EmitCode(ResultType, TEXT("LWCLess(%, %)"), LhsCast, RhsCast);
		}
		else
		{
			Result = EmitCode(ResultType, TEXT("(% < %)"), LhsCast, RhsCast);
		}
		break;
	default:
		checkNoEntry();
		break;
	}

	return Result;
}

FEmitShaderValues FEmitContext::EmitBinaryOp(EBinaryOp Op, FEmitShaderValues Lhs, FEmitShaderValues Rhs, EExpressionDerivative Derivative)
{
	FEmitShaderValues Result;
	Result.Code = EmitBinaryOp(Op, Lhs.Code, Rhs.Code);
	if (Derivative == EExpressionDerivative::Valid && Lhs.HasDerivatives() && Rhs.HasDerivatives())
	{
		const Shader::FValueTypeDescription ResultTypeDesc = Shader::GetValueTypeDescription(Result.Code->Type);
		const Shader::EValueType DerivativeType = Shader::MakeValueType(Shader::EValueComponentType::Float, ResultTypeDesc.NumComponents);

		switch (Op)
		{
		case EBinaryOp::Add:
			Result.CodeDdx = EmitAdd(Lhs.CodeDdx, Rhs.CodeDdx);
			Result.CodeDdy = EmitAdd(Lhs.CodeDdy, Rhs.CodeDdy);
			break;
		case EBinaryOp::Sub:
			Result.CodeDdx = EmitSub(Lhs.CodeDdx, Rhs.CodeDdx);
			Result.CodeDdy = EmitSub(Lhs.CodeDdy, Rhs.CodeDdy);
			break;
		case EBinaryOp::Mul:
			Result.CodeDdx = EmitAdd(EmitMul(Lhs.CodeDdx, Rhs.Code), EmitMul(Rhs.CodeDdx, Lhs.Code));
			Result.CodeDdy = EmitAdd(EmitMul(Lhs.CodeDdy, Rhs.Code), EmitMul(Rhs.CodeDdy, Lhs.Code));
			break;
		case EBinaryOp::Div:
		{
			FEmitShaderCode* Denom = EmitRcp(EmitMul(Rhs.Code, Rhs.Code));
			FEmitShaderCode* dFdA = EmitMul(Rhs.Code, Denom);
			FEmitShaderCode* dFdB = EmitNeg(EmitMul(Lhs.Code, Denom));
			Result.CodeDdx = EmitAdd(EmitMul(dFdA, Lhs.CodeDdx), EmitMul(dFdB, Rhs.CodeDdx));
			Result.CodeDdy = EmitAdd(EmitMul(dFdA, Lhs.CodeDdy), EmitMul(dFdB, Rhs.CodeDdy));
			break;
		}
		default:
			checkNoEntry();
			break;
		}
	}

	return Result;
}

} // namespace HLSLTree
} // namespace UE
