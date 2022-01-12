// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTree.h"
#include "HLSLTree/HLSLTreeEmit.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "Misc/MemStackUtility.h"
#include "Shader/ShaderTypes.h"
#include "Shader/Preshader.h"

namespace UE
{
namespace HLSLTree
{

/**
 * Represents a phi node (see various topics on single static assignment)
 * A phi node takes on a value based on the previous scope that was executed.
 * In practice, this means the generated HLSL code will declare a local variable before all the previous scopes, then assign that variable the proper value from within each scope
 */
class FExpressionLocalPHI final : public FExpression
{
public:
	FExpressionLocalPHI(const FName& InLocalName, TArrayView<FScope*> InPreviousScopes) : LocalName(InLocalName), NumValues(InPreviousScopes.Num())
	{
		for (int32 i = 0; i < InPreviousScopes.Num(); ++i)
		{
			Scopes[i] = InPreviousScopes[i];
			Values[i] = nullptr;
		}
	}

	FExpressionLocalPHI(const FExpressionLocalPHI* Source, EDerivativeCoordinate Coord);

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual bool PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;

	TArray<EDerivativeCoordinate, TInlineAllocator<8>> DerivativeChain;
	FName LocalName;
	FScope* Scopes[MaxNumPreviousScopes];
	FExpression* Values[MaxNumPreviousScopes];
	int32 NumValues = 0;
};

/**
 * Represents a call to a function that includes its own scope/control-flow
 * Scope for the function will be linked into the generated material
 */
class FExpressionFunctionCall final : public FExpression
{
public:
	FExpressionFunctionCall(FFunction* InFunction, int32 InOutputIndex) : Function(InFunction), OutputIndex(InOutputIndex) {}

	virtual bool PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;

	FFunction* Function;
	int32 OutputIndex;
};

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

EExpressionEvaluation CombineEvaluations(EExpressionEvaluation Lhs, EExpressionEvaluation Rhs)
{
	if (Lhs == EExpressionEvaluation::None)
	{
		// If either is 'None', return the other
		return Rhs;
	}
	else if (Rhs == EExpressionEvaluation::None)
	{
		return Lhs;
	}
	else if (Lhs == EExpressionEvaluation::Constant && Rhs == EExpressionEvaluation::Constant)
	{
		// 2 constants make a constant
		return EExpressionEvaluation::Constant;
	}
	else if (Lhs == EExpressionEvaluation::Shader || Rhs == EExpressionEvaluation::Shader)
	{
		// If either requires shader, shader is required
		return EExpressionEvaluation::Shader;
	}
	// Any combination of constants/preshader can make a preshader
	return EExpressionEvaluation::Preshader;
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

FExpressionLocalPHI::FExpressionLocalPHI(const FExpressionLocalPHI* Source, EDerivativeCoordinate Coord)
	: DerivativeChain(Source->DerivativeChain)
	, LocalName(Source->LocalName)
	, NumValues(Source->NumValues)
{
	DerivativeChain.Add(Coord);
	for (int32 i = 0; i < NumValues; ++i)
	{
		Scopes[i] = Source->Scopes[i];
	}
}

void FExpressionLocalPHI::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// We don't have values assigned at the time analytic derivatives are computed
	// It's possible the derivatives will be end up being invalid, but that case will need to be detected later, during PrepareValue
	OutResult.ExpressionDdx = Tree.NewExpression<FExpressionLocalPHI>(this, EDerivativeCoordinate::Ddx);
	OutResult.ExpressionDdy = Tree.NewExpression<FExpressionLocalPHI>(this, EDerivativeCoordinate::Ddy);
}

bool FExpressionLocalPHI::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
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
		if (Context.PrepareScope(Scopes[i]))
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
			if (TypePerValue[i].IsVoid() && Context.PrepareScope(Scopes[i]))
			{
				const FPreparedType& ValueType = Context.PrepareExpression(Values[i], RequestedType);
				if (!ValueType.IsVoid())
				{
					TypePerValue[i] = ValueType;
					const FPreparedType MergedType = MergePreparedTypes(CurrentType, ValueType);
					if (MergedType.IsVoid())
					{
						return Context.Errors->AddErrorf(TEXT("Mismatched types for local variable %s and %s"),
							CurrentType.GetType().GetName(),
							ValueType.GetType().GetName());
					}
					CurrentType = MergedType;
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
	CurrentType.SetEvaluation(EExpressionEvaluation::Shader); // TODO - No support for preshader flow control
	if (!OutResult.SetType(Context, RequestedType, CurrentType))
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
		if (NumValidTypes < NumValues)
		{
			return Context.Errors->AddError(TEXT("Failed to compute all types for LocalPHI"));
		}
	}

	return true;
}

void FExpressionLocalPHI::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderExpression* const* PrevEmitExpression = Context.EmitLocalPHIMap.Find(this);
	FEmitShaderExpression* EmitExpression = PrevEmitExpression ? *PrevEmitExpression : nullptr;
	if (!EmitExpression)
	{
		const int32 LocalPHIIndex = Context.NumExpressionLocalPHIs++;
		const FRequestedType LocalType = GetRequestedType();

		// This is the first time we've emitted shader code for this PHI
		// Create an expression and add it to the map first, so if this is called recursively this path will only be taken the first time
		EmitExpression = OutResult.Code = Context.EmitInlineExpression(Scope,
			LocalType.GetType(),
			TEXT("LocalPHI%"), LocalPHIIndex);
		Context.EmitLocalPHIMap.Add(this, EmitExpression);

		// Find the outermost scope to declare our local variable
		FEmitScope* EmitDeclarationScope = &Scope;
		FEmitScope* EmitValueScopes[MaxNumPreviousScopes] = { nullptr };
		for (int32 i = 0; i < NumValues; ++i)
		{
			EmitValueScopes[i] = Context.AcquireEmitScope(Scopes[i]);
			EmitDeclarationScope = FEmitScope::FindSharedParent(EmitDeclarationScope, EmitValueScopes[i]);
			if (!EmitDeclarationScope)
			{
				Context.Errors->AddError(TEXT("Invalid LocalPHI"));
				return;
			}
		}

		FEmitShaderStatement* EmitDeclaration = nullptr;
		for (int32 i = 0; i < NumValues; ++i)
		{
			FEmitScope* EmitValueScope = EmitValueScopes[i];
			if (EmitValueScope == EmitDeclarationScope)
			{
				FEmitShaderExpression* ShaderValue = Values[i]->GetValueShader(Context, *EmitValueScope, LocalType);
				EmitDeclaration = Context.EmitStatement(*EmitValueScope, TEXT("% LocalPHI% = %;"),
					LocalType.GetName(),
					LocalPHIIndex,
					ShaderValue);
				break;
			}
		}
		if (!EmitDeclaration)
		{
			EmitDeclaration = Context.EmitStatement(*EmitDeclarationScope, TEXT("% LocalPHI%;"),
				LocalType.GetName(),
				LocalPHIIndex);
		}

		FEmitShaderNode* Dependencies[MaxNumPreviousScopes] = { nullptr };
		int32 NumDependencies = 0;
		for (int32 i = 0; i < NumValues; ++i)
		{
			FEmitScope* EmitValueScope = EmitValueScopes[i];
			if (EmitValueScope != EmitDeclarationScope)
			{
				FEmitShaderExpression* ShaderValue = Values[i]->GetValueShader(Context, *EmitValueScope, LocalType);
				FEmitShaderStatement* EmitAssignment = Context.EmitStatementWithDependency(*EmitValueScope, EmitDeclaration, TEXT("LocalPHI% = %;"),
					LocalPHIIndex,
					ShaderValue);
				Dependencies[NumDependencies++] = EmitAssignment;
			}
		}

		// Fill in the expression's dependencies
		EmitExpression->Dependencies = MemStack::AllocateArrayView(*Context.Allocator, MakeArrayView(Dependencies, NumDependencies));
	}

	OutResult.Code = EmitExpression;
}

bool FExpressionFunctionCall::PrepareValue(FEmitContext& Context, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	if (!Context.PrepareScopeWithParent(Function->RootScope, Function->CalledScope))
	{
		return false;
	}

	FPreparedType OutputType = Context.PrepareExpression(Function->OutputExpressions[OutputIndex], RequestedType);
	return OutResult.SetType(Context, RequestedType, OutputType);
}

void FExpressionFunctionCall::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderNode* const* PrevDependency = Context.EmitFunctionMap.Find(Function);
	FEmitShaderNode* Dependency = PrevDependency ? *PrevDependency : nullptr;
	if (!Dependency)
	{
		// Inject the function's root scope at scope where it's called
		FEmitScope* EmitCalledScope = Context.AcquireEmitScope(Function->CalledScope);
		Dependency = Context.EmitNextScope(*EmitCalledScope, Function->RootScope);
		Context.EmitFunctionMap.Add(Function, Dependency);
	}

	FEmitShaderExpression* EmitFunctionOutput = Function->OutputExpressions[OutputIndex]->GetValueShader(Context, Scope, RequestedType);
	OutResult.Code = Context.EmitInlineExpressionWithDependency(Scope, Dependency, EmitFunctionOutput->Type, TEXT("%"), EmitFunctionOutput);
}

void FExpression::Reset()
{
	CurrentRequestedType.Reset();
	PrepareValueResult = FPrepareValueResult();
}

FRequestedType::FRequestedType(int32 NumComponents, bool bDefaultRequest)
{
	RequestedComponents.Init(bDefaultRequest, NumComponents);
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

void FRequestedType::SetComponentRequest(int32 Index, bool bRequested)
{
	if (bRequested)
	{
		RequestedComponents.PadToNum(Index + 1, false);
	}
	if (RequestedComponents.IsValidIndex(Index))
	{
		RequestedComponents[Index] = bRequested;
	}
}

void FRequestedType::SetFieldRequested(const Shader::FStructField* Field, bool bRequested)
{
	const int32 NumComponents = Field->GetNumComponents();
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		SetComponentRequest(Field->ComponentIndex + Index, bRequested);
	}
}

void FRequestedType::SetField(const Shader::FStructField* Field, const FRequestedType& InRequest)
{
	const int32 NumComponents = Field->GetNumComponents();
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		SetComponentRequest(Field->ComponentIndex + Index, InRequest.IsComponentRequested(Index));
	}
}

FRequestedType FRequestedType::GetField(const Shader::FStructField* Field) const
{
	FRequestedType Result(Field->Type);
	const int32 NumComponents = Field->GetNumComponents();
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		Result.SetComponentRequest(Index, IsComponentRequested(Field->ComponentIndex + Index));
	}
	return Result;
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
		const int32 MaxComponentIndex = PreparedComponents.FindLastByPredicate([](EExpressionEvaluation InEvaluation) { return InEvaluation != EExpressionEvaluation::None; });
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
			const EExpressionEvaluation ComponentEvaluation = GetComponentEvaluation(Index);
			if (ComponentEvaluation != EExpressionEvaluation::None)
			{
				Result.SetComponentRequest(Index);
			}
		}
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetEvaluation() const
{
	EExpressionEvaluation Result = EExpressionEvaluation::None;
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		Result = CombineEvaluations(Result, PreparedComponents[Index]);
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetEvaluation(const FRequestedType& RequestedType) const
{
	EExpressionEvaluation Result = EExpressionEvaluation::None;
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			Result = CombineEvaluations(Result, PreparedComponents[Index]);
		}
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetFieldEvaluation(int32 ComponentIndex, int32 NumComponents) const
{
	EExpressionEvaluation Result = EExpressionEvaluation::None;
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		Result = CombineEvaluations(Result, GetComponentEvaluation(ComponentIndex + Index));
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetComponentEvaluation(int32 Index) const
{
	return PreparedComponents.IsValidIndex(Index) ? PreparedComponents[Index] : EExpressionEvaluation::None;
}

void FPreparedType::SetComponentEvaluation(int32 Index, EExpressionEvaluation Evaluation)
{
	if (Evaluation != EExpressionEvaluation::None && Index >= PreparedComponents.Num())
	{
		PreparedComponents.AddZeroed(Index + 1 - PreparedComponents.Num());
	}
	if (PreparedComponents.IsValidIndex(Index))
	{
		PreparedComponents[Index] = Evaluation;
	}
}

void FPreparedType::MergeComponentEvaluation(int32 Index, EExpressionEvaluation Evaluation)
{
	if (Evaluation != EExpressionEvaluation::None && Index >= PreparedComponents.Num())
	{
		PreparedComponents.AddZeroed(Index + 1 - PreparedComponents.Num());
	}
	if (PreparedComponents.IsValidIndex(Index))
	{
		PreparedComponents[Index] = CombineEvaluations(PreparedComponents[Index], Evaluation);
	}
}

void FPreparedType::SetEvaluation(EExpressionEvaluation Evaluation)
{
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		if (PreparedComponents[Index] != EExpressionEvaluation::None)
		{
			PreparedComponents[Index] = Evaluation;
		}
	}
}

void FPreparedType::SetField(const Shader::FStructField* Field, const FPreparedType& FieldType)
{
	for (int32 Index = 0; Index < Field->GetNumComponents(); ++Index)
	{
		SetComponentEvaluation(Field->ComponentIndex + Index, FieldType.GetComponentEvaluation(Index));
	}
}

FPreparedType FPreparedType::GetFieldType(const Shader::FStructField* Field) const
{
	FPreparedType Result(Field->Type);
	for (int32 Index = 0; Index < Field->GetNumComponents(); ++Index)
	{
		Result.SetComponentEvaluation(Index, GetComponentEvaluation(Field->ComponentIndex + Index));
	}
	return Result;
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
		const EExpressionEvaluation LhsEvaluation = Lhs.GetComponentEvaluation(Index);
		const EExpressionEvaluation RhsEvaluation = Rhs.GetComponentEvaluation(Index);
		Result.SetComponentEvaluation(Index, CombineEvaluations(LhsEvaluation, RhsEvaluation));
	}

	return Result;
}

bool FPrepareValueResult::TryMergePreparedType(FEmitContext& Context, const Shader::FStructType* StructType, Shader::EValueComponentType ComponentType)
{
	// If we previously had a forwarded value set, reset that and start over
	if (ForwardValue || !PreparedType.IsInitialized())
	{
		PreparedType.PreparedComponents.Reset();
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
			return Context.Errors->AddError(TEXT("Invalid type"));
		}
	}
	else
	{
		if (ComponentType == Shader::EValueComponentType::Void)
		{
			return false;
		}
		PreparedType.ValueComponentType = Shader::CombineComponentTypes(PreparedType.ValueComponentType, ComponentType);
	}

	return true;
}

bool FPrepareValueResult::SetTypeVoid()
{
	PreparedType.PreparedComponents.Reset();
	PreparedType.ValueComponentType = Shader::EValueComponentType::Void;
	PreparedType.StructType = nullptr;
	ForwardValue = nullptr;
	return false;
}

bool FPrepareValueResult::SetType(FEmitContext& Context, const FRequestedType& RequestedType, EExpressionEvaluation Evaluation, const Shader::FType& Type)
{
	if (TryMergePreparedType(Context, Type.StructType, Shader::GetValueTypeDescription(Type.ValueType).ComponentType))
	{
		if (Evaluation != EExpressionEvaluation::None)
		{
			const int32 NumComponents = Type.GetNumComponents();
			for (int32 Index = 0; Index < NumComponents; ++Index)
			{
				if (RequestedType.IsComponentRequested(Index))
				{
					PreparedType.MergeComponentEvaluation(Index, Evaluation);
				}
			}
		}
		return true;
	}
	return false;
}

bool FPrepareValueResult::SetType(FEmitContext& Context, const FRequestedType& RequestedType, const FPreparedType& Type)
{
	if (TryMergePreparedType(Context, Type.StructType, Type.ValueComponentType))
	{
		const int32 NumComponents = RequestedType.GetNumComponents();
		for (int32 Index = 0; Index < NumComponents; ++Index)
		{
			if (RequestedType.IsComponentRequested(Index))
			{
				PreparedType.MergeComponentEvaluation(Index, Type.GetComponentEvaluation(Index));
			}
		}
		return true;
	}
	return false;
}

bool FPrepareValueResult::SetForwardValue(FEmitContext& Context, const FRequestedType& RequestedType, FExpression* InForwardValue)
{
	if (InForwardValue != ForwardValue)
	{
		PreparedType = Context.PrepareExpression(InForwardValue, RequestedType);
		ForwardValue = InForwardValue;
	}
	return (bool)InForwardValue;
}

void FExpression::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// nop
}

void FExpression::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	check(false);
}

void FExpression::EmitValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	check(false);
}

FEmitShaderExpression* FExpression::GetValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, const Shader::FType& ResultType)
{
	FOwnerScope OwnerScope(*Context.Errors, GetOwner());
	if (PrepareValueResult.ForwardValue)
	{
		return PrepareValueResult.ForwardValue->GetValueShader(Context, Scope, RequestedType, ResultType);
	}

	const EExpressionEvaluation Evaluation = PrepareValueResult.PreparedType.GetEvaluation(RequestedType);
	check(Evaluation != EExpressionEvaluation::None);

	FEmitShaderExpression* Value = nullptr;
	if (Evaluation == EExpressionEvaluation::Constant || Evaluation == EExpressionEvaluation::Preshader)
	{
		Value = Context.EmitPreshaderOrConstant(Scope, RequestedType, this);
	}
	else
	{
		check(Evaluation == EExpressionEvaluation::Shader);
		FEmitValueShaderResult Result;
		EmitValueShader(Context, Scope, RequestedType, Result);
		Value = Result.Code;
	}

	Value = Context.EmitCast(Scope, Value, ResultType);
	return Value;
}

FEmitShaderExpression* FExpression::GetValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType)
{
	return GetValueShader(Context, Scope, RequestedType, RequestedType.GetType());
}

FEmitShaderExpression* FExpression::GetValueShader(FEmitContext& Context, FEmitScope& Scope)
{
	return GetValueShader(Context, Scope, GetRequestedType());
}

void FExpression::GetValuePreshader(FEmitContext& Context, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader)
{
	FOwnerScope OwnerScope(*Context.Errors, GetOwner());
	if (PrepareValueResult.ForwardValue)
	{
		return PrepareValueResult.ForwardValue->GetValuePreshader(Context, RequestedType, OutPreshader);
	}

	check(!bReentryFlag);
	const EExpressionEvaluation Evaluation = PrepareValueResult.PreparedType.GetEvaluation(RequestedType);
	if (Evaluation == EExpressionEvaluation::Preshader)
	{
		bReentryFlag = true;
		EmitValuePreshader(Context, RequestedType, OutPreshader);
		bReentryFlag = false;
	}
	else if (Evaluation == EExpressionEvaluation::Constant)
	{
		const Shader::FValue ConstantValue = GetValueConstant(Context, RequestedType);
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(ConstantValue);
	}
	else
	{
		// Value is not used, write a dummy value
		check(Evaluation == EExpressionEvaluation::None);
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(Shader::EValueType::Float1);
	}
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context, const FRequestedType& RequestedType)
{
	FOwnerScope OwnerScope(*Context.Errors, GetOwner());
	if (PrepareValueResult.ForwardValue)
	{
		return PrepareValueResult.ForwardValue->GetValueConstant(Context, RequestedType);
	}

	check(!bReentryFlag);
	check(PrepareValueResult.PreparedType.GetEvaluation(RequestedType) == EExpressionEvaluation::Constant);
	
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

void FOwnerContext::PushOwner(UObject* Owner)
{
	OwnerStack.Add(Owner);
}

UObject* FOwnerContext::PopOwner()
{
	return OwnerStack.Pop(false);
}

UObject* FOwnerContext::GetCurrentOwner() const
{
	return (OwnerStack.Num() > 0) ? OwnerStack.Last() : nullptr;
}

uint64 Private::GetNextTypeHash()
{
	static uint64 Hash = 1;
	return Hash++;
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

bool FTree::Finalize()
{
	// Resolve values for any PHI nodes that were generated
	// Resolving a PHI may produce additional PHIs
	while (PHIExpressions.Num() > 0)
	{
		FExpressionLocalPHI* Expression = PHIExpressions.Pop(false);
		for (int32 i = 0; i < Expression->NumValues; ++i)
		{
			FExpression* LocalValue = AcquireLocal(*Expression->Scopes[i], Expression->LocalName);
			if (!LocalValue)
			{
				//Errorf(TEXT("Local %s is not assigned on all control paths"), *Expression->LocalName.ToString());
				return false;
			}

			for(EDerivativeCoordinate DerivativeCoord : Expression->DerivativeChain)
			{
				const FExpressionDerivatives Derivatives = GetAnalyticDerivatives(LocalValue);
				LocalValue = Derivatives.Get(DerivativeCoord);
			}
			// May be nullptr if derivatives are not valid
			Expression->Values[i] = LocalValue;
		}
	}

	return true;
}

bool FTree::EmitShader(FEmitContext& Context, FStringBuilderBase& OutCode) const
{
	FEmitScope* EmitRootScope = Context.InternalEmitScope(RootScope);
	if (EmitRootScope)
	{
		// Link all nodes to their proper scope
		for (FEmitShaderNode* EmitNode : Context.EmitNodes)
		{
			FEmitScope* EmitScope = EmitNode->Scope;
			if (EmitScope)
			{
				EmitNode->NextScopedNode = EmitScope->FirstNode;
				EmitScope->FirstNode = EmitNode;
			}
		}

		{
			FEmitShaderScopeStack Stack;
			TStringBuilder<2048> ScopeCode;
			Stack.Emplace(EmitRootScope, 1, ScopeCode);
			EmitRootScope->EmitShaderCode(Stack);
			check(Stack.Num() == 1);
			OutCode.Append(ScopeCode.ToView());
		}
	}

	Context.Finalize();

	return true;
}

void FTree::RegisterNode(FNode* Node)
{
	Node->Owner = GetCurrentOwner();
	Node->NextNode = Nodes;
	Nodes = Node;
}

FExpression* FTree::FindExpression(FXxHash64 Hash) const
{
	FExpression* const* FoundExpression = ExpressionMap.Find(Hash);
	if (FoundExpression)
	{
		return *FoundExpression;
	}
	return nullptr;
}

void FTree::RegisterExpression(FExpression* Expression, FXxHash64 Hash)
{
	ExpressionMap.Add(Hash, Expression);
}

void FTree::RegisterExpression(FExpressionLocalPHI* Expression, FXxHash64 Hash)
{
	PHIExpressions.Add(Expression);
	RegisterExpression(static_cast<FExpression*>(Expression), Hash);
}

void FTree::RegisterStatement(FScope& Scope, FStatement* Statement)
{
	check(!Scope.ContainedStatement)
	check(!Statement->ParentScope);
	Statement->ParentScope = &Scope;
	Scope.ContainedStatement = Statement;
}

void FTree::AssignLocal(FScope& Scope, const FName& LocalName, FExpression* Value)
{
	Scope.LocalMap.Add(LocalName, Value);
}

FExpression* FTree::AcquireLocal(FScope& Scope, const FName& LocalName)
{
	FExpression** FoundExpression = Scope.LocalMap.Find(LocalName);
	if (FoundExpression)
	{
		return *FoundExpression;
	}

	const TArrayView<FScope*> PreviousScopes = Scope.GetPreviousScopes();
	if (PreviousScopes.Num() > 1)
	{
		FExpression* Expression = NewExpression<FExpressionLocalPHI>(LocalName, PreviousScopes);
		Scope.LocalMap.Add(LocalName, Expression);
		return Expression;
	}

	if (PreviousScopes.Num() == 1)
	{
		return AcquireLocal(*PreviousScopes[0], LocalName);
	}

	return nullptr;
}

FExpression* FTree::NewFunctionCall(FScope& Scope, FFunction* Function, int32 OutputIndex)
{
	FScope* CalledScope = &Scope;
	if (Function->CalledScope)
	{
		CalledScope = FScope::FindSharedParent(CalledScope, Function->CalledScope);
		check(CalledScope);
	}
	Function->CalledScope = CalledScope;
	return NewExpression<FExpressionFunctionCall>(Function, OutputIndex);
}

const FExpressionDerivatives& FTree::GetAnalyticDerivatives(FExpression* InExpression)
{
	if (!InExpression)
	{
		static const FExpressionDerivatives EmptyDerivatives;
		return EmptyDerivatives;
	}

	if (!InExpression->bComputedDerivatives)
	{
		FExpressionReentryScope ReentryScope(InExpression);
		FOwnerScope OwnerScope(*this, InExpression->GetOwner()); // Associate any newly created nodes with the same owner as the input expression

		InExpression->ComputeAnalyticDerivatives(*this, InExpression->Derivatives);
		InExpression->bComputedDerivatives = true;
	}
	return InExpression->Derivatives;
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

FFunction* FTree::NewFunction()
{
	FFunction* NewFunction = NewNode<FFunction>();
	NewFunction->RootScope = NewNode<FScope>();
	return NewFunction;
}

FTextureParameterDeclaration* FTree::NewTextureParameterDeclaration(const FName& Name, const FTextureDescription& DefaultValue)
{
	FTextureParameterDeclaration* Declaration = NewNode<FTextureParameterDeclaration>(Name, DefaultValue);
	return Declaration;
}

} // namespace HLSLTree
} // namespace UE
