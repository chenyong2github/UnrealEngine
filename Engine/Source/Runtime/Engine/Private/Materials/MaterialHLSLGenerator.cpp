// Copyright Epic Games, Inc. All Rights Reserved.
#include "MaterialHLSLGenerator.h"

#if WITH_EDITOR

#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionVolumetricAdvancedMaterialOutput.h"
#include "Materials/Material.h"
#include "ShaderCore.h"
#include "HLSLTree/HLSLTree.h"
#include "HLSLTree/HLSLTreeCommon.h"
#include "Containers/LazyPrintf.h"

static UE::Shader::EValueType GetShaderType(EMaterialValueType MaterialType)
{
	switch (MaterialType)
	{
	case MCT_Float1: return UE::Shader::EValueType::Float1;
	case MCT_Float2: return UE::Shader::EValueType::Float2;
	case MCT_Float3: return UE::Shader::EValueType::Float3;
	case MCT_Float4: return UE::Shader::EValueType::Float4;
	case MCT_Float: return UE::Shader::EValueType::Float1;
	case MCT_StaticBool: return UE::Shader::EValueType::Bool1;
	case MCT_MaterialAttributes: return UE::Shader::EValueType::Struct;
	case MCT_ShadingModel: return UE::Shader::EValueType::Int1;
	case MCT_LWCScalar: return UE::Shader::EValueType::Double1;
	case MCT_LWCVector2: return UE::Shader::EValueType::Double2;
	case MCT_LWCVector3: return UE::Shader::EValueType::Double3;
	case MCT_LWCVector4: return UE::Shader::EValueType::Double4;
	default:return UE::Shader::EValueType::Void;
	}
}

FMaterialHLSLGenerator::FMaterialHLSLGenerator(UMaterial* InTargetMaterial,
	const FMaterialCompileTargetParameters& InCompileTarget,
	UE::Shader::FStructTypeRegistry& InOutTypeRegistry,
	UE::HLSLTree::FTree& InOutTree)
	: CompileTarget(InCompileTarget)
	, TargetMaterial(InTargetMaterial)
	, HLSLTree(&InOutTree)
	, TypeRegistry(&InOutTypeRegistry)
	, bGeneratedResult(false)
{
	const EMaterialShadingModel DefaultShadingModel = InTargetMaterial->GetShadingModels().GetFirstShadingModel();

	FFunctionCallEntry* RootFunctionEntry = new(InOutTree.GetAllocator()) FFunctionCallEntry();
	FunctionCallStack.Add(RootFunctionEntry);

	TArray<UE::Shader::FStructFieldInitializer, TInlineAllocator<MP_MAX>> MaterialAttributeFields;

	const TArray<FGuid>& OrderedVisibleAttributes = FMaterialAttributeDefinitionMap::GetOrderedVisibleAttributeList();
	for (const FGuid& AttributeID : OrderedVisibleAttributes)
	{
		const FString& PropertyName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
		const EMaterialValueType PropertyType = FMaterialAttributeDefinitionMap::GetValueType(AttributeID);
		const UE::Shader::EValueType ValueType = GetShaderType(PropertyType);
		
		if (ValueType != UE::Shader::EValueType::Void &&
			ValueType != UE::Shader::EValueType::Struct)
		{
			MaterialAttributeFields.Emplace(PropertyName, ValueType);

			if (PropertyType == MCT_ShadingModel)
			{
				check(ValueType == UE::Shader::EValueType::Int1);
				MaterialAttributesDefaultValue.Component.Add((int32)DefaultShadingModel);
			}
			else
			{
				const UE::Shader::FValue DefaultValue = UE::Shader::Cast(FMaterialAttributeDefinitionMap::GetDefaultValue(AttributeID), ValueType);
				MaterialAttributesDefaultValue.Component.Append(DefaultValue.Component);
			}
		}
	}

	UE::Shader::FStructTypeInitializer MaterialAttributesInitializer;
	MaterialAttributesInitializer.Name = TEXT("FMaterialAttributes");
	MaterialAttributesInitializer.Fields = MaterialAttributeFields;
	MaterialAttributesType = InOutTypeRegistry.NewType(MaterialAttributesInitializer);

	check(MaterialAttributesDefaultValue.Component.Num() == MaterialAttributesType->ComponentTypes.Num());
	MaterialAttributesDefaultValue.Type = MaterialAttributesType;
}

void FMaterialHLSLGenerator::AcquireErrors(FMaterial& OutMaterial)
{
	OutMaterial.CompileErrors = MoveTemp(CompileErrors);
	OutMaterial.ErrorExpressions = MoveTemp(ErrorExpressions);
}

bool FMaterialHLSLGenerator::Finalize()
{
	check(FunctionCallStack.Num() == 1);

	if (!bGeneratedResult)
	{
		Error(TEXT("Missing connection to material output"));
		return false;
	}

	if (!ResultExpression || !ResultStatement)
	{
		Error(TEXT("Failed to initialize result"));
		return false;
	}

	for (const auto& It : StatementMap)
	{
		const UMaterialExpression* Expression = It.Key;
		const FStatementEntry& Entry = It.Value;
		if (Entry.NumInputs != Expression->NumExecutionInputs)
		{
			Error(TEXT("Invalid number of input connections"));
			return false;
		}
	}

	if (JoinedScopeStack.Num() != 0)
	{
		Error(TEXT("Invalid control flow"));
		return false;
	}

	return HLSLTree->Finalize();
}

EMaterialGenerateHLSLStatus FMaterialHLSLGenerator::Error(const FString& Message)
{
	UMaterialExpression* ExpressionToError = nullptr;
	FString ErrorString;

	if (ExpressionStack.Num() > 0)
	{
		UMaterialExpression* ErrorExpression = ExpressionStack.Last();
		check(ErrorExpression);

		if (ErrorExpression->GetClass() != UMaterialExpressionMaterialFunctionCall::StaticClass()
			&& ErrorExpression->GetClass() != UMaterialExpressionFunctionInput::StaticClass()
			&& ErrorExpression->GetClass() != UMaterialExpressionFunctionOutput::StaticClass())
		{
			// Add the expression currently being compiled to ErrorExpressions so we can draw it differently
			ExpressionToError = ErrorExpression;

			const int32 ChopCount = FCString::Strlen(TEXT("MaterialExpression"));
			const FString ErrorClassName = ErrorExpression->GetClass()->GetName();

			// Add the node type to the error message
			ErrorString += FString(TEXT("(Node ")) + ErrorClassName.Right(ErrorClassName.Len() - ChopCount) + TEXT(") ");
		}
	}

	ErrorString += Message;

	// Standard error handling, immediately append one-off errors and signal failure
	CompileErrors.AddUnique(ErrorString);

	if (ExpressionToError)
	{
		ErrorExpressions.Add(ExpressionToError);
		ExpressionToError->LastErrorText = Message;
	}

	return EMaterialGenerateHLSLStatus::Error;
}

static UE::HLSLTree::FExpression* CompileMaterialInput(FMaterialHLSLGenerator& Generator,
	UE::HLSLTree::FScope& Scope,
	EMaterialProperty InputProperty,
	UMaterial* Material,
	UE::HLSLTree::FExpression* AttributesExpression)
{
#if 0
	UE::HLSLTree::FExpression* Expression = nullptr;
	if (Material->IsPropertyActive(InputProperty))
	{
		FMaterialInputDescription InputDescription;
		if (Material->GetExpressionInputDescription(InputProperty, InputDescription))
		{
			if (InputDescription.bUseConstant)
			{
				UE::Shader::FValue DefaultValue = FMaterialAttributeDefinitionMap::GetDefaultValue(InputProperty);
				DefaultValue.NumComponents = UE::Shader::GetValueTypeDescription(InputDescription.Type).NumComponents;
				if (InputDescription.ConstantValue != DefaultValue)
				{
					Expression = Generator.NewConstant(InputDescription.ConstantValue);
				}
			}
			else
			{
				check(InputDescription.Input);
				if (InputProperty >= MP_CustomizedUVs0 && InputProperty <= MP_CustomizedUVs7)
				{
					const int32 TexCoordIndex = (int32)InputProperty - MP_CustomizedUVs0;
					if (TexCoordIndex < Material->NumCustomizedUVs)
					{
						Expression = InputDescription.Input->AcquireHLSLExpression(Generator, Scope);
					}
					if (!Expression)
					{
						Expression = Generator.NewTexCoord(Scope, TexCoordIndex);
					}
				}
				else
				{
					Expression = InputDescription.Input->AcquireHLSLExpression(Generator, Scope);
				}
			}
		}
	}

	if (Expression)
	{
		UE::HLSLTree::FExpressionSetMaterialAttribute* SetAttributeExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSetMaterialAttribute>(Scope);
		SetAttributeExpression->AttributeID = FMaterialAttributeDefinitionMap::GetID(InputProperty);
		SetAttributeExpression->AttributesExpression = AttributesExpression;
		SetAttributeExpression->ValueExpression = Expression;
		return SetAttributeExpression;
	}
#endif
	return AttributesExpression;
}

bool FMaterialHLSLGenerator::GenerateResult(UE::HLSLTree::FScope& Scope)
{
	bool bResult = false;
	if (bGeneratedResult)
	{
		Error(TEXT("Multiple connections to execution output"));
	}
	else
	{
		check(!ResultStatement);
		check(!ResultExpression);

		if (TargetMaterial)
		{
			UE::HLSLTree::FExpression* AttributesExpression = nullptr;
			if (TargetMaterial->bUseMaterialAttributes)
			{
				FMaterialInputDescription InputDescription;
				if (TargetMaterial->GetExpressionInputDescription(MP_MaterialAttributes, InputDescription))
				{
					check(InputDescription.Type == UE::Shader::EValueType::Struct);
					AttributesExpression = InputDescription.Input->AcquireHLSLExpression(*this, Scope);
				}
			}
			else
			{
				AttributesExpression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionConstant>(MaterialAttributesDefaultValue);
				for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
				{
					const EMaterialProperty Property = (EMaterialProperty)PropertyIndex;
					AttributesExpression = CompileMaterialInput(*this, Scope, Property, TargetMaterial, AttributesExpression);
				}
			}

			if (AttributesExpression)
			{
				UE::HLSLTree::FStatementReturn* ReturnStatement = HLSLTree->NewStatement<UE::HLSLTree::FStatementReturn>(Scope);
				ReturnStatement->Expression = AttributesExpression;
				ResultExpression = AttributesExpression;
				ResultStatement = ReturnStatement;
				bResult = true;
			}
		}
		else
		{
			check(false);
		}
		bGeneratedResult = true;
	}
	return bResult;
}

UE::HLSLTree::FScope* FMaterialHLSLGenerator::NewScope(UE::HLSLTree::FScope& Scope, EMaterialNewScopeFlag Flags)
{
	UE::HLSLTree::FScope* NewScope = HLSLTree->NewScope(Scope);
	if (!EnumHasAllFlags(Flags, EMaterialNewScopeFlag::NoPreviousScope))
	{
		NewScope->AddPreviousScope(Scope);
	}

	return NewScope;
}

UE::HLSLTree::FScope* FMaterialHLSLGenerator::NewOwnedScope(UE::HLSLTree::FStatement& Owner)
{
	UE::HLSLTree::FScope* NewScope = HLSLTree->NewOwnedScope(Owner);
	NewScope->AddPreviousScope(*Owner.ParentScope);
	return NewScope;
}

UE::HLSLTree::FScope* FMaterialHLSLGenerator::NewJoinedScope(UE::HLSLTree::FScope& Scope)
{
	UE::HLSLTree::FScope* NewScope = HLSLTree->NewScope(Scope);
	JoinedScopeStack.Add(NewScope);
	return NewScope;
}

UE::HLSLTree::FExpressionConstant* FMaterialHLSLGenerator::NewConstant(const UE::Shader::FValue& Value)
{
	UE::HLSLTree::FExpressionConstant* Expression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionConstant>(Value);
	return Expression;
}

UE::HLSLTree::FExpressionExternalInput* FMaterialHLSLGenerator::NewTexCoord(int32 Index)
{
	UE::HLSLTree::FExpressionExternalInput* Expression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionExternalInput>(UE::HLSLTree::MakeInputTexCoord(Index));
	return Expression;
}

UE::HLSLTree::FExpressionSwizzle* FMaterialHLSLGenerator::NewSwizzle(const UE::HLSLTree::FSwizzleParameters& Params, UE::HLSLTree::FExpression* Input)
{
	UE::HLSLTree::FExpressionSwizzle* Expression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionSwizzle>(Params, Input);
	return Expression;
}

UE::HLSLTree::FTextureParameterDeclaration* FMaterialHLSLGenerator::AcquireTextureDeclaration(const UE::HLSLTree::FTextureDescription& Value)
{
	FString SamplerTypeError;
	if (!UMaterialExpressionTextureBase::VerifySamplerType(CompileTarget.FeatureLevel, CompileTarget.TargetPlatform, Value.Texture, Value.SamplerType, SamplerTypeError))
	{
		Errorf(TEXT("%s"), *SamplerTypeError);
		return nullptr;
	}

	UE::HLSLTree::FTextureParameterDeclaration*& Declaration = TextureDeclarationMap.FindOrAdd(Value);
	if (!Declaration)
	{
		Declaration = HLSLTree->NewTextureParameterDeclaration(FName(), Value);
	}
	return Declaration;
}

UE::HLSLTree::FTextureParameterDeclaration* FMaterialHLSLGenerator::AcquireTextureParameterDeclaration(const FName& Name, const UE::HLSLTree::FTextureDescription& DefaultValue)
{
	FString SamplerTypeError;
	if (!UMaterialExpressionTextureBase::VerifySamplerType(CompileTarget.FeatureLevel, CompileTarget.TargetPlatform, DefaultValue.Texture, DefaultValue.SamplerType, SamplerTypeError))
	{
		Errorf(TEXT("%s"), *SamplerTypeError);
		return nullptr;
	}

	UE::HLSLTree::FTextureParameterDeclaration*& Declaration = TextureParameterDeclarationMap.FindOrAdd(Name);
	if (!Declaration)
	{
		Declaration = HLSLTree->NewTextureParameterDeclaration(Name, DefaultValue);
	}
	return Declaration;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::AcquireExpression(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex)
{
	UE::HLSLTree::FExpression* Expression = nullptr;

	ExpressionStack.Add(MaterialExpression);
	const EMaterialGenerateHLSLStatus Status = MaterialExpression->GenerateHLSLExpression(*this, Scope, OutputIndex, Expression);
	verify(ExpressionStack.Pop() == MaterialExpression);

	return Expression;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::AcquireFunctionInputExpression(UE::HLSLTree::FScope& Scope, const UMaterialExpressionFunctionInput* MaterialExpression)
{
	const FFunctionCallEntry* FunctionEntry = FunctionCallStack.Last();
	const FFunctionInput* ConnectedInput = nullptr;
	for (const FFunctionInput& Input : FunctionEntry->Inputs)
	{
		if (Input.FunctionInputExpression == MaterialExpression)
		{
			ConnectedInput = &Input;
			break;
		}
	}

	if (!ConnectedInput)
	{
		return nullptr;
	}

	return AcquireExpression(Scope, ConnectedInput->ConnectedExpression, ConnectedInput->ConnectedOutputIndex);
}

UE::HLSLTree::FTextureParameterDeclaration* FMaterialHLSLGenerator::AcquireTextureDeclaration(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex)
{
	// No need to cache at this level, TextureDeclarations are cached at a lower level, as they're generated by UMaterialExpression
	UE::HLSLTree::FTextureParameterDeclaration* TextureDeclaration = nullptr;
	const EMaterialGenerateHLSLStatus Status = MaterialExpression->GenerateHLSLTexture(*this, Scope, OutputIndex, TextureDeclaration);
	return TextureDeclaration;
}

bool FMaterialHLSLGenerator::GenerateStatements(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression)
{
	FStatementEntry& Entry = StatementMap.FindOrAdd(MaterialExpression);
	check(Entry.NumInputs >= 0);
	check(Entry.NumInputs < MaterialExpression->NumExecutionInputs);
	if (Entry.NumInputs == MaxNumPreviousScopes)
	{
		Error(TEXT("Bad control flow"));
		return false;
	}

	Entry.PreviousScope[Entry.NumInputs++] = &Scope;

	bool bResult = true;
	if (Entry.NumInputs == MaterialExpression->NumExecutionInputs)
	{
		UE::HLSLTree::FScope* ScopeToUse = &Scope;
		if (MaterialExpression->NumExecutionInputs > 1u)
		{
			if (JoinedScopeStack.Num() == 0)
			{
				Error(TEXT("Bad control flow"));
				return false;
			}

			ScopeToUse = JoinedScopeStack.Pop(false);
			for (int32 i = 0; i < Entry.NumInputs; ++i)
			{
				ScopeToUse->AddPreviousScope(*Entry.PreviousScope[i]);
			}
		}

		ExpressionStack.Add(MaterialExpression);
		const EMaterialGenerateHLSLStatus Status = MaterialExpression->GenerateHLSLStatements(*this, *ScopeToUse);
		verify(ExpressionStack.Pop() == MaterialExpression);
	}

	return bResult;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::GenerateFunctionCall(UE::HLSLTree::FScope& Scope, UMaterialFunctionInterface* Function, TArrayView<const FFunctionExpressionInput> ConnectedInputs, int32 OutputIndex)
{
	using namespace UE::HLSLTree;

	if (!Function)
	{
		Error(TEXT("Missing material function"));
		return nullptr;
	}

	TArray<FFunctionExpressionInput> FunctionInputs;
	TArray<FFunctionExpressionOutput> FunctionOutputs;
	Function->GetInputsAndOutputs(FunctionInputs, FunctionOutputs);

	if (FunctionInputs.Num() != ConnectedInputs.Num())
	{
		Error(TEXT("Mismatched function inputs"));
		return nullptr;
	}

	const UMaterialExpressionFunctionOutput* ExpressionOutput = FunctionOutputs.IsValidIndex(OutputIndex) ? FunctionOutputs[OutputIndex].ExpressionOutput.Get() : nullptr;
	if (!ExpressionOutput)
	{
		Error(TEXT("Invalid function output"));
		return nullptr;
	}

	FSHA1 Hasher;
	Hasher.Update((uint8*)&Function, sizeof(UMaterialFunctionInterface*));

	FFunctionInputArray Inputs;
	for (int32 InputIndex = 0; InputIndex < ConnectedInputs.Num(); ++InputIndex)
	{
		// FunctionInputs are the inputs from the UMaterialFunction object
		// ConnectedInputs are the inputs from the UMaterialFunctionCall object
		// We want to connect the UMaterialExpressionFunctionInput from the UMaterialFunction to whatever UMaterialExpression is passed to the UMaterialFunctionCall
		const FFunctionExpressionInput& FunctionInput = FunctionInputs[InputIndex];
		const FFunctionExpressionInput& ConnectedInput = ConnectedInputs[InputIndex];

		FFunctionInput& Input = Inputs.AddDefaulted_GetRef();
		Input.FunctionInputExpression = FunctionInput.ExpressionInput;
		Input.ConnectedExpression = ConnectedInput.Input.Expression;
		Input.ConnectedOutputIndex = ConnectedInput.Input.OutputIndex;

		Hasher.Update((uint8*)&Input.ConnectedExpression, sizeof(Input.ConnectedExpression));
		Hasher.Update((uint8*)&Input.ConnectedOutputIndex, sizeof(Input.ConnectedOutputIndex));
	}

	const FSHAHash Hash = Hasher.Finalize();

	FFunctionCallEntry** ExistingFunctionCall = FunctionCallMap.Find(Hash);
	FFunctionCallEntry* FunctionCall = ExistingFunctionCall ? *ExistingFunctionCall : nullptr;
	if (!FunctionCall)
	{
		FunctionCall = new(HLSLTree->GetAllocator()) FFunctionCallEntry();
		FunctionCall->Function = Function;
		FunctionCall->Inputs = MoveTemp(Inputs);
		FunctionCallMap.Add(Hash, FunctionCall);
	}

	FunctionCallStack.Add(FunctionCall);
	UE::HLSLTree::FExpression* Result = ExpressionOutput->A.AcquireHLSLExpression(*this, Scope);
	verify(FunctionCallStack.Pop() == FunctionCall);

	return Result;
}

void FMaterialHLSLGenerator::InternalRegisterExpressionData(const FName& Type, const UMaterialExpression* MaterialExpression, void* Data)
{
	const FExpressionDataKey Key(Type, MaterialExpression);
	check(!ExpressionDataMap.Contains(Key));
	ExpressionDataMap.Add(Key, Data);
}

void* FMaterialHLSLGenerator::InternalFindExpressionData(const FName& Type, const UMaterialExpression* MaterialExpression)
{
	const FExpressionDataKey Key(Type, MaterialExpression);
	void** Result = ExpressionDataMap.Find(Key);
	return Result ? *Result : nullptr;
}

#endif // WITH_EDITOR
