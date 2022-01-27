// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTree.h"
#include "HLSLTree/HLSLTreeEmit.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "Misc/MemStackUtility.h"
#include "Shader/ShaderTypes.h"
#include "Shader/Preshader.h"

// TODO - M_ForLoop doesn't work yet
// FPreparedType::GetEvaluation takes scope, checks loop scope automatically

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
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const override;

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

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const override;

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
	case EBinaryOp::Less: return FBinaryOpDescription(TEXT("Less"), TEXT("<"), Shader::EPreshaderOpcode::Less); break;
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
	else if (Lhs == EExpressionEvaluation::Unknown)
	{
		return Rhs;
	}
	else if (Rhs == EExpressionEvaluation::Unknown)
	{
		return Lhs;
	}
	else if (Lhs == EExpressionEvaluation::Shader || Rhs == EExpressionEvaluation::Shader)
	{
		// If either requires shader, shader is required
		return EExpressionEvaluation::Shader;
	}
	else if (Lhs == EExpressionEvaluation::PreshaderLoop || Rhs == EExpressionEvaluation::PreshaderLoop)
	{
		// Otherwise if either requires preshader, preshader is required
		return EExpressionEvaluation::PreshaderLoop;
	}
	else if (Lhs == EExpressionEvaluation::Preshader || Rhs == EExpressionEvaluation::Preshader)
	{
		// Otherwise if either requires preshader, preshader is required
		return EExpressionEvaluation::Preshader;
	}
	else if (Lhs == EExpressionEvaluation::ConstantLoop || Rhs == EExpressionEvaluation::ConstantLoop)
	{
		return EExpressionEvaluation::ConstantLoop;
	}

	// Otherwise must be constant
	check(Lhs == EExpressionEvaluation::Constant);
	check(Rhs == EExpressionEvaluation::Constant);
	return EExpressionEvaluation::Constant;
}

EExpressionEvaluation MakeLoopEvaluation(EExpressionEvaluation Evaluation)
{
	if (Evaluation == EExpressionEvaluation::Preshader)
	{
		return EExpressionEvaluation::PreshaderLoop;
	}
	else if (Evaluation == EExpressionEvaluation::Constant)
	{
		return EExpressionEvaluation::ConstantLoop;
	}
	return Evaluation;
}

EExpressionEvaluation MakeNonLoopEvaluation(EExpressionEvaluation Evaluation)
{
	if (Evaluation == EExpressionEvaluation::PreshaderLoop)
	{
		return EExpressionEvaluation::Preshader;
	}
	else if (Evaluation == EExpressionEvaluation::ConstantLoop)
	{
		return EExpressionEvaluation::Constant;
	}
	return Evaluation;
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

bool FExpressionLocalPHI::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
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
		return OutResult.SetForwardValue(Context, Scope, RequestedType, ForwardExpression);
	}

	// 2) PHI has different values in previous scopes, but possible some previous scopes may become dead due to constant folding
	// In this case, we check to see if the value is the same in all live scopes, and forward if possible
	FEmitScope* EmitScopes[MaxNumPreviousScopes] = { nullptr };
	for (int32 i = 0; i < NumValues; ++i)
	{
		// Ignore values in dead scopes
		EmitScopes[i] = Context.PrepareScope(Scopes[i]);
		if (EmitScopes[i])
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
		return OutResult.SetForwardValue(Context, Scope, RequestedType, ForwardExpression);
	}

	FPreparedType TypePerValue[MaxNumPreviousScopes];
	int32 NumValidTypes = 0;
	FPreparedType CurrentType;

	auto UpdateValueTypes = [&]()
	{
		for (int32 i = 0; i < NumValues; ++i)
		{
			if (!EmitScopes[i] || EmitScopes[i]->Evaluation == EExpressionEvaluation::Unknown)
			{
				EmitScopes[i] = Context.PrepareScope(Scopes[i]);
			}
			if (TypePerValue[i].IsVoid() && EmitScopes[i])
			{
				const FPreparedType& ValueType = Context.PrepareExpression(Values[i], *EmitScopes[i], RequestedType);
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
					CurrentType.MergeEvaluation(EmitScopes[i]->Evaluation);
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
	const FPreparedType InitialType(CurrentType);
	if (!OutResult.SetType(Context, RequestedType, InitialType))
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

		if (CurrentType != InitialType)
		{
			// Update type again based on computing remaining values
			if (!OutResult.SetType(Context, RequestedType, CurrentType))
			{
				return false;
			}

			// Since we changed our type, need to update any dependant values again
			for (int32 i = 0; i < NumValues; ++i)
			{
				const FPreparedType& ValueType = Context.PrepareExpression(Values[i], *EmitScopes[i], RequestedType);
				// Don't expect types to change *again*
				if (ValueType.IsVoid() || MergePreparedTypes(CurrentType, ValueType) != CurrentType)
				{
					return Context.Errors->AddError(TEXT("Mismatched types for local variable %s and %s"));
				}
			}
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

void FExpressionLocalPHI::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	int32 ValueStackPosition = INDEX_NONE;
	for (int32 Index = Context.PreshaderLocalPHIScopes.Num() - 1; Index >= 0; --Index)
	{
		const FPreshaderLocalPHIScope* LocalPHIScope = Context.PreshaderLocalPHIScopes[Index];
		if (LocalPHIScope->ExpressionLocalPHI == this)
		{
			ValueStackPosition = LocalPHIScope->ValueStackPosition;
			break;
		}
	}

	if (ValueStackPosition == INDEX_NONE)
	{
		// Assign the initial value
		const FRequestedType LocalType = GetRequestedType();
		Context.PreshaderStackPosition++;
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(LocalType.GetType());

		ValueStackPosition = Context.PreshaderStackPosition;
		const FPreshaderLocalPHIScope LocalPHIScope(this, ValueStackPosition);
		Context.PreshaderLocalPHIScopes.Add(&LocalPHIScope);

		FEmitScope* EmitRootScope = nullptr;// &Scope;
		FEmitPreshaderScope PreshaderScopes[MaxNumPreviousScopes];
		for (int32 i = 0; i < NumValues; ++i)
		{
			FEmitPreshaderScope& PreshaderScope = PreshaderScopes[i];
			PreshaderScope.Scope = Context.AcquireEmitScope(Scopes[i]);
			PreshaderScope.Value = Values[i];
			EmitRootScope = FEmitScope::FindSharedParent(PreshaderScopes[i].Scope, EmitRootScope);
		}

		Context.EmitPreshaderScope(*EmitRootScope, RequestedType, MakeArrayView(PreshaderScopes, NumValues), OutPreshader);
		verify(Context.PreshaderLocalPHIScopes.Pop(false) == &LocalPHIScope);
		check(Context.PreshaderStackPosition == ValueStackPosition);
	}
	else
	{
		const int32 PreshaderStackOffset = Context.PreshaderStackPosition - ValueStackPosition;
		check(PreshaderStackOffset >= 0 && PreshaderStackOffset <= 0xffff);

		Context.PreshaderStackPosition++;
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::PushValue).Write((uint16)PreshaderStackOffset);
	}
}

bool FExpressionFunctionCall::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FEmitScope* EmitFunctionScope = Context.PrepareScopeWithParent(Function->RootScope, Function->CalledScope);
	if (!EmitFunctionScope)
	{
		return false;
	}

	FPreparedType OutputType = Context.PrepareExpression(Function->OutputExpressions[OutputIndex], Scope, RequestedType);
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

void FExpressionFunctionCall::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	Function->OutputExpressions[OutputIndex]->GetValuePreshader(Context, Scope, RequestedType, OutPreshader);
}

void FExpression::Reset()
{
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

EExpressionEvaluation FPreparedComponent::GetEvaluation(const FEmitScope& Scope) const
{
	EExpressionEvaluation Result = Evaluation;
	if (IsLoopEvaluation(Result))
	{
		// We only want to return a 'Loop' evaluation if we're within the loop's scope
		if (!Scope.HasParent(LoopScope))
		{
			switch (Result)
			{
			case EExpressionEvaluation::ConstantLoop: Result = EExpressionEvaluation::Constant; break;
			case EExpressionEvaluation::PreshaderLoop: Result = EExpressionEvaluation::Preshader; break;
			default: checkNoEntry(); break;
			}
		}
	}
	return Result;
}

FPreparedComponent CombineComponents(const FPreparedComponent& Lhs, const FPreparedComponent& Rhs)
{
	const EExpressionEvaluation Evaluation = CombineEvaluations(Lhs.Evaluation, Rhs.Evaluation);
	FEmitScope* LoopScope = IsLoopEvaluation(Evaluation) ? FEmitScope::FindSharedParent(Lhs.LoopScope, Rhs.LoopScope) : nullptr;
	return FPreparedComponent(Evaluation, LoopScope);
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
		const int32 MaxComponentIndex = PreparedComponents.FindLastByPredicate([](const FPreparedComponent& InComponent) { return InComponent.Evaluation != EExpressionEvaluation::None; });
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
			const FPreparedComponent Component = GetComponent(Index);
			if (!Component.IsNone())
			{
				Result.SetComponentRequest(Index);
			}
		}
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetEvaluation(const FEmitScope& Scope) const
{
	EExpressionEvaluation Result = EExpressionEvaluation::None;
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		Result = CombineEvaluations(Result, PreparedComponents[Index].GetEvaluation(Scope));
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetEvaluation(const FEmitScope& Scope, const FRequestedType& RequestedType) const
{
	EExpressionEvaluation Result = EExpressionEvaluation::None;
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			Result = CombineEvaluations(Result, PreparedComponents[Index].GetEvaluation(Scope));
		}
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetFieldEvaluation(const FEmitScope& Scope, int32 ComponentIndex, int32 NumComponents) const
{
	EExpressionEvaluation Result = EExpressionEvaluation::None;
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		const FPreparedComponent Component = GetComponent(Index);
		Result = CombineEvaluations(Result, Component.GetEvaluation(Scope));
	}
	return Result;
}

FPreparedComponent FPreparedType::GetComponent(int32 Index) const
{
	return PreparedComponents.IsValidIndex(Index) ? PreparedComponents[Index] : FPreparedComponent();
}

void FPreparedType::EnsureNumComponents(int32 NumComponents)
{
	if (NumComponents > PreparedComponents.Num())
	{
		static_assert((uint8)EExpressionEvaluation::None == 0u, "Assume zero initializes to None");
		PreparedComponents.AddZeroed(NumComponents - PreparedComponents.Num());
	}
}

void FPreparedType::SetComponent(int32 Index, const FPreparedComponent& InComponent)
{
	if (InComponent.Evaluation != EExpressionEvaluation::None)
	{
		EnsureNumComponents(Index + 1);
	}
	if (PreparedComponents.IsValidIndex(Index))
	{
		PreparedComponents[Index] = InComponent;
	}
}

void FPreparedType::MergeComponent(int32 Index, const FPreparedComponent& InComponent)
{
	if (InComponent.Evaluation != EExpressionEvaluation::None)
	{
		EnsureNumComponents(Index + 1);
	}
	if (PreparedComponents.IsValidIndex(Index))
	{
		PreparedComponents[Index] = CombineComponents(PreparedComponents[Index], InComponent);
	}
}

void FPreparedType::SetEvaluation(EExpressionEvaluation Evaluation)
{
	check(!IsLoopEvaluation(Evaluation));
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		if (!PreparedComponents[Index].IsNone())
		{
			PreparedComponents[Index] = Evaluation;
		}
	}
}

void FPreparedType::MergeEvaluation(EExpressionEvaluation Evaluation)
{
	check(!IsLoopEvaluation(Evaluation));
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		if (!PreparedComponents[Index].IsNone())
		{
			FPreparedComponent& Component = PreparedComponents[Index];
			Component = CombineComponents(Component, Evaluation);
		}
	}
}

void FPreparedType::SetLoopEvaluation(FEmitScope& Scope, const FRequestedType& RequestedType)
{
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			FPreparedComponent& Component = PreparedComponents[Index];
			Component.Evaluation = MakeLoopEvaluation(Component.Evaluation);
			if (IsLoopEvaluation(Component.Evaluation))
			{
				Component.LoopScope = FEmitScope::FindSharedParent(&Scope, Component.LoopScope);
			}
		}
	}
}

void FPreparedType::SetField(const Shader::FStructField* Field, const FPreparedType& FieldType)
{
	for (int32 Index = 0; Index < Field->GetNumComponents(); ++Index)
	{
		SetComponent(Field->ComponentIndex + Index, FieldType.GetComponent(Index));
	}
}

FPreparedType FPreparedType::GetFieldType(const Shader::FStructField* Field) const
{
	FPreparedType Result(Field->Type);
	for (int32 Index = 0; Index < Field->GetNumComponents(); ++Index)
	{
		Result.SetComponent(Index, GetComponent(Field->ComponentIndex + Index));
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
		const FPreparedComponent LhsComponent = Lhs.GetComponent(Index);
		const FPreparedComponent RhsComponent = Rhs.GetComponent(Index);
		Result.SetComponent(Index, CombineComponents(LhsComponent, RhsComponent));
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
					PreparedType.MergeComponent(Index, Evaluation);
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
				PreparedType.MergeComponent(Index, Type.GetComponent(Index));
			}
		}
		return true;
	}
	return false;
}

bool FPrepareValueResult::SetForwardValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FExpression* InForwardValue)
{
	if (InForwardValue != ForwardValue)
	{
		PreparedType = Context.PrepareExpression(InForwardValue, Scope, RequestedType);
		ForwardValue = InForwardValue;
	}
	return !PreparedType.IsVoid();
}

void FStatement::EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const
{
	check(false);
}

void FExpression::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// nop
}

void FExpression::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	check(false);
}

void FExpression::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
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

	const EExpressionEvaluation Evaluation = PrepareValueResult.PreparedType.GetEvaluation(Scope, RequestedType);
	check(Evaluation != EExpressionEvaluation::None);

	FEmitShaderExpression* Value = nullptr;
	if (Evaluation == EExpressionEvaluation::Constant || Evaluation == EExpressionEvaluation::Preshader)
	{
		Value = Context.EmitPreshaderOrConstant(Scope, RequestedType, this);
	}
	else
	{
		check(Evaluation != EExpressionEvaluation::None && Evaluation != EExpressionEvaluation::Unknown);
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

void FExpression::GetValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader)
{
	FOwnerScope OwnerScope(*Context.Errors, GetOwner());
	if (PrepareValueResult.ForwardValue)
	{
		return PrepareValueResult.ForwardValue->GetValuePreshader(Context, Scope, RequestedType, OutPreshader);
	}

	const int32 PrevStackPosition = Context.PreshaderStackPosition;
	const EExpressionEvaluation Evaluation = PrepareValueResult.PreparedType.GetEvaluation(Scope, RequestedType);

	if (Evaluation == EExpressionEvaluation::Constant)
	{
		const Shader::FValue ConstantValue = GetValueConstant(Context, Scope, RequestedType);
		Context.PreshaderStackPosition++;
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(ConstantValue);
	}
	else
	{
		check(Evaluation != EExpressionEvaluation::None && Evaluation != EExpressionEvaluation::Unknown && Evaluation != EExpressionEvaluation::Shader);
		EmitValuePreshader(Context, Scope, RequestedType, OutPreshader);
	}
	check(Context.PreshaderStackPosition == PrevStackPosition + 1);
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType)
{
	FOwnerScope OwnerScope(*Context.Errors, GetOwner());
	if (PrepareValueResult.ForwardValue)
	{
		return PrepareValueResult.ForwardValue->GetValueConstant(Context, Scope, RequestedType);
	}

	check(!bReentryFlag);

	const EExpressionEvaluation Evaluation = PrepareValueResult.PreparedType.GetEvaluation(Scope, RequestedType);
	check(Evaluation == EExpressionEvaluation::Constant || Evaluation == EExpressionEvaluation::ConstantLoop);
	
	Shader::FPreshaderData ConstantPreshader;
	{
		FExpressionReentryScope ReentryScope(this);
		const int32 PrevPreshaderStackPosition = Context.PreshaderStackPosition;
		EmitValuePreshader(Context, Scope, RequestedType, ConstantPreshader);
		check(Context.PreshaderStackPosition == PrevPreshaderStackPosition + 1);
		Context.PreshaderStackPosition--;
	}

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
