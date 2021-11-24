// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTree.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "MaterialShared.h" // TODO - split preshader out into its own module

// From MaterialUniformExpressions.cpp
extern void WriteMaterialUniformAccess(UE::Shader::EValueComponentType ComponentType, uint32 NumComponents, uint32 UniformOffset, FStringBuilderBase& OutResult);

static const TCHAR* AllocateString(FMemStackBase& Allocator, const TCHAR* String, int32 Length)
{
	TCHAR* Result = New<TCHAR>(Allocator, Length + 1);
	FMemory::Memcpy(Result, String, Length * sizeof(TCHAR));
	Result[Length] = 0;
	return Result;
}

static const TCHAR* AllocateString(FMemStackBase& Allocator, FStringView String)
{
	return AllocateString(Allocator, String.GetData(), String.Len());
}

static const TCHAR* AllocateString(FMemStackBase& Allocator, const FStringBuilderBase& StringBuilder)
{
	return AllocateString(Allocator, StringBuilder.GetData(), StringBuilder.Len());
}

template <typename FormatType, typename... ArgTypes>
static const TCHAR* AllocateStringf(FMemStackBase& Allocator, const FormatType& Format, ArgTypes... Args)
{
	TStringBuilder<1024> String;
	String.Appendf(Format, Forward<ArgTypes>(Args)...);
	return AllocateString(Allocator, String);
}

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
	if (Lhs == EExpressionEvaluationType::Constant && Rhs == EExpressionEvaluationType::Constant)
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

FEmitContext::FEmitContext(FMemStackBase& InAllocator)
	: Allocator(&InAllocator)
	, Errors(InAllocator)
{
}

FEmitContext::~FEmitContext()
{
}

const TCHAR* FEmitContext::AcquireLocalDeclarationCode()
{
	return AllocateStringf(*Allocator, TEXT("Local%d"), NumExpressionLocals++);
}

const TCHAR* FEmitContext::CastShaderValue(const FNode* Node, const TCHAR* Code, const FType& SourceType, const FType& DestType, ECastFlags Flags)
{
	if (SourceType == DestType)
	{
		return Code;
	}

	const bool bAllowTruncate = EnumHasAnyFlags(Flags, ECastFlags::AllowTruncate);
	const bool bAllowAppendZeroes = EnumHasAnyFlags(Flags, ECastFlags::AllowAppendZeroes);
	bool bReplicateScalar = EnumHasAnyFlags(Flags, ECastFlags::ReplicateScalar);

	const Shader::FValueTypeDescription SourceTypeDesc = Shader::GetValueTypeDescription(SourceType);
	const Shader::FValueTypeDescription DestTypeDesc = Shader::GetValueTypeDescription(DestType);

	if (SourceTypeDesc.NumComponents > 0 && DestTypeDesc.NumComponents > 0)
	{
		if (SourceTypeDesc.NumComponents != 1)
		{
			bReplicateScalar = false;
		}
		if (!bReplicateScalar && !bAllowAppendZeroes && DestTypeDesc.NumComponents > SourceTypeDesc.NumComponents)
		{
			Errors.AddErrorf(Node, TEXT("Cannot cast from smaller type %s to larger type %s."), SourceTypeDesc.Name, DestTypeDesc.Name);
			return TEXT("");
		}
		if (!bReplicateScalar && !bAllowTruncate && DestTypeDesc.NumComponents < SourceTypeDesc.NumComponents)
		{
			Errors.AddErrorf(Node, TEXT("Cannot cast from larger type %s to smaller type %s."), SourceTypeDesc.Name, DestTypeDesc.Name);
			return TEXT("");
		}

		const bool bIsSourceLWC = SourceTypeDesc.ComponentType == Shader::EValueComponentType::Double;
		const bool bIsLWC = DestTypeDesc.ComponentType == Shader::EValueComponentType::Double;
		if (bIsLWC != bIsSourceLWC)
		{
			if (bIsLWC)
			{
				// float->LWC
				const TCHAR* CodeAsFloat = CastShaderValue(Node, Code, SourceType, Shader::MakeValueType(Shader::EValueComponentType::Float, DestTypeDesc.NumComponents), Flags);
				return AllocateStringf(*Allocator, TEXT("LWCPromote(%s)"), CodeAsFloat);
			}
			else
			{
				//LWC->float
				const TCHAR* CodeAsFloat = AllocateStringf(*Allocator, TEXT("LWCToFloat(%s)"), Code);
				return CastShaderValue(Node, CodeAsFloat, Shader::MakeValueType(Shader::EValueComponentType::Float, SourceTypeDesc.NumComponents), DestType, Flags);
			}
		}

		TStringBuilder<1024> Result;
		int32 NumComponents = 0;
		bool bNeedClosingParen = false;
		if (bIsLWC)
		{
			Result.Append(TEXT("MakeLWCVector("));
			bNeedClosingParen = true;
		}
		else
		{
			if (bReplicateScalar || SourceTypeDesc.NumComponents == DestTypeDesc.NumComponents)
			{
				NumComponents = DestTypeDesc.NumComponents;
				// Cast the scalar to the correct type, HLSL language will replicate the scalar if needed when performing this cast
				Result.Appendf(TEXT("((%s)%s)"), DestTypeDesc.Name, Code);
			}
			else
			{
				NumComponents = FMath::Min(SourceTypeDesc.NumComponents, DestTypeDesc.NumComponents);
				if (NumComponents < DestTypeDesc.NumComponents)
				{
					Result.Appendf(TEXT("%s("), DestTypeDesc.Name);
					bNeedClosingParen = true;
				}
				if (NumComponents == SourceTypeDesc.NumComponents && SourceTypeDesc.ComponentType == DestTypeDesc.ComponentType)
				{
					// If we're taking all the components from the source, can avoid adding a swizzle
					Result.Append(Code);
				}
				else
				{
					// Use a cast to truncate the source to the correct number of types
					const Shader::EValueType IntermediateType = Shader::MakeValueType(DestTypeDesc.ComponentType, NumComponents);
					const Shader::FValueTypeDescription IntermediateTypeDesc = Shader::GetValueTypeDescription(IntermediateType);
					Result.Appendf(TEXT("((%s)%s)"), IntermediateTypeDesc.Name, Code);
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
					Result.Append(TEXT(","));
				}
				if (bIsLWC)
				{
					if (!bReplicateScalar && ComponentIndex >= SourceTypeDesc.NumComponents)
					{
						check(bAllowAppendZeroes);
						Result.Append(TEXT("LWCPromote(0.0f)"));
					}
					else
					{
						Result.Appendf(TEXT("LWCGetComponent(%s, %d)"), Code, bReplicateScalar ? 0 : ComponentIndex);
					}
				}
				else
				{
					// Non-LWC case should only be zero-filling here, other cases should have already been handled
					check(bAllowAppendZeroes);
					check(!bReplicateScalar);
					check(ComponentIndex >= SourceTypeDesc.NumComponents);
					ZeroValue.ToString(Shader::EValueStringFormat::HLSL, Result);
				}
			}
			NumComponents = DestTypeDesc.NumComponents;
			Result.Append(TEXT(")"));
		}

		check(NumComponents == DestTypeDesc.NumComponents);
		return AllocateString(*Allocator, Result);
	}
	else
	{
		Errors.AddErrorf(Node, TEXT("Cannot cast between non-numeric types %s to %s."), SourceTypeDesc.Name, DestTypeDesc.Name);
		return TEXT("");
	}
}

const TCHAR* FEmitContext::AcquirePreshader(Shader::EValueType Type, const Shader::FPreshaderData& Preshader)
{
	FSHA1 Hasher;
	Hasher.Update((uint8*)&Type, sizeof(Type));
	Preshader.AppendHash(Hasher);
	const FSHAHash Hash = Hasher.Finalize();
	TCHAR const* const* PrevPreshader = Preshaders.Find(Hash);
	if (PrevPreshader)
	{
		return *PrevPreshader;
	}

	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(Type);
	ensure(TypeDesc.ComponentType == Shader::EValueComponentType::Float || TypeDesc.ComponentType == Shader::EValueComponentType::Double);

	FUniformExpressionSet& UniformExpressionSet = MaterialCompilationOutput->UniformExpressionSet;
	FMaterialUniformPreshaderHeader& PreshaderHeader = UniformExpressionSet.UniformPreshaders.AddDefaulted_GetRef();

	const uint32 RegisterOffset = UniformPreshaderOffset % 4;
	if (TypeDesc.ComponentType == Shader::EValueComponentType::Float && RegisterOffset + TypeDesc.NumComponents > 4u)
	{
		// If this uniform would span multiple registers, align offset to the next register to avoid this
		UniformPreshaderOffset = Align(UniformPreshaderOffset, 4u);
	}

	PreshaderHeader.OpcodeOffset = UniformExpressionSet.UniformPreshaderData.Num();
	UniformExpressionSet.UniformPreshaderData.Append(Preshader);
	PreshaderHeader.OpcodeSize = Preshader.Num();
	PreshaderHeader.BufferOffset = UniformPreshaderOffset;
	PreshaderHeader.ComponentType = TypeDesc.ComponentType;
	PreshaderHeader.NumComponents = TypeDesc.NumComponents;

	TStringBuilder<1024> FormattedCode;
	if (TypeDesc.ComponentType == Shader::EValueComponentType::Double)
	{
		if (TypeDesc.NumComponents == 1)
		{
			FormattedCode.Append(TEXT("MakeLWCScalar("));
		}
		else
		{
			FormattedCode.Appendf(TEXT("MakeLWCVector%d("), TypeDesc.NumComponents);
		}

		WriteMaterialUniformAccess(Shader::EValueComponentType::Float, TypeDesc.NumComponents, UniformPreshaderOffset, FormattedCode); // Tile
		UniformPreshaderOffset += TypeDesc.NumComponents;
		FormattedCode.Append(TEXT(","));
		WriteMaterialUniformAccess(Shader::EValueComponentType::Float, TypeDesc.NumComponents, UniformPreshaderOffset, FormattedCode); // Offset
		UniformPreshaderOffset += TypeDesc.NumComponents;
		FormattedCode.Append(TEXT(")"));
	}
	else
	{
		WriteMaterialUniformAccess(TypeDesc.ComponentType, TypeDesc.NumComponents, UniformPreshaderOffset, FormattedCode);
		UniformPreshaderOffset += TypeDesc.NumComponents;
	}

	const TCHAR* Code = AllocateString(*Allocator, FormattedCode);
	Preshaders.Add(Hash, Code);
	return Code;
}

void FEmitContext::Finalize()
{
	MaterialCompilationOutput->UniformExpressionSet.UniformPreshaderBufferSize = (UniformPreshaderOffset + 3u) / 4u;
}

const TCHAR* FType::GetName() const
{
	if (StructType)
	{
		return StructType->Name;
	}

	check(ValueType != Shader::EValueType::Struct);
	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(ValueType);
	return TypeDesc.Name;
}

int32 FType::GetNumComponents() const
{
	if (StructType)
	{
		return StructType->ComponentTypes.Num();
	}
	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(ValueType);
	return TypeDesc.NumComponents;
}

bool FType::Merge(const FType& OtherType)
{
	if (ValueType == Shader::EValueType::Void)
	{
		ValueType = OtherType.ValueType;
		StructType = OtherType.StructType;
		return true;
	}

	if (IsStruct() || OtherType.IsStruct())
	{
		return StructType == OtherType.StructType;
	}

	if (ValueType != OtherType.ValueType)
	{
		const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(ValueType);
		const Shader::FValueTypeDescription OtherTypeDesc = Shader::GetValueTypeDescription(OtherType);
		if (TypeDesc.ComponentType != OtherTypeDesc.ComponentType)
		{
			return false;
		}

		const int8 NumComponents = FMath::Max(TypeDesc.NumComponents, OtherTypeDesc.NumComponents);
		ValueType = Shader::MakeValueType(TypeDesc.ComponentType, NumComponents);
	}
	return true;
}

FStructFieldRef FStructType::FindFieldByName(const TCHAR* InName) const
{
	for (const FStructField& Field : Fields)
	{
		if (FCString::Strcmp(Field.Name, InName) == 0)
		{
			return FStructFieldRef(Field.Type, Field.ComponentIndex, Field.Type.GetNumComponents());
		}
	}

	return FStructFieldRef();
}

FConstantValue::FConstantValue(const Shader::FValue& InValue) : Type(InValue.GetType())
{
	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(Type);
	Component.Empty(TypeDesc.NumComponents);
	for (int32 i = 0; i < TypeDesc.NumComponents; ++i)
	{
		Component.Add(InValue.Component[i]);
	}
}

void FScope::Reset()
{
	State = EScopeState::Uninitialized;
	ExpressionCodeMap.Reset();
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

void FExpressionLocalPHI::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType)
{
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
		return SetForwardValue(Context, ForwardExpression, RequestedType);
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
		return SetForwardValue(Context, ForwardExpression, RequestedType);
	}

	FType TypePerValue[MaxNumPreviousScopes];
	int32 NumValidTypes = 0;
	FType CurrentType;

	auto UpdateValueTypes = [&]()
	{
		for (int32 i = 0; i < NumValues; ++i)
		{
			if (!TypePerValue[i] && PrepareScope(Context, Scopes[i]))
			{
				const FPrepareValueResult ValueResult = PrepareExpressionValue(Context, Values[i], RequestedType);
				if (ValueResult.Type)
				{
					TypePerValue[i] = ValueResult.Type;
					if (!CurrentType.Merge(ValueResult.Type))
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
	SetType(Context, EExpressionEvaluationType::Shader, CurrentType);

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

void FExpressionLocalPHI::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	const int32 LocalPHIIndex = Context.NumLocalPHIs++;
	OutShader.Code.Appendf(TEXT("LocalPHI%d"), LocalPHIIndex);
	OutShader.bInline = true;
	OutShader.bHasDependencies = true;
}

void FExpressionLocalPHI::EmitShaderDependencies(FEmitContext& Context, const FShaderValue& Shader) const
{
	// Find the outermost scope to declare our local variable
	FScope* DeclarationScope = ParentScope;
	for (int32 i = 0; i < NumValues; ++i)
	{
		DeclarationScope = FScope::FindSharedParent(DeclarationScope, Scopes[i]);
		if (!DeclarationScope)
		{
			return Context.Errors.AddError(this, TEXT("Invalid LocalPHI"));
		}
	}

	const FType LocalType = GetType();

	bool bNeedToAddDeclaration = true;
	for (int32 i = 0; i < NumValues; ++i)
	{
		FScope* ValueScope = Scopes[i];
		check(IsScopeLive(ValueScope));

		const TCHAR* ValueCode = Values[i]->GetValueShader(Context, LocalType);
		if (ValueScope == DeclarationScope)
		{
			ValueScope->EmitDeclarationf(Context, TEXT("%s %s = %s;"),
				LocalType.GetName(),
				Shader.Code.ToString(),
				ValueCode);
			bNeedToAddDeclaration = false;
		}
		else
		{
			ValueScope->EmitStatementf(Context, TEXT("%s = %s;"),
				Shader.Code.ToString(),
				ValueCode);
		}
	}

	if (bNeedToAddDeclaration)
	{
		check(IsScopeLive(DeclarationScope));
		DeclarationScope->EmitDeclarationf(Context, TEXT("%s %s;"),
			LocalType.GetName(),
			Shader.Code.ToString());
	}
}

void FNodeVisitor::VisitNode(FNode* Node)
{
	if (Node)
	{
		Node->Visit(*this);
	}
}

ENodeVisitResult FStatement::Visit(FNodeVisitor& Visitor)
{
	return Visitor.OnStatement(*this);
}

ENodeVisitResult FExpression::Visit(FNodeVisitor& Visitor)
{
	return Visitor.OnExpression(*this);
}

ENodeVisitResult FTextureParameterDeclaration::Visit(FNodeVisitor& Visitor)
{
	return Visitor.OnTextureParameterDeclaration(*this);
}

ENodeVisitResult FScope::Visit(FNodeVisitor& Visitor)
{
	const ENodeVisitResult Result = Visitor.OnScope(*this);
	if (ShouldVisitDependentNodes(Result))
	{
		Visitor.VisitNode(ContainedStatement);
	}
	return Result;
}

void FStatement::Reset()
{
	bEmitShader = false;
}

void FExpression::Reset()
{
	LocalVariableName = nullptr;
	Code = nullptr;
	ForwardedValue = nullptr;
	CurrentRequestedType.Reset();
	PrepareValueResult = FPrepareValueResult();
}

FPrepareValueResult PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression, const FRequestedType& RequestedType)
{
	if (!InExpression)
	{
		return FPrepareValueResult();
	}

	if (InExpression->bReentryFlag)
	{
		// Valid for this to be called reentrantly
		// Code should ensure that the type is set before the reentrant call, otherwise type will not be valid here
		// LocalPHI nodes rely on this to break loops
		return InExpression->PrepareValueResult;
	}

	bool bNeedToUpdateType = false;
	if (InExpression->CurrentRequestedType.RequestedComponents.Num() == 0)
	{
		InExpression->CurrentRequestedType = RequestedType;
		bNeedToUpdateType = !RequestedType.IsVoid();
	}
	else if(InExpression->CurrentRequestedType.StructType != RequestedType.StructType)
	{
		Context.Errors.AddError(InExpression, TEXT("Type mismatch"));
		return FPrepareValueResult();
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
		InExpression->PrepareValue(Context, InExpression->CurrentRequestedType);
		InExpression->bReentryFlag = false;

		if (InExpression->PrepareValueResult.EvaluationType == EExpressionEvaluationType::None)
		{
			// If we failed to assign a valid type, reset the requested type as well
			// This ensures we'll try to compute a type again the next time we're called
			InExpression->CurrentRequestedType.Reset();
		}
		else if (InExpression->PrepareValueResult.EvaluationType == EExpressionEvaluationType::Constant &&
			InExpression->PrepareValueResult.Type != FType(InExpression->PrepareValueResult.ConstantValue.GetType()))
		{
			Shader::FPreshaderData ConstantPreshader;
			InExpression->bReentryFlag = true;
			InExpression->EmitValuePreshader(Context, ConstantPreshader);
			InExpression->bReentryFlag = false;

			// Evaluate the constant preshader and store its value
			FMaterialRenderContext RenderContext(nullptr, *Context.Material, nullptr);
			Shader::FValue Value;
			ConstantPreshader.Evaluate(nullptr, RenderContext, Value);
			InExpression->PrepareValueResult.ConstantValue = Shader::Cast(Value, InExpression->PrepareValueResult.Type);
		}
	}

	return InExpression->PrepareValueResult;
}

void FExpression::SetType(FEmitContext& Context, EExpressionEvaluationType InEvaluationType, const FType& InType)
{
	PrepareValueResult.EvaluationType = InEvaluationType;
	PrepareValueResult.Type = InType;
	ForwardedValue = nullptr;
}

void FExpression::SetForwardValue(FEmitContext& Context, FExpression* InForwardValue, const FRequestedType& RequestedType)
{
	check(InForwardValue);
	PrepareValueResult = PrepareExpressionValue(Context, InForwardValue, RequestedType);
	ForwardedValue = InForwardValue;
}

void FExpression::EmitValueShader(FEmitContext& Context, FShaderValue& OutShader) const
{
	check(false);
}

void FExpression::EmitShaderDependencies(FEmitContext& Context, const FShaderValue& Shader) const
{
	check(false);
}

void FExpression::EmitValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader) const
{
	check(false);
}

const TCHAR* FExpression::GetValueShader(FEmitContext& Context)
{
	check(PrepareValueResult.EvaluationType != EExpressionEvaluationType::None);
	if (ForwardedValue)
	{
		return ForwardedValue->GetValueShader(Context);
	}

	if (!Code)
	{
		check(!bReentryFlag);

		TStringBuilder<1024> FormattedCode;
		if (PrepareValueResult.EvaluationType == EExpressionEvaluationType::Shader)
		{
			FShaderValue ShaderValue(FormattedCode);

			bReentryFlag = true;
			EmitValueShader(Context, ShaderValue);
			bReentryFlag = false;

			if (ShaderValue.bInline)
			{
				Code = AllocateString(*Context.Allocator, FormattedCode);
			}
			else
			{
				// Check to see if we've already generated code for an equivalent expression in either this scope, or any outer visible scope
				const FSHAHash Hash = HashString(FormattedCode);
				const TCHAR* Declaration = nullptr;

				FScope* CheckScope = ParentScope;
				while (CheckScope)
				{
					const TCHAR** FoundDeclaration = CheckScope->ExpressionCodeMap.Find(Hash);
					if (FoundDeclaration)
					{
						// Re-use results from previous matching expression
						check(IsScopeLive(CheckScope));
						Declaration = *FoundDeclaration;
						break;
					}
					CheckScope = CheckScope->ParentScope;
				}

				if (!Declaration)
				{
					check(IsScopeLive(ParentScope));
					Declaration = Context.AcquireLocalDeclarationCode();
					ParentScope->ExpressionCodeMap.Add(Hash, Declaration);
					ParentScope->EmitDeclarationf(Context, TEXT("const %s %s = %s;"),
						PrepareValueResult.Type.GetName(),
						Declaration,
						FormattedCode.ToString());
				}
				Code = Declaration;
			}
			check(Code);
			if (ShaderValue.bHasDependencies)
			{
				bReentryFlag = true;
				EmitShaderDependencies(Context, ShaderValue);
				bReentryFlag = false;
			}
		}
		else if (PrepareValueResult.EvaluationType == EExpressionEvaluationType::Preshader)
		{
			Shader::FPreshaderData Preshader;

			bReentryFlag = true;
			EmitValuePreshader(Context, Preshader);
			bReentryFlag = false;

			Code = Context.AcquirePreshader(PrepareValueResult.Type, Preshader);
		}
		else
		{
			check(PrepareValueResult.EvaluationType == EExpressionEvaluationType::Constant);
			PrepareValueResult.ConstantValue.ToString(Shader::EValueStringFormat::HLSL, FormattedCode);
			Code = AllocateString(*Context.Allocator, FormattedCode);
		}
	}

	check(Code);
	return Code;
}

const TCHAR* FExpression::GetValueShader(FEmitContext& Context, const FType& InType)
{
	if (PrepareValueResult.EvaluationType == EExpressionEvaluationType::Constant && PrepareValueResult.Type != InType)
	{
		// If we need to cast a constant value, perform the cast first, then convert the result to HLSL
		const Shader::FValue ConstantValue = Shader::Cast(PrepareValueResult.ConstantValue, InType);

		TStringBuilder<1024> FormattedCode;
		ConstantValue.ToString(Shader::EValueStringFormat::HLSL, FormattedCode);
		return AllocateString(*Context.Allocator, FormattedCode);
	}

	const ECastFlags CastFlags = ECastFlags::ReplicateScalar | ECastFlags::AllowAppendZeroes | ECastFlags::AllowTruncate;
	return Context.CastShaderValue(this, GetValueShader(Context), PrepareValueResult.Type, InType, CastFlags);
}

void FExpression::GetValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader)
{
	switch (PrepareValueResult.EvaluationType)
	{
	case EExpressionEvaluationType::Preshader:
		if (ForwardedValue)
		{
			ForwardedValue->EmitValuePreshader(Context, OutPreshader);
		}
		else
		{
			EmitValuePreshader(Context, OutPreshader);
		}
		break;
	case EExpressionEvaluationType::Constant:
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(PrepareValueResult.ConstantValue);
		break;
	default:
		check(false);
		break;
	}
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context)
{
	check(PrepareValueResult.EvaluationType == EExpressionEvaluationType::Constant);
	return PrepareValueResult.ConstantValue;
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

class FNodeVisitor_MoveToScope : public FNodeVisitor
{
public:
	explicit FNodeVisitor_MoveToScope(FScope* InScope) : Scope(InScope) {}

	virtual ENodeVisitResult OnScope(FScope& InScope) override
	{
		InScope.ParentScope = Scope;
		// Don't want to recurse into any child scopes
		return ENodeVisitResult::SkipDependentNodes;
	}

	virtual ENodeVisitResult OnExpression(FExpression& InExpression) override
	{
		InExpression.ParentScope = FScope::FindSharedParent(Scope, InExpression.ParentScope);
		return ENodeVisitResult::VisitDependentNodes;
	}

	FScope* Scope;
};

void FScope::UseExpression(FExpression* Expression)
{
	// Need to move all dependent expressions/declarations into this scope
	FNodeVisitor_MoveToScope Visitor(this);
	Visitor.VisitNode(Expression);
}

void FScope::InternalEmitCode(FEmitContext& Context, FCodeList& List, ENextScopeFormat ScopeFormat, FScope* Scope, const TCHAR* String, int32 Length)
{
	if (Scope && Scope->ContainedStatement && !Scope->ContainedStatement->bEmitShader)
	{
		Scope->ContainedStatement->bEmitShader = true;
		Scope->ContainedStatement->EmitShader(Context);
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

void FStructType::WriteHLSL(FStringBuilderBase& OutString) const
{
	OutString.Appendf(TEXT("struct %s\n"), Name);
	OutString.Append(TEXT("{\n"));
	for (const FStructField& Field : Fields)
	{
		OutString.Appendf(TEXT("\t%s %s;\n"), Field.Type.GetName(), Field.Name);
	}
	OutString.Append(TEXT("};\n"));

	for (const FStructField& Field : Fields)
	{
		OutString.Appendf(TEXT("%s %s_Set%s(%s Self, %s Value) { Self.%s = Value; return Self; }\n"),
			Name, Name, Field.Name, Name, Field.Type.GetName(), Field.Name);
	}
	OutString.Append(TEXT("\n"));
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

void FTree::EmitDeclarationsCode(FStringBuilderBase& OutCode) const
{
	const FStructType* StructType = StructTypes;
	while (StructType)
	{
		StructType->WriteHLSL(OutCode);
		StructType = StructType->NextType;
	}
}

bool FTree::EmitShader(FEmitContext& Context, FStringBuilderBase& OutCode) const
{
	if (RootScope->ContainedStatement)
	{
		RootScope->ContainedStatement->bEmitShader = true;
		RootScope->ContainedStatement->EmitShader(Context);
		if (Context.Errors.Num() > 0)
		{
			return false;
		}
	}

	Context.Finalize();
	RootScope->WriteHLSL(1, OutCode);
	return Context.Errors.Num() == 0;
}

void FTree::RegisterExpression(FScope& Scope, FExpression* Expression)
{
	check(!Expression->ParentScope);
	Expression->ParentScope = &Scope;
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

namespace Private
{
void SetComponentTypes(Shader::EValueComponentType* ComponentTypes, int32 ComponentIndex, const HLSLTree::FType& Type)
{
	if (Type.IsStruct())
	{
		for (const FStructField& Field : Type.StructType->Fields)
		{
			SetComponentTypes(ComponentTypes, ComponentIndex + Field.ComponentIndex, Field.Type);
		}
	}
	else
	{
		const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(Type);
		for (int32 i = 0; i < TypeDesc.NumComponents; ++i)
		{
			ComponentTypes[ComponentIndex + i] = TypeDesc.ComponentType;
		}
	}
}
} // namespace Private

const FStructType* FTree::NewStructType(const FStructTypeInitializer& Initializer)
{
	const int32 NumFields = Initializer.Fields.Num();
	int32 ComponentIndex = 0;
	FStructField* Fields = new(*Allocator) FStructField[NumFields];
	for (int32 FieldIndex = 0; FieldIndex < NumFields; ++FieldIndex)
	{
		const FStructFieldInitializer& FieldInitializer = Initializer.Fields[FieldIndex];
		const FType& FieldType = FieldInitializer.Type;
		FStructField& Field = Fields[FieldIndex];
		Field.Name = AllocateString(*Allocator, FieldInitializer.Name);
		Field.Type = FieldType;
		Field.ComponentIndex = ComponentIndex;
		ComponentIndex += FieldType.GetNumComponents();
	}

	Shader::EValueComponentType* ComponentTypes = new(*Allocator) Shader::EValueComponentType[ComponentIndex];
	for (int32 FieldIndex = 0; FieldIndex < NumFields; ++FieldIndex)
	{
		const FStructField& Field = Fields[FieldIndex];
		Private::SetComponentTypes(ComponentTypes, Field.ComponentIndex, Field.Type);
	}

	FStructType* StructType = NewNode<FStructType>();
	StructType->Name = AllocateString(*Allocator, Initializer.Name);
	StructType->Fields = MakeArrayView(Fields, NumFields);
	StructType->ComponentTypes = MakeArrayView(ComponentTypes, ComponentIndex);
	StructType->NextType = StructTypes;
	StructTypes = StructType;
	return StructType;
}

} // namespace HLSLTree
} // namespace UE