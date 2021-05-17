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

UE::HLSLTree::EExpressionEvaluationType UE::HLSLTree::CombineEvaluationTypes(UE::HLSLTree::EExpressionEvaluationType Lhs, UE::HLSLTree::EExpressionEvaluationType Rhs)
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

UE::HLSLTree::FEmitContext::FScopeEntry* UE::HLSLTree::FEmitContext::FindScope(UE::HLSLTree::FScope* Scope)
{
	for (int32 Index = ScopeStack.Num() - 1; Index >= 0; --Index)
	{
		FScopeEntry& Entry = ScopeStack[Index];
		if (Entry.Scope == Scope)
		{
			return &Entry;
		}
	}
	return nullptr;
}

int32 UE::HLSLTree::FEmitContext::FindScopeIndex(FScope* Scope)
{
	for (int32 Index = ScopeStack.Num() - 1; Index >= 0; --Index)
	{
		FScopeEntry& Entry = ScopeStack[Index];
		if (Entry.Scope == Scope)
		{
			return Index;
		}
	}
	return INDEX_NONE;
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

const UE::HLSLTree::FEmitValue* UE::HLSLTree::FEmitContext::AcquireValue(UE::HLSLTree::FLocalDeclaration* Declaration)
{
	check(Declaration);
	check(Declaration->Type != Shader::EValueType::Void);

	FDeclarationEntry*& Entry = FunctionStack.Last().DeclarationMap.FindOrAdd(Declaration);
	if (!Entry)
	{
		FScopeEntry* ScopeEntry = FindScope(Declaration->ParentScope);
		check(ScopeEntry);
		Entry = new(*Allocator) FDeclarationEntry();
		Entry->Value.EvaluationType = EExpressionEvaluationType::Shader;
		Entry->Value.ExpressionType = Declaration->Type;

		FLocalHLSLCodeWriter LocalWriter;
		LocalWriter.WriteConstant(Shader::FValue(Declaration->Type));

		Entry->Value.Code = AllocateStringf(*Allocator, TEXT("Local%d"), NumExpressionLocals++);
		const Shader::FValueTypeDescription& TypeDesc = Shader::GetValueTypeDescription(Declaration->Type);
		ScopeEntry->ExpressionCodeWriter->WriteLinef(TEXT("%s %s = %s;"),
			TypeDesc.Name,
			Entry->Value.Code,
			LocalWriter.StringBuilder->ToString());
	}

	return &Entry->Value;
}

const UE::HLSLTree::FEmitValue* UE::HLSLTree::FEmitContext::AcquireValue(UE::HLSLTree::FExpression* Expression)
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
		if (Expression->EmitCode(*this, EmitResult))
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
					const int32 ScopeIndex = FindScopeIndex(Expression->ParentScope);
					check(ScopeIndex != INDEX_NONE);

					// Check to see if we've already generated code for an equivalent expression in either this scope, or any outer visible scope
					const FSHAHash Hash = LocalWriter.GetCodeHash();
					const TCHAR* Declaration = nullptr;
					for (int32 CheckScopeIndex = ScopeIndex; CheckScopeIndex >= 0; --CheckScopeIndex)
					{
						const FScopeEntry& CheckScopeEntry = ScopeStack[CheckScopeIndex];
						const TCHAR** FoundDeclaration = CheckScopeEntry.ExpressionMap->Find(Hash);
						if (FoundDeclaration)
						{
							// Re-use results from previous matching expression
							Declaration = *FoundDeclaration;
							break;
						}
					}

					if (!Declaration)
					{
						const Shader::FValueTypeDescription& TypeDesc = Shader::GetValueTypeDescription(EmitResult.Type);
						Declaration = AllocateStringf(*Allocator, TEXT("Local%d"), NumExpressionLocals++);
						ScopeStack[ScopeIndex].ExpressionCodeWriter->WriteLinef(TEXT("const %s %s = %s;"),
							TypeDesc.Name,
							Declaration,
							LocalWriter.GetStringBuilder().ToString());
						ScopeStack[ScopeIndex].ExpressionMap->Add(Hash, Declaration);
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
		FScopeEntry* ParentScopeEntry = FindScope(FunctionCall->ParentScope);
		check(ParentScopeEntry);

		{
			FFunctionStackEntry& StackEntry = FunctionStack.AddDefaulted_GetRef();
			StackEntry.FunctionCall = FunctionCall;
		}

		{
			FScopeEntry& ScopeEntry = ScopeStack.AddDefaulted_GetRef();
			ScopeEntry.Scope = FunctionCall->FunctionScope;
			ScopeEntry.ExpressionCodeWriter = ParentScopeEntry->ExpressionCodeWriter;
			ScopeEntry.ExpressionMap = ParentScopeEntry->ExpressionMap;
		}

		// Emit the function's HLSL into the current scope (allows sharing expressions across functions called from the same scope)
		FLocalHLSLCodeWriter LocalWriter;
		FunctionCall->FunctionScope->EmitUnscopedHLSL(*this, LocalWriter);
		ParentScopeEntry->ExpressionCodeWriter->Append(LocalWriter);

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

		{
			const FScopeEntry PoppedScopeEntry = ScopeStack.Pop(false);
			check(PoppedScopeEntry.Scope == FunctionCall->FunctionScope);
		}

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

void UE::HLSLTree::FNodeVisitor::VisitNode(UE::HLSLTree::FNode* Node)
{
	if (Node)
	{
		Node->Visit(*this);
	}
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FStatement::Visit(UE::HLSLTree::FNodeVisitor& Visitor)
{
	return Visitor.OnStatement(*this);
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FExpression::Visit(UE::HLSLTree::FNodeVisitor& Visitor)
{
	return Visitor.OnExpression(*this);
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FLocalDeclaration::Visit(UE::HLSLTree::FNodeVisitor& Visitor)
{
	return Visitor.OnLocalDeclaration(*this);
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FParameterDeclaration::Visit(UE::HLSLTree::FNodeVisitor& Visitor)
{
	return Visitor.OnParameterDeclaration(*this);
}

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FTextureParameterDeclaration::Visit(UE::HLSLTree::FNodeVisitor& Visitor)
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

UE::HLSLTree::ENodeVisitResult UE::HLSLTree::FScope::Visit(UE::HLSLTree::FNodeVisitor& Visitor)
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

bool UE::HLSLTree::FScope::EmitHLSL(FEmitContext& Context, FCodeWriter& OutWriter) const
{
	const int32 ScopeIndentLevel = OutWriter.IndentLevel + 1;

	FLocalHLSLCodeWriter DeclarationCodeWriter;
	DeclarationCodeWriter.IndentLevel = ScopeIndentLevel;

	TMap<FSHAHash, const TCHAR*> LocalExpressionMap;

	FEmitContext::FScopeEntry& ScopeEntry = Context.ScopeStack.AddDefaulted_GetRef();
	ScopeEntry.Scope = this;
	ScopeEntry.ExpressionCodeWriter = &DeclarationCodeWriter;
	ScopeEntry.ExpressionMap = &LocalExpressionMap;

	OutWriter.WriteLine(TEXT("{"));

	FLocalHLSLCodeWriter StatementCodeWriter;
	StatementCodeWriter.IndentLevel = ScopeIndentLevel;
	const FStatement* Statement = FirstStatement;
	bool bResult = true;
	while (Statement)
	{
		if (!Statement->EmitHLSL(Context, StatementCodeWriter))
		{
			bResult = false;
			break;
		}

		// First write any expressions needed by the statement, then the statement itself
		OutWriter.Append(DeclarationCodeWriter);
		OutWriter.Append(StatementCodeWriter);

		DeclarationCodeWriter.Reset();
		StatementCodeWriter.Reset();

		Statement = Statement->NextStatement;
	}

	OutWriter.WriteLine(TEXT("}"));
	Context.ScopeStack.Pop(false);

	return bResult;
}

bool UE::HLSLTree::FScope::EmitUnscopedHLSL(FEmitContext& Context, FCodeWriter& OutWriter) const
{
	FCodeWriter& DeclarationCodeWriter = *Context.ScopeStack.Last().ExpressionCodeWriter;

	FLocalHLSLCodeWriter StatementCodeWriter;
	StatementCodeWriter.IndentLevel = OutWriter.IndentLevel;
	const FStatement* Statement = FirstStatement;
	bool bResult = true;
	while (Statement)
	{
		if (!Statement->EmitHLSL(Context, StatementCodeWriter))
		{
			bResult = false;
			break;
		}

		// First write any expressions needed by the statement, then the statement itself
		OutWriter.Append(DeclarationCodeWriter);
		OutWriter.Append(StatementCodeWriter);

		DeclarationCodeWriter.Reset();
		StatementCodeWriter.Reset();

		Statement = Statement->NextStatement;
	}
	return bResult;
}

void UE::HLSLTree::FScope::AddDeclaration(UE::HLSLTree::FLocalDeclaration* Declaration)
{
	check(!Declaration->ParentScope);
	Declaration->ParentScope = this;
}

void UE::HLSLTree::FScope::AddExpression(UE::HLSLTree::FExpression* Expression)
{
	check(!Expression->ParentScope);
	Expression->ParentScope = this;
}

void UE::HLSLTree::FScope::UseNode(UE::HLSLTree::FNode* Node)
{
	FScope* Scope0 = this;
	FScope* Scope1 = Node->ParentScope;
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
	Node->ParentScope = Scope0;
}

void UE::HLSLTree::FScope::UseDeclaration(UE::HLSLTree::FLocalDeclaration* Declaration)
{
	UseNode(Declaration);
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

	virtual ENodeVisitResult OnLocalDeclaration(FLocalDeclaration& InDeclaration) override
	{
		Scope->UseNode(&InDeclaration);
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

void UE::HLSLTree::FScope::UseExpression(UE::HLSLTree::FExpression* Expression)
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

void UE::HLSLTree::FScope::AddStatement(UE::HLSLTree::FStatement* Statement)
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

bool UE::HLSLTree::FTree::EmitHLSL(UE::HLSLTree::FEmitContext& Context, UE::HLSLTree::FCodeWriter& Writer) const
{
	return RootScope->EmitHLSL(Context, Writer);
}

UE::HLSLTree::FScope* UE::HLSLTree::FTree::NewScope(UE::HLSLTree::FScope& Scope)
{
	FScope* NewScope = NewNode<FScope>();
	NewScope->ParentScope = &Scope;
	NewScope->NestedLevel = Scope.NestedLevel + 1;
	return NewScope;
}

UE::HLSLTree::FLocalDeclaration* UE::HLSLTree::FTree::NewLocalDeclaration(UE::HLSLTree::FScope& Scope, Shader::EValueType Type, const FName& Name)
{
	FLocalDeclaration* Declaration = NewNode<FLocalDeclaration>(Name, Type);
	Scope.AddDeclaration(Declaration);
	return Declaration;
}

UE::HLSLTree::FParameterDeclaration* UE::HLSLTree::FTree::NewParameterDeclaration(UE::HLSLTree::FScope& Scope, const FName& Name, const Shader::FValue& DefaultValue)
{
	FParameterDeclaration* Declaration = NewNode<FParameterDeclaration>(Name, DefaultValue);
	Declaration->ParentScope = &Scope;
	return Declaration;
}

UE::HLSLTree::FTextureParameterDeclaration* UE::HLSLTree::FTree::NewTextureParameterDeclaration(UE::HLSLTree::FScope& Scope, const FName& Name, const UE::HLSLTree::FTextureDescription& DefaultValue)
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
