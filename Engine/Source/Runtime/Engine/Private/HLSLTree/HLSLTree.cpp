// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTree.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "MaterialShared.h" // TODO - split preshader out into its own module

UE::HLSLTree::EExpressionEvaluationType UE::HLSLTree::CombineEvaluationTypes(EExpressionEvaluationType Lhs, EExpressionEvaluationType Rhs)
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

const TCHAR* AllocateString(FMemStackBase& Allocator, const TCHAR* String, int32 Length)
{
	TCHAR* Result = New<TCHAR>(Allocator, Length + 1);
	FMemory::Memcpy(Result, String, Length * sizeof(TCHAR));
	Result[Length] = 0;
	return Result;
}

const TCHAR* AllocateString(FMemStackBase& Allocator, FStringView String)
{
	return AllocateString(Allocator, String.GetData(), String.Len());
}

const TCHAR* AllocateString(FMemStackBase& Allocator, const FStringBuilderBase& StringBuilder)
{
	return AllocateString(Allocator, StringBuilder.GetData(), StringBuilder.Len());
}

template <typename FormatType, typename... ArgTypes>
const TCHAR* AllocateStringf(FMemStackBase& Allocator, const FormatType& Format, ArgTypes... Args)
{
	TStringBuilder<1024> String;
	String.Appendf(Format, Forward<ArgTypes>(Args)...);
	return AllocateString(Allocator, String);
}

FSHAHash HashString(const TCHAR* String, int32 Length)
{
	FSHAHash Hash;
	FSHA1::HashBuffer(String, Length * sizeof(TCHAR), Hash.Hash);
	return Hash;
}

FSHAHash HashString(const FStringBuilderBase& StringBuilder)
{
	return HashString(StringBuilder.GetData(), StringBuilder.Len());
}

UE::HLSLTree::FErrors::FErrors(FMemStackBase& InAllocator)
	: Allocator(&InAllocator)
{
}

void UE::HLSLTree::FErrors::AddError(const FNode* InNode, FStringView InError)
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

UE::HLSLTree::FEmitContext::FEmitContext(FMemStackBase& InAllocator)
	: Allocator(&InAllocator)
	, Errors(InAllocator)
{
}

UE::HLSLTree::FEmitContext::~FEmitContext()
{
}

const TCHAR* UE::HLSLTree::FEmitContext::AcquireLocalDeclarationCode()
{
	return AllocateStringf(*Allocator, TEXT("Local%d"), NumExpressionLocals++);
}

const TCHAR* UE::HLSLTree::FEmitContext::CastShaderValue(const FNode* Node, const TCHAR* Code, const FType& SourceType, const FType& DestType, ECastFlags Flags)
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

// From MaterialUniformExpressions.cpp
extern void WriteMaterialUniformAccess(UE::Shader::EValueComponentType ComponentType, uint32 NumComponents, uint32 UniformOffset, FStringBuilderBase& OutResult);

void UE::HLSLTree::FEmitContext::AddPreshader(Shader::EValueType Type, const Shader::FPreshaderData& Preshader, FStringBuilderBase& OutCode)
{
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

	if (TypeDesc.ComponentType == Shader::EValueComponentType::Double)
	{
		if (TypeDesc.NumComponents == 1)
		{
			OutCode.Append(TEXT("MakeLWCScalar("));
		}
		else
		{
			OutCode.Appendf(TEXT("MakeLWCVector%d("), TypeDesc.NumComponents);
		}

		WriteMaterialUniformAccess(UE::Shader::EValueComponentType::Float, TypeDesc.NumComponents, UniformPreshaderOffset, OutCode); // Tile
		UniformPreshaderOffset += TypeDesc.NumComponents;
		OutCode.Append(TEXT(","));
		WriteMaterialUniformAccess(UE::Shader::EValueComponentType::Float, TypeDesc.NumComponents, UniformPreshaderOffset, OutCode); // Offset
		UniformPreshaderOffset += TypeDesc.NumComponents;
		OutCode.Append(TEXT(")"));
	}
	else
	{
		WriteMaterialUniformAccess(TypeDesc.ComponentType, TypeDesc.NumComponents, UniformPreshaderOffset, OutCode);
		UniformPreshaderOffset += TypeDesc.NumComponents;
	}
}

void UE::HLSLTree::FEmitContext::Finalize()
{
	MaterialCompilationOutput->UniformExpressionSet.UniformPreshaderBufferSize = (UniformPreshaderOffset + 3u) / 4u;
}

const TCHAR* UE::HLSLTree::FType::GetName() const
{
	if (StructType)
	{
		return StructType->Name;
	}

	check(ValueType != Shader::EValueType::Struct);
	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(ValueType);
	return TypeDesc.Name;
}

int32 UE::HLSLTree::FType::GetNumComponents() const
{
	if (StructType)
	{
		return StructType->ComponentTypes.Num();
	}
	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(ValueType);
	return TypeDesc.NumComponents;
}

bool UE::HLSLTree::FType::Merge(const FType& OtherType)
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

UE::HLSLTree::FStructFieldRef UE::HLSLTree::FStructType::FindFieldByName(const TCHAR* InName) const
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

UE::HLSLTree::FConstantValue::FConstantValue(const Shader::FValue& InValue) : Type(InValue.GetType())
{
	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(Type);
	Component.Empty(TypeDesc.NumComponents);
	for (int32 i = 0; i < TypeDesc.NumComponents; ++i)
	{
		Component.Add(InValue.Component[i]);
	}
}

UE::HLSLTree::FScope* UE::HLSLTree::FScope::FindSharedParent(FScope* Lhs, FScope* Rhs)
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

void UE::HLSLTree::FExpressionLocalPHI::UpdateType(FUpdateTypeContext& Context, const FRequestedType& RequestedType)
{
	FType TypePerValue[MaxNumPreviousScopes];
	int32 NumValidTypes = 0;
	FType CurrentType;

	auto UpdateValueTypes = [&]()
	{
		for (int32 i = 0; i < NumValues; ++i)
		{
			if (!TypePerValue[i])
			{
				TypePerValue[i] = RequestExpressionType(Context, Values[i], RequestedType);
				if (TypePerValue[i])
				{
					if (!CurrentType.Merge(TypePerValue[i]))
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
	SetType(Context, CurrentType);

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

void UE::HLSLTree::FExpressionLocalPHI::PrepareValue(FEmitContext& Context)
{
	FExpression* ForwardExpression = Values[0];
	for (int32 i = 1; i < NumValues; ++i)
	{
		if (Values[i] != ForwardExpression)
		{
			ForwardExpression = nullptr;
			break;
		}
	}

	// If we have the same value in all of our scopes, just forward that value directly; no need to create/assign a local variable
	if (ForwardExpression)
	{
		return SetValueForward(Context, ForwardExpression);
	}

	const int32 LocalPHIIndex = Context.NumLocalPHIs++;
	SetValueInlineShaderf(Context, TEXT("LocalPHI%d"), LocalPHIIndex);

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

	for (int32 i = 0; i < NumValues; ++i)
	{
		if (PrepareExpressionValue(Context, Values[i]) == EExpressionEvaluationType::None)
		{
			return Context.Errors.AddError(this, TEXT("Invalid LocalPHI"));
		}
	}

	const FType LocalType = GetType();

	bool bNeedToAddDeclaration = true;
	for (int32 i = 0; i < NumValues; ++i)
	{
		FScope* ValueScope = Scopes[i];
		const TCHAR* ValueCode = Values[i]->GetValueShader(Context, LocalType);
		ValueScope->MarkLiveRecursive();
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
		check(DeclarationScope->IsLive());
		DeclarationScope->EmitDeclarationf(Context, TEXT("%s LocalPHI%d;"),
			LocalType.GetName(),
			LocalPHIIndex);
	}
}

void UE::HLSLTree::FNodeVisitor::VisitNode(FNode* Node)
{
	if (Node)
	{
		Node->Visit(*this);
	}
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FStatement::Visit(FNodeVisitor& Visitor)
{
	return Visitor.OnStatement(*this);
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FExpression::Visit(FNodeVisitor& Visitor)
{
	return Visitor.OnExpression(*this);
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FTextureParameterDeclaration::Visit(FNodeVisitor& Visitor)
{
	return Visitor.OnTextureParameterDeclaration(*this);
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FScope::Visit(FNodeVisitor& Visitor)
{
	const ENodeVisitResult Result = Visitor.OnScope(*this);
	if (ShouldVisitDependentNodes(Result))
	{
		Visitor.VisitNode(Statement);
	}
	return Result;
}

UE::HLSLTree::FType UE::HLSLTree::RequestExpressionType(FUpdateTypeContext& Context, FExpression* InExpression, const FRequestedType& RequestedType)
{
	if (!InExpression)
	{
		return Shader::EValueType::Void;
	}

	if (InExpression->bReentryFlag)
	{
		// Valid for this to be called reentrantly
		// Code should ensure that the type is set before the reentrant call, otherwise type will not be valid here
		// LocalPHI nodes rely on this to break loops
		return InExpression->Type;
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
		return Shader::EValueType::Void;
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
		InExpression->UpdateType(Context, InExpression->CurrentRequestedType);
		InExpression->bReentryFlag = false;

		if (!InExpression->Type)
		{
			// If we failed to assign a valid type, reset the requested type as well
			// This ensures we'll try to compute a type again the next time we're called
			InExpression->CurrentRequestedType.Reset();
		}
	}

	return InExpression->Type;
}

UE::HLSLTree::EExpressionEvaluationType UE::HLSLTree::PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression)
{
	if (!InExpression)
	{
		return EExpressionEvaluationType::None;
	}

	if (!InExpression->Type)
	{
		// Can't prepare value if we didn't successfully prepare a type
		return EExpressionEvaluationType::None;
	}

	if (InExpression->EvaluationType == EExpressionEvaluationType::None)
	{
		check(!InExpression->bReentryFlag); // Re-entry is not supported

		InExpression->bReentryFlag = true;
		InExpression->PrepareValue(Context);
		InExpression->bReentryFlag = false;
	}
	return InExpression->EvaluationType;
}

void UE::HLSLTree::FExpression::SetType(FUpdateTypeContext& Context, const FType& InType)
{
	Type = InType;
}

void UE::HLSLTree::FExpression::InternalSetValueShader(FEmitContext& Context, const TCHAR* InCode, int32 InLength, bool bInline)
{
	check(!Code);
	check(EvaluationType == EExpressionEvaluationType::None);

	EvaluationType = EExpressionEvaluationType::Shader;
	if (bInline)
	{
		Code = AllocateString(*Context.Allocator, InCode, InLength);
	}
	else
	{
		// Check to see if we've already generated code for an equivalent expression in either this scope, or any outer visible scope
		const FSHAHash Hash = HashString(InCode, InLength);
		const TCHAR* Declaration = nullptr;

		FScope* CheckScope = ParentScope;
		while (CheckScope)
		{
			const TCHAR** FoundDeclaration = CheckScope->ExpressionCodeMap.Find(Hash);
			if (FoundDeclaration)
			{
				// Re-use results from previous matching expression
				Declaration = *FoundDeclaration;
				break;
			}
			CheckScope = CheckScope->ParentScope;
		}

		if (!Declaration)
		{
			check(ParentScope);
			Declaration = Context.AcquireLocalDeclarationCode();
			ParentScope->ExpressionCodeMap.Add(Hash, Declaration);
			ParentScope->MarkLiveRecursive();
			ParentScope->EmitStatementf(Context, TEXT("const %s %s = %s;"),
				Type.GetName(),
				Declaration,
				InCode);
		}
		Code = Declaration;
	}
}

void UE::HLSLTree::FExpression::SetValuePreshader(FEmitContext& Context, Shader::FPreshaderData& InPreshader)
{
	check(!Preshader);
	check(EvaluationType == EExpressionEvaluationType::None);
	check(!Type.IsStruct());

	EvaluationType = EExpressionEvaluationType::Preshader;
	Preshader = new Shader::FPreshaderData(MoveTemp(InPreshader));
}

void UE::HLSLTree::FExpression::SetValueConstant(FEmitContext& Context, const Shader::FValue& InValue)
{
	check(EvaluationType == EExpressionEvaluationType::None);
	check(!Type.IsStruct());

	EvaluationType = EExpressionEvaluationType::Constant;
	ConstantValue = Shader::Cast(InValue, Type);
}

void UE::HLSLTree::FExpression::SetValueForward(FEmitContext& Context, FExpression* Source)
{
	check(EvaluationType == EExpressionEvaluationType::None);
	check(Source);

	ensure(Type == Source->GetType());

	EvaluationType = PrepareExpressionValue(Context, Source);
	switch (EvaluationType)
	{
	case EExpressionEvaluationType::None: break;
	case EExpressionEvaluationType::Shader: Code = Source->Code; break;
	case EExpressionEvaluationType::Preshader: Preshader = Source->Preshader; break;
	case EExpressionEvaluationType::Constant: ConstantValue = Source->ConstantValue; break;
	default: checkNoEntry(); break;
	}
}

void UE::HLSLTree::FExpression::SetValuePreshader(FEmitContext& Context, EExpressionEvaluationType InEvaluationType, Shader::FPreshaderData& InPreshader)
{
	if (InEvaluationType == EExpressionEvaluationType::Constant)
	{
		// Evaluate the constant preshader and store its value
		FMaterialRenderContext RenderContext(nullptr, *Context.Material, nullptr);
		Shader::FValue Value;
		InPreshader.Evaluate(nullptr, RenderContext, Value);
		SetValueConstant(Context, Value);
	}
	else
	{
		check(InEvaluationType == EExpressionEvaluationType::Preshader);
		SetValuePreshader(Context, InPreshader);
	}
}

const TCHAR* UE::HLSLTree::FExpression::GetValueShader(FEmitContext& Context)
{
	check(EvaluationType != EExpressionEvaluationType::None);
	if (!Code)
	{
		TStringBuilder<1024> FormattedCode;
		if (EvaluationType == EExpressionEvaluationType::Constant)
		{
			ConstantValue.ToString(Shader::EValueStringFormat::HLSL, FormattedCode);
		}
		else
		{
			check(EvaluationType == EExpressionEvaluationType::Preshader);
			check(Preshader);
			Context.AddPreshader(Type, *Preshader, FormattedCode);
		}
		Code = AllocateString(*Context.Allocator, FormattedCode);
	}

	return Code;
}

const TCHAR* UE::HLSLTree::FExpression::GetValueShader(FEmitContext& Context, const FType& InType)
{
	check(EvaluationType != EExpressionEvaluationType::None);

	if (EvaluationType == EExpressionEvaluationType::Constant && Type != InType)
	{
		// If we need to cast a constant value, perform the cast first, then convert the result to HLSL
		const Shader::FValue CastConstantValue = Shader::Cast(ConstantValue, Type);

		TStringBuilder<1024> FormattedCode;
		CastConstantValue.ToString(Shader::EValueStringFormat::HLSL, FormattedCode);
		return AllocateString(*Context.Allocator, FormattedCode);
	}

	const ECastFlags CastFlags = ECastFlags::ReplicateScalar | ECastFlags::AllowAppendZeroes | ECastFlags::AllowTruncate;
	return Context.CastShaderValue(this, GetValueShader(Context), Type, InType, CastFlags);
}

void UE::HLSLTree::FExpression::GetValuePreshader(FEmitContext& Context, Shader::FPreshaderData& OutPreshader)
{
	switch (EvaluationType)
	{
	case EExpressionEvaluationType::Preshader:
		check(Preshader);
		OutPreshader.Append(*Preshader);
		break;
	case EExpressionEvaluationType::Constant:
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(ConstantValue);
		break;
	default:
		check(false);
		break;
	}
}

UE::Shader::FValue UE::HLSLTree::FExpression::GetValueConstant(FEmitContext& Context)
{
	check(EvaluationType == EExpressionEvaluationType::Constant);
	return ConstantValue;
}

bool UE::HLSLTree::FScope::HasParentScope(const FScope& InParentScope) const
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

void UE::HLSLTree::FScope::AddPreviousScope(FScope& Scope)
{
	check(NumPreviousScopes < MaxNumPreviousScopes);
	PreviousScope[NumPreviousScopes++] = &Scope;
}

namespace UE
{
namespace HLSLTree
{
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
} // namespace HLSLTree
} // namespace UE

void UE::HLSLTree::FScope::UseExpression(FExpression* Expression)
{
	// Need to move all dependent expressions/declarations into this scope
	FNodeVisitor_MoveToScope Visitor(this);
	Visitor.VisitNode(Expression);
}

void UE::HLSLTree::FScope::InternalEmitCode(FEmitContext& Context, FCodeList& List, FScope* NestedScope, const TCHAR* String, int32 Length)
{
	if (NestedScope && NestedScope->Statement && !NestedScope->Statement->bEmitHLSL)
	{
		NestedScope->Statement->bEmitHLSL = true;
		NestedScope->Statement->EmitHLSL(Context);
	}

	const int32 SizeofString = sizeof(TCHAR) * Length;
	void* Memory = Context.Allocator->Alloc(sizeof(FCodeEntry) + SizeofString, alignof(FCodeEntry));
	FCodeEntry* CodeEntry = new(Memory) FCodeEntry();
	FMemory::Memcpy(CodeEntry->String, String, SizeofString);
	CodeEntry->String[Length] = 0;
	CodeEntry->Length = Length;
	CodeEntry->NestedScope = NestedScope;
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

void UE::HLSLTree::RequestScopeTypes(FUpdateTypeContext& Context, const FScope* InScope)
{
	if (InScope && InScope->Statement)
	{
		InScope->Statement->RequestTypes(Context);
	}
}

UE::HLSLTree::FTree* UE::HLSLTree::FTree::Create(FMemStackBase& Allocator)
{
	FTree* Tree = new(Allocator) FTree();
	Tree->Allocator = &Allocator;
	Tree->RootScope = Tree->NewNode<FScope>();
	return Tree;
}

void UE::HLSLTree::FTree::Destroy(FTree* Tree)
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

void UE::HLSLTree::FScope::MarkLive()
{
	bLive = true;
}

void UE::HLSLTree::FScope::MarkLiveRecursive()
{
	FScope* Scope = this;
	while (Scope)
	{
		Scope->bLive = true;
		Scope = Scope->ParentScope;
	}
}

void UE::HLSLTree::FScope::WriteHLSL(int32 Indent, FStringBuilderBase& OutString) const
{
	{
		const FCodeEntry* CodeDeclaration = Declarations.First;
		while (CodeDeclaration)
		{
			check(!CodeDeclaration->NestedScope);
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
			if (CodeStatement->NestedScope)
			{
				WriteIndent(Indent, OutString);
				OutString.Append(TEXT("{\n"));
				CodeStatement->NestedScope->WriteHLSL(Indent + 1, OutString);
				WriteIndent(Indent, OutString);
				OutString.Append(TEXT("}\n"));
			}
			CodeStatement = CodeStatement->Next;
		}
	}
}

void UE::HLSLTree::FStructType::WriteHLSL(FStringBuilderBase& OutString) const
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

bool UE::HLSLTree::FTree::EmitHLSL(FEmitContext& Context,
	FStringBuilderBase& OutDeclarations,
	FStringBuilderBase& OutCode) const
{
	if (!ResultStatement)
	{
		return false;
	}

	{
		FUpdateTypeContext UpdateTypeContext(Context.Errors);
		RequestScopeTypes(UpdateTypeContext, RootScope);
		if (Context.Errors.Num() > 0)
		{
			return false;
		}
	}

	ResultStatement->bEmitHLSL = true;
	ResultStatement->EmitHLSL(Context);
	if (!RootScope->IsLive())
	{
		return false;
	}

	if (Context.Errors.Num() > 0)
	{
		return false;
	}

	if (RootScope->Statement)
	{
		RootScope->Statement->EmitHLSL(Context);
		if (Context.Errors.Num() > 0)
		{
			return false;
		}
	}

	Context.Finalize();

	RootScope->WriteHLSL(1, OutCode);

	const FStructType* StructType = StructTypes;
	while (StructType)
	{
		StructType->WriteHLSL(OutDeclarations);
		StructType = StructType->NextType;
	}

	return Context.Errors.Num() == 0;
}

void UE::HLSLTree::FTree::RegisterExpression(FScope& Scope, FExpression* Expression)
{
	check(!Expression->ParentScope);
	Expression->ParentScope = &Scope;
}

void UE::HLSLTree::FTree::RegisterStatement(FScope& Scope, FStatement* Statement)
{
	check(!Scope.Statement)
	check(!Statement->ParentScope);
	Statement->ParentScope = &Scope;
	Scope.Statement = Statement;
}

UE::HLSLTree::FScope* UE::HLSLTree::FTree::NewScope(FScope& Scope)
{
	FScope* NewScope = NewNode<FScope>();
	NewScope->ParentScope = &Scope;
	NewScope->NestedLevel = Scope.NestedLevel + 1;
	NewScope->NumPreviousScopes = 0;
	return NewScope;
}

UE::HLSLTree::FTextureParameterDeclaration* UE::HLSLTree::FTree::NewTextureParameterDeclaration(const FName& Name, const FTextureDescription& DefaultValue)
{
	FTextureParameterDeclaration* Declaration = NewNode<FTextureParameterDeclaration>(Name, DefaultValue);
	return Declaration;
}

namespace UE
{
namespace HLSLTree
{
namespace Private
{

static void SetComponentTypes(Shader::EValueComponentType* ComponentTypes, int32 ComponentIndex, const HLSLTree::FType& Type)
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

}
}
}

const UE::HLSLTree::FStructType* UE::HLSLTree::FTree::NewStructType(const FStructTypeInitializer& Initializer)
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

void UE::HLSLTree::FTree::SetResult(FStatement& InResult)
{
	check(!ResultStatement);
	ResultStatement = &InResult;
}

