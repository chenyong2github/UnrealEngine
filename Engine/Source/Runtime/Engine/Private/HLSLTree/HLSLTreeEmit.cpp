// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeEmit.h"
#include "HLSLTree/HLSLTree.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "Misc/MemStackUtility.h"
#include "Shader/ShaderTypes.h"
#include "Shader/Preshader.h"
#include "MaterialShared.h" // TODO - split preshader out into its own module
#include "Misc/LargeWorldRenderPosition.h"

namespace UE
{
namespace HLSLTree
{

FEmitShaderNode::FEmitShaderNode(FEmitScope& InScope, TArrayView<FEmitShaderNode*> InDependencies)
	: Scope(&InScope)
	, Dependencies(InDependencies)
{
	for (FEmitShaderNode* Dependency : InDependencies)
	{
		check(Dependency);
	}
}

namespace Private
{

void WriteIndent(int32 IndentLevel, FStringBuilderBase& InOutString)
{
	const int32 Offset = InOutString.AddUninitialized(IndentLevel);
	TCHAR* Result = InOutString.GetData() + Offset;
	for (int32 i = 0; i < IndentLevel; ++i)
	{
		*Result++ = TCHAR('\t');
	}
}

void EmitShaderCode(FEmitShaderNode* EmitNode, FEmitShaderScopeStack& Stack)
{
	if (EmitNode && EmitNode->Scope)
	{
		const FEmitScope* Scope = EmitNode->Scope;
		FEmitShaderScopeEntry EmitEntry;
		for (int32 Index = Stack.Num() - 1; Index >= 0; --Index)
		{
			FEmitShaderScopeEntry& CheckEntry = Stack[Index];
			if (CheckEntry.Scope == Scope)
			{
				EmitEntry = CheckEntry;
				break;
			}
		}

		// LocalPHI can sometimes generate circular dependency on expressions that execute in the future
		// Should revist this once dependencies are cleaned up
		if (EmitEntry.Code)
		{
			EmitNode->Scope = nullptr; // only emit code once
			for (FEmitShaderNode* Dependency : EmitNode->Dependencies)
			{
				Private::EmitShaderCode(Dependency, Stack);
			}
			EmitNode->EmitShaderCode(Stack, EmitEntry.Indent, *EmitEntry.Code);
		}
	}
}

} // namespace Private

void FEmitShaderExpression::EmitShaderCode(FEmitShaderScopeStack& Stack, int32 Indent, FStringBuilderBase& OutString)
{
	// Don't need a declaration for inline values
	if (!IsInline())
	{
		Private::WriteIndent(Indent, OutString);
		OutString.Appendf(TEXT("const %s %s = %s;\n"), Type.GetName(), Reference, Value);
	}
}

void FEmitShaderStatement::EmitShaderCode(FEmitShaderScopeStack& Stack, int32 Indent, FStringBuilderBase& OutString)
{
	TStringBuilder<2048> ScopeCode;
	for (int32 i = 0; i < 2; ++i)
	{
		bool bNeedToCloseScope = false;
		int32 NestedScopeIndent = Indent;

		if (Code[i].Len() > 0)
		{
			Private::WriteIndent(Indent, ScopeCode);
			ScopeCode.Append(Code[i]);
			ScopeCode.AppendChar(TEXT('\n'));

			// If ScopeFormat is set to 'Scoped', we need to emit an empty {}, even if our NestedScope is nullptr
			if (ScopeFormat == EEmitScopeFormat::Scoped)
			{
				Private::WriteIndent(Indent, ScopeCode);
				ScopeCode.Append(TEXT("{\n"));
				bNeedToCloseScope = true;
				NestedScopeIndent++;
			}
		}

		FEmitScope* NestedScope = NestedScopes[i];
		if (NestedScope)
		{
			Stack.Emplace(NestedScopes[i], NestedScopeIndent, ScopeCode);
			NestedScope->EmitShaderCode(Stack);
			Stack.Pop(false);
		}

		if (bNeedToCloseScope)
		{
			Private::WriteIndent(Indent, ScopeCode);
			ScopeCode.Append(TEXT("}\n"));
		}
	}
	OutString.Append(ScopeCode.ToView());
}

void FEmitScope::EmitShaderCode(FEmitShaderScopeStack& Stack)
{
	FEmitShaderNode* EmitNode = FirstNode;
	while (EmitNode)
	{
		Private::EmitShaderCode(EmitNode, Stack);
		EmitNode = EmitNode->NextScopedNode;
	}
}

FEmitScope* FEmitScope::FindSharedParent(FEmitScope* Lhs, FEmitScope* Rhs)
{
	FEmitScope* Scope0 = Lhs;
	FEmitScope* Scope1 = Rhs;
	if (Scope1)
	{
		while (Scope0 != Scope1)
		{
			if (Scope0->NestedLevel > Scope1->NestedLevel)
			{
				check(Scope0->ParentScope);
				Scope0 = Scope0->ParentScope;
			}
			else
			{
				check(Scope1->ParentScope);
				Scope1 = Scope1->ParentScope;
			}
		}
	}
	return Scope0;
}

FEmitContext::FEmitContext(FMemStackBase& InAllocator, FErrorHandlerInterface& InErrors, const Shader::FStructTypeRegistry& InTypeRegistry)
	: Allocator(&InAllocator)
	, Errors(&InErrors)
	, TypeRegistry(&InTypeRegistry)
{
}

FEmitContext::~FEmitContext()
{
}

const FPreparedType& FEmitContext::PrepareExpression(FExpression* InExpression, const FRequestedType& RequestedType)
{
	static FPreparedType VoidType;
	if (!InExpression)
	{
		return VoidType;
	}

	FOwnerScope OwnerScope(*Errors, InExpression->GetOwner());
	if (InExpression->bReentryFlag)
	{
		// Valid for this to be called reentrantly
		// Code should ensure that the type is set before the reentrant call, otherwise type will not be valid here
		// LocalPHI nodes rely on this to break loops
		return InExpression->PrepareValueResult.GetPreparedType();
	}

	bool bNeedToUpdateType = false;
	if (InExpression->CurrentRequestedType.RequestedComponents.Num() == 0)
	{
		InExpression->CurrentRequestedType = RequestedType;
		bNeedToUpdateType = !RequestedType.IsVoid();
	}
	else if (InExpression->CurrentRequestedType.GetStructType() != RequestedType.GetStructType())
	{
		Errors->AddError(TEXT("Type mismatch"));
		return VoidType;
	}
	else
	{
		const int32 NumComponents = RequestedType.GetNumComponents();
		InExpression->CurrentRequestedType.RequestedComponents.PadToNum(NumComponents, false);
		for (int32 Index = 0; Index < NumComponents; ++Index)
		{
			const bool bPrevRequest = InExpression->CurrentRequestedType.IsComponentRequested(Index);
			const bool bRequest = RequestedType.IsComponentRequested(Index);
			if (!bPrevRequest && bRequest)
			{
				InExpression->CurrentRequestedType.SetComponentRequest(Index);
				bNeedToUpdateType = true;
			}
		}
	}

	if (bNeedToUpdateType)
	{
		check(!InExpression->CurrentRequestedType.IsVoid());

		bool bResult = false;
		{
			FExpressionReentryScope ReentryScope(InExpression);
			bResult = InExpression->PrepareValue(*this, InExpression->CurrentRequestedType, InExpression->PrepareValueResult);
		}

		if (!bResult)
		{
			// If we failed to assign a valid type, reset the requested type as well
			// This ensures we'll try to compute a type again the next time we're called
			InExpression->CurrentRequestedType.Reset();
			InExpression->PrepareValueResult.SetTypeVoid();
		}
		else
		{
			check(!InExpression->PrepareValueResult.GetPreparedType().IsVoid());
		}
	}

	return InExpression->PrepareValueResult.GetPreparedType();
}

FEmitScope* FEmitContext::InternalPrepareScope(FScope* Scope, FScope* ParentScope, bool bMarkDead)
{
	FEmitScope* EmitScope = nullptr;
	if (Scope)
	{
		FEmitScope* EmitParentScope = InternalPrepareScope(ParentScope, ParentScope ? ParentScope->ParentScope : nullptr, false);
		EmitScope = AcquireEmitScopeWithParent(Scope, EmitParentScope);

		if (bMarkDead || (EmitParentScope && EmitParentScope->State == EEmitScopeState::Dead))
		{
			EmitScope->State = EEmitScopeState::Dead;
		}
		else if (EmitScope->State == EEmitScopeState::Uninitialized)
		{
			EmitScope->State = EEmitScopeState::Initializing;
			bool bPrepareResult = true;
			if (Scope->OwnerStatement)
			{
				bPrepareResult = Scope->OwnerStatement->Prepare(*this);
			}
			if (EmitScope->State == EEmitScopeState::Initializing)
			{
				// If Prepare() returns false, we're still uninitialized
				EmitScope->State = bPrepareResult ? EEmitScopeState::Live : EEmitScopeState::Uninitialized;
			}
		}
	}

	return EmitScope;
}

bool FEmitContext::PrepareScope(FScope* Scope)
{
	FEmitScope* EmitScope = InternalPrepareScope(Scope, Scope ? Scope->ParentScope : nullptr, false);
	return EmitScope && EmitScope->State != EEmitScopeState::Dead;
}

bool FEmitContext::PrepareScopeWithParent(FScope* Scope, FScope* ParentScope)
{
	FEmitScope* EmitScope = InternalPrepareScope(Scope, ParentScope, false);
	return EmitScope && EmitScope->State != EEmitScopeState::Dead;
}

bool FEmitContext::MarkScopeDead(FScope* Scope)
{
	InternalPrepareScope(Scope, Scope ? Scope->ParentScope : nullptr, true);
	return false;
}

FEmitScope* FEmitContext::AcquireEmitScopeWithParent(const FScope* Scope, FEmitScope* EmitParentScope)
{
	FEmitScope* EmitScope = nullptr;
	if (Scope)
	{
		FEmitScope* const* PrevEmitScope = EmitScopeMap.Find(Scope);
		EmitScope = PrevEmitScope ? *PrevEmitScope : nullptr;
		if (!EmitScope)
		{
			EmitScope = new(*Allocator) FEmitScope();
			EmitScope->ParentScope = EmitParentScope;
			EmitScope->NestedLevel = EmitParentScope ? EmitParentScope->NestedLevel + 1 : 0;
			EmitScopeMap.Add(Scope, EmitScope);
		}
		check(!EmitParentScope || EmitScope->ParentScope == EmitParentScope);
	}
	return EmitScope;
}

FEmitScope* FEmitContext::AcquireEmitScope(const FScope* Scope)
{
	FEmitScope* EmitParentScope = Scope ? AcquireEmitScope(Scope->ParentScope) : nullptr;
	return AcquireEmitScopeWithParent(Scope, EmitParentScope);
}

FEmitScope* FEmitContext::InternalEmitScope(const FScope* Scope)
{
	FEmitScope* EmitScope = AcquireEmitScope(Scope);
	if (EmitScope && EmitScope->State != EEmitScopeState::Dead)
	{
		if (Scope->ContainedStatement)
		{
			Scope->ContainedStatement->EmitShader(*this, *EmitScope);
		}
		return EmitScope;
	}
	return nullptr;
}

namespace Private
{
void MoveToScope(FEmitShaderNode* EmitNode, FEmitScope& Scope)
{
	if (EmitNode->Scope != &Scope)
	{
		FEmitScope* NewScope = &Scope;
		if (EmitNode->Scope)
		{
			NewScope = FEmitScope::FindSharedParent(EmitNode->Scope, &Scope);
			check(NewScope);
		}

		EmitNode->Scope = NewScope;
		for (FEmitShaderNode* Dependency : EmitNode->Dependencies)
		{
			MoveToScope(Dependency, *NewScope);
		}
	}
}

void FormatArg_ShaderValue(FEmitShaderExpression* ShaderValue, FEmitShaderDependencies& OutDependencies, FStringBuilderBase& OutCode)
{
	OutDependencies.Add(ShaderValue);
	OutCode.Append(ShaderValue->Reference);
}

int32 InternalFormatString(FStringBuilderBase* OutString, FEmitShaderDependencies& OutDependencies, FStringView Format, TArrayView<const FFormatArgVariant> ArgList, int32 BaseArgIndex)
{
	int32 ArgIndex = BaseArgIndex;
	if (Format.Len() > 0)
	{
		check(OutString);
		for (TCHAR Char : Format)
		{
			if (Char == TEXT('%'))
			{
				const FFormatArgVariant& Arg = ArgList[ArgIndex++];
				switch (Arg.Type)
				{
				case EFormatArgType::ShaderValue: FormatArg_ShaderValue(Arg.ShaderValue, OutDependencies, *OutString); break;
				case EFormatArgType::String: OutString->Append(Arg.String); break;
				case EFormatArgType::Int: OutString->Appendf(TEXT("%d"), Arg.Int); break;
				default:
					checkNoEntry();
					break;
				}
			}
			else
			{
				OutString->AppendChar(Char);
			}
		}
	}
	return ArgIndex;
}

void InternalFormatStrings(FStringBuilderBase* OutString0, FStringBuilderBase* OutString1, FEmitShaderDependencies& OutDependencies, FStringView Format0, FStringView Format1, const FFormatArgList& ArgList)
{
	int32 ArgIndex = 0;
	ArgIndex = InternalFormatString(OutString0, OutDependencies, Format0, ArgList, ArgIndex);
	ArgIndex = InternalFormatString(OutString1, OutDependencies, Format1, ArgList, ArgIndex);
	checkf(ArgIndex == ArgList.Num(), TEXT("%d args were provided, but %d were used"), ArgList.Num(), ArgIndex);
}

} // namespace Private

FEmitShaderExpression* FEmitContext::InternalEmitExpression(FEmitScope& Scope, TArrayView<FEmitShaderNode*> Dependencies, bool bInline, const Shader::FType& Type, FStringView Code)
{
	FEmitShaderExpression* ShaderValue = nullptr;

	FXxHash64Builder Hasher;
	Hasher.Update(Code.GetData(), Code.Len() * sizeof(TCHAR));
	if (bInline)
	{
		uint8 InlineFlag = 1;
		Hasher.Update(&InlineFlag, 1);
	}

	// Check to see if we've already generated code for an equivalent expression
	const FXxHash64 ShaderHash = Hasher.Finalize();
	FEmitShaderExpression** const PrevShaderValue = EmitExpressionMap.Find(ShaderHash);
	if (PrevShaderValue)
	{
		ShaderValue = *PrevShaderValue;
		check(ShaderValue->Type == Type);
		Private::MoveToScope(ShaderValue, Scope);
	}
	else
	{
		ShaderValue = new(*Allocator) FEmitShaderExpression(Scope, MemStack::AllocateArrayView(*Allocator, Dependencies), Type, ShaderHash);
		if (bInline)
		{
			ShaderValue->Reference = MemStack::AllocateString(*Allocator, Code);
		}
		else
		{
			ShaderValue->Reference = MemStack::AllocateStringf(*Allocator, TEXT("Local%d"), NumExpressionLocals++);
			ShaderValue->Value = MemStack::AllocateString(*Allocator, Code);
		}
		EmitExpressionMap.Add(ShaderHash, ShaderValue);
		EmitNodes.Add(ShaderValue);
	}

	return ShaderValue;
}

FEmitShaderStatement* FEmitContext::InternalEmitStatement(FEmitScope& Scope, TArrayView<FEmitShaderNode*> Dependencies, EEmitScopeFormat ScopeFormat, FEmitScope* NestedScope0, FEmitScope* NestedScope1, FStringView Code0, FStringView Code1)
{
	FEmitShaderStatement* EmitStatement = new(*Allocator) FEmitShaderStatement(Scope, MemStack::AllocateArrayView(*Allocator, Dependencies));
	EmitStatement->ScopeFormat = ScopeFormat;
	EmitStatement->NestedScopes[0] = NestedScope0;
	EmitStatement->NestedScopes[1] = NestedScope1;
	EmitStatement->Code[0] = MemStack::AllocateString(*Allocator, Code0);
	EmitStatement->Code[1] = MemStack::AllocateString(*Allocator, Code1);

	EmitNodes.Add(EmitStatement);
	return EmitStatement;
}

namespace Private
{
void WriteMaterialUniformAccess(Shader::EValueComponentType ComponentType, uint32 NumComponents, uint32 UniformOffset, FStringBuilderBase& OutResult)
{
	static const TCHAR IndexToMask[] = TEXT("xyzw");
	uint32 RegisterIndex = UniformOffset / 4;
	uint32 RegisterOffset = UniformOffset % 4;
	uint32 NumComponentsToWrite = NumComponents;
	bool bConstructor = false;

	check(ComponentType == Shader::EValueComponentType::Float || ComponentType == Shader::EValueComponentType::Int);
	const bool bIsInt = (ComponentType == Shader::EValueComponentType::Int);

	while (NumComponentsToWrite > 0u)
	{
		const uint32 NumComponentsInRegister = FMath::Min(NumComponentsToWrite, 4u - RegisterOffset);
		if (NumComponentsInRegister < NumComponents && !bConstructor)
		{
			// Uniform will be split across multiple registers, so add the constructor to concat them together
			OutResult.Appendf(TEXT("%s%d("), Shader::GetComponentTypeName(ComponentType), NumComponents);
			bConstructor = true;
		}

		if (bIsInt)
		{
			// PreshaderBuffer is typed as float4, so reinterpret as 'int' if needed
			OutResult.Append(TEXT("asint("));
		}

		OutResult.Appendf(TEXT("Material.PreshaderBuffer[%u]"), RegisterIndex);
		// Can skip writing mask if we're taking all 4 components from the register
		if (NumComponentsInRegister < 4u)
		{
			OutResult.AppendChar(TCHAR('.'));
			for (uint32 i = 0u; i < NumComponentsInRegister; ++i)
			{
				OutResult.AppendChar(IndexToMask[RegisterOffset + i]);
			}
		}

		if (bIsInt)
		{
			OutResult.Append(TEXT(")"));
		}

		NumComponentsToWrite -= NumComponentsInRegister;
		RegisterIndex++;
		RegisterOffset = 0u;
		if (NumComponentsToWrite > 0u)
		{
			OutResult.Append(TEXT(", "));
		}
	}
	if (bConstructor)
	{
		OutResult.Append(TEXT(")"));
	}
}
} // namespace Private

FEmitShaderExpression* FEmitContext::EmitPreshaderOrConstant(FEmitScope& Scope, const FRequestedType& RequestedType, FExpression* Expression)
{
	Shader::FPreshaderData LocalPreshader;
	Expression->EmitValuePreshader(*this, RequestedType, LocalPreshader);

	const Shader::FType Type = RequestedType.GetType();

	FXxHash64Builder Hasher;
	Hasher.Update(&Type, sizeof(Type));
	LocalPreshader.AppendHash(Hasher);
	const FXxHash64 Hash = Hasher.Finalize();
	FEmitShaderExpression* const* PrevShaderValue = EmitPreshaderMap.Find(Hash);
	if (PrevShaderValue)
	{
		FEmitShaderExpression* ShaderValue = *PrevShaderValue;
		check(ShaderValue->Type == Type);
		Private::MoveToScope(ShaderValue, Scope);
		return ShaderValue;
	}

	Shader::FPreshaderStack Stack;
	const Shader::FPreshaderValue ConstantValue = LocalPreshader.EvaluateConstant(*Material, Stack);

	TStringBuilder<1024> FormattedCode;
	if (Type.IsStruct())
	{
		FormattedCode.Append(TEXT("{ "));
	}

	FMaterialUniformPreshaderHeader* PreshaderHeader = nullptr;

	int32 ComponentIndex = 0;
	for (int32 FieldIndex = 0; FieldIndex < Type.GetNumFlatFields(); ++FieldIndex)
	{
		if (FieldIndex > 0)
		{
			FormattedCode.Append(TEXT(", "));
		}

		const Shader::EValueType FieldType = Type.GetFlatFieldType(FieldIndex);
		const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(FieldType);
		const int32 NumFieldComponents = TypeDesc.NumComponents;
		const EExpressionEvaluation FieldEvaluation = Expression->GetPreparedType().GetFieldEvaluation(ComponentIndex, NumFieldComponents);

		if (FieldEvaluation == EExpressionEvaluation::Preshader)
		{
			// Only need to allocate uniform buffer for non-constant components
			// Constant components can have their value inlined into the shader directly
			FUniformExpressionSet& UniformExpressionSet = MaterialCompilationOutput->UniformExpressionSet;
			if (!PreshaderHeader)
			{
				// Allocate a preshader header the first time we hit a non-constant field
				PreshaderHeader = &UniformExpressionSet.UniformPreshaders.AddDefaulted_GetRef();
				PreshaderHeader->FieldIndex = UniformExpressionSet.UniformPreshaderFields.Num();
				PreshaderHeader->NumFields = 0u;
				PreshaderHeader->OpcodeOffset = UniformExpressionSet.UniformPreshaderData.Num();
				Expression->EmitValuePreshader(*this, RequestedType, UniformExpressionSet.UniformPreshaderData);
				PreshaderHeader->OpcodeSize = UniformExpressionSet.UniformPreshaderData.Num() - PreshaderHeader->OpcodeOffset;
			}

			FMaterialUniformPreshaderField& PreshaderField = UniformExpressionSet.UniformPreshaderFields.AddDefaulted_GetRef();
			PreshaderField.ComponentIndex = ComponentIndex;
			PreshaderField.Type = FieldType;
			PreshaderHeader->NumFields++;

			if (TypeDesc.ComponentType == Shader::EValueComponentType::Bool)
			{
				// 'Bool' uniforms are packed into bits
				if (CurrentNumBoolComponents + NumFieldComponents > 32u)
				{
					CurrentBoolUniformOffset = UniformPreshaderOffset++;
					CurrentNumBoolComponents = 0u;
				}

				const uint32 RegisterIndex = CurrentBoolUniformOffset / 4;
				const uint32 RegisterOffset = CurrentBoolUniformOffset % 4;
				FormattedCode.Appendf(TEXT("UnpackUniform_%s(asuint(Material.PreshaderBuffer[%u][%u]), %u)"),
					TypeDesc.Name,
					RegisterIndex,
					RegisterOffset,
					CurrentNumBoolComponents);

				PreshaderField.BufferOffset = CurrentBoolUniformOffset * 32u + CurrentNumBoolComponents;
				CurrentNumBoolComponents += NumFieldComponents;
			}
			else if (TypeDesc.ComponentType == Shader::EValueComponentType::Double)
			{
				// Double uniforms are split into Tile/Offset components to make FLWCScalar/FLWCVectors
				PreshaderField.BufferOffset = UniformPreshaderOffset;

				if (NumFieldComponents > 1)
				{
					FormattedCode.Appendf(TEXT("MakeLWCVector%d("), NumFieldComponents);
				}
				else
				{
					FormattedCode.Append(TEXT("MakeLWCScalar("));
				}

				// Write the tile uniform
				Private::WriteMaterialUniformAccess(Shader::EValueComponentType::Float, NumFieldComponents, UniformPreshaderOffset, FormattedCode);
				UniformPreshaderOffset += NumFieldComponents;
				FormattedCode.Append(TEXT(", "));

				// Write the offset uniform
				Private::WriteMaterialUniformAccess(Shader::EValueComponentType::Float, NumFieldComponents, UniformPreshaderOffset, FormattedCode);
				UniformPreshaderOffset += NumFieldComponents;
				FormattedCode.Append(TEXT(")"));
			}
			else
			{
				// Float/Int uniforms are written directly to the uniform buffer
				const uint32 RegisterOffset = UniformPreshaderOffset % 4;
				if (RegisterOffset + NumFieldComponents > 4u)
				{
					// If this uniform would span multiple registers, align offset to the next register to avoid this
					// TODO - we could keep track of this empty padding space, and pack other smaller uniform types here
					UniformPreshaderOffset = Align(UniformPreshaderOffset, 4u);
				}

				PreshaderField.BufferOffset = UniformPreshaderOffset;
				Private::WriteMaterialUniformAccess(TypeDesc.ComponentType, NumFieldComponents, UniformPreshaderOffset, FormattedCode);
				UniformPreshaderOffset += NumFieldComponents;
			}
		}
		else
		{
			// We allow FieldEvaluation to be 'None', since in that case we still need to fill in a value for the HLSL initializer
			check(FieldEvaluation == EExpressionEvaluation::Constant || FieldEvaluation == EExpressionEvaluation::None);

			// The type generated by the preshader might not match the expected type
			// In the future, with new HLSLTree, preshader could potentially include explicit cast opcodes, and avoid implicit conversions
			Shader::FValue FieldConstantValue(ConstantValue.Type.GetComponentType(ComponentIndex), NumFieldComponents);
			for (int32 i = 0; i < NumFieldComponents; ++i)
			{
				// Allow replicating scalar values
				FieldConstantValue.Component[i] = ConstantValue.Component.Num() == 1 ? ConstantValue.Component[0] : ConstantValue.Component[ComponentIndex + i];
			}

			if (TypeDesc.ComponentType == Shader::EValueComponentType::Double)
			{
				const Shader::FDoubleValue DoubleValue = FieldConstantValue.AsDouble();
				TStringBuilder<256> TileValue;
				TStringBuilder<256> OffsetValue;
				for (int32 Index = 0; Index < NumFieldComponents; ++Index)
				{
					if (Index > 0)
					{
						TileValue.Append(TEXT(", "));
						OffsetValue.Append(TEXT(", "));
					}

					const FLargeWorldRenderScalar Value(DoubleValue[Index]);
					TileValue.Appendf(TEXT("%#.9gf"), Value.GetTile());
					OffsetValue.Appendf(TEXT("%#.9gf"), Value.GetOffset());
				}

				if (NumFieldComponents > 1)
				{
					FormattedCode.Appendf(TEXT("MakeLWCVector%d(float%d(%s), float%d(%s))"), NumFieldComponents, NumFieldComponents, TileValue.ToString(), NumFieldComponents, OffsetValue.ToString());
				}
				else
				{
					FormattedCode.Appendf(TEXT("MakeLWCScalar(%s, %s)"), TileValue.ToString(), OffsetValue.ToString());
				}
			}
			else
			{
				const Shader::FValue CastFieldConstantValue = Shader::Cast(FieldConstantValue, FieldType);
				if (NumFieldComponents > 1)
				{
					FormattedCode.Appendf(TEXT("%s("), TypeDesc.Name);
				}
				for (int32 Index = 0; Index < NumFieldComponents; ++Index)
				{
					if (Index > 0)
					{
						FormattedCode.Append(TEXT(", "));
					}
					CastFieldConstantValue.Component[Index].ToString(TypeDesc.ComponentType, FormattedCode);
				}
				if (NumFieldComponents > 1)
				{
					FormattedCode.Append(TEXT(")"));
				}
			}
		}
		ComponentIndex += NumFieldComponents;
	}
	check(ComponentIndex == Type.GetNumComponents());

	if (Type.IsStruct())
	{
		FormattedCode.Append(TEXT(" }"));
	}

	const bool bInline = !Type.IsStruct(); // struct declarations can't be inline, due to HLSL syntax
	FEmitShaderExpression* ShaderValue = InternalEmitExpression(Scope, TArrayView<FEmitShaderNode*>(), bInline, Type, FormattedCode.ToView());
	EmitPreshaderMap.Add(Hash, ShaderValue);

	return ShaderValue;
}

FEmitShaderExpression* FEmitContext::EmitConstantZero(FEmitScope& Scope, const Shader::FType& Type)
{
	return EmitInlineExpression(Scope, Type, TEXT("((%)0)"), Type.GetName());
}

FEmitShaderExpression* FEmitContext::EmitCast(FEmitScope& Scope, FEmitShaderExpression* ShaderValue, const Shader::FType& DestType)
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
				ShaderValue = EmitCast(Scope, ShaderValue, Shader::MakeValueType(Shader::EValueComponentType::Float, DestTypeDesc.NumComponents));
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
		Errors->AddErrorf(TEXT("Cannot cast between non-numeric types %s to %s."), SourceTypeDesc.Name, DestTypeDesc.Name);
		FormattedCode.Appendf(TEXT("((%s)0)"), DestType.GetName());
	}

	check(IntermediateType != ShaderValue->Type);
	ShaderValue = EmitInlineExpressionWithDependency(Scope, ShaderValue, IntermediateType, FormattedCode.ToView());
	if (ShaderValue->Type != DestType)
	{
		// May need to cast through multiple intermediate types to reach our destination type
		ShaderValue = EmitCast(Scope, ShaderValue, DestType);
	}
	return ShaderValue;
}

void FEmitContext::Finalize()
{
	// Unlink all nodes from scopes
	for (FEmitShaderNode* EmitNode : EmitNodes)
	{
		EmitNode->Scope = nullptr;
		EmitNode->NextScopedNode = nullptr;
	}
	
	// Don't reset Expression/Preshader maps, allow future passes to share matching preshaders/expressions

	EmitScopeMap.Reset();
	EmitFunctionMap.Reset();
	EmitLocalPHIMap.Reset();

	MaterialCompilationOutput->UniformExpressionSet.UniformPreshaderBufferSize = (UniformPreshaderOffset + 3u) / 4u;
}

} // namespace HLSLTree
} // namespace UE
