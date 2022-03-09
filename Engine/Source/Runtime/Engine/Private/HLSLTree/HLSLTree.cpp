// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_EDITOR

#include "HLSLTree/HLSLTree.h"
#include "HLSLTree/HLSLTreeEmit.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "Misc/MemStackUtility.h"
#include "Shader/ShaderTypes.h"
#include "Shader/Preshader.h"

namespace UE::HLSLTree
{

enum ELocalPHIChainType : uint8
{
	Ddx,
	Ddy,
	PreviousFrame,
};

struct FLocalPHIChainEntry
{
	FLocalPHIChainEntry() = default;
	FLocalPHIChainEntry(const ELocalPHIChainType& InType, const FRequestedType& InRequestedType) : Type(InType), RequestedType(InRequestedType) {}

	ELocalPHIChainType Type;
	FRequestedType RequestedType;
};

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

	FExpressionLocalPHI(const FExpressionLocalPHI* Source, ELocalPHIChainType Type, const FRequestedType& RequestedType = FRequestedType());

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual const FExpression* ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;

	TArray<FLocalPHIChainEntry, TInlineAllocator<8>> Chain;
	FName LocalName;
	FScope* Scopes[MaxNumPreviousScopes];
	const FExpression* Values[MaxNumPreviousScopes];
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
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;

	FFunction* Function;
	int32 OutputIndex;
};

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
	else if (Lhs == EExpressionEvaluation::Constant || Rhs == EExpressionEvaluation::Constant)
	{
		return EExpressionEvaluation::Constant;
	}

	// Otherwise must be constant
	check(Lhs == EExpressionEvaluation::ConstantZero);
	check(Rhs == EExpressionEvaluation::ConstantZero);
	return EExpressionEvaluation::ConstantZero;
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

FExpressionLocalPHI::FExpressionLocalPHI(const FExpressionLocalPHI* Source, ELocalPHIChainType Type, const FRequestedType& RequestedType)
	: Chain(Source->Chain)
	, LocalName(Source->LocalName)
	, NumValues(Source->NumValues)
{
	Chain.Emplace(Type, RequestedType);
	for (int32 i = 0; i < NumValues; ++i)
	{
		Scopes[i] = Source->Scopes[i];
	}
}

void FExpressionLocalPHI::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// We don't have values assigned at the time analytic derivatives are computed
	// It's possible the derivatives will be end up being invalid, but that case will need to be detected later, during PrepareValue
	OutResult.ExpressionDdx = Tree.NewExpression<FExpressionLocalPHI>(this, ELocalPHIChainType::Ddx);
	OutResult.ExpressionDdy = Tree.NewExpression<FExpressionLocalPHI>(this, ELocalPHIChainType::Ddy);
}

const FExpression* FExpressionLocalPHI::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	return Tree.NewExpression<FExpressionLocalPHI>(this, ELocalPHIChainType::PreviousFrame, RequestedType);
}

bool FExpressionLocalPHI::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	check(NumValues <= MaxNumPreviousScopes);

	FEmitScope* EmitValueScopes[MaxNumPreviousScopes];
	FScope* LiveScopes[MaxNumPreviousScopes];
	const FExpression* LiveValues[MaxNumPreviousScopes];
	int32 NumLiveScopes = 0;

	for (int32 i = 0; i < NumValues; ++i)
	{
		FEmitScope* EmitValueScope = Context.PrepareScope(Scopes[i]);
		if (EmitValueScope)
		{
			const int32 LiveScopeIndex = NumLiveScopes++;
			EmitValueScopes[LiveScopeIndex] = EmitValueScope;
			LiveScopes[LiveScopeIndex] = Scopes[i];
			LiveValues[LiveScopeIndex] = Values[i];
		}
	}

	if (NumLiveScopes == 0)
	{
		return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::ConstantZero, Shader::EValueType::Float1);
	}

	FPreparedType TypePerValue[MaxNumPreviousScopes];
	int32 NumValidTypes = 0;
	FPreparedType CurrentType;

	auto UpdateValueTypes = [&]()
	{
		for (int32 i = 0; i < NumLiveScopes; ++i)
		{
			if (EmitValueScopes[i]->Evaluation == EExpressionEvaluation::Unknown)
			{
				EmitValueScopes[i] = Context.PrepareScope(LiveScopes[i]);
			}
			if (TypePerValue[i].IsVoid())
			{
				const FPreparedType& ValueType = Context.PrepareExpression(LiveValues[i], *EmitValueScopes[i], RequestedType);
				if (!ValueType.IsVoid())
				{
					TypePerValue[i] = ValueType;
					const FPreparedType MergedType = MergePreparedTypes(CurrentType, ValueType);
					if (MergedType.IsVoid())
					{
						return Context.Errors->AddErrorf(TEXT("Mismatched types for local variable %s and %s"),
							CurrentType.GetName(),
							ValueType.GetName());
					}
					CurrentType = MergedType;
					CurrentType.MergeEvaluation(EmitValueScopes[i]->Evaluation);
					check(NumValidTypes < NumLiveScopes);
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

	if (NumValidTypes < NumLiveScopes)
	{
		// Now try to assign remaining types that failed the first iteration 
		if (!UpdateValueTypes())
		{
			return false;
		}
		if (NumValidTypes < NumLiveScopes)
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
			for (int32 i = 0; i < NumLiveScopes; ++i)
			{
				const FPreparedType& ValueType = Context.PrepareExpression(LiveValues[i], *EmitValueScopes[i], RequestedType);
				// Don't expect types to change *again*
				if (ValueType.IsVoid() || MergePreparedTypes(CurrentType, ValueType) != CurrentType)
				{
					return Context.Errors->AddErrorf(TEXT("Mismatched types for local variable %s and %s"),
						CurrentType.GetName(),
						ValueType.GetName());
				}
			}
		}
	}

	return true;
}

namespace Private
{
struct FLocalPHILiveScopes
{
	FEmitScope* EmitDeclarationScope = nullptr;
	FEmitScope* EmitValueScopes[MaxNumPreviousScopes];
	const FExpression* LiveValues[MaxNumPreviousScopes];
	int32 NumScopes = 0;
	bool bCanForwardValue = true;
};

bool GetLiveScopes(FEmitContext& Context, const FExpressionLocalPHI& Expression, FLocalPHILiveScopes& OutLiveScopes)
{
	for (int32 ScopeIndex = 0; ScopeIndex < Expression.NumValues; ++ScopeIndex)
	{
		FEmitScope* EmitValueScope = Context.AcquireEmitScope(Expression.Scopes[ScopeIndex]);
		if (!IsScopeDead(EmitValueScope))
		{
			if (OutLiveScopes.bCanForwardValue)
			{
				for (int32 PrevScopeIndex = 0; PrevScopeIndex < OutLiveScopes.NumScopes; ++PrevScopeIndex)
				{
					if (Expression.Values[ScopeIndex] != OutLiveScopes.LiveValues[PrevScopeIndex])
					{
						OutLiveScopes.bCanForwardValue = false;
					}
				}
			}

			const int32 LiveScopeIndex = OutLiveScopes.NumScopes++;
			OutLiveScopes.LiveValues[LiveScopeIndex] = Expression.Values[ScopeIndex];
			OutLiveScopes.EmitValueScopes[LiveScopeIndex] = EmitValueScope;
			OutLiveScopes.EmitDeclarationScope = FEmitScope::FindSharedParent(OutLiveScopes.EmitDeclarationScope, EmitValueScope);
			if (!OutLiveScopes.EmitDeclarationScope)
			{
				return Context.Errors->AddError(TEXT("Invalid LocalPHI"));
			}
		}
	}
	return OutLiveScopes.NumScopes > 0;
}
}

void FExpressionLocalPHI::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderExpression* const* PrevEmitExpression = Context.EmitLocalPHIMap.Find(this);
	FEmitShaderExpression* EmitExpression = PrevEmitExpression ? *PrevEmitExpression : nullptr;
	if (!EmitExpression)
	{
		// Find the outermost scope to declare our local variable
		Private::FLocalPHILiveScopes LiveScopes;
		if (!Private::GetLiveScopes(Context, *this, LiveScopes))
		{
			return;
		}

		if (LiveScopes.bCanForwardValue)
		{
			EmitExpression = LiveScopes.LiveValues[0]->GetValueShader(Context, Scope);
			Context.EmitLocalPHIMap.Add(this, EmitExpression);
		}
		else
		{
			// This is the first time we've emitted shader code for this PHI
			// Create an expression and add it to the map first, so if this is called recursively this path will only be taken the first time
			const int32 LocalPHIIndex = Context.NumExpressionLocalPHIs++;
			const Shader::FType LocalType = Context.GetType(this);

			EmitExpression = OutResult.Code = Context.EmitInlineExpression(Scope,
				LocalType,
				TEXT("LocalPHI%"), LocalPHIIndex);
			Context.EmitLocalPHIMap.Add(this, EmitExpression);

			FEmitShaderStatement* EmitDeclaration = nullptr;
			for (int32 i = 0; i < LiveScopes.NumScopes; ++i)
			{
				FEmitScope* EmitValueScope = LiveScopes.EmitValueScopes[i];
				if (EmitValueScope == LiveScopes.EmitDeclarationScope)
				{
					FEmitShaderExpression* ShaderValue = LiveScopes.LiveValues[i]->GetValueShader(Context, *EmitValueScope, LocalType);
					EmitDeclaration = Context.EmitStatement(*EmitValueScope, TEXT("% LocalPHI% = %;"),
						LocalType.GetName(),
						LocalPHIIndex,
						ShaderValue);
					break;
				}
			}
			if (!EmitDeclaration)
			{
				EmitDeclaration = Context.EmitStatement(*LiveScopes.EmitDeclarationScope, TEXT("% LocalPHI%;"),
					LocalType.GetName(),
					LocalPHIIndex);
			}

			FEmitShaderNode* Dependencies[MaxNumPreviousScopes] = { nullptr };
			int32 NumDependencies = 0;
			for (int32 i = 0; i < LiveScopes.NumScopes; ++i)
			{
				FEmitScope* EmitValueScope = LiveScopes.EmitValueScopes[i];
				if (EmitValueScope != LiveScopes.EmitDeclarationScope)
				{
					FEmitShaderExpression* ShaderValue = LiveScopes.LiveValues[i]->GetValueShader(Context, *EmitValueScope, LocalType);
					FEmitShaderStatement* EmitAssignment = Context.EmitStatementWithDependency(*EmitValueScope, EmitDeclaration, TEXT("LocalPHI% = %;"),
						LocalPHIIndex,
						ShaderValue);
					Dependencies[NumDependencies++] = EmitAssignment;
				}
			}

			// Fill in the expression's dependencies
			EmitExpression->Dependencies = MemStack::AllocateArrayView(*Context.Allocator, MakeArrayView(Dependencies, NumDependencies));
		}
	}

	OutResult.Code = EmitExpression;
}

void FExpressionLocalPHI::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
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

	OutResult.Type = Context.GetType(this);
	if (ValueStackPosition == INDEX_NONE)
	{
		Private::FLocalPHILiveScopes LiveScopes;
		if (!Private::GetLiveScopes(Context, *this, LiveScopes))
		{
			return;
		}

		if (LiveScopes.bCanForwardValue)
		{
			OutResult.Type = LiveScopes.LiveValues[0]->GetValuePreshader(Context, Scope, RequestedType, OutResult.Preshader);
		}
		else
		{
			// Assign the initial value
			Context.PreshaderStackPosition++;
			OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(OutResult.Type);

			ValueStackPosition = Context.PreshaderStackPosition;
			const FPreshaderLocalPHIScope LocalPHIScope(this, ValueStackPosition);
			Context.PreshaderLocalPHIScopes.Add(&LocalPHIScope);

			FEmitPreshaderScope PreshaderScopes[MaxNumPreviousScopes];
			for (int32 i = 0; i < LiveScopes.NumScopes; ++i)
			{
				FEmitPreshaderScope& PreshaderScope = PreshaderScopes[i];
				PreshaderScope.Scope = LiveScopes.EmitValueScopes[i];
				PreshaderScope.Value = LiveScopes.LiveValues[i];
			}

			Context.EmitPreshaderScope(*LiveScopes.EmitDeclarationScope, RequestedType, MakeArrayView(PreshaderScopes, LiveScopes.NumScopes), OutResult.Preshader);
			verify(Context.PreshaderLocalPHIScopes.Pop(false) == &LocalPHIScope);
			check(Context.PreshaderStackPosition == ValueStackPosition);
		}
	}
	else
	{
		const int32 PreshaderStackOffset = Context.PreshaderStackPosition - ValueStackPosition;
		check(PreshaderStackOffset >= 0 && PreshaderStackOffset <= 0xffff);

		Context.PreshaderStackPosition++;
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::PushValue).Write((uint16)PreshaderStackOffset);
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

void FExpressionFunctionCall::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	OutResult.Type = Function->OutputExpressions[OutputIndex]->GetValuePreshader(Context, Scope, RequestedType, OutResult.Preshader);
}

FRequestedType::FRequestedType(ERequestedType InType)
{
	const int32 NumComponents = (uint8)InType;
	check(NumComponents >= 0 && NumComponents <= 16);
	RequestedComponents.Init(true, NumComponents);
}

FRequestedType::FRequestedType(Shader::EValueType InType)
{
	const Shader::FValueTypeDescription TypeDesc = Shader::GetValueTypeDescription(InType);
	RequestedComponents.Init(true, TypeDesc.NumComponents);
}

FRequestedType::FRequestedType(const Shader::FType& InType)
{
	RequestedComponents.Init(true, InType.GetNumComponents());
}

int32 FRequestedType::GetNumComponents() const
{
	const int32 MaxComponentIndex = RequestedComponents.FindLast(true);
	if (MaxComponentIndex != INDEX_NONE)
	{
		return MaxComponentIndex + 1;
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
	FRequestedType Result;
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
	if (Lhs.IsNone())
	{
		return Rhs;
	}
	else if (Rhs.IsNone())
	{
		return Lhs;
	}
	else
	{
		FPreparedComponent Result;
		Result.Evaluation = CombineEvaluations(Lhs.Evaluation, Rhs.Evaluation);
		if (IsLoopEvaluation(Result.Evaluation))
		{
			Result.LoopScope = FEmitScope::FindSharedParent(Lhs.LoopScope, Rhs.LoopScope);
		}
		if (Lhs.Bounds.Min == Rhs.Bounds.Min)
		{
			Result.Bounds.Min = Lhs.Bounds.Min;
		}
		if (Lhs.Bounds.Max == Rhs.Bounds.Max)
		{
			Result.Bounds.Max = Lhs.Bounds.Max;
		}
		return Result;
	}
}

FPreparedType::FPreparedType(Shader::EValueType InType, const FPreparedComponent& InComponent)
	: FPreparedType(Shader::FType(InType), InComponent)
{
}

FPreparedType::FPreparedType(const Shader::FType& InType, const FPreparedComponent& InComponent)
{
	if (InType.IsStruct())
	{
		StructType = InType.StructType;
	}
	else
	{
		ValueComponentType = Shader::GetValueTypeDescription(InType).ComponentType;
	}

	if (!InComponent.IsNone())
	{
		const int32 NumComponents = InType.GetNumComponents();
		PreparedComponents.Reserve(NumComponents);
		for (int32 Index = 0; Index < NumComponents; ++Index)
		{
			SetComponent(Index, InComponent);
		}
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
		auto Predicate = [](const FPreparedComponent& InComponent) { return InComponent.IsRequested(); };
		const int32 MaxComponentIndex = PreparedComponents.FindLastByPredicate(Predicate);
		return (MaxComponentIndex != INDEX_NONE) ? MaxComponentIndex + 1 : 1;
	}
	return 0;
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
	FRequestedType Result;
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		const FPreparedComponent& Component = PreparedComponents[Index];
		if (Component.IsRequested())
		{
			Result.SetComponentRequest(Index);
		}
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetEvaluation(const FEmitScope& Scope) const
{
	EExpressionEvaluation Result = EExpressionEvaluation::ConstantZero;
	for (int32 Index = 0; Index < PreparedComponents.Num(); ++Index)
	{
		Result = CombineEvaluations(Result, PreparedComponents[Index].GetEvaluation(Scope));
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetEvaluation(const FEmitScope& Scope, const FRequestedType& RequestedType) const
{
	const int32 NumComponents = RequestedType.GetNumComponents();
	EExpressionEvaluation Result = EExpressionEvaluation::ConstantZero;
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			Result = CombineEvaluations(Result, GetComponent(Index).GetEvaluation(Scope));
		}
	}
	return Result;
}

EExpressionEvaluation FPreparedType::GetFieldEvaluation(const FEmitScope& Scope, const FRequestedType& RequestedType, int32 ComponentIndex, int32 NumComponents) const
{
	EExpressionEvaluation Result = EExpressionEvaluation::ConstantZero;
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		const FPreparedComponent Component = GetComponent(ComponentIndex + Index);
		if (RequestedType.IsComponentRequested(ComponentIndex + Index))
		{
			Result = CombineEvaluations(Result, Component.GetEvaluation(Scope));
		}
	}
	return Result;
}

FPreparedComponent FPreparedType::GetMergedComponent() const
{
	FPreparedComponent Result;
	for (const FPreparedComponent& Component : PreparedComponents)
	{
		Result = CombineComponents(Result, Component);
	}
	return Result;
}

FPreparedComponent FPreparedType::GetComponent(int32 Index) const
{
	if (PreparedComponents.IsValidIndex(Index))
	{
		return PreparedComponents[Index];
	}

	// Return 'ConstantZero' for unprepared components
	// TODO - do we ever want to return 'None' instead?
	const int32 NumComponents = GetNumComponents();
	return (NumComponents == 1) ? PreparedComponents[0] : FPreparedComponent(EExpressionEvaluation::ConstantZero);
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

void FPreparedType::SetComponentBounds(int32 Index, const Shader::FComponentBounds Bounds)
{
	if (PreparedComponents.IsValidIndex(Index))
	{
		FPreparedComponent& Component = PreparedComponents[Index];
		if (!Component.IsNone())
		{
			Component.Bounds = Bounds;
		}
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

Shader::FComponentBounds FPreparedType::GetComponentBounds(int32 Index) const
{
	const FPreparedComponent Component = GetComponent(Index);
	if (Component.Evaluation == EExpressionEvaluation::None)
	{
		return Shader::FComponentBounds();
	}
	else if (Component.Evaluation == EExpressionEvaluation::ConstantZero)
	{
		return Shader::FComponentBounds(Shader::EComponentBound::Zero, Shader::EComponentBound::Zero);
	}

	const Shader::EValueComponentType ComponentType = StructType ? StructType->ComponentTypes[Index] : ValueComponentType;
	check(ComponentType != Shader::EValueComponentType::Void);

	const Shader::FValueComponentTypeDescription ComponentTypeDesc = Shader::GetValueComponentTypeDescription(ComponentType);
	const Shader::EComponentBound MinBound = Shader::MaxBound(Component.Bounds.Min, ComponentTypeDesc.Bounds.Min);
	const Shader::EComponentBound MaxBound = Shader::MinBound(Component.Bounds.Max, ComponentTypeDesc.Bounds.Max);
	return Shader::FComponentBounds(MinBound, MaxBound);
}

Shader::FComponentBounds FPreparedType::GetBounds(const FRequestedType& RequestedType) const
{
	Shader::FComponentBounds Result(Shader::EComponentBound::DoubleMax, Shader::EComponentBound::NegDoubleMax);
	for (int32 Index = 0; Index < RequestedType.RequestedComponents.Num(); ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			const Shader::FComponentBounds ComponentBounds = GetComponentBounds(Index);
			Result.Min = Shader::MinBound(Result.Min, ComponentBounds.Min);
			Result.Max = Shader::MaxBound(Result.Max, ComponentBounds.Max);
		}
	}
	return Result;
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

	Result.PreparedComponents.Reset(NumComponents);
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		const FPreparedComponent LhsComponent = Lhs.GetComponent(Index);
		const FPreparedComponent RhsComponent = Rhs.GetComponent(Index);
		Result.SetComponent(Index, CombineComponents(LhsComponent, RhsComponent));
	}

	return Result;
}

FPreparedType MakeNonLWCType(const FPreparedType& Type)
{
	FPreparedType Result(Type);
	if (Result.IsNumeric())
	{
		Result.ValueComponentType = Shader::MakeNonLWCType(Result.ValueComponentType);
	}
	return Result;
}

bool FPrepareValueResult::TryMergePreparedType(FEmitContext& Context, const Shader::FStructType* StructType, Shader::EValueComponentType ComponentType)
{
	if (!StructType && ComponentType == Shader::EValueComponentType::Void)
	{
		return false;
	}

	if (!PreparedType.IsInitialized())
	{
		PreparedType.PreparedComponents.Reset();
		PreparedType.ValueComponentType = ComponentType;
		PreparedType.StructType = StructType;
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
		PreparedType.ValueComponentType = Shader::CombineComponentTypes(PreparedType.ValueComponentType, ComponentType);
	}

	return true;
}

bool FPrepareValueResult::SetTypeVoid()
{
	PreparedType.PreparedComponents.Reset();
	PreparedType.ValueComponentType = Shader::EValueComponentType::Void;
	PreparedType.StructType = nullptr;
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
				else
				{
					// If component wasn't requested, it can be replaced with constant-0
					PreparedType.MergeComponent(Index, EExpressionEvaluation::ConstantZero);
				}
			}
		}
		return true;
	}
	return false;
}

bool FPrepareValueResult::SetType(FEmitContext& Context, const FRequestedType& RequestedType, const FPreparedType& Type)
{
	Shader::EValueComponentType ComponentType = Type.ValueComponentType;
	if (ComponentType == Shader::EValueComponentType::Double)
	{
		const Shader::FComponentBounds Bounds = Type.GetBounds(RequestedType);
		if (Shader::IsWithinBounds(Bounds, Shader::GetValueComponentTypeDescription(Shader::EValueComponentType::Float).Bounds))
		{
			ComponentType = Shader::EValueComponentType::Float;
		}
	}

	if (TryMergePreparedType(Context, Type.StructType, ComponentType))
	{
		const int32 NumComponents = Type.GetNumComponents();
		for (int32 Index = 0; Index < NumComponents; ++Index)
		{
			const FPreparedComponent& Component = Type.GetComponent(Index);
			if (RequestedType.IsComponentRequested(Index))
			{
				PreparedType.MergeComponent(Index, Component);
			}
			else if(!Component.IsNone())
			{
				// If component wasn't requested, it can be replaced with constant-0
				PreparedType.MergeComponent(Index, EExpressionEvaluation::ConstantZero);
			}
		}
		return true;
	}
	return false;
}

void FStatement::EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const
{
	check(false);
}

void FExpression::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// nop
}

const FExpression* FExpression::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	return nullptr;
}

void FExpression::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	check(false);
}

void FExpression::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	check(false);
}

FEmitShaderExpression* FExpression::GetValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, const FPreparedType& PreparedType, const Shader::FType& ResultType) const
{
	FOwnerScope OwnerScope(*Context.Errors, GetOwner());

	const EExpressionEvaluation Evaluation = PreparedType.GetEvaluation(Scope, RequestedType);
	check(Evaluation != EExpressionEvaluation::None && Evaluation != EExpressionEvaluation::Unknown);

	FEmitShaderExpression* Value = nullptr;
	if (Evaluation == EExpressionEvaluation::ConstantZero)
	{
		Value = Context.EmitConstantZero(Scope, ResultType);
	}
	else if (Evaluation == EExpressionEvaluation::Constant || Evaluation == EExpressionEvaluation::Preshader)
	{
		Value = Context.EmitPreshaderOrConstant(Scope, RequestedType, ResultType, this);
	}
	else
	{
		FEmitValueShaderResult Result;
		EmitValueShader(Context, Scope, RequestedType, Result);
		check(Result.Code);
		Value = Result.Code;
	}
	return Context.EmitCast(Scope, Value, ResultType);
}

FEmitShaderExpression* FExpression::GetValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, const Shader::FType& ResultType) const
{
	const FPreparedType& PreparedType = Context.GetPreparedType(this);
	return GetValueShader(Context, Scope, RequestedType, PreparedType, ResultType);
}

FEmitShaderExpression* FExpression::GetValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType) const
{
	const FPreparedType& PreparedType = Context.GetPreparedType(this);
	return GetValueShader(Context, Scope, RequestedType, PreparedType, PreparedType.GetType());
}

FEmitShaderExpression* FExpression::GetValueShader(FEmitContext& Context, FEmitScope& Scope, const Shader::FType& ResultType) const
{
	return GetValueShader(Context, Scope, ResultType, ResultType);
}

FEmitShaderExpression* FExpression::GetValueShader(FEmitContext& Context, FEmitScope& Scope, Shader::EValueType ResultType) const
{
	return GetValueShader(Context, Scope, ResultType, ResultType);
}

FEmitShaderExpression* FExpression::GetValueShader(FEmitContext& Context, FEmitScope& Scope) const
{
	const FPreparedType& PreparedType = Context.GetPreparedType(this);
	return GetValueShader(Context, Scope, PreparedType.GetRequestedType(), PreparedType, PreparedType.GetType());
}

Shader::FType FExpression::GetValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, const FPreparedType& PreparedType, const Shader::FType& ResultType, Shader::FPreshaderData& OutPreshader) const
{
	FOwnerScope OwnerScope(*Context.Errors, GetOwner());
	
	const int32 PrevStackPosition = Context.PreshaderStackPosition;
	const EExpressionEvaluation Evaluation = PreparedType.GetEvaluation(Scope, RequestedType);
	check(Evaluation != EExpressionEvaluation::None && Evaluation != EExpressionEvaluation::Unknown);

	FEmitValuePreshaderResult Result(OutPreshader);
	if (Evaluation == EExpressionEvaluation::ConstantZero)
	{
		check(!ResultType.IsVoid());
		Context.PreshaderStackPosition++;
		Result.Type = ResultType;
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(ResultType);
	}
	else if (Evaluation == EExpressionEvaluation::Constant)
	{
		const Shader::FValue ConstantValue = GetValueConstant(Context, Scope, RequestedType, ResultType);
		Context.PreshaderStackPosition++;
		OutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(ConstantValue);
		Result.Type = ConstantValue.Type;
	}
	else
	{
		check(Evaluation != EExpressionEvaluation::Shader);
		EmitValuePreshader(Context, Scope, RequestedType, Result);
	}
	check(Context.PreshaderStackPosition == PrevStackPosition + 1);
	return Result.Type;
}

Shader::FType FExpression::GetValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, const Shader::FType& ResultType, Shader::FPreshaderData& OutPreshader) const
{
	const FPreparedType& PreparedType = Context.GetPreparedType(this);
	return GetValuePreshader(Context, Scope, RequestedType, PreparedType, ResultType, OutPreshader);
}

Shader::FType FExpression::GetValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, Shader::FPreshaderData& OutPreshader) const
{
	const FPreparedType& PreparedType = Context.GetPreparedType(this);
	return GetValuePreshader(Context, Scope, RequestedType, PreparedType, PreparedType.GetType(), OutPreshader);
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, const FPreparedType& PreparedType, const Shader::FType& ResultType) const
{
	FOwnerScope OwnerScope(*Context.Errors, GetOwner());

	const EExpressionEvaluation Evaluation = PreparedType.GetEvaluation(Scope, RequestedType);
	if (Evaluation == EExpressionEvaluation::ConstantZero)
	{
		return Shader::FValue(ResultType);
	}
	else
	{
		check(Evaluation == EExpressionEvaluation::Constant || Evaluation == EExpressionEvaluation::ConstantLoop);
		Shader::FPreshaderData ConstantPreshader;
		{
			const int32 PrevPreshaderStackPosition = Context.PreshaderStackPosition;
			FEmitValuePreshaderResult PreshaderResult(ConstantPreshader);
			EmitValuePreshader(Context, Scope, RequestedType, PreshaderResult);
			check(Context.PreshaderStackPosition == PrevPreshaderStackPosition + 1);
			Context.PreshaderStackPosition--;
		}

		// Evaluate the constant preshader and store its value
		Shader::FPreshaderStack Stack;
		const Shader::FPreshaderValue PreshaderValue = ConstantPreshader.EvaluateConstant(*Context.Material, Stack);
		Shader::FValue Result = PreshaderValue.AsShaderValue(Context.TypeRegistry);
		return Shader::Cast(Result, ResultType);
	}
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, const FPreparedType& PreparedType) const
{
	return GetValueConstant(Context, Scope, RequestedType, PreparedType, PreparedType.GetType());
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, const Shader::FType& ResultType) const
{
	const FPreparedType PreparedType = Context.GetPreparedType(this);
	return GetValueConstant(Context, Scope, RequestedType, PreparedType, ResultType);
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType) const
{
	const FPreparedType PreparedType = Context.GetPreparedType(this);
	return GetValueConstant(Context, Scope, RequestedType, PreparedType, PreparedType.GetType());
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context, FEmitScope& Scope, const FPreparedType& PreparedType, const Shader::FType& ResultType) const
{
	return GetValueConstant(Context, Scope, FRequestedType(ResultType), PreparedType, ResultType);
}

Shader::FValue FExpression::GetValueConstant(FEmitContext& Context, FEmitScope& Scope, const FPreparedType& PreparedType, Shader::EValueType ResultType) const
{
	return GetValueConstant(Context, Scope, FRequestedType(ResultType), PreparedType, ResultType);
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

bool FTree::Finalize()
{
	// Resolve values for any PHI nodes that were generated
	// Resolving a PHI may produce additional PHIs
	while (PHIExpressions.Num() > 0)
	{
		FExpressionLocalPHI* Expression = PHIExpressions.Pop(false);
		for (int32 i = 0; i < Expression->NumValues; ++i)
		{
			const FExpression* LocalValue = AcquireLocal(*Expression->Scopes[i], Expression->LocalName);
			if (!LocalValue)
			{
				//Errorf(TEXT("Local %s is not assigned on all control paths"), *Expression->LocalName.ToString());
				return false;
			}

			for(const FLocalPHIChainEntry& Entry : Expression->Chain)
			{
				const ELocalPHIChainType ChainType = Entry.Type;
				if (ChainType == ELocalPHIChainType::Ddx || ChainType == ELocalPHIChainType::Ddy)
				{
					const FExpressionDerivatives Derivatives = GetAnalyticDerivatives(LocalValue);
					LocalValue = (ChainType == ELocalPHIChainType::Ddx) ? Derivatives.ExpressionDdx : Derivatives.ExpressionDdy;
				}
				else
				{
					check(ChainType == ELocalPHIChainType::PreviousFrame);
					LocalValue = GetPreviousFrame(LocalValue, Entry.RequestedType);
				}
			}
			// May be nullptr if derivatives are not valid
			Expression->Values[i] = LocalValue;
		}
	}

	PHIExpressions.Shrink();
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

const FExpression* FTree::FindExpression(FXxHash64 Hash) const
{
	FExpression const* const* FoundExpression = ExpressionMap.Find(Hash);
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

void FTree::AssignLocal(FScope& Scope, const FName& LocalName, const FExpression* Value)
{
	Scope.LocalMap.Add(LocalName, Value);
}

const FExpression* FTree::AcquireLocal(FScope& Scope, const FName& LocalName)
{
	FExpression const* const* FoundExpression = Scope.LocalMap.Find(LocalName);
	if (FoundExpression)
	{
		return *FoundExpression;
	}

	const TArrayView<FScope*> PreviousScopes = Scope.GetPreviousScopes();
	if (PreviousScopes.Num() > 1)
	{
		const FExpression* Expression = NewExpression<FExpressionLocalPHI>(LocalName, PreviousScopes);
		Scope.LocalMap.Add(LocalName, Expression);
		return Expression;
	}

	if (PreviousScopes.Num() == 1)
	{
		return AcquireLocal(*PreviousScopes[0], LocalName);
	}

	return nullptr;
}

const FExpression* FTree::NewFunctionCall(FScope& Scope, FFunction* Function, int32 OutputIndex)
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

FExpressionDerivatives FTree::GetAnalyticDerivatives(const FExpression* InExpression)
{
	FExpressionDerivatives Derivatives;
	if (InExpression)
	{
		FOwnerScope OwnerScope(*this, InExpression->GetOwner()); // Associate any newly created nodes with the same owner as the input expression
		InExpression->ComputeAnalyticDerivatives(*this, Derivatives);
	}
	return Derivatives;
}

const FExpression* FTree::GetPreviousFrame(const FExpression* InExpression, const FRequestedType& RequestedType)
{
	const FExpression* Result = InExpression;
	if (Result && !RequestedType.IsVoid())
	{
		FOwnerScope OwnerScope(*this, InExpression->GetOwner()); // Associate any newly created nodes with the same owner as the input expression
		const FExpression* PrevFrameExpression = InExpression->ComputePreviousFrame(*this, RequestedType);
		if (PrevFrameExpression)
		{
			Result = PrevFrameExpression;
		}
	}
	return Result;
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

} // namespace UE::HLSLTree

#endif // WITH_EDITOR
