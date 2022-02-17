// Copyright Epic Games, Inc. All Rights Reserved.
#include "MaterialHLSLGenerator.h"

#if WITH_EDITOR

#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionVolumetricAdvancedMaterialOutput.h"
#include "Materials/MaterialExpressionExecBegin.h"
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

FMaterialHLSLErrorHandler::FMaterialHLSLErrorHandler(FMaterial& InOutMaterial)
	: Material(&InOutMaterial)
{
	Material->CompileErrors.Reset();
	Material->ErrorExpressions.Reset();
}

void FMaterialHLSLErrorHandler::AddErrorInternal(UObject* InOwner, FStringView InError)
{
	UMaterialExpression* MaterialExpressionOwner = Cast<UMaterialExpression>(InOwner);
	UMaterialExpression* ExpressionToError = nullptr;
	TStringBuilder<1024> FormattedError;

	if (MaterialExpressionOwner)
	{
		if (MaterialExpressionOwner->GetClass() != UMaterialExpressionMaterialFunctionCall::StaticClass()
			&& MaterialExpressionOwner->GetClass() != UMaterialExpressionFunctionInput::StaticClass()
			&& MaterialExpressionOwner->GetClass() != UMaterialExpressionFunctionOutput::StaticClass())
		{
			// Add the expression currently being compiled to ErrorExpressions so we can draw it differently
			ExpressionToError = MaterialExpressionOwner;

			const int32 ChopCount = FCString::Strlen(TEXT("MaterialExpression"));
			const FString ErrorClassName = MaterialExpressionOwner->GetClass()->GetName();

			// Add the node type to the error message
			FormattedError.Appendf(TEXT("(Node %s) "), *ErrorClassName.Right(ErrorClassName.Len() - ChopCount));
		}
	}

	FormattedError.Append(InError);
	const FString Error(FormattedError.ToView());

	// Standard error handling, immediately append one-off errors and signal failure
	Material->CompileErrors.AddUnique(Error);

	if (ExpressionToError)
	{
		Material->ErrorExpressions.Add(ExpressionToError);
		ExpressionToError->LastErrorText = Error;
	}
}

FMaterialHLSLGenerator::FMaterialHLSLGenerator(const FMaterialCompileTargetParameters& InCompileTarget,
	FMaterial& InOutMaterial,
	UE::Shader::FStructTypeRegistry& InOutTypeRegistry,
	UE::HLSLTree::FTree& InOutTree)
	: CompileTarget(InCompileTarget)
	, Errors(InOutMaterial)
	, HLSLTree(&InOutTree)
	, TypeRegistry(&InOutTypeRegistry)
	, bGeneratedResult(false)
{
	UMaterialInterface* MaterialInterface = InOutMaterial.GetMaterialInterface();
	TargetMaterial = MaterialInterface->GetMaterial();
	const EMaterialShadingModel DefaultShadingModel = TargetMaterial->GetShadingModels().GetFirstShadingModel();

	FFunctionCallEntry* RootFunctionEntry = new(InOutTree.GetAllocator()) FFunctionCallEntry();
	FunctionCallStack.Add(RootFunctionEntry);

	TArray<UE::Shader::FStructFieldInitializer, TInlineAllocator<MP_MAX + 16>> MaterialAttributeFields;

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

	MaterialAttributeFields.Emplace(TEXT("PrevWorldPositionOffset"), UE::Shader::EValueType::Float3);
	MaterialAttributesDefaultValue.Component.Append({ 0.0f, 0.0f, 0.0f });

	UE::Shader::FStructTypeInitializer MaterialAttributesInitializer;
	MaterialAttributesInitializer.Name = TEXT("FMaterialAttributes");
	MaterialAttributesInitializer.Fields = MaterialAttributeFields;
	MaterialAttributesType = InOutTypeRegistry.NewType(MaterialAttributesInitializer);

	check(MaterialAttributesDefaultValue.Component.Num() == MaterialAttributesType->ComponentTypes.Num());
	MaterialAttributesDefaultValue.Type = MaterialAttributesType;
}

bool FMaterialHLSLGenerator::Generate()
{
	using namespace UE::HLSLTree;

	FScope& RootScope = HLSLTree->GetRootScope();

	bool bResult = false;
	if (TargetMaterial->IsUsingControlFlow())
	{
		UMaterialExpression* BaseExpression = TargetMaterial->ExpressionExecBegin;
		if (!BaseExpression)
		{
			bResult = Errors.AddError(TEXT("Missing ExpressionExecBegin"));
		}
		else
		{
			bResult = GenerateStatements(RootScope, BaseExpression);
		}
	}
	else
	{
		bResult = GenerateResult(RootScope);
	}

	if (!bResult)
	{
		return false;
	}

	check(FunctionCallStack.Num() == 1);
	if (!bGeneratedResult)
	{
		return Errors.AddError(TEXT("Missing connection to material output"));
	}

	if (!ResultExpression || !ResultStatement)
	{
		return Errors.AddError(TEXT("Failed to initialize result"));
	}

	for (const auto& It : StatementMap)
	{
		const UMaterialExpression* Expression = It.Key;
		const FStatementEntry& Entry = It.Value;
		if (Entry.NumInputs != Expression->NumExecutionInputs)
		{
			return Errors.AddError(TEXT("Invalid number of input connections"));
		}
	}

	if (JoinedScopeStack.Num() != 0)
	{
		return Errors.AddError(TEXT("Invalid control flow"));
	}

	return HLSLTree->Finalize();
}

static UE::HLSLTree::FExpression* CompileMaterialInput(FMaterialHLSLGenerator& Generator,
	UE::HLSLTree::FScope& Scope,
	EMaterialProperty InputProperty,
	UMaterial* Material,
	UE::HLSLTree::FExpression* AttributesExpression)
{
	using namespace UE::HLSLTree;

	// We're only interesting in attributes that map to valid fields
	const UE::Shader::FStructField* AttributeField = Generator.GetMaterialAttributesType()->FindFieldByName(*FMaterialAttributeDefinitionMap::GetAttributeName(InputProperty));

	FExpression* Expression = nullptr;
	if (AttributeField && Material->IsPropertyActive(InputProperty))
	{
		FMaterialInputDescription InputDescription;
		if (Material->GetExpressionInputDescription(InputProperty, InputDescription))
		{
			if (InputDescription.bUseConstant)
			{
				UE::Shader::FValue DefaultValue = UE::Shader::Cast(FMaterialAttributeDefinitionMap::GetDefaultValue(InputProperty), InputDescription.Type);
				if (InputDescription.ConstantValue != DefaultValue)
				{
					Expression = Generator.NewConstant(InputDescription.ConstantValue);
				}
			}
			else
			{
				check(InputDescription.Input);
				Expression = InputDescription.Input->TryAcquireHLSLExpression(Generator, Scope);
			}
		}
	}

	if (Expression)
	{
		FExpression* SetAttributeExpression = Generator.GetTree().NewExpression<FExpressionSetStructField>(
			Generator.GetMaterialAttributesType(),
			AttributeField,
			AttributesExpression,
			Expression);
		return SetAttributeExpression;
	}

	return AttributesExpression;
}

bool FMaterialHLSLGenerator::GenerateResult(UE::HLSLTree::FScope& Scope)
{
	using namespace UE::HLSLTree;
	using namespace UE::Shader;

	FFunctionCallEntry* FunctionEntry = FunctionCallStack.Last();

	bool bResult = false;
	if (FunctionEntry->MaterialFunction)
	{
		// Result for function call
		FFunction* HLSLFunction = FunctionEntry->HLSLFunction;
		HLSLFunction->OutputExpressions.Reserve(FunctionEntry->FunctionOutputs.Num());
		for (const UMaterialExpressionFunctionOutput* ExpressionOutput : FunctionEntry->FunctionOutputs)
		{
			HLSLFunction->OutputExpressions.Add(ExpressionOutput->A.TryAcquireHLSLExpression(*this, Scope));
		}
		FunctionEntry->bGeneratedResult = true;
		bResult = true;
	}
	else if (bGeneratedResult)
	{
		return Errors.AddError(TEXT("Multiple connections to execution output"));
	}
	else
	{
		check(!ResultStatement);
		check(!ResultExpression);

		if (TargetMaterial)
		{
			FExpression* AttributesExpression = nullptr;
			if (TargetMaterial->bUseMaterialAttributes)
			{
				FMaterialInputDescription InputDescription;
				if (TargetMaterial->GetExpressionInputDescription(MP_MaterialAttributes, InputDescription))
				{
					check(InputDescription.Type == UE::Shader::EValueType::Struct);
					AttributesExpression = InputDescription.Input->AcquireHLSLExpression(*this, Scope);

					const FString& WPOName = FMaterialAttributeDefinitionMap::GetAttributeName(MP_WorldPositionOffset);
					const FStructField* WPOField = GetMaterialAttributesType()->FindFieldByName(*WPOName);
					const FStructField* PrevWPOField = GetMaterialAttributesType()->FindFieldByName(TEXT("PrevWorldPositionOffset"));

					FRequestedType PrevRequestedType;
					PrevRequestedType.SetFieldRequested(WPOField);

					FExpression* PrevAttributesExpression = HLSLTree->GetPreviousFrame(AttributesExpression, PrevRequestedType);
					FExpression* PrevWPOExpression = HLSLTree->NewExpression<FExpressionGetStructField>(GetMaterialAttributesType(), WPOField, PrevAttributesExpression);
					AttributesExpression = HLSLTree->NewExpression<FExpressionSetStructField>(GetMaterialAttributesType(), PrevWPOField, AttributesExpression, PrevWPOExpression);
				}
			}
			else
			{
				AttributesExpression = HLSLTree->NewExpression<FExpressionConstant>(MaterialAttributesDefaultValue);
				for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
				{
					const EMaterialProperty Property = (EMaterialProperty)PropertyIndex;
					AttributesExpression = CompileMaterialInput(*this, Scope, Property, TargetMaterial, AttributesExpression);
				}
			}

			if (AttributesExpression)
			{
				FStatementReturn* ReturnStatement = HLSLTree->NewStatement<FStatementReturn>(Scope);
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
	NewScope->AddPreviousScope(Owner.GetParentScope());
	return NewScope;
}

UE::HLSLTree::FScope* FMaterialHLSLGenerator::NewJoinedScope(UE::HLSLTree::FScope& Scope)
{
	UE::HLSLTree::FScope* NewScope = HLSLTree->NewScope(Scope);
	JoinedScopeStack.Add(NewScope);
	return NewScope;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::NewConstant(const UE::Shader::FValue& Value)
{
	return HLSLTree->NewConstant(Value);
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::NewTexCoord(int32 Index)
{
	return HLSLTree->NewExpression<UE::HLSLTree::FExpressionExternalInput>(UE::HLSLTree::MakeInputTexCoord(Index));
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::NewSwizzle(const UE::HLSLTree::FSwizzleParameters& Params, UE::HLSLTree::FExpression* Input)
{
	return HLSLTree->NewExpression<UE::HLSLTree::FExpressionSwizzle>(Params, Input);
}

UE::HLSLTree::FTextureParameterDeclaration* FMaterialHLSLGenerator::AcquireTextureDeclaration(const UE::HLSLTree::FTextureDescription& Value)
{
	FString SamplerTypeError;
	if (!UMaterialExpressionTextureBase::VerifySamplerType(CompileTarget.FeatureLevel, CompileTarget.TargetPlatform, Value.Texture, Value.SamplerType, SamplerTypeError))
	{
		Errors.AddError(SamplerTypeError);
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
		Errors.AddError(SamplerTypeError);
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
	using namespace UE::HLSLTree;
	FOwnerScope TreeOwnerScope(GetTree(), MaterialExpression);
	FOwnerScope ErrorOwnerScope(Errors, MaterialExpression);

	FExpression* Expression = nullptr;
	if (MaterialExpression->GenerateHLSLExpression(*this, Scope, OutputIndex, Expression))
	{
		return Expression;
	}
	return nullptr;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::AcquireFunctionInputExpression(UE::HLSLTree::FScope& Scope, const UMaterialExpressionFunctionInput* MaterialExpression)
{
	using namespace UE::HLSLTree;
	const FFunctionCallEntry* FunctionEntry = FunctionCallStack.Last();
	FExpression* InputExpression = nullptr;
	if (FunctionEntry->MaterialFunction)
	{
		bool bFoundInput = false;
		for (int32 Index = 0; Index < FunctionEntry->FunctionInputs.Num(); ++Index)
		{
			if (FunctionEntry->FunctionInputs[Index] == MaterialExpression)
			{
				bFoundInput = true;
				InputExpression = FunctionEntry->ConnectedInputs[Index];
				break;
			}
		}

		if (!bFoundInput)
		{
			// Finding a connected input is always expected if we're in a function call
			Errors.AddError(TEXT("Invalid function input"));
			return nullptr;
		}
	}

	if (!InputExpression && (MaterialExpression->bUsePreviewValueAsDefault || !FunctionEntry->MaterialFunction))
	{
		// Either we're previewing the material function, or the input isn't connected and we're using preview as default value
		InputExpression = MaterialExpression->Preview.TryAcquireHLSLExpression(*this, Scope);
		if (!InputExpression)
		{
			const FVector4f PreviewValue(MaterialExpression->PreviewValue);
			UE::Shader::FValue DefaultValue;
			switch (MaterialExpression->InputType)
			{
			case FunctionInput_Scalar: DefaultValue = PreviewValue.X; break;
			case FunctionInput_Vector2: DefaultValue = FVector2f(PreviewValue.X, PreviewValue.Y); break;
			case FunctionInput_Vector3: DefaultValue = FVector3f(PreviewValue.X, PreviewValue.Y, PreviewValue.Z); break;
			case FunctionInput_Vector4: DefaultValue = PreviewValue; break;
			case FunctionInput_MaterialAttributes: DefaultValue = GetMaterialAttributesDefaultValue(); break;
			case FunctionInput_Texture2D:
			case FunctionInput_TextureCube:
			case FunctionInput_Texture2DArray:
			case FunctionInput_VolumeTexture:
			case FunctionInput_StaticBool:
			case FunctionInput_TextureExternal:
				Errors.AddErrorf(TEXT("Missing Preview connection for function input '%s'"), *MaterialExpression->InputName.ToString());
				return nullptr;
			default:
				Errors.AddError(TEXT("Unknown input type"));
				return nullptr;
			}

			InputExpression = NewConstant(DefaultValue);
		}
	}

	return InputExpression;
}

UE::HLSLTree::FTextureParameterDeclaration* FMaterialHLSLGenerator::AcquireTextureDeclaration(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex)
{
	using namespace UE::HLSLTree;
	FOwnerScope TreeOwnerScope(GetTree(), MaterialExpression);
	FOwnerScope ErrorOwnerScope(Errors, MaterialExpression);

	FTextureParameterDeclaration* TextureDeclaration = nullptr;
	if (MaterialExpression->GenerateHLSLTexture(*this, Scope, OutputIndex, TextureDeclaration))
	{
		return TextureDeclaration;
	}
	return nullptr;
}

bool FMaterialHLSLGenerator::GenerateStatements(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression)
{
	using namespace UE::HLSLTree;
	FOwnerScope ErrorOwnerScope(Errors, MaterialExpression);

	FStatementEntry& Entry = StatementMap.FindOrAdd(MaterialExpression);
	check(Entry.NumInputs >= 0);

	if (Entry.NumInputs >= MaterialExpression->NumExecutionInputs)
	{
		return Errors.AddErrorf(TEXT("Bad control flow, found %d inputs out of %d reported"), Entry.NumInputs, MaterialExpression->NumExecutionInputs);
	}
	if (Entry.NumInputs == MaxNumPreviousScopes)
	{
		return Errors.AddErrorf(TEXT("Bad control flow, too many execution inputs"));
	}

	Entry.PreviousScope[Entry.NumInputs++] = &Scope;

	bool bResult = true;
	if (Entry.NumInputs == MaterialExpression->NumExecutionInputs)
	{
		FScope* ScopeToUse = &Scope;
		if (MaterialExpression->NumExecutionInputs > 1u)
		{
			if (JoinedScopeStack.Num() == 0)
			{
				return Errors.AddError(TEXT("Bad control flow"));
			}

			ScopeToUse = JoinedScopeStack.Pop(false);
			for (int32 i = 0; i < Entry.NumInputs; ++i)
			{
				ScopeToUse->AddPreviousScope(*Entry.PreviousScope[i]);
			}
		}

		FOwnerScope TreeOwnerScope(GetTree(), MaterialExpression);
		bResult = MaterialExpression->GenerateHLSLStatements(*this, *ScopeToUse);
	}

	return bResult;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::GenerateFunctionCall(UE::HLSLTree::FScope& Scope, UMaterialFunctionInterface* MaterialFunction, TArrayView<const FFunctionExpressionInput> ConnectedInputs, int32 OutputIndex)
{
	using namespace UE::HLSLTree;

	if (!MaterialFunction)
	{
		Errors.AddError(TEXT("Missing material function"));
		return nullptr;
	}

	TArray<FFunctionExpressionInput> FunctionInputs;
	TArray<FFunctionExpressionOutput> FunctionOutputs;
	MaterialFunction->GetInputsAndOutputs(FunctionInputs, FunctionOutputs);

	if (FunctionInputs.Num() != ConnectedInputs.Num())
	{
		Errors.AddError(TEXT("Mismatched function inputs"));
		return nullptr;
	}

	const UMaterialExpressionFunctionOutput* ExpressionOutput = FunctionOutputs.IsValidIndex(OutputIndex) ? FunctionOutputs[OutputIndex].ExpressionOutput.Get() : nullptr;
	if (!ExpressionOutput)
	{
		Errors.AddError(TEXT("Invalid function output"));
		return nullptr;
	}

	const bool bInlineFunction = !MaterialFunction->IsUsingControlFlow();

	FSHA1 Hasher;
	Hasher.Update((uint8*)&MaterialFunction, sizeof(UMaterialFunctionInterface*));

	FFunctionInputArray LocalFunctionInputs;
	FConnectedInputArray LocalConnectedInputs;
	for (int32 InputIndex = 0; InputIndex < ConnectedInputs.Num(); ++InputIndex)
	{
		// FunctionInputs are the inputs from the UMaterialFunction object
		const FFunctionExpressionInput& FunctionInput = FunctionInputs[InputIndex];

		// ConnectedInputs are the inputs from the UMaterialFunctionCall object
		// We want to connect the UMaterialExpressionFunctionInput from the UMaterialFunction to whatever UMaterialExpression is passed to the UMaterialFunctionCall
		FExpression* ConnectedInput = ConnectedInputs[InputIndex].Input.TryAcquireHLSLExpression(*this, Scope);

		LocalFunctionInputs.Add(FunctionInput.ExpressionInput);
		LocalConnectedInputs.Add(ConnectedInput);
		Hasher.Update((uint8*)&ConnectedInput, sizeof(ConnectedInput));
	}
	const FSHAHash Hash = Hasher.Finalize();

	FFunctionCallEntry** ExistingFunctionCall = FunctionCallMap.Find(Hash);
	FFunctionCallEntry* FunctionCall = ExistingFunctionCall ? *ExistingFunctionCall : nullptr;
	if (!FunctionCall)
	{
		// Generate an HLSL function object, if this is not an inline function call
		FFunction* HLSLFunction = !bInlineFunction ? HLSLTree->NewFunction() : nullptr;
		FunctionCall = new(HLSLTree->GetAllocator()) FFunctionCallEntry();
		FunctionCall->MaterialFunction = MaterialFunction;
		FunctionCall->HLSLFunction = HLSLFunction;
		FunctionCall->FunctionInputs = MoveTemp(LocalFunctionInputs);
		FunctionCall->ConnectedInputs = MoveTemp(LocalConnectedInputs);
		FunctionCall->FunctionOutputs.Reserve(FunctionOutputs.Num());
		for (const FFunctionExpressionOutput& Output : FunctionOutputs)
		{
			FunctionCall->FunctionOutputs.Add(Output.ExpressionOutput);
		}

		FunctionCallMap.Add(Hash, FunctionCall);

		if (HLSLFunction)
		{
			UMaterialFunction* BaseMaterialFunction = Cast<UMaterialFunction>(MaterialFunction->GetBaseFunction());
			FunctionCallStack.Add(FunctionCall);
			GenerateStatements(HLSLFunction->GetRootScope(), BaseMaterialFunction->ExpressionExecBegin);
			verify(FunctionCallStack.Pop() == FunctionCall);
			check(FunctionCall->bGeneratedResult);
		}
	}

	FExpression* Result = nullptr;
	FunctionCallStack.Add(FunctionCall);
	if (bInlineFunction)
	{
		Result = ExpressionOutput->A.AcquireHLSLExpression(*this, Scope);
	}
	else
	{
		FFunction* HLSLFunction = FunctionCall->HLSLFunction;
		check(HLSLFunction);
		check(HLSLFunction->OutputExpressions.Num() == FunctionOutputs.Num());
		if (HLSLFunction->OutputExpressions[OutputIndex])
		{
			Result = HLSLTree->NewFunctionCall(Scope, HLSLFunction, OutputIndex);
		}
		else
		{
			Errors.AddError(TEXT("Invalid function output"));
		}
	}
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
