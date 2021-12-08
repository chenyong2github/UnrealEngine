// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTree.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStackUtility.h"
#include "MaterialShared.h" // TODO - split preshader out into its own module

static FSHAHash HashString(const TCHAR* String, int32 Length)
{
	FSHAHash Hash;
	FSHA1::HashBuffer(String, Length * sizeof(TCHAR), Hash.Hash);
	return Hash;
}

static FSHAHash HashString(const FStringBuilderBase& StringBuilder)
{
	return HashString(StringBuilder.GetData(), StringBuilder.Len());
}

namespace UE
{
namespace HLSLTree
{

EExpressionEvaluationType CombineEvaluationTypes(EExpressionEvaluationType Lhs, EExpressionEvaluationType Rhs)
{
	if (Lhs == EExpressionEvaluationType::None)
	{
		// If either is 'None', return the other
		return Rhs;
	}
	else if (Rhs == EExpressionEvaluationType::None)
	{
		return Lhs;
	}
	else if (Lhs == EExpressionEvaluationType::Constant && Rhs == EExpressionEvaluationType::Constant)
	{
		// 2 constants make a constant
		return EExpressionEvaluationType::Constant;
	}
	else if (Lhs == EExpressionEvaluationType::Shader || Rhs == EExpressionEvaluationType::Shader)
	{
		// If either requires shader, shader is required
		return EExpressionEvaluationType::Shader;
	}
	// Any combination of constants/preshader can make a preshader
	return EExpressionEvaluationType::Preshader;
}

FErrors::FErrors(FMemStackBase& InAllocator)
	: Allocator(&InAllocator)
{
}

void FErrors::AddError(const FNode* InNode, FStringView InError)
{
	const int32 SizeofString = InError.Len() * sizeof(TCHAR);
	void* Memory = Allocator->Alloc(sizeof(FError) + SizeofString, alignof(FError));
	FError* Error = new(Memory) FError();
	FMemory::Memcpy(Error->Message, InError.GetData(), SizeofString);
	Error->Message[InError.Len()] = 0;
	Error->MessageLength = InError.Len();
	Error->Node = InNode;
	Error->Next = FirstError;
	FirstError = Error;
	NumErrors++;

	ensureMsgf(false, TEXT("%s"), Error->Message);
}

FEmitContext::FEmitContext(FMemStackBase& InAllocator, const Shader::FStructTypeRegistry& InTypeRegistry)
	: Allocator(&InAllocator)
	, TypeRegistry(&InTypeRegistry)
	, Errors(InAllocator)
{
}

FEmitContext::~FEmitContext()
{
}

const TCHAR* FEmitContext::AcquireLocalDeclarationCode()
{
	return MemStack::AllocateStringf(*Allocator, TEXT("Local%d"), NumExpressionLocals++);
}

FEmitShaderValue* FEmitContext::CastShaderValue(FNode* Node, FScope* Scope, FEmitShaderValue* ShaderValue, const Shader::FType& DestType)
{
	if (ShaderValue->Type == DestType)
	{
		return ShaderValue;
	}

	const Shader::FValueTypeDescription SourceTypeDesc = Shader::GetValueTypeDescription(ShaderValue->Type);
	const Shader::FValueTypeDescription DestTypeDesc = Shader::GetValueTypeDescription(DestType);

	TStringBuilder<1024> FormattedCode;
	FShaderValue Shader(FormattedCode);
	Shader.Type = DestType;
	Shader.bInline = true;

	if (SourceTypeDesc.NumComponents > 0 && DestTypeDesc.NumComponents > 0)
	{
		const bool bIsSourceLWC = SourceTypeDesc.ComponentType == Shader::EValueComponentType::Double;
		const bool bIsLWC = DestTypeDesc.ComponentType == Shader::EValueComponentType::Double;

		if (bIsLWC != bIsSourceLWC)
		{
			if (bIsLWC)
			{
				// float->LWC
				ShaderValue = CastShaderValue(Node, Scope, ShaderValue, Shader::MakeValueType(Shader::EValueComponentType::Float, DestTypeDesc.NumComponents));
				Shader.Code.Appendf(TEXT("LWCPromote(%s)"), ShaderValue->Reference);
			}
			else
			{
				//LWC->float
				Shader.Code.Appendf(TEXT("LWCToFloat(%s)"), ShaderValue->Reference);
				Shader.Type = Shader::MakeValueType(Shader::EValueComponentType::Float, SourceTypeDesc.NumComponents);
			}
		}
		else
		{
			const bool bReplicateScalar = (SourceTypeDesc.NumComponents == 1);

			int32 NumComponents = 0;
			bool bNeedClosingParen = false;
			if (bIsLWC)
			{
				Shader.Code.Append(TEXT("MakeLWCVector("));
				bNeedClosingParen = true;
			}
			else
			{
				if (SourceTypeDesc.NumComponents == 1 || SourceTypeDesc.NumComponents == DestTypeDesc.NumComponents)
				{
					NumComponents = DestTypeDesc.NumComponents;
					// Cast the scalar to the correct type, HLSL language will replicate the scalar if needed when performing this cast
					Shader.Code.Appendf(TEXT("((%s)%s)"), DestTypeDesc.Name, ShaderValue->Reference);
				}
				else
				{
					NumComponents = FMath::Min(SourceTypeDesc.NumComponents, DestTypeDesc.NumComponents);
					if (NumComponents < DestTypeDesc.NumComponents)
					{
						Shader.Code.Appendf(TEXT("%s("), DestTypeDesc.Name);
						bNeedClosingParen = true;
					}
					if (NumComponents == SourceTypeDesc.NumComponents && SourceTypeDesc.ComponentType == DestTypeDesc.ComponentType)
					{
						// If we're taking all the components from the source, can avoid adding a swizzle
						Shader.Code.Append(ShaderValue->Reference);
					}
					else
					{
						// Use a cast to truncate the source to the correct number of types
						const Shader::EValueType IntermediateType = Shader::MakeValueType(DestTypeDesc.ComponentType, NumComponents);
						const Shader::FValueTypeDescription IntermediateTypeDesc = Shader::GetValueTypeDescription(IntermediateType);
						Shader.Code.Appendf(TEXT("((%s)%s)"), IntermediateTypeDesc.Name, ShaderValue->Reference);
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
						Shader.Code.Append(TEXT(","));
					}
					if (bIsLWC)
					{
						if (!bReplicateScalar && ComponentIndex >= SourceTypeDesc.NumComponents)
						{
							Shader.Code.Append(TEXT("LWCPromote(0.0f)"));
						}
						else
						{
							Shader.Code.Appendf(TEXT("LWCGetComponent(%s, %d)"), ShaderValue->Reference, bReplicateScalar ? 0 : ComponentIndex);
						}
					}
					else
					{
						// Non-LWC case should only be zero-filling here, other cases should have already been handled
						check(!bReplicateScalar);
						check(ComponentIndex >= SourceTypeDesc.NumComponents);
						ZeroValue.ToString(Shader::EValueStringFormat::HLSL, Shader.Code);
					}
				}
				NumComponents = DestTypeDesc.NumComponents;
				Shader.Code.Append(TEXT(")"));
			}

			check(NumComponents == DestTypeDesc.NumComponents);
		}
	}
	else
	{
		Errors.AddErrorf(Node, TEXT("Cannot cast between non-numeric types %s to %s."), SourceTypeDesc.Name, DestTypeDesc.Name);
		Shader.Code.Appendf(TEXT("((%s)0)"), DestType.GetName());
		Shader.bInline = false;
	}

	check(Shader.Type != ShaderValue->Type);
	ShaderValue = AcquireShader(Scope, Shader, MakeArrayView(&ShaderValue, 1));
	if (ShaderValue->Type != DestType)
	{
		// May need to cast through multiple intermediate types to reach our destination type
		ShaderValue = CastShaderValue(Node, Scope, ShaderValue, DestType);
	}
	return ShaderValue;
}

namespace Private
{
void MoveToScope(FEmitShaderValue* ShaderValue, FScope* Scope)
{
	if (ShaderValue->Scope != Scope)
	{
		FScope* NewScope = FScope::FindSharedParent(ShaderValue->Scope, Scope);
		check(NewScope);

		ShaderValue->Scope = NewScope;
		for (FEmitShaderValue* Dependency : ShaderValue->Dependencies)
		{
			MoveToScope(Dependency, NewScope);
		}
	}
}
}

FEmitShaderValue* FEmitContext::AcquireShader(FScope* Scope, const FShaderValue& Shader, TArrayView<FEmitShaderValue*> Dependencies)
{
	FEmitShaderValue* ShaderValue = nullptr;

	// Check to see if we've already generated code for an equivalent expression
	const FSHAHash ShaderHash = HashString(Shader.Code);
	FEmitShaderValue** const PrevShaderValue = ShaderValueMap.Find(ShaderHash);
	if (PrevShaderValue)
	{
		ShaderValue = *PrevShaderValue;
		check(ShaderValue->Type == Shader.Type);
		Private::MoveToScope(ShaderValue, Scope);
	}
	else
	{
		ShaderValue = new(*Allocator) FEmitShaderValue(Scope, Shader.Type);
		ShaderValue->Hash = ShaderHash;
		ShaderValue->Dependencies = MemStack::AllocateArrayView(*Allocator, Dependencies);
		if (Shader.bInline)
		{
			ShaderValue->Reference = MemStack::AllocateString(*Allocator, Shader.Code);
		}
		else
		{
			ShaderValue->Reference = AcquireLocalDeclarationCode();
			ShaderValue->Value = MemStack::AllocateString(*Allocator, Shader.Code);
			ShaderValueMap.Add(ShaderHash, ShaderValue);
		}
	}

	return ShaderValue;
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
			OutResult.Append(TCHAR('.'));
			for (uint32 i = 0u; i < NumComponentsInRegister; ++i)
			{
				OutResult.Append(IndexToMask[RegisterOffset + i]);
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

FEmitShaderValue* FEmitContext::AcquirePreshaderOrConstant(const FRequestedType& RequestedType, FScope* Scope, FExpression* Expression)
{
	Shader::FPreshaderData LocalPreshader;
	Expression->EmitValuePreshader(*this, RequestedType, LocalPreshader);

	const Shader::FType Type = RequestedType.GetType();

	FSHA1 Hasher;
	Hasher.Update((uint8*)&Type, sizeof(Type));
	LocalPreshader.AppendHash(Hasher);
	const FSHAHash Hash = Hasher.Finalize();
	FEmitShaderValue* const* PrevShaderValue = PreshaderValueMap.Find(Hash);
	if (PrevShaderValue)
	{
		FEmitShaderValue* ShaderValue = *PrevShaderValue;
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
	uint32 CurrentBoolUniformOffset = ~0u;
	uint32 CurrentNumBoolComponents = 32u;

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
		const EExpressionEvaluationType FieldEvaluationType = Expression->GetPreparedType().GetFieldEvaluationType(ComponentIndex, NumFieldComponents);

		if (FieldEvaluationType == EExpressionEvaluationType::Preshader)
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
			// We allow FieldEvaluationType to be 'None', since in that case we still need to fill in a value for the HLSL initializer
			check(FieldEvaluationType == EExpressionEvaluationType::Constant || FieldEvaluationType == EExpressionEvaluationType::None);

			// The type generated by the preshader might not match the expected type
			// In the future, with new HLSLTree, preshader could potentially include explicit cast opcodes, and avoid implicit conversions
			Shader::FValue FieldConstantValue(ConstantValue.Type.GetComponentType(ComponentIndex), NumFieldComponents);
			for (int32 i = 0; i < NumFieldComponents; ++i)
			{
				FieldConstantValue.Component[i] = ConstantValue.Component[ComponentIndex + i];
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

	FShaderValue Shader(FormattedCode);
	Shader.Type = Type;
	Shader.bInline = !Type.IsStruct(); // struct declarations can't be inline, due to HLSL syntax
	FEmitShaderValue* ShaderValue = AcquireShader(Scope, Shader, TArrayView<FEmitShaderValue*>());
	PreshaderValueMap.Add(Hash, ShaderValue);

	return ShaderValue;
}

namespace Private
{
void EmitShaderValue(FEmitContext& Context, FEmitShaderValue* ShaderValue)
{
	if (ShaderValue->Scope)
	{
		// Emit dependencies first
		for (FEmitShaderValue* Dependency : ShaderValue->Dependencies)
		{
			EmitShaderValue(Context, Dependency);
		}
		// Don't need a declaration for inline values
		if (!ShaderValue->IsInline())
		{
			ShaderValue->Scope->EmitDeclarationf(Context, TEXT("const %s %s = %s;"),
				ShaderValue->Type.GetName(),
				ShaderValue->Reference,
				ShaderValue->Value);
		}
		ShaderValue->Scope = nullptr; // Don't emit again
	}
}
}

void FEmitContext::Finalize()
{
	check(ScopeStack.Num() == 0);
	check(ShaderValueStack.Num() == 0);

	for (const auto& It : ShaderValueMap)
	{
		Private::EmitShaderValue(*this, It.Value);
	}

	ShaderValueMap.Reset();
	PreshaderValueMap.Reset();
	LocalPHIs.Reset();

	MaterialCompilationOutput->UniformExpressionSet.UniformPreshaderBufferSize = (UniformPreshaderOffset + 3u) / 4u;
}

void FScope::Reset()
{
	State = EScopeState::Uninitialized;
	Declarations = FCodeList();
	Statements = FCodeList();
}

FScope* FScope::FindSharedParent(FScope* Lhs, FScope* Rhs)
{
	FScope* Scope0 = Lhs;
	FScope* Scope1 = Rhs;
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

void FExpressionLocalPHI::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	check(NumValues <= MaxNumPreviousScopes);
	FExpression* ForwardExpression = Values[0];
	bool bForwardExpressionValid = true;

	// There are 2 cases we want to optimize here
	// 1) If the PHI node has the same value in all the previous scopes, we can avoid generating code for the previous scopes, and just use the value directly
	for (int32 i = 1; i < NumValues; ++i)
	{
		FExpression* ScopeExpression = Values[i];
		if (ScopeExpression != ForwardExpression)
		{
			ForwardExpression = nullptr;
			bForwardExpressionValid = false;
			break;
		}
	}

	if (bForwardExpressionValid)
	{
		check(ForwardExpression);
		return OutResult.SetForwardValue(Context, RequestedType, ForwardExpression);
	}

	// 2) PHI has different values in previous scopes, but possible some previous scopes may become dead due to constant folding
	// In this case, we check to see if the value is the same in all live scopes, and forward if possible
	for (int32 i = 0; i < NumValues; ++i)
	{
		// Ignore values in dead scopes
		if (PrepareScope(Context, Scopes[i]))
		{
			FExpression* ScopeExpression = Values[i];
			if (!ForwardExpression)
			{
				ForwardExpression = ScopeExpression;
				bForwardExpressionValid = true;
			}
			else if (ForwardExpression != ScopeExpression)
			{
				bForwardExpressionValid = false;
			}
		}
	}

	if (bForwardExpressionValid)
	{
		check(ForwardExpression);
		return OutResult.SetForwardValue(Context, RequestedType, ForwardExpression);
	}

	FPreparedType TypePerValue[MaxNumPreviousScopes];
	int32 NumValidTypes = 0;
	FPreparedType CurrentType;

	auto UpdateValueTypes = [&]()
	{
		for (int32 i = 0; i < NumValues; ++i)
		{
			if (TypePerValue[i].IsVoid() && PrepareScope(Context, Scopes[i]))
			{
				const FPreparedType& ValueType = PrepareExpressionValue(Context, Values[i], RequestedType);
				if (!ValueType.IsVoid())
				{
					TypePerValue[i] = ValueType;
					CurrentType = MergePreparedTypes(CurrentType, ValueType);
					if (CurrentType.IsVoid())
					{
						return Context.Errors.AddError(this, TEXT("Type mismatch"));
					}
					check(NumValidTypes < NumValues);
					NumValidTypes++;
				}
			}
		}
	};

	// First try to assign all the values we can
	UpdateValueTypes();

	// Assuming we have at least one value with a valid type, we use that to initialize our type
	CurrentType.SetEvaluationType(EExpressionEvaluationType::Shader); // TODO - No support for preshader flow control
	OutResult.SetType(Context, RequestedType, CurrentType);

	if (NumValidTypes < NumValues)
	{
		// Now try to assign remaining types that failed the first iteration 
		UpdateValueTypes();
		if (NumValidTypes < NumValues)
		{
			return Context.Errors.AddError(this, TEXT("Failed to compute all types for LocalPHI"));
		}
	}
}

void FExpressionLocalPHI::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	int32 LocalPHIIndex = Context.LocalPHIs.Find(this);
	if (LocalPHIIndex == INDEX_NONE)
	{
		// This is the first time we've emitted shader code for this PHI
		// First add it to the list, so if this is called recursively, this path will only be taken the first time
		LocalPHIIndex = Context.LocalPHIs.Add(this);

		// Find the outermost scope to declare our local variable
		FScope* DeclarationScope = Context.ScopeStack.Last();
		for (int32 i = 0; i < NumValues; ++i)
		{
			DeclarationScope = FScope::FindSharedParent(DeclarationScope, Scopes[i]);
			if (!DeclarationScope)
			{
				return Context.Errors.AddError(this, TEXT("Invalid LocalPHI"));
			}
		}

		const FRequestedType LocalType = GetRequestedType();

		bool bNeedToAddDeclaration = true;
		for (int32 i = 0; i < NumValues; ++i)
		{
			FScope* ValueScope = Scopes[i];
			check(IsScopeLive(ValueScope));

			Context.ScopeStack.Add(ValueScope);
			const TCHAR* ValueCode = Values[i]->GetValueShader(Context, LocalType);
			Context.ScopeStack.Pop();

			if (ValueScope == DeclarationScope)
			{
				ValueScope->EmitDeclarationf(Context, TEXT("%s LocalPHI%d = %s;"),
					LocalType.GetName(),
					LocalPHIIndex,
					ValueCode);
				bNeedToAddDeclaration = false;
			}
			else
			{
				ValueScope->EmitStatementf(Context, TEXT("LocalPHI%d = %s;"),
					LocalPHIIndex,
					ValueCode);
			}
		}

		if (bNeedToAddDeclaration)
		{
			check(IsScopeLive(DeclarationScope));
			DeclarationScope->EmitDeclarationf(Context, TEXT("%s LocalPHI%d;"),
				LocalType.GetName(),
				LocalPHIIndex);
		}
	}

	OutShader.Code.Appendf(TEXT("LocalPHI%d"), LocalPHIIndex);
	OutShader.Type = GetType();
	OutShader.bInline = true;
}

void FStatement::Reset()
{
	bEmitShader = false;
}

void FExpression::Reset()
{
	CurrentRequestedType.Reset();
	PrepareValueResult = FPrepareValueResult();
}

const FPreparedType& PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression, const FRequestedType& RequestedType)
{
	static FPreparedType VoidType;
	if (!InExpression)
	{
		return VoidType;
	}

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
	else if(InExpression->CurrentRequestedType.GetStructType() != RequestedType.GetStructType())
	{
		Context.Errors.AddError(InExpression, TEXT("Type mismatch"));
		return VoidType;
	}
	else
	{
		const int32 NumComponents = RequestedType.GetNumComponents();
		InExpression->CurrentRequestedType.RequestedComponents.PadToNum(NumComponents, false);
		for (int32 ComponentIndex = 0; ComponentIndex < NumComponents; ++ComponentIndex)
		{
			if (!InExpression->CurrentRequestedType.IsComponentRequested(ComponentIndex) && RequestedType.IsComponentRequested(ComponentIndex))
			{
				InExpression->CurrentRequestedType.RequestedComponents[ComponentIndex] = true;
				bNeedToUpdateType = true;
			}
		}
	}

	if (bNeedToUpdateType)
	{
		check(!InExpression->CurrentRequestedType.IsVoid());

		InExpression->bReentryFlag = true;
		InExpression->PrepareValue(Context, InExpression->CurrentRequestedType, InExpression->PrepareValueResult);
		InExpression->bReentryFlag = false;

		if (InExpression->PrepareValueResult.GetPreparedType().IsVoid())
		{
			// If we failed to assign a valid type, reset the requested type as well
			// This ensures we'll try to compute a type again the next time we're called
			InExpression->CurrentRequestedType.Reset();
		}
	}

	return InExpression->PrepareValueResult.GetPreparedType();
}

FRequestedType::FRequestedType(const Shader::FType& InType, bool bDefaultRequest)
{
	int32 NumComponents = 0;
	if (InType.IsStruct())
	{
		StructType = InType.StructType;
		NumComponents = InType.StructType->ComponentTypes.Num();
	}
	else
	{
		const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(InType);
		ValueComponentType = TypeDesc.ComponentType;
		NumComponents = TypeDesc.NumComponents;
	}
	RequestedComponents.Init(bDefaultRequest, NumComponents);
}

FRequestedType::FRequestedType(const Shader::EValueType& InType, bool bDefaultRequest)
{
	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(InType);
	ValueComponentType = TypeDesc.ComponentType;
	RequestedComponents.Init(bDefaultRequest, TypeDesc.NumComponents);
}

const Shader::FType FRequestedType::GetType() const
{
	if (IsStruct())
	{
		return StructType;
	}
	return Shader::MakeValueType(ValueComponentType, GetNumComponents());
}

int32 FRequestedType::GetNumComponents() const
{
	if (StructType)
	{
		return StructType->ComponentTypes.Num();
	}
	else
	{
		const int32 MaxComponentIndex = RequestedComponents.FindLast(true);
		if (MaxComponentIndex != INDEX_NONE)
		{
			return MaxComponentIndex + 1;
		}
	}
	return 0;
}

bool FRequestedType::Merge(const FRequestedType& OtherType)
{
	if ((StructType || OtherType.StructType) && StructType != OtherType.StructType)
	{
		return false;
	}

	StructType = OtherType.StructType;
	if (!StructType)
	{
		ValueComponentType = Shader::CombineComponentTypes(ValueComponentType, OtherType.ValueComponentType);
	}

	for (int32 Index = 0; Index < OtherType.RequestedComponents.Num(); ++Index)
	{
		if (OtherType.IsComponentRequested(Index))
		{
			SetComponentRequested(Index);
		}
	}

	return true;
}

FPreparedType::FPreparedType(const Shader::FType& InType)
{
	if (InType.IsStruct())
	{
		StructType = InType.StructType;
	}
	else
	{
		ValueComponentType = Shader::GetValueTypeDescription(InType).ComponentType;
	}
}

int32 FPreparedType::GetNumComponents() const
{
	if (StructType)
	{
		return StructType->ComponentTypes.Num();
	}
	else if (ValueComponentType != Shader::EValueComponentType::Void)
	{
		const int32 MaxComponentIndex = ComponentEvaluationType.FindLastByPredicate([](EExpressionEvaluationType InEvaluationType) { return InEvaluationType != EExpressionEvaluationType::None; });
		if (MaxComponentIndex != INDEX_NONE)
		{
			return MaxComponentIndex + 1;
		}
	}
	return 0;
}

FRequestedType MakeRequestedType(Shader::EValueComponentType ComponentType, const FRequestedType& RequestedComponents)
{
	check(!RequestedComponents.IsStruct());
	FRequestedType Result;
	Result.ValueComponentType = ComponentType;
	Result.RequestedComponents = RequestedComponents.RequestedComponents;
	return Result;
}

bool FPreparedType::IsVoid() const
{
	return GetNumComponents() == 0;
}

Shader::FType FPreparedType::GetType() const
{
	if (IsStruct())
	{
		return StructType;
	}
	return Shader::MakeValueType(ValueComponentType, GetNumComponents());
}

FRequestedType FPreparedType::GetRequestedType() const
{
	const int32 NumComponents = GetNumComponents();
	FRequestedType Result;
	if (NumComponents > 0)
	{
		if (StructType)
		{
			Result.StructType = StructType;
		}
		else
		{
			Result.ValueComponentType = ValueComponentType;
		}
		for (int32 Index = 0; Index < NumComponents; ++Index)
		{
			if (GetComponentEvaluationType(Index) != EExpressionEvaluationType::None)
			{
				Result.SetComponentRequested(Index);
			}
		}
	}
	return Result;
}

EExpressionEvaluationType FPreparedType::GetEvaluationType(const FRequestedType& RequestedType) const
{
	EExpressionEvaluationType Result = EExpressionEvaluationType::None;
	for(int32 Index = 0; Index < ComponentEvaluationType.Num(); ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			Result = CombineEvaluationTypes(Result, ComponentEvaluationType[Index]);
		}
	}
	return Result;
}

EExpressionEvaluationType FPreparedType::GetFieldEvaluationType(int32 ComponentIndex, int32 NumComponents) const
{
	EExpressionEvaluationType Result = EExpressionEvaluationType::None;
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		Result = CombineEvaluationTypes(Result, GetComponentEvaluationType(ComponentIndex + Index));
	}
	return Result;
}

void FPreparedType::SetEvaluationType(EExpressionEvaluationType EvaluationType)
{
	const int32 NumComponents = GetNumComponents();
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		if (ComponentEvaluationType[Index] != EExpressionEvaluationType::None)
		{
			ComponentEvaluationType[Index] = EvaluationType;
		}
	}
}

void FPreparedType::SetField(const Shader::FStructField* Field, const FPreparedType& FieldType)
{
	for (int32 Index = 0; Index < Field->GetNumComponents(); ++Index)
	{
		SetComponentEvaluationType(Field->ComponentIndex + Index, FieldType.GetComponentEvaluationType(Index));
	}
}

FPreparedType FPreparedType::GetFieldType(const Shader::FStructField* Field) const
{
	FPreparedType Result(Field->Type);
	for (int32 Index = 0; Index < Field->GetNumComponents(); ++Index)
	{
		Result.SetComponentEvaluationType(Index, GetComponentEvaluationType(Field->ComponentIndex + Index));
	}
	return Result;
}

void FPreparedType::SetComponentEvaluationType(int32 Index, EExpressionEvaluationType EvaluationType)
{
	if (Index >= ComponentEvaluationType.Num())
	{
		static_assert((uint8)EExpressionEvaluationType::None == 0u, "Assume AddZeroed() will initialize to None");
		ComponentEvaluationType.AddZeroed(Index + 1 - ComponentEvaluationType.Num());
	}
	ComponentEvaluationType[Index] = EvaluationType;
}

FPreparedType MergePreparedTypes(const FPreparedType& Lhs, const FPreparedType& Rhs)
{
	// If one type is not initialized yet, just use the other type
	if (!Lhs.IsInitialized())
	{
		return Rhs;
	}
	else if (!Rhs.IsInitialized())
	{
		return Lhs;
	}

	int32 NumComponents = 0;
	FPreparedType Result;
	if (Lhs.IsStruct() || Rhs.IsStruct())
	{
		if (Lhs.StructType != Rhs.StructType)
		{
			// Mismatched structs
			return Result;
		}
		Result.StructType = Lhs.StructType;
		NumComponents = Result.StructType->ComponentTypes.Num();
	}
	else
	{
		Result.ValueComponentType = Shader::CombineComponentTypes(Lhs.ValueComponentType, Rhs.ValueComponentType);
		NumComponents = FMath::Max(Lhs.GetNumComponents(), Rhs.GetNumComponents());
	}

	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		const EExpressionEvaluationType LhsEvaluation = Lhs.GetComponentEvaluationType(Index);
		const EExpressionEvaluationType RhsEvaluation = Rhs.GetComponentEvaluationType(Index);
		Result.SetComponentEvaluationType(Index, CombineEvaluationTypes(LhsEvaluation, RhsEvaluation));
	}

	return Result;
}

bool FPrepareValueResult::TryMergePreparedType(FEmitContext& Context, const Shader::FStructType* StructType, Shader::EValueComponentType ComponentType)
{
	// If we previously had a forwarded value set, reset that and start over
	if (ForwardValue || !PreparedType.IsInitialized())
	{
		PreparedType.ComponentEvaluationType.Reset();
		PreparedType.ValueComponentType = ComponentType;
		PreparedType.StructType = StructType;
		ForwardValue = nullptr;
		return true;
	}

	if (StructType)
	{
		check(ComponentType == Shader::EValueComponentType::Void);
		if (StructType != PreparedType.StructType)
		{
			Context.Errors.AddError(nullptr, TEXT("Invalid type"));
			return false;
		}
	}
	else
	{
		check(ComponentType != Shader::EValueComponentType::Void);
		PreparedType.ValueComponentType = Shader::CombineComponentTypes(PreparedType.ValueComponentType, ComponentType);
	}

	return true;
}

void FPrepareValueResult::SetType(FEmitContext& Context, const FRequestedType& RequestedType, EExpressionEvaluationType EvaluationType, const Shader::FType& Type)
{
	if (TryMergePreparedType(Context, Type.StructType, Shader::GetValueTypeDescription(Type.ValueType).ComponentType))
	{
		if (EvaluationType != EExpressionEvaluationType::None)
		{
			const int32 NumComponents = Type.GetNumComponents();
			for (int32 Index = 0; Index < NumComponents; ++Index)
			{
				if (RequestedType.IsComponentRequested(Index))
				{
					PreparedType.SetComponentEvaluationType(Index, EvaluationType);
				}
			}
		}
	}
}

void FPrepareValueResult::SetType(FEmitContext& Context, const FRequestedType& RequestedType, EExpressionEvaluationType EvaluationType, Shader::EValueComponentType ComponentType)
{
	if (TryMergePreparedType(Context, nullptr, ComponentType))
	{
		if (EvaluationType != EExpressionEvaluationType::None)
		{
			const int32 NumComponents = RequestedType.GetNumComponents();
			for (int32 Index = 0; Index < NumComponents; ++Index)
			{
				if (RequestedType.IsComponentRequested(Index))
				{
					PreparedType.SetComponentEvaluationType(Index, EvaluationType);
				}
			}
		}
	}
}

void FPrepareValueResult::SetType(FEmitContext& Context, const FRequestedType& RequestedType, const FPreparedType& Type)
{
	if (TryMergePreparedType(Context, Type.StructType, Type.ValueComponentType))
	{
		const int32 NumComponents = RequestedType.GetNumComponents();
		for (int32 Index = 0; Index < NumComponents; ++Index)
		{
			if (RequestedType.IsComponentRequested(Index))
			{
				PreparedType.SetComponentEvaluationType(Index, Type.GetComponentEvaluationType(Index));
			}
		}
	}
}

void FPrepareValueResult::SetForwardValue(FEmitContext& Context, const FRequestedType& RequestedType, FExpression* InForwardValue)
{
	check(InForwardValue);
	if (InForwardValue != ForwardValue)
	{
		PreparedType = PrepareExpressionValue(Context, InForwardValue, RequestedType);
		ForwardValue = InForwardValue;
	}
}

void FExpression::EmitValueShader(FEmitContext& Context, const FRequestedType& RequestedType, FShaderValue& OutShader) const
{
	check(false);
}

void FExpression::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	check(false);
}

const TCHAR* FExpression::GetValueShader(FEmitContext& Context, const FRequestedType& RequestedType)
{
	if (PrepareValueResult.ForwardValue)
	{
		return PrepareValueResult.ForwardValue->GetValueShader(Context, RequestedType);
	}

	const EExpressionEvaluationType EvaluationType = PrepareValueResult.PreparedType.GetEvaluationType(RequestedType);
	check(EvaluationType != EExpressionEvaluationType::None);

	FScope* CurrentScope = Context.ScopeStack.Last();
	check(IsScopeLive(CurrentScope));

	FEmitShaderValue* ShaderValue = nullptr;
	if (EvaluationType == EExpressionEvaluationType::Constant || EvaluationType == EExpressionEvaluationType::Preshader)
	{
		ShaderValue = Context.AcquirePreshaderOrConstant(RequestedType, CurrentScope, this);
	}
	else
	{
		check(EvaluationType == EExpressionEvaluationType::Shader);

		TStringBuilder<1024> FormattedCode;
		FShaderValue ShaderResult(FormattedCode);
		FEmitShaderValueDependencies Dependencies;
		{
			FEmitShaderValueContext& ValueContext = Context.ShaderValueStack.AddDefaulted_GetRef();

			EmitValueShader(Context, RequestedType, ShaderResult);
			check(!ShaderResult.Type.IsVoid());

			Dependencies = MoveTemp(ValueContext.Dependencies);
			Context.ShaderValueStack.Pop(false);
		}
		ShaderValue = Context.AcquireShader(CurrentScope, ShaderResult, Dependencies);
	}

	check(ShaderValue);
	ShaderValue = Context.CastShaderValue(this, CurrentScope, ShaderValue, RequestedType.GetType());

	if (Context.ShaderValueStack.Num() > 0)
	{
		Context.ShaderValueStack.Last().Dependencies.Add(ShaderValue);
	}

	return ShaderValue->Reference;
}

const TCHAR* FExpression::GetValueShader(FEmitContext& Context)
{
	return GetValueShader(Context, GetRequestedType());
}

void FExpression::GetValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader)
{
	if (PrepareValueResult.ForwardValue)
	{
		return PrepareValueResult.ForwardValue->GetValuePreshader(Context, RequestedType, OutPreshader);
	}

	check(!bReentryFlag);
	const EExpressionEvaluationType EvaluationType = PrepareValueResult.PreparedType.GetEvaluationType(RequestedType);
	if (EvaluationType == EExpressionEvaluationType::Preshader)
	{
		bReentryFlag = true;
		EmitValuePreshader(Context, RequestedType, OutPreshader);
		bReentryFlag = false;
	}
	else
	{
		check(EvaluationType == EExpressionEvaluationType::Constant);
		const Shader::FValue ConstantValue = GetValueConstant(Context, RequestedType);
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(ConstantValue);
	}
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context, const FRequestedType& RequestedType)
{
	if (PrepareValueResult.ForwardValue)
	{
		return PrepareValueResult.ForwardValue->GetValueConstant(Context, RequestedType);
	}

	check(!bReentryFlag);
	check(PrepareValueResult.PreparedType.GetEvaluationType(RequestedType) == EExpressionEvaluationType::Constant);
	
	Shader::FPreshaderData ConstantPreshader;
	bReentryFlag = true;
	EmitValuePreshader(Context, RequestedType, ConstantPreshader);
	bReentryFlag = false;

	// Evaluate the constant preshader and store its value
	Shader::FPreshaderStack Stack;
	const Shader::FPreshaderValue PreshaderValue = ConstantPreshader.EvaluateConstant(*Context.Material, Stack);
	Shader::FValue Result = PreshaderValue.AsShaderValue(Context.TypeRegistry);

	const Shader::FType RequestedConstantType = RequestedType.GetType();
	if (Result.Type.IsNumeric() && RequestedConstantType.IsNumeric())
	{
		Result = Shader::Cast(Result, RequestedConstantType.ValueType);
	}

	check(Result.Type == RequestedConstantType);
	return Result;
}

bool FScope::HasParentScope(const FScope& InParentScope) const
{
	const FScope* CurrentScope = this;
	while (CurrentScope)
	{
		if (CurrentScope == &InParentScope)
		{
			return true;
		}
		CurrentScope = CurrentScope->ParentScope;
	}
	return false;
}

void FScope::AddPreviousScope(FScope& Scope)
{
	check(NumPreviousScopes < MaxNumPreviousScopes);
	PreviousScope[NumPreviousScopes++] = &Scope;
}

void FScope::InternalEmitCode(FEmitContext& Context, FCodeList& List, ENextScopeFormat ScopeFormat, FScope* Scope, const TCHAR* String, int32 Length)
{
	if (Scope && Scope->ContainedStatement && !Scope->ContainedStatement->bEmitShader)
	{
		Scope->ContainedStatement->bEmitShader = true;
		Context.ScopeStack.Add(Scope);
		Scope->ContainedStatement->EmitShader(Context);
		Context.ScopeStack.Pop();
	}

	const int32 SizeofString = sizeof(TCHAR) * Length;
	void* Memory = Context.Allocator->Alloc(sizeof(FCodeEntry) + SizeofString, alignof(FCodeEntry));
	FCodeEntry* CodeEntry = new(Memory) FCodeEntry();
	FMemory::Memcpy(CodeEntry->String, String, SizeofString);
	CodeEntry->String[Length] = 0;
	CodeEntry->Length = Length;
	CodeEntry->Scope = Scope;
	CodeEntry->ScopeFormat = ScopeFormat;
	CodeEntry->Next = nullptr;

	if (!List.First)
	{
		List.First = CodeEntry;
		List.Last = CodeEntry;
	}
	else
	{
		List.Last->Next = CodeEntry;
		List.Last = CodeEntry;
	}
	List.Num++;
}

bool PrepareScope(FEmitContext& Context, FScope* InScope)
{
	if (InScope && InScope->State == EScopeState::Uninitialized)
	{
		if (!InScope->ParentScope || PrepareScope(Context, InScope->ParentScope))
		{
			if (InScope->OwnerStatement)
			{
				InScope->OwnerStatement->Prepare(Context);
			}
			else
			{
				InScope->State = EScopeState::Live;
			}
		}
		else
		{
			InScope->State = EScopeState::Dead;
		}
	}

	return InScope && InScope->State != EScopeState::Dead;
}

FTree* FTree::Create(FMemStackBase& Allocator)
{
	FTree* Tree = new(Allocator) FTree();
	Tree->Allocator = &Allocator;
	Tree->RootScope = Tree->NewNode<FScope>();
	return Tree;
}

void FTree::Destroy(FTree* Tree)
{
	if (Tree)
	{
		FNode* Node = Tree->Nodes;
		while (Node)
		{
			FNode* Next = Node->NextNode;
			Node->~FNode();
			Node = Next;
		}
		Tree->~FTree();
		FMemory::Memzero(*Tree);
	}
}

//
static void WriteIndent(int32 IndentLevel, FStringBuilderBase& InOutString)
{
	const int32 Offset = InOutString.AddUninitialized(IndentLevel);
	TCHAR* Result = InOutString.GetData() + Offset;
	for (int32 i = 0; i < IndentLevel; ++i)
	{
		*Result++ = TCHAR('\t');
	}
}

void FScope::MarkLive()
{
	if (State == EScopeState::Uninitialized)
	{
		State = EScopeState::Live;
	}
}

void FScope::MarkLiveRecursive()
{
	return MarkLive();

	FScope* Scope = this;
	while (Scope && Scope->State == EScopeState::Uninitialized)
	{
		Scope->State = EScopeState::Live;
		Scope = Scope->ParentScope;
	}
}

void FScope::MarkDead()
{
	// TODO - mark child scopes as dead as well
	State = EScopeState::Dead;
}

void FScope::WriteHLSL(int32 Indent, FStringBuilderBase& OutString) const
{
	{
		const FCodeEntry* CodeDeclaration = Declarations.First;
		while (CodeDeclaration)
		{
			check(!CodeDeclaration->Scope);
			WriteIndent(Indent, OutString);
			OutString.Append(CodeDeclaration->String, CodeDeclaration->Length);
			OutString.Append(TEXT('\n'));
			CodeDeclaration = CodeDeclaration->Next;
		}
	}

	{
		const FCodeEntry* CodeStatement = Statements.First;
		while (CodeStatement)
		{
			if (CodeStatement->Length > 0)
			{
				WriteIndent(Indent, OutString);
				OutString.Append(CodeStatement->String, CodeStatement->Length);
				OutString.Append(TEXT('\n'));
			}
			if (CodeStatement->Scope)
			{
				int32 NextIndent = Indent;
				bool bNeedToCloseScope = false;
				if (CodeStatement->ScopeFormat == ENextScopeFormat::Scoped)
				{
					WriteIndent(Indent, OutString);
					OutString.Append(TEXT("{\n"));
					NextIndent++;
					bNeedToCloseScope = true;
				}

				CodeStatement->Scope->WriteHLSL(NextIndent, OutString);
				if (bNeedToCloseScope)
				{
					WriteIndent(Indent, OutString);
					OutString.Append(TEXT("}\n"));
				}
			}
			CodeStatement = CodeStatement->Next;
		}
	}
}

void FTree::ResetNodes()
{
	FNode* Node = Nodes;
	while (Node)
	{
		FNode* Next = Node->NextNode;
		Node->Reset();
		Node = Next;
	}
}

bool FTree::EmitShader(FEmitContext& Context, FStringBuilderBase& OutCode) const
{
	if (RootScope->ContainedStatement)
	{
		RootScope->ContainedStatement->bEmitShader = true;
		Context.ScopeStack.Add(RootScope);
		RootScope->ContainedStatement->EmitShader(Context);
		Context.ScopeStack.Pop(false);

		if (Context.Errors.Num() > 0)
		{
			return false;
		}
	}

	Context.Finalize();
	RootScope->WriteHLSL(1, OutCode);
	return Context.Errors.Num() == 0;
}

void FTree::RegisterExpression(FExpression* Expression)
{
}

void FTree::RegisterStatement(FScope& Scope, FStatement* Statement)
{
	check(!Scope.ContainedStatement)
	check(!Statement->ParentScope);
	Statement->ParentScope = &Scope;
	Scope.ContainedStatement = Statement;
}

FScope* FTree::NewScope(FScope& Scope)
{
	FScope* NewScope = NewNode<FScope>();
	NewScope->ParentScope = &Scope;
	NewScope->NestedLevel = Scope.NestedLevel + 1;
	NewScope->NumPreviousScopes = 0;
	return NewScope;
}

FScope* FTree::NewOwnedScope(FStatement& Owner)
{
	FScope* NewScope = NewNode<FScope>();
	NewScope->OwnerStatement = &Owner;
	NewScope->ParentScope = Owner.ParentScope;
	NewScope->NestedLevel = NewScope->ParentScope->NestedLevel + 1;
	NewScope->NumPreviousScopes = 0;
	return NewScope;
}

FTextureParameterDeclaration* FTree::NewTextureParameterDeclaration(const FName& Name, const FTextureDescription& DefaultValue)
{
	FTextureParameterDeclaration* Declaration = NewNode<FTextureParameterDeclaration>(Name, DefaultValue);
	return Declaration;
}

} // namespace HLSLTree
} // namespace UE