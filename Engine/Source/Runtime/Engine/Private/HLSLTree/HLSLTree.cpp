// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTree.h"
#include "Misc/StringBuilder.h"
#include "Misc/MemStack.h"
#include "MaterialShared.h" // TODO - split preshader out into its own module

class FLocalHLSLCodeWriter : public UE::HLSLTree::FCodeWriter
{
public:
	FLocalHLSLCodeWriter() : UE::HLSLTree::FCodeWriter(&LocalStringBuilder) {}

	TStringBuilder<2048> LocalStringBuilder;
};

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

template <typename FormatType, typename... ArgTypes>
const TCHAR* AllocateStringf(FMemStackBase& Allocator, const FormatType& Format, ArgTypes... Args)
{
	TCHAR Buffer[1024];
	const int32 Length = FCString::Snprintf(Buffer, 1024, Format, Forward<ArgTypes>(Args)...);
	check(Length > 0);
	TCHAR* Result = New<TCHAR>(Allocator, Length + 1);
	FMemory::Memcpy(Result, Buffer, Length * sizeof(TCHAR));
	Result[Length] = 0;
	return Result;
}

const TCHAR* AllocateString(FMemStackBase& Allocator, const FStringBuilderBase& StringBuilder)
{
	const int32 Length = StringBuilder.Len();
	TCHAR* Result = New<TCHAR>(Allocator, Length + 1);
	FMemory::Memcpy(Result, StringBuilder.GetData(), Length * sizeof(TCHAR));
	Result[Length] = 0;
	return Result;
}

UE::HLSLTree::FCodeWriter* UE::HLSLTree::FCodeWriter::Create(FMemStackBase& Allocator)
{
	static const int32 InitialBufferSize = 4 * 1024;
	TCHAR* Buffer = New<TCHAR>(Allocator, InitialBufferSize);
	FStringBuilderBase* LocalStringBuilder = new(Allocator) TStringBuilderBase<TCHAR>(Buffer, InitialBufferSize);
	return new(Allocator) FCodeWriter(LocalStringBuilder);
}

FSHAHash UE::HLSLTree::FCodeWriter::GetCodeHash() const
{
	FSHAHash Hash;
	FSHA1::HashBuffer(StringBuilder->GetData(), StringBuilder->Len() * sizeof(TCHAR), Hash.Hash);
	return Hash;
}

void UE::HLSLTree::FCodeWriter::IncreaseIndent()
{
	++IndentLevel;
}

void UE::HLSLTree::FCodeWriter::DecreaseIndent()
{
	check(IndentLevel > 0);
	--IndentLevel;
}

void UE::HLSLTree::FCodeWriter::WriteIndent()
{
	for (int32 i = 0; i < IndentLevel; ++i)
	{
		StringBuilder->Append(TCHAR('\t'));
	}
}

void UE::HLSLTree::FCodeWriter::Reset()
{
	StringBuilder->Reset();
}

void UE::HLSLTree::FCodeWriter::Append(const FCodeWriter& InWriter)
{
	const int32 Length = InWriter.StringBuilder->Len();
	if (Length > 0)
	{
		StringBuilder->Append(InWriter.StringBuilder->GetData(), Length);
	}
}


void UE::HLSLTree::FCodeWriter::WriteConstant(const Shader::FValue& Value)
{
	auto ToString = [](bool v)
	{
		return v ? TEXT("true") : TEXT("false");
	};

	switch (Value.GetType())
	{
	case Shader::EValueType::Float1:
		Writef(TEXT("%0.8f"), Value.Component[0].Float);
		break;
	case Shader::EValueType::Float2:
		Writef(TEXT("float2(%0.8f, %0.8f)"), Value.Component[0].Float, Value.Component[1].Float);
		break;
	case Shader::EValueType::Float3:
		Writef(TEXT("float3(%0.8f, %0.8f, %0.8f)"), Value.Component[0].Float, Value.Component[1].Float, Value.Component[2].Float);
		break;
	case Shader::EValueType::Float4:
		Writef(TEXT("float4(%0.8f, %0.8f, %0.8f, %0.8f)"), Value.Component[0].Float, Value.Component[1].Float, Value.Component[2].Float, Value.Component[3].Float);
		break;
	case Shader::EValueType::Int1:
		Writef(TEXT("%d"), Value.Component[0].Int);
		break;
	case Shader::EValueType::Int2:
		Writef(TEXT("int2(%d, %d)"), Value.Component[0].Int, Value.Component[1].Int);
		break;
	case Shader::EValueType::Int3:
		Writef(TEXT("int3(%d, %d, %d)"), Value.Component[0].Int, Value.Component[1].Int, Value.Component[2].Int);
		break;
	case Shader::EValueType::Int4:
		Writef(TEXT("int4(%d, %d, %d, %d)"), Value.Component[0].Int, Value.Component[1].Int, Value.Component[2].Int, Value.Component[3].Int);
		break;
	case Shader::EValueType::Bool1:
		Writef(TEXT("%s"), ToString(Value.Component[0].Bool));
		break;
	case Shader::EValueType::Bool2:
		Writef(TEXT("bool2(%s, %s)"), ToString(Value.Component[0].Bool), ToString(Value.Component[1].Bool));
		break;
	case Shader::EValueType::Bool3:
		Writef(TEXT("bool3(%s, %s, %s)"), ToString(Value.Component[0].Bool), ToString(Value.Component[1].Bool), ToString(Value.Component[2].Bool));
		break;
	case Shader::EValueType::Bool4:
		Writef(TEXT("bool4(%s, %s, %s, %s)"), ToString(Value.Component[0].Bool), ToString(Value.Component[1].Bool), ToString(Value.Component[2].Bool), ToString(Value.Component[3].Bool));
		break;
	default:
		checkNoEntry();
		break;
	}
}

UE::HLSLTree::FEmitContext::FEmitContext()
{
	FunctionStack.AddDefaulted();
}

UE::HLSLTree::FEmitContext::~FEmitContext()
{
	// Make sure we have only the root stack entry
	check(FunctionStack.Num() == 1);

	for (Shader::FPreshaderData* Preshader : TempPreshaders)
	{
		delete Preshader;
	}
}

UE::HLSLTree::FEmitScope* UE::HLSLTree::FEmitContext::FindScope(const FScope& Scope)
{
	FEmitScope** FoundScope = ScopeMap.Find(&Scope);
	return FoundScope ? *FoundScope : nullptr;
}

UE::HLSLTree::FEmitScope* UE::HLSLTree::FEmitContext::AcquireScope(const FScope& Scope)
{
	FEmitScope** FoundScope = ScopeMap.Find(&Scope);
	FEmitScope* EmitScope = nullptr;
	if (FoundScope)
	{
		EmitScope = *FoundScope;
	}
	else
	{
		EmitScope = new(*Allocator) FEmitScope();
		EmitScope->SourceScope = &Scope;
		if (Scope.ParentScope)
		{
			EmitScope->ParentScope = AcquireScope(*Scope.ParentScope);
		}
		ScopeMap.Add(&Scope, EmitScope);
	}

	return EmitScope;
}

UE::HLSLTree::FEmitScope& UE::HLSLTree::FEmitContext::GetCurrentScope()
{
	return *ScopeStack.Last();
}

void UE::HLSLTree::FExpressionEmitResult::ForwardValue(FEmitContext& Context, const FEmitValue* InValue)
{
	check(InValue);
	EvaluationType = InValue->GetEvaluationType();
	Type = InValue->GetExpressionType();
	if (EvaluationType == EExpressionEvaluationType::Shader)
	{
		bInline = true;
		Writer.Writef(TEXT("%s"), Context.GetCode(InValue));
	}
	else
	{
		Context.AppendPreshader(InValue, Preshader);
	}
}

const UE::HLSLTree::FEmitValue* UE::HLSLTree::FEmitContext::AcquireValue(FExpression* Expression)
{
	if (!Expression)
	{
		return nullptr;
	}

	FFunctionStackEntry& FunctionStackEntry = FunctionStack.Last();
	FDeclarationEntry** FoundEntry = FunctionStackEntry.DeclarationMap.Find(Expression);
	FDeclarationEntry* Entry = FoundEntry ? *FoundEntry : nullptr;
	if (!Entry)
	{
		FLocalHLSLCodeWriter LocalWriter;
		Shader::FPreshaderData LocalPreshader;
		FExpressionEmitResult EmitResult(LocalWriter, LocalPreshader);

		bool bEmitResult = false;
		{
			bool bAlreadyPending = false;
			const FSetElementId Id = PendingEmitValueExpressions.Add(Expression, &bAlreadyPending);
			if (!bAlreadyPending)
			{
				bEmitResult = Expression->EmitCode(*this, EmitResult);
				PendingEmitValueExpressions.Remove(Id);
			}
		}

		if (bEmitResult)
		{
			check(EmitResult.EvaluationType != EExpressionEvaluationType::None);
			check(EmitResult.Type != Shader::EValueType::Void);

			Entry = new(*Allocator) FDeclarationEntry();
			FunctionStackEntry.DeclarationMap.Add(Expression, Entry);
			Entry->Value.ExpressionType = EmitResult.Type;
			Entry->Value.EvaluationType = EmitResult.EvaluationType;
			if (EmitResult.EvaluationType == EExpressionEvaluationType::Constant)
			{
				// Evaluate the constant preshader and store its value
				FMaterialRenderContext RenderContext(nullptr, *Material, nullptr);
				LocalPreshader.Evaluate(nullptr, RenderContext, Entry->Value.ConstantValue);
			}
			else if (EmitResult.EvaluationType == EExpressionEvaluationType::Preshader)
			{
				// Non-constant preshader, store it
				Shader::FPreshaderData* Preshader = new Shader::FPreshaderData(MoveTemp(LocalPreshader));
				TempPreshaders.Add(Preshader); // TODO - dedupe? store more efficiently?
				Entry->Value.Preshader = Preshader;
			}
			else
			{
				check(EmitResult.EvaluationType == EExpressionEvaluationType::Shader);
				if (EmitResult.bInline)
				{
					Entry->Value.Code = AllocateString(*Allocator, LocalWriter.GetStringBuilder());
				}
				else
				{
					FEmitScope* EmitScope = FindScope(*Expression->ParentScope);
					check(EmitScope);

					// Check to see if we've already generated code for an equivalent expression in either this scope, or any outer visible scope
					const FSHAHash Hash = LocalWriter.GetCodeHash();
					const TCHAR* Declaration = nullptr;

					FEmitScope* CheckScope = EmitScope;
					while (CheckScope)
					{
						const TCHAR** FoundDeclaration = CheckScope->ExpressionMap.Find(Hash);
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
						const Shader::FValueTypeDescription& TypeDesc = Shader::GetValueTypeDescription(EmitResult.Type);
						Declaration = AcquireLocalDeclarationCode();
						WriteStatementToScopef(*EmitScope, TEXT("const %s %s = %s;"),
							TypeDesc.Name,
							Declaration,
							LocalWriter.GetStringBuilder().ToString());
						EmitScope->ExpressionMap.Add(Hash, Declaration);
					}
					Entry->Value.Code = Declaration;
				}
			}
		}
	}

	return &Entry->Value;
}

const UE::HLSLTree::FEmitValue* UE::HLSLTree::FEmitContext::AcquireValue(FFunctionCall* FunctionCall, int32 OutputIndex)
{
	check(FunctionCall);

	FFunctionCallEntry*& Entry = FunctionStack.Last().FunctionCallMap.FindOrAdd(FunctionCall);
	if (!Entry)
	{
		FEmitScope* EmitScope = FindScope(*FunctionCall->ParentScope);
		check(EmitScope);

		{
			FFunctionStackEntry& StackEntry = FunctionStack.AddDefaulted_GetRef();
			StackEntry.FunctionCall = FunctionCall;
		}

		// Link the function scope to our parent scope
		ScopeMap.Add(FunctionCall->FunctionScope, EmitScope);
		FunctionCall->FunctionScope->EmitHLSL(*this, *EmitScope);

		// Assign function outputs
		FEmitValue* OutputValues = new(*Allocator) FEmitValue[FunctionCall->NumOutputs];
		for (int32 i = 0; i < FunctionCall->NumOutputs; ++i)
		{
			FExpression* OutputExpression = FunctionCall->Outputs[i];

			const FEmitValue* OutputValue = AcquireValue(OutputExpression);
			FEmitValue& Output = OutputValues[i];
			if (OutputValue)
			{
				Output = *OutputValue;
			}
			else
			{
				Output.EvaluationType = EExpressionEvaluationType::Constant;
				Output.ExpressionType = Shader::EValueType::Float1;
				Output.ConstantValue = 0.0f;
			}
		}

		verify(ScopeMap.Remove(FunctionCall->FunctionScope) == 1);

		{
			const FFunctionStackEntry PoppedStackEntry = FunctionStack.Pop(false);
			check(PoppedStackEntry.FunctionCall == FunctionCall);
		}

		Entry = new(*Allocator) FFunctionCallEntry();
		Entry->OutputValues = OutputValues;
		Entry->NumOutputs = FunctionCall->NumOutputs;
	}

	check(Entry->NumOutputs == FunctionCall->NumOutputs);
	check(OutputIndex >= 0 && OutputIndex < Entry->NumOutputs);
	return &Entry->OutputValues[OutputIndex];
}

const TCHAR* UE::HLSLTree::FEmitContext::AcquireLocalDeclarationCode()
{
	return AllocateStringf(*Allocator, TEXT("Local%d"), NumExpressionLocals++);
}

const TCHAR* UE::HLSLTree::FEmitContext::GetCode(const FEmitValue* Value) const
{
	check(Value);
	if (!Value->Code)
	{
		FLocalHLSLCodeWriter LocalWriter;
		if (Value->EvaluationType == EExpressionEvaluationType::Constant)
		{
			LocalWriter.WriteConstant(Value->ConstantValue);
		}
		else
		{
			check(Value->EvaluationType == EExpressionEvaluationType::Preshader);
			check(Value->Preshader);

			FUniformExpressionSet& UniformExpressionSet = MaterialCompilationOutput->UniformExpressionSet;
			FMaterialUniformPreshaderHeader* PreshaderHeader = nullptr;
			if (Value->ExpressionType == Shader::EValueType::Float1)
			{
				const static TCHAR IndexToMask[] = { 'x', 'y', 'z', 'w' };
				const int32 ScalarInputIndex = UniformExpressionSet.UniformScalarPreshaders.Num();
				LocalWriter.Writef(TEXT("Material.ScalarExpressions[%u].%c"), ScalarInputIndex / 4, IndexToMask[ScalarInputIndex % 4]);

				PreshaderHeader = &UniformExpressionSet.UniformScalarPreshaders.AddDefaulted_GetRef();
			}
			else
			{
				const TCHAR* Mask = TEXT("");
				switch (Value->ExpressionType)
				{
				case Shader::EValueType::Float1: Mask = TEXT(".r"); break;
				case Shader::EValueType::Float2: Mask = TEXT(".rg"); break;
				case Shader::EValueType::Float3: Mask = TEXT(".rgb"); break;
				case Shader::EValueType::Float4: break;
				default: checkNoEntry(); break;
				};
				const int32 VectorInputIndex = UniformExpressionSet.UniformVectorPreshaders.Num();
				LocalWriter.Writef(TEXT("Material.VectorExpressions[%u]%s"), VectorInputIndex, Mask);

				PreshaderHeader = &UniformExpressionSet.UniformVectorPreshaders.AddDefaulted_GetRef();
			}
			PreshaderHeader->OpcodeOffset = UniformExpressionSet.UniformPreshaderData.Num();
			UniformExpressionSet.UniformPreshaderData.Append(*Value->Preshader);
			PreshaderHeader->OpcodeSize = Value->Preshader->Num();
		}
		Value->Code = AllocateString(*Allocator, LocalWriter.GetStringBuilder());
	}

	return Value->Code;
}

void UE::HLSLTree::FEmitContext::AppendPreshader(const FEmitValue* Value, Shader::FPreshaderData& InOutPreshader) const
{
	check(Value);
	if (Value->EvaluationType == EExpressionEvaluationType::Constant)
	{
		// Push the constant value
		InOutPreshader.WriteOpcode(Shader::EPreshaderOpcode::Constant);
		InOutPreshader.Write(Value->ConstantValue);
	}
	else
	{
		check(Value->EvaluationType == EExpressionEvaluationType::Preshader);
		check(Value->Preshader);
		InOutPreshader.Append(*Value->Preshader);
	}
}

FStringView UE::HLSLTree::FEmitContext::InternalAcquireInternedString(const TCHAR* InString, int32 InLength)
{
	TCHAR* InternedString = nullptr;
	int32 Length = 0;
	if (InString)
	{
		Length = InLength > 0 ? InLength : FCString::Strlen(InString);
		InternedString = new(*Allocator) TCHAR[Length + 1];
		FMemory::Memcpy(InternedString, InString, Length * sizeof(TCHAR));
		InternedString[Length] = 0;
	}
	return FStringView(InternedString, Length);
}

FStringView UE::HLSLTree::FEmitContext::AcquireInternedString(const TCHAR* Format, ...)
{
	TCHAR Buffer[1024];

	va_list va;
	va_start(va, Format);
	const int32 Length = FPlatformString::GetVarArgs(Buffer, UE_ARRAY_COUNT(Buffer), Format, va);
	va_end(va);

	check(Length >= 0 && Length < UE_ARRAY_COUNT(Buffer));

	return InternalAcquireInternedString(Buffer, Length);
}

void UE::HLSLTree::FEmitContext::InternalWriteStatementToScope(FEmitScope& EmitScope, FStringView InternedCode)
{
	FEmitStatement* EmitStatement = new(*Allocator) FEmitStatement();
	EmitStatement->Code = InternedCode;
	if (EmitScope.LastStatement)
	{
		EmitStatement->LinkAfter(EmitScope.LastStatement);
	}
	else
	{
		EmitScope.FirstStatement = EmitStatement;
	}
	EmitScope.LastStatement = EmitStatement;
}

bool UE::HLSLTree::FEmitContext::InternalWriteScope(const FScope& Scope, FStringView InternedCode)
{
	FEmitScope* EmitScope = AcquireScope(Scope);
	if (!Scope.EmitHLSL(*this, *EmitScope))
	{
		return false;
	}

	FEmitScopeLink* Link = InternalWriteScopeLink(InternedCode);
	Link->NextScope = EmitScope;
	return true;
}

UE::HLSLTree::FEmitScopeLink* UE::HLSLTree::FEmitContext::InternalWriteScopeLink(FStringView InternedCode)
{
	FEmitScope& EmitScope = GetCurrentScope();
	FEmitScopeLink* EmitLink = new(*Allocator) FEmitScopeLink();
	EmitLink->Code = InternedCode;
	if (EmitScope.LastLink)
	{
		EmitLink->LinkAfter(EmitScope.LastLink);
	}
	else
	{
		EmitScope.FirstLink = EmitLink;
	}
	EmitScope.LastLink = EmitLink;
	return EmitLink;
}

void UE::HLSLTree::FEmitContext::WriteDeclaration(FEmitScope& EmitScope, Shader::EValueType Type, const TCHAR* Declaration, const TCHAR* Value)
{
	FEmitDeclaration* EmitDeclaration = new(*Allocator) FEmitDeclaration();
	EmitDeclaration->Type = Type;
	EmitDeclaration->Declaration = Declaration;
	EmitDeclaration->Value = Value;
	EmitDeclaration->LinkHead(EmitScope.FirstDeclaration);
}

bool UE::HLSLTree::FEmitContext::WriteAssignment(FEmitScope& EmitScope, const TCHAR* Declaration, FExpression* Expression, Shader::EValueType& InOutType)
{
	const FEmitValue* Value = AcquireValue(Expression);
	if (Value)
	{
		const Shader::EValueType ValueType = Value->GetExpressionType();
		if (InOutType == Shader::EValueType::Void)
		{
			InOutType = ValueType;
		}
		else if (InOutType != ValueType)
		{
			return false;
		}
		WriteStatementToScopef(EmitScope, TEXT("%s = %s;"), Declaration, GetCode(Value));
	}
	else
	{
		FEmitAssignment* EmitAssignment = new(*Allocator) FEmitAssignment();
		EmitAssignment->Declaration = Declaration;
		EmitAssignment->Expression = Expression;
		EmitAssignment->LinkHead(EmitScope.FirstAssignment);
	}

	return true;
}

bool UE::HLSLTree::FEmitContext::FinalizeScope(FEmitScope& EmitScope)
{
	if (EmitScope.FirstAssignment)
	{
		// Scope has pending assigmnets, flush all of them, then restart finalization
		// (Since flushing pending assignments may generate additional pending assignments)
		FEmitAssignment::TConstIterator It(EmitScope.FirstAssignment);
		while (It)
		{
			const FEmitAssignment& EmitAssignment = *It;
			const FEmitValue* Value = AcquireValue(EmitAssignment.Expression);
			WriteStatementToScopef(EmitScope, TEXT("%s = %s;"), EmitAssignment.Declaration, GetCode(Value));
			It.Next();
		}
		EmitScope.FirstAssignment = nullptr;
		return false;
	}

	UE::HLSLTree::FEmitScopeLink::TConstIterator It(EmitScope.FirstLink);
	while (It)
	{
		const UE::HLSLTree::FEmitScopeLink& EmitLink = *It;
		if (EmitLink.NextScope)
		{
			if (!FinalizeScope(*EmitLink.NextScope))
			{
				return false;
			}
		}

		It.Next();
	}

	return true;
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

bool UE::HLSLTree::FExpressionLocalPHI::EmitCode(FEmitContext& Context, FExpressionEmitResult& OutResult) const
{
	FEmitScope* EmitScopes[MaxNumPreviousScopes] = { nullptr };
	const FEmitValue* EmitValues[MaxNumPreviousScopes] = { nullptr };
	Shader::EValueType CombinedValueType = Shader::EValueType::Void;

	const TCHAR* Declaration = Context.AcquireLocalDeclarationCode();

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

	bool bNeedToAddDeclaration = true;
	for (int32 i = 0; i < NumValues; ++i)
	{
		EmitScopes[i] = Context.AcquireScope(*Scopes[i]);
		if (Scopes[i] == DeclarationScope)
		{
			const FEmitValue* Value = Context.AcquireValue(Values[i]);
			CombinedValueType = Value->GetExpressionType();
			Context.WriteDeclaration(*EmitScopes[i], CombinedValueType, Declaration, Context.GetCode(Value));
			bNeedToAddDeclaration = false;
		}
		else
		{
			if (!Context.WriteAssignment(*EmitScopes[i], Declaration, Values[i], CombinedValueType))
			{
				return false;
			}
		}
	}

	if (CombinedValueType == Shader::EValueType::Void)
	{
		return false;
	}

	if (bNeedToAddDeclaration)
	{
		FEmitScope* EmitDeclarationScope = Context.FindScope(*DeclarationScope);
		Context.WriteDeclaration(*EmitDeclarationScope, CombinedValueType, Declaration);
	}

	OutResult.EvaluationType = EExpressionEvaluationType::Shader;
	OutResult.Type = CombinedValueType;
	OutResult.bInline = true;
	OutResult.Writer.Writef(TEXT("%s"), Declaration);
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

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FParameterDeclaration::Visit(FNodeVisitor& Visitor)
{
	return Visitor.OnParameterDeclaration(*this);
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FTextureParameterDeclaration::Visit(FNodeVisitor& Visitor)
{
	return Visitor.OnTextureParameterDeclaration(*this);
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FFunctionCall::Visit(FNodeVisitor& Visitor)
{
	const ENodeVisitResult Result = Visitor.OnFunctionCall(*this);
	if (ShouldVisitDependentNodes(Result))
	{
		// Don't visit the function scope, as that is from a different tree
		for (int32 i = 0; i < NumInputs; ++i)
		{
			Visitor.VisitNode(Inputs[i]);
		}
	}
	return Result;
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FScope::Visit(FNodeVisitor& Visitor)
{
	const ENodeVisitResult Result = Visitor.OnScope(*this);
	if (ShouldVisitDependentNodes(Result))
	{
		FStatement* Statement = FirstStatement;
		while (Statement)
		{
			Visitor.VisitNode(Statement);
			Statement = Statement->NextStatement;
		}
	}
	return Result;
}

bool UE::HLSLTree::FScope::EmitHLSL(FEmitContext& Context, FEmitScope& Scope) const
{
	const FStatement* Statement = FirstStatement;
	bool bResult = true;
	
	Context.ScopeStack.Add(&Scope);
	
	while (Statement)
	{
		if (!Statement->EmitHLSL(Context))
		{
			bResult = false;
			break;
		}
		Statement = Statement->NextStatement;
	}

	verify(Context.ScopeStack.Pop(false) == &Scope);

	return bResult;
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

void UE::HLSLTree::FScope::AddExpression(FExpression* Expression)
{
	check(!Expression->ParentScope);
	Expression->ParentScope = this;
}

void UE::HLSLTree::FScope::UseNode(FNode* Node)
{
	Node->ParentScope = FScope::FindSharedParent(this, Node->ParentScope);
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
		Scope->UseNode(&InExpression);
		return ENodeVisitResult::VisitDependentNodes;
	}

	virtual ENodeVisitResult OnFunctionCall(FFunctionCall& InFunctionCall) override
	{
		Scope->UseNode(&InFunctionCall);
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

void UE::HLSLTree::FScope::UseFunctionCall(FFunctionCall* FunctionCall)
{
	FNodeVisitor_MoveToScope Visitor(this);
	Visitor.VisitNode(FunctionCall);
}

void UE::HLSLTree::FScope::AddStatement(FStatement* Statement)
{
	check(!Statement->ParentScope);
	check(!Statement->NextStatement);

	Statement->ParentScope = this;
	if (!FirstStatement)
	{
		check(!LastStatement);
		FirstStatement = Statement;
		LastStatement = Statement;
	}
	else
	{
		check(LastStatement);
		LastStatement->NextStatement = Statement;
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
	for (int32 i = 0; i < IndentLevel; ++i)
	{
		InOutString.Append(TCHAR('\t'));
	}
}

static void WriteScope(const UE::HLSLTree::FEmitScope& EmitScope, int32 IndentLevel, FStringBuilderBase& InOutString)
{
	{
		UE::HLSLTree::FEmitDeclaration::TConstIterator It(EmitScope.FirstDeclaration);
		while (It)
		{
			const UE::HLSLTree::FEmitDeclaration& EmitDeclaration = *It;
			const UE::Shader::FValueTypeDescription TypeDesc = UE::Shader::GetValueTypeDescription(EmitDeclaration.Type);
			WriteIndent(IndentLevel, InOutString);
			if (EmitDeclaration.Value)
			{
				InOutString.Appendf(TEXT("%s %s = %s;\n"), TypeDesc.Name, EmitDeclaration.Declaration, EmitDeclaration.Value);
			}
			else
			{
				InOutString.Appendf(TEXT("%s %s;\n"), TypeDesc.Name, EmitDeclaration.Declaration);
			}
			It.Next();
		}
	}

	{
		UE::HLSLTree::FEmitStatement::TConstIterator It(EmitScope.FirstStatement);
		while (It)
		{
			const UE::HLSLTree::FEmitStatement& EmitStatement = *It;
			WriteIndent(IndentLevel, InOutString);
			InOutString.Append(EmitStatement.Code);
			InOutString.Append(TCHAR('\n'));
			It.Next();
		}
	}

	{
		UE::HLSLTree::FEmitScopeLink::TConstIterator It(EmitScope.FirstLink);
		while (It)
		{
			const UE::HLSLTree::FEmitScopeLink& EmitLink = *It;
			if (EmitLink.Code.Len() > 0)
			{
				WriteIndent(IndentLevel, InOutString);
				InOutString.Append(EmitLink.Code);
				InOutString.Append(TCHAR('\n'));
			}

			if (EmitLink.NextScope)
			{
				WriteIndent(IndentLevel, InOutString);
				InOutString.Append(TEXT("{\n"));
				WriteScope(*EmitLink.NextScope, IndentLevel + 1, InOutString);
				WriteIndent(IndentLevel, InOutString);
				InOutString.Append(TEXT("}\n"));
			}

			It.Next();
		}
	}
}

bool UE::HLSLTree::FTree::EmitHLSL(UE::HLSLTree::FEmitContext& Context, FCodeWriter& Writer) const
{
	FEmitScope* EmitRootScope = Context.AcquireScope(*RootScope);
	if (RootScope->EmitHLSL(Context, *EmitRootScope))
	{
		// Need to continue iterating FinalizeScope until it succeeds
		bool bFinalizeResult = false;
		while (!bFinalizeResult)
		{
			bFinalizeResult = Context.FinalizeScope(*EmitRootScope);
		}

		WriteScope(*EmitRootScope, 0, *Writer.StringBuilder);
		return true;
	}
	return false;
}

UE::HLSLTree::FScope* UE::HLSLTree::FTree::NewScope(FScope& Scope)
{
	FScope* NewScope = NewNode<FScope>();
	NewScope->ParentScope = &Scope;
	NewScope->NestedLevel = Scope.NestedLevel + 1;
	NewScope->NumPreviousScopes = 0;
	return NewScope;
}

UE::HLSLTree::FParameterDeclaration* UE::HLSLTree::FTree::NewParameterDeclaration(FScope& Scope, const FName& Name, const Shader::FValue& DefaultValue)
{
	FParameterDeclaration* Declaration = NewNode<FParameterDeclaration>(Name, DefaultValue);
	Declaration->ParentScope = &Scope;
	return Declaration;
}

UE::HLSLTree::FTextureParameterDeclaration* UE::HLSLTree::FTree::NewTextureParameterDeclaration(FScope& Scope, const FName& Name, const FTextureDescription& DefaultValue)
{
	FTextureParameterDeclaration* Declaration = NewNode<FTextureParameterDeclaration>(Name, DefaultValue);
	Declaration->ParentScope = &Scope;
	return Declaration;
}

UE::HLSLTree::FFunctionCall* UE::HLSLTree::FTree::NewFunctionCall(FScope& Scope,
	const FScope& InFunctionScope,
	FExpression* const* InInputs,
	FExpression* const* InOutputs,
	int32 InNumInputs,
	int32 InNumOutputs)
{
	FExpression** Inputs = New<FExpression*>(*Allocator, InNumInputs);
	FExpression** Outputs = New<FExpression*>(*Allocator, InNumOutputs);
	FMemory::Memcpy(Inputs, InInputs, InNumInputs * sizeof(FExpression*));
	FMemory::Memcpy(Outputs, InOutputs, InNumOutputs * sizeof(FExpression*));

	FFunctionCall* FunctionCall = NewNode<FFunctionCall>();
	FunctionCall->ParentScope = &Scope;
	FunctionCall->FunctionScope = &InFunctionScope;
	FunctionCall->Inputs = Inputs;
	FunctionCall->Outputs = Outputs;
	FunctionCall->NumInputs = InNumInputs;
	FunctionCall->NumOutputs = InNumOutputs;
	return FunctionCall;
}
