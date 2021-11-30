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


template<typename T, typename AllocatorType>
static TArrayView<T> AllocateArrayView(FMemStackBase& Allocator, const TArray<T, AllocatorType>& Array)
{
	T* Data = new(Allocator) T[Array.Num()];
	for (int32 i = 0; i < Array.Num(); ++i)
	{
		Data[i] = Array[i];
	}
	return MakeArrayView(Data, Array.Num());
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
				ShaderValue->Expression->GetType().GetName(),
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

void FExpressionLocalPHI::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult)
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
		return OutResult.SetForwardValue(Context, ForwardExpression, RequestedType);
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
		return OutResult.SetForwardValue(Context, ForwardExpression, RequestedType);
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
	OutResult.SetType(Context, EExpressionEvaluationType::Shader, CurrentType);

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
	FScope* DeclarationScope = Context.ScopeStack.Last();
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

		Context.ScopeStack.Add(ValueScope);
		const TCHAR* ValueCode = Values[i]->GetValueShader(Context, LocalType);
		Context.ScopeStack.Pop();

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

void FStatement::Reset()
{
	bEmitShader = false;
}

void FExpression::Reset()
{
	ShaderValue = nullptr;
	CurrentRequestedType.Reset();
	PrepareValueResult = FPrepareValueResult();
}

const FPrepareValueResult& PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression, const FRequestedType& RequestedType)
{
	static FPrepareValueResult EmptyResult;
	if (!InExpression)
	{
		return EmptyResult;
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
		return EmptyResult;
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

	FPrepareValueResult& Result = InExpression->PrepareValueResult;
	if (bNeedToUpdateType)
	{
		check(!InExpression->CurrentRequestedType.IsVoid());

		InExpression->bReentryFlag = true;
		InExpression->PrepareValue(Context, InExpression->CurrentRequestedType, Result);
		InExpression->bReentryFlag = false;

		if (Result.EvaluationType == EExpressionEvaluationType::None)
		{
			// If we failed to assign a valid type, reset the requested type as well
			// This ensures we'll try to compute a type again the next time we're called
			InExpression->CurrentRequestedType.Reset();
		}
		else if (Result.EvaluationType == EExpressionEvaluationType::Constant &&
			Result.Type != FType(Result.ConstantValue.GetType()))
		{
			Shader::FPreshaderData ConstantPreshader;
			InExpression->bReentryFlag = true;
			InExpression->EmitValuePreshader(Context, ConstantPreshader);
			InExpression->bReentryFlag = false;

			// Evaluate the constant preshader and store its value
			FMaterialRenderContext RenderContext(nullptr, *Context.Material, nullptr);
			Shader::FValue Value;
			ConstantPreshader.Evaluate(nullptr, RenderContext, Value);
			Result.ConstantValue = Shader::Cast(Value, Result.Type);
		}
	}

	return Result;
}

void FPrepareValueResult::SetConstant(FEmitContext& Context, const Shader::FValue& InValue)
{
	EvaluationType = EExpressionEvaluationType::Constant;
	Type = InValue.GetType();
	ConstantValue = InValue;
	ForwardValue = nullptr;
}

void FPrepareValueResult::SetType(FEmitContext& Context, EExpressionEvaluationType InEvaluationType, const FType& InType)
{
	EvaluationType = InEvaluationType;
	Type = InType;
	ForwardValue = nullptr;
}

void FPrepareValueResult::SetForwardValue(FEmitContext& Context, FExpression* InForwardValue, const FRequestedType& RequestedType)
{
	check(InForwardValue);
	const FPrepareValueResult& ForwardResult = PrepareExpressionValue(Context, InForwardValue, RequestedType);
	EvaluationType = ForwardResult.EvaluationType;
	Type = ForwardResult.Type;
	ConstantValue = ForwardResult.ConstantValue;
	ForwardValue = InForwardValue;
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

const TCHAR* FExpression::GetValueShader(FEmitContext& Context)
{
	check(PrepareValueResult.EvaluationType != EExpressionEvaluationType::None);
	if (PrepareValueResult.ForwardValue)
	{
		return PrepareValueResult.ForwardValue->GetValueShader(Context);
	}

	FScope* CurrentScope = Context.ScopeStack.Last();
	check(IsScopeLive(CurrentScope));

	if (!ShaderValue)
	{
		check(!bReentryFlag);

		TStringBuilder<1024> FormattedCode;
		if (PrepareValueResult.EvaluationType == EExpressionEvaluationType::Shader)
		{
			FShaderValue ShaderResult(FormattedCode);
			FEmitShaderValueDependencies Dependencies;
			{
				FEmitShaderValueContext& ValueContext = Context.ShaderValueStack.AddDefaulted_GetRef();

				bReentryFlag = true;
				EmitValueShader(Context, ShaderResult);
				bReentryFlag = false;

				Dependencies = MoveTemp(ValueContext.Dependencies);
				Context.ShaderValueStack.Pop(false);
			}

			if (ShaderResult.bInline)
			{
				ShaderValue = new(*Context.Allocator) FEmitShaderValue(this, CurrentScope);
				ShaderValue->Reference = AllocateString(*Context.Allocator, FormattedCode);
				ShaderValue->Dependencies = AllocateArrayView(*Context.Allocator, Dependencies);
			}
			else
			{
				// Check to see if we've already generated code for an equivalent expression
				const FSHAHash ShaderHash = HashString(FormattedCode);
				FEmitShaderValue** const PrevShaderValue = Context.ShaderValueMap.Find(ShaderHash);
				if (PrevShaderValue)
				{
					ShaderValue = *PrevShaderValue;
				}
				else
				{
					ShaderValue = new(*Context.Allocator) FEmitShaderValue(this, CurrentScope);
					ShaderValue->Hash = ShaderHash;
					ShaderValue->Reference = Context.AcquireLocalDeclarationCode();
					ShaderValue->Value = AllocateString(*Context.Allocator, FormattedCode);
					ShaderValue->Dependencies = AllocateArrayView(*Context.Allocator, Dependencies);
					Context.ShaderValueMap.Add(ShaderHash, ShaderValue);
				}
			}

			check(ShaderValue);
			if (ShaderResult.bHasDependencies)
			{
				bReentryFlag = true;
				EmitShaderDependencies(Context, ShaderResult);
				bReentryFlag = false;
			}
		}
		else if (PrepareValueResult.EvaluationType == EExpressionEvaluationType::Preshader)
		{
			Shader::FPreshaderData Preshader;

			bReentryFlag = true;
			EmitValuePreshader(Context, Preshader);
			bReentryFlag = false;

			ShaderValue = new(*Context.Allocator) FEmitShaderValue(this, CurrentScope);
			ShaderValue->Reference = Context.AcquirePreshader(PrepareValueResult.Type, Preshader);
		}
		else
		{
			check(PrepareValueResult.EvaluationType == EExpressionEvaluationType::Constant);
			PrepareValueResult.ConstantValue.ToString(Shader::EValueStringFormat::HLSL, FormattedCode);

			ShaderValue = new(*Context.Allocator) FEmitShaderValue(this, CurrentScope);
			ShaderValue->Reference = AllocateString(*Context.Allocator, FormattedCode);
		}
	}

	check(ShaderValue);
	Private::MoveToScope(ShaderValue, CurrentScope);

	if (Context.ShaderValueStack.Num() > 0)
	{
		Context.ShaderValueStack.Last().Dependencies.Add(ShaderValue);
	}
	return ShaderValue->Reference;
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
		if (PrepareValueResult.ForwardValue)
		{
			PrepareValueResult.ForwardValue->EmitValuePreshader(Context, OutPreshader);
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