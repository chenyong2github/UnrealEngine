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

} // namespace HLSLTree
} // namespace UE
