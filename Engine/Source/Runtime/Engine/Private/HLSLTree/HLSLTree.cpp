// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTree.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "Shader/ShaderTypes.h"
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

bool UE::HLSLTree::FErrors::AddError(const FNode* InNode, FStringView InError)
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

	return false;
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

const TCHAR* UE::HLSLTree::FEmitContext::CastShaderValue(const FNode* Node, const TCHAR* Code, Shader::EValueType SourceType, Shader::EValueType DestType, ECastFlags Flags)
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

bool UE::HLSLTree::FExpressionLocalPHI::UpdateType(FUpdateTypeContext& Context, int8 InRequestedNumComponents)
{
	Shader::EValueType TypePerValue[MaxNumPreviousScopes] = { Shader::EValueType::Void };
	int32 NumValidTypes = 0;
	Shader::EValueComponentType ComponentType = Shader::EValueComponentType::Void;
	int8 NumComponents = 0;

	auto UpdateValueTypes = [&]()
	{
		for (int32 i = 0; i < NumValues; ++i)
		{
			if (TypePerValue[i] == Shader::EValueType::Void)
			{
				TypePerValue[i] = RequestExpressionType(Context, Values[i], InRequestedNumComponents);
				if (TypePerValue[i] != Shader::EValueType::Void)
				{
					const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(TypePerValue[i]);
					if (ComponentType == Shader::EValueComponentType::Void)
					{
						ComponentType = TypeDesc.ComponentType;
						NumComponents = TypeDesc.NumComponents;
					}
					else
					{
						NumComponents = FMath::Max(NumComponents, TypeDesc.NumComponents);
						if (ComponentType != TypeDesc.ComponentType)
						{
							return Context.Errors.AddError(this, TEXT("Type mismatch"));
						}
					}
					check(NumValidTypes < NumValues);
					NumValidTypes++;
				}
			}
		}
		return true;
	};

	// First try to assign all the values we can
	if (!UpdateValueTypes())
	{
		return false;
	}

	// Assuming we have at least one value with a valid type, we use that to initialize our type
	if (!SetType(Context, Shader::MakeValueType(ComponentType, NumComponents)))
	{
		return false;
	}

	if (NumValidTypes < NumValues)
	{
		// Now try to assign remaining types that failed the first iteration 
		if (!UpdateValueTypes())
		{
			return false;
		}
	}

	return NumValidTypes == NumValues;
}

bool UE::HLSLTree::FExpressionLocalPHI::PrepareValue(FEmitContext& Context)
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
	if (!SetValueInlineShaderf(Context, TEXT("LocalPHI%d"), LocalPHIIndex))
	{
		return false;
	}

	// Find the outermost scope to declare our local variable
	FScope* DeclarationScope = ParentScope;
	for (int32 i = 0; i < NumValues; ++i)
	{
		DeclarationScope = FScope::FindSharedParent(DeclarationScope, Scopes[i]);
		if (!DeclarationScope)
		{
			return false;
		}
	}

	for (int32 i = 0; i < NumValues; ++i)
	{
		if (PrepareExpressionValue(Context, Values[i]) == EExpressionEvaluationType::None)
		{
			return false;
		}
	}

	const Shader::EValueType Type = GetValueType();
	const Shader::FValueTypeDescription& TypeDesc = Shader::GetValueTypeDescription(Type);

	bool bNeedToAddDeclaration = true;
	for (int32 i = 0; i < NumValues; ++i)
	{
		FScope* ValueScope = Scopes[i];
		const TCHAR* ValueCode = Values[i]->GetValueShader(Context, Type);
		ValueScope->MarkLiveRecursive();
		if (ValueScope == DeclarationScope)
		{
			ValueScope->EmitDeclarationf(Context, TEXT("%s LocalPHI%d = %s;"),
				TypeDesc.Name,
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
			TypeDesc.Name,
			LocalPHIIndex);
	}

	return true;
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

UE::Shader::EValueType UE::HLSLTree::RequestExpressionType(FUpdateTypeContext& Context, FExpression* InExpression, int8 InRequestedNumComponents)
{
	if (!InExpression)
	{
		return Shader::EValueType::Void;
	}

	if (!InExpression->bReentryFlag && InRequestedNumComponents > InExpression->RequestedNumComponents)
	{
		InExpression->bReentryFlag = true;
		const bool bUpdateResult = InExpression->UpdateType(Context, InRequestedNumComponents);
		InExpression->bReentryFlag = false;

		if (bUpdateResult)
		{
			check(InExpression->ValueType != Shader::EValueType::Void);
			InExpression->RequestedNumComponents = InRequestedNumComponents;
		}
		else
		{
			InExpression->ValueType = Shader::EValueType::Void;
		}
	}

	return InExpression->ValueType;
}

UE::HLSLTree::EExpressionEvaluationType UE::HLSLTree::PrepareExpressionValue(FEmitContext& Context, FExpression* InExpression)
{
	if (!InExpression)
	{
		return EExpressionEvaluationType::None;
	}

	if (InExpression->ValueType == Shader::EValueType::Void)
	{
		// Can't prepare value if we didn't successfully prepare a type
		return EExpressionEvaluationType::None;
	}

	if (InExpression->EvaluationType == EExpressionEvaluationType::None)
	{
		check(!InExpression->bReentryFlag); // Re-entry is not supported

		InExpression->bReentryFlag = true;
		const bool bPrepareResult = InExpression->PrepareValue(Context);
		InExpression->bReentryFlag = false;

		if (bPrepareResult)
		{
			check(InExpression->EvaluationType != EExpressionEvaluationType::None);
		}
		else
		{
			InExpression->EvaluationType = EExpressionEvaluationType::None;
		}
	}
	return InExpression->EvaluationType;
}

bool UE::HLSLTree::FExpression::InternalSetValueShader(FEmitContext& Context, const TCHAR* InCode, int32 InLength, bool bInline)
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
			const Shader::FValueTypeDescription& TypeDesc = Shader::GetValueTypeDescription(ValueType);
			ParentScope->MarkLiveRecursive();
			ParentScope->EmitStatementf(Context, TEXT("const %s %s = %s;"),
				TypeDesc.Name,
				Declaration,
				InCode);
		}
		Code = Declaration;
	}

	return true;
}

bool UE::HLSLTree::FExpression::SetValuePreshader(FEmitContext& Context, Shader::FPreshaderData& InPreshader)
{
	check(!Preshader);
	check(EvaluationType == EExpressionEvaluationType::None);

	EvaluationType = EExpressionEvaluationType::Preshader;
	Preshader = new Shader::FPreshaderData(MoveTemp(InPreshader));
	return true;
}

bool UE::HLSLTree::FExpression::SetValueConstant(FEmitContext& Context, const Shader::FValue& InValue)
{
	check(EvaluationType == EExpressionEvaluationType::None);

	EvaluationType = EExpressionEvaluationType::Constant;
	ConstantValue = Shader::Cast(InValue, ValueType);
	return true;
}

bool UE::HLSLTree::FExpression::SetValueForward(FEmitContext& Context, FExpression* Source)
{
	check(EvaluationType == EExpressionEvaluationType::None);
	check(Source);

	EvaluationType = PrepareExpressionValue(Context, Source);
	switch (EvaluationType)
	{
	case EExpressionEvaluationType::None: return false;
	case EExpressionEvaluationType::Shader: Code = Source->Code; break;
	case EExpressionEvaluationType::Preshader: Preshader = Source->Preshader; break;
	case EExpressionEvaluationType::Constant: ConstantValue = Source->ConstantValue; break;
	default: checkNoEntry(); return false;
	}
	return true;
}

bool UE::HLSLTree::FExpression::SetValuePreshader(FEmitContext& Context, EExpressionEvaluationType InEvaluationType, Shader::FPreshaderData& InPreshader)
{
	if (InEvaluationType == EExpressionEvaluationType::Constant)
	{
		// Evaluate the constant preshader and store its value
		FMaterialRenderContext RenderContext(nullptr, *Context.Material, nullptr);
		Shader::FValue Value;
		InPreshader.Evaluate(nullptr, RenderContext, Value);
		return SetValueConstant(Context, Value);
	}
	else
	{
		check(InEvaluationType == EExpressionEvaluationType::Preshader);
		return SetValuePreshader(Context, InPreshader);
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
			Context.AddPreshader(ValueType, *Preshader, FormattedCode);
		}
		Code = AllocateString(*Context.Allocator, FormattedCode);
	}

	return Code;
}

const TCHAR* UE::HLSLTree::FExpression::GetValueShader(FEmitContext& Context, Shader::EValueType Type)
{
	check(EvaluationType != EExpressionEvaluationType::None);

	if (EvaluationType == EExpressionEvaluationType::Constant && Type != ValueType)
	{
		// If we need to cast a constant value, perform the cast first, then convert the result to HLSL
		const Shader::FValue CastConstantValue = Shader::Cast(ConstantValue, Type);

		TStringBuilder<1024> FormattedCode;
		CastConstantValue.ToString(Shader::EValueStringFormat::HLSL, FormattedCode);
		return AllocateString(*Context.Allocator, FormattedCode);
	}

	const ECastFlags CastFlags = ECastFlags::ReplicateScalar | ECastFlags::AllowAppendZeroes | ECastFlags::AllowTruncate;
	return Context.CastShaderValue(this, GetValueShader(Context), ValueType, Type, CastFlags);
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
		*Result++ = TEXT('\t');
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
			OutString.AppendChar(TEXT('\n'));
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
				OutString.AppendChar(TEXT('\n'));
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

bool UE::HLSLTree::FTree::EmitHLSL(FEmitContext& Context, FStringBuilderBase& Writer) const
{
	if (!ResultStatement)
	{
		return false;
	}

	{
		FUpdateTypeContext UpdateTypeContext(Context.Errors);
		RequestScopeTypes(UpdateTypeContext, RootScope);
	}

	ResultStatement->bEmitHLSL = true;
	ResultStatement->EmitHLSL(Context);
	if (!RootScope->IsLive())
	{
		return false;
	}

	if (RootScope->Statement)
	{
		RootScope->Statement->EmitHLSL(Context);
	}

	Context.Finalize();

	RootScope->WriteHLSL(1, Writer);
	return true;
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

void UE::HLSLTree::FTree::SetResult(FStatement& InResult)
{
	check(!ResultStatement);
	ResultStatement = &InResult;
}

