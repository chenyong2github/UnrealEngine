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

UE::HLSLTree::FExpressionTypeDescription UE::HLSLTree::GetExpressionTypeDescription(EExpressionType Type)
{
	switch (Type)
	{
	case EExpressionType::Void: return FExpressionTypeDescription(TEXT("void"), EExpressionComponentType::Void, 0);
	case EExpressionType::Float1: return FExpressionTypeDescription(TEXT("float"), EExpressionComponentType::Float, 1);
	case EExpressionType::Float2: return FExpressionTypeDescription(TEXT("float2"), EExpressionComponentType::Float, 2);
	case EExpressionType::Float3: return FExpressionTypeDescription(TEXT("float3"), EExpressionComponentType::Float, 3);
	case EExpressionType::Float4: return FExpressionTypeDescription(TEXT("float4"), EExpressionComponentType::Float, 4);
	case EExpressionType::MaterialAttributes: return FExpressionTypeDescription(TEXT("FMaterialAttributes"), EExpressionComponentType::MaterialAttributes, 0);
	default: checkNoEntry(); return FExpressionTypeDescription();
	}
}

UE::HLSLTree::EExpressionType UE::HLSLTree::MakeExpressionType(EExpressionComponentType ComponentType, int32 NumComponents)
{
	switch (ComponentType)
	{
	case EExpressionComponentType::Void:
		check(NumComponents == 0);
		return EExpressionType::Void;
	case EExpressionComponentType::MaterialAttributes:
		check(NumComponents == 0);
		return EExpressionType::MaterialAttributes;
	case EExpressionComponentType::Float:
		switch (NumComponents)
		{
		case 1: return EExpressionType::Float1;
		case 2: return EExpressionType::Float2;
		case 3: return EExpressionType::Float3;
		case 4: return EExpressionType::Float4;
		default: break;
		}
	default:
		break;
	}

	checkNoEntry();
	return EExpressionType::Void;
}

UE::HLSLTree::EExpressionType UE::HLSLTree::MakeExpressionType(EExpressionType BaseType, int32 NumComponents)
{
	return MakeExpressionType(GetExpressionTypeDescription(BaseType).ComponentType, NumComponents);
}

UE::HLSLTree::EExpressionType UE::HLSLTree::MakeArithmeticResultType(EExpressionType Lhs, EExpressionType Rhs, FString& OutErrorMessage)
{
	const FExpressionTypeDescription LhsDesc = GetExpressionTypeDescription(Lhs);
	const FExpressionTypeDescription RhsDesc = GetExpressionTypeDescription(Rhs);
	// Types with 0 components are non-arithmetic
	if (LhsDesc.NumComponents > 0 && RhsDesc.NumComponents > 0)
	{
		if (Lhs == Rhs)
		{
			return Lhs;
		}
		if (LhsDesc.ComponentType == RhsDesc.ComponentType)
		{
			if (LhsDesc.NumComponents == 1 || RhsDesc.NumComponents == 1)
			{
				// single component type is valid to combine with other type
				return MakeExpressionType(LhsDesc.ComponentType, FMath::Max(LhsDesc.NumComponents, RhsDesc.NumComponents));
			}
		}
		OutErrorMessage = FString::Printf(TEXT("Arithmetic between types %s and %s are undefined"), LhsDesc.Name, RhsDesc.Name);
	}
	else
	{
		OutErrorMessage = FString::Printf(TEXT("Attempting to perform arithmetic on non-numeric types: %s %s"), LhsDesc.Name, RhsDesc.Name);
	}

	return EExpressionType::Void;
}

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
		StringBuilder->Append(TCHAR(' '));
	}
}

FLinearColor UE::HLSLTree::FConstant::ToLinearColor() const
{
	switch (Type)
	{
	case EExpressionType::Float1: return FLinearColor(Float[0], 0.0f, 0.0f, 0.0f); break;
	case EExpressionType::Float2: return FLinearColor(Float[0], Float[1], 0.0f, 0.0f); break;
	case EExpressionType::Float3: return FLinearColor(Float[0], Float[1], Float[2], 0.0f); break;
	case EExpressionType::Float4: return FLinearColor(Float[0], Float[1], Float[2], Float[3]); break;
	default: checkNoEntry(); return FLinearColor(ForceInitToZero);
	}
}

void UE::HLSLTree::FConstant::EmitHLSL(UE::HLSLTree::FCodeWriter& Writer) const
{
	switch (Type)
	{
	case EExpressionType::Float1:
		Writer.Writef(TEXT("float(%0.8f)"), Float[0]);
		break;
	case EExpressionType::Float2:
		Writer.Writef(TEXT("float2(%0.8f, %0.8f)"), Float[0], Float[1]);
		break;
	case EExpressionType::Float3:
		Writer.Writef(TEXT("float3(%0.8f, %0.8f, %0.8f)"), Float[0], Float[1], Float[2]);
		break;
	case EExpressionType::Float4:
		Writer.Writef(TEXT("float4(%0.8f, %0.8f, %0.8f, %0.8f)"), Float[0], Float[1], Float[2], Float[3]);
		break;
	default:
		checkNoEntry();
		break;
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

const TCHAR* UE::HLSLTree::FEmitContext::AcquireHLSLReference(UE::HLSLTree::FLocalDeclaration* Declaration)
{
	check(Declaration);
	check(Declaration->Type != EExpressionType::Void);

	FDeclarationEntry& Entry = DeclarationMap.FindOrAdd(Declaration);
	if (!Entry.Definition)
	{
		FScopeEntry* ScopeEntry = FindScope(Declaration->ParentScope);
		check(ScopeEntry);

		Entry.Definition = AllocateStringf(*Allocator, TEXT("Local%d"), NumExpressionLocals++);
		const FExpressionTypeDescription& TypeDesc = GetExpressionTypeDescription(Declaration->Type);
		ScopeEntry->ExpressionCodeWriter->WriteLinef(TEXT("%s %s = (%s)0.0f;"),
			TypeDesc.Name,
			Entry.Definition,
			TypeDesc.Name);
	}

	return Entry.Definition;
}

const TCHAR* UE::HLSLTree::FEmitContext::AcquireHLSLReference(UE::HLSLTree::FExpression* Expression)
{
	check(Expression);
	check(Expression->Type != EExpressionType::Void);

	FDeclarationEntry& Entry = DeclarationMap.FindOrAdd(Expression);
	if (!Entry.Definition)
	{
		bool bInline = false;
		FLocalHLSLCodeWriter LocalWriter;
		if (Expression->EvaluationType == EExpressionEvaluationType::Shader)
		{
			bInline = Expression->IsInline();
			Expression->EmitHLSL(*this, LocalWriter);
		}
		else
		{
			if (Expression->EvaluationType == EExpressionEvaluationType::Constant)
			{
				// Evaluate the constant preshader, and write out its value
				FMaterialPreshaderData Preshader;
				Expression->EmitPreshader(*this, Preshader);

				FMaterialRenderContext RenderContext(nullptr, *Material, nullptr);
				FLinearColor ConstantValue;
				Preshader.Evaluate(nullptr, RenderContext, ConstantValue);

				const FConstant Constant(Expression->Type, ConstantValue);
				Constant.EmitHLSL(LocalWriter);
			}
			else
			{
				// Non-constant preshader, store it on the material
				check(Expression->EvaluationType == EExpressionEvaluationType::Preshader);
				FUniformExpressionSet& UniformExpressionSet = MaterialCompilationOutput->UniformExpressionSet;

				FMaterialUniformPreshaderHeader* PreshaderHeader = nullptr;
				if (Expression->Type == EExpressionType::Float1)
				{
					const static TCHAR IndexToMask[] = { 'x', 'y', 'z', 'w' };
					const int32 ScalarInputIndex = UniformExpressionSet.UniformScalarPreshaders.Num();
					LocalWriter.Writef(TEXT("Material.ScalarExpressions[%u].%c"), ScalarInputIndex / 4, IndexToMask[ScalarInputIndex % 4]);

					PreshaderHeader = &UniformExpressionSet.UniformScalarPreshaders.AddDefaulted_GetRef();
				}
				else
				{
					const TCHAR* Mask = TEXT("");
					switch (Expression->Type)
					{
					case EExpressionType::Float1: Mask = TEXT(".r"); break;
					case EExpressionType::Float2: Mask = TEXT(".rg"); break;
					case EExpressionType::Float3: Mask = TEXT(".rgb"); break;
					case EExpressionType::Float4: break;
					default: checkNoEntry(); break;
					};
					const int32 VectorInputIndex = UniformExpressionSet.UniformVectorPreshaders.Num();
					LocalWriter.Writef(TEXT("Material.VectorExpressions[%u]%s"), VectorInputIndex, Mask);

					PreshaderHeader = &UniformExpressionSet.UniformVectorPreshaders.AddDefaulted_GetRef();
				}
				PreshaderHeader->OpcodeOffset = UniformExpressionSet.UniformPreshaderData.Num();
				Expression->EmitPreshader(*this, UniformExpressionSet.UniformPreshaderData);
				PreshaderHeader->OpcodeSize = UniformExpressionSet.UniformPreshaderData.Num() - PreshaderHeader->OpcodeOffset;
			}
			bInline = true;
		}

		if (bInline)
		{
			Entry.Definition = AllocateString(*Allocator, LocalWriter.GetStringBuilder());
		}
		else
		{
			FScopeEntry* ScopeEntry = FindScope(Expression->ParentScope);
			check(ScopeEntry);

			const FExpressionTypeDescription& TypeDesc = GetExpressionTypeDescription(Expression->Type);
			Entry.Definition = AllocateStringf(*Allocator, TEXT("Local%d"), NumExpressionLocals++);
			ScopeEntry->ExpressionCodeWriter->WriteLinef(TEXT("%s %s = %s;"),
				TypeDesc.Name,
				Entry.Definition,
				LocalWriter.GetStringBuilder().ToString());
		}
	}

	return Entry.Definition;
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

void UE::HLSLTree::FExpression::EmitHLSL(UE::HLSLTree::FEmitContext& Context, UE::HLSLTree::FCodeWriter& Writer) const
{
	check(false);
}

void UE::HLSLTree::FExpression::EmitPreshader(UE::HLSLTree::FEmitContext& Context, FMaterialPreshaderData& OutPreshader) const
{
	check(false);
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

void UE::HLSLTree::FScope::EmitHLSL(UE::HLSLTree::FEmitContext& Context, UE::HLSLTree::FCodeWriter& Writer) const
{
	const int32 ScopeIndentLevel = Writer.IndentLevel + 1;

	FLocalHLSLCodeWriter DeclarationCodeWriter;
	DeclarationCodeWriter.IndentLevel = ScopeIndentLevel;

	FEmitContext::FScopeEntry& ScopeEntry = Context.ScopeStack.AddDefaulted_GetRef();
	ScopeEntry.Scope = this;
	ScopeEntry.ExpressionCodeWriter = &DeclarationCodeWriter;

	Writer.WriteLine(TEXT("{"));

	FLocalHLSLCodeWriter StatementCodeWriter;
	StatementCodeWriter.IndentLevel = ScopeIndentLevel;
	const FStatement* Statement = FirstStatement;
	while (Statement)
	{
		Statement->EmitHLSL(Context, StatementCodeWriter);

		// First write any expressions needed by the statement, then the statement itself
		if (DeclarationCodeWriter.StringBuilder->Len() > 0)
		{
			Writer.StringBuilder->Append(DeclarationCodeWriter.StringBuilder->GetData(), DeclarationCodeWriter.StringBuilder->Len());
		}
		Writer.StringBuilder->Append(StatementCodeWriter.StringBuilder->GetData(), StatementCodeWriter.StringBuilder->Len());

		DeclarationCodeWriter.StringBuilder->Reset();
		StatementCodeWriter.StringBuilder->Reset();

		Statement = Statement->NextStatement;
	}

	Writer.WriteLine(TEXT("}"));

	Context.ScopeStack.Pop(false);
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

bool UE::HLSLTree::FScope::TryMoveStatement(UE::HLSLTree::FStatement* Statement)
{
	bool bResult = false;
	FScope* PrevScope = Statement->ParentScope;
	if (!PrevScope)
	{
		// Statement does not currently belong to any scope, just add it
		AddStatement(Statement);
		bResult = true;
	}
	else if (LinkedScope && ParentScope == PrevScope)
	{
		// Already moved
		bResult = true;
	}
	else if(LinkedScope == PrevScope)
	{
		// Statement belongs to our linked scope, so move it into our parent scope
		check(ParentScope->FirstStatement);
		check(ParentScope->LastStatement);

		// First need to unlink the statement from its previous scope
		FStatement* PrevStatement = PrevScope->FirstStatement;
		bool bFoundInPrevScope = false;
		while (PrevStatement)
		{
			FStatement* NextStatement = PrevStatement->NextStatement;
			if (NextStatement == Statement)
			{
				PrevScope->LastStatement = PrevStatement;
				PrevStatement->NextStatement = nullptr;
				bFoundInPrevScope = true;
				break;
			}
			PrevStatement = NextStatement;
		}
		check(bFoundInPrevScope);

		// Now link it into the new scope
		FNodeVisitor_MoveToScope Visitor(ParentScope);
		ParentScope->LastStatement->NextStatement = Statement;
		FStatement* StatementToLink = Statement;
		while (StatementToLink)
		{
			StatementToLink->ParentScope = ParentScope;
			ParentScope->LastStatement = StatementToLink;
			Visitor.VisitNode(StatementToLink);
			StatementToLink = StatementToLink->NextStatement;
		}
		bResult = true;
	}
	return bResult;
}

UE::HLSLTree::FTree* UE::HLSLTree::FTree::Create(FMemStackBase& Allocator)
{
	FTree* Tree = new(Allocator) FTree();
	Tree->Allocator = &Allocator;
	Tree->RootScope = Tree->NewNode<FScope>();
	return Tree;
}

void UE::HLSLTree::FTree::EmitHLSL(UE::HLSLTree::FEmitContext& Context, UE::HLSLTree::FCodeWriter& Writer) const
{
	RootScope->EmitHLSL(Context, Writer);
}

UE::HLSLTree::FScope* UE::HLSLTree::FTree::NewScope(UE::HLSLTree::FScope& Scope)
{
	FScope* NewScope = NewNode<FScope>();
	NewScope->ParentScope = &Scope;
	NewScope->NestedLevel = Scope.NestedLevel + 1;
	return NewScope;
}

UE::HLSLTree::FScope* UE::HLSLTree::FTree::NewLinkedScope(UE::HLSLTree::FScope& Scope)
{
	FScope* NewScope = NewNode<FScope>();
	NewScope->ParentScope = Scope.ParentScope;
	NewScope->LinkedScope = &Scope;
	NewScope->NestedLevel = Scope.NestedLevel;
	return NewScope;
}

UE::HLSLTree::FLocalDeclaration* UE::HLSLTree::FTree::NewLocalDeclaration(UE::HLSLTree::FScope& Scope, UE::HLSLTree::EExpressionType Type, const FName& Name)
{
	FLocalDeclaration* Declaration = NewNode<FLocalDeclaration>(Name, Type);
	Scope.AddDeclaration(Declaration);
	return Declaration;
}

UE::HLSLTree::FParameterDeclaration* UE::HLSLTree::FTree::NewParameterDeclaration(UE::HLSLTree::FScope& Scope, const FName& Name, const UE::HLSLTree::FConstant& DefaultValue)
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
