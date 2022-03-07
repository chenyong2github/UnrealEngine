// Copyright Epic Games, Inc. All Rights Reserved.
#include "MaterialHLSLGenerator.h"

#if WITH_EDITOR

#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionExecBegin.h"
#include "Materials/MaterialHLSLTree.h"
#include "Materials/Material.h"
#include "MaterialCachedHLSLTree.h"
#include "ShaderCore.h"
#include "HLSLTree/HLSLTree.h"
#include "HLSLTree/HLSLTreeCommon.h"
#include "Containers/LazyPrintf.h"
#include "Misc/MemStackUtility.h"

FMaterialHLSLGenerator::FMaterialHLSLGenerator(UMaterial* Material,
	const FMaterialLayersFunctions* InLayerOverrides,
	UMaterialExpression* InPreviewExpression,
	FMaterialCachedHLSLTree& OutCachedTree)
	: TargetMaterial(Material)
	, LayerOverrides(InLayerOverrides)
	, PreviewExpression(InPreviewExpression)
	, CachedTree(OutCachedTree)
	, bGeneratedResult(false)
{
	FunctionCallStack.Add(&RootFunctionCallEntry);
}

UE::HLSLTree::FTree& FMaterialHLSLGenerator::GetTree() const
{
	return CachedTree.GetTree();
}

UE::Shader::FStructTypeRegistry& FMaterialHLSLGenerator::GetTypeRegistry() const
{
	return CachedTree.GetTypeRegistry();
}

const UE::Shader::FStructType* FMaterialHLSLGenerator::GetMaterialAttributesType() const
{
	return CachedTree.GetMaterialAttributesType();
}

const UE::Shader::FValue& FMaterialHLSLGenerator::GetMaterialAttributesDefaultValue() const
{
	return CachedTree.GetMaterialAttributesDefaultValue();
}

bool FMaterialHLSLGenerator::Generate()
{
	using namespace UE::HLSLTree;

	FScope& RootScope = CachedTree.GetTree().GetRootScope();

	bool bResult = false;
	if (TargetMaterial->IsUsingControlFlow())
	{
		UMaterialExpression* BaseExpression = TargetMaterial->ExpressionExecBegin;
		if (!BaseExpression)
		{
			bResult = Error(TEXT("Missing ExpressionExecBegin"));
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
		return Error(TEXT("Missing connection to material output"));
	}

	if (!CachedTree.GetResultExpression() || !CachedTree.GetResultStatement())
	{
		return Error(TEXT("Failed to initialize result"));
	}

	for (const auto& It : StatementMap)
	{
		const UMaterialExpression* Expression = It.Key;
		const FStatementEntry& Entry = It.Value;
		if (Entry.NumInputs != Expression->NumExecutionInputs)
		{
			return Error(TEXT("Invalid number of input connections"));
		}
	}

	if (JoinedScopeStack.Num() != 0)
	{
		return Error(TEXT("Invalid control flow"));
	}

	return GetTree().Finalize();
}

static UE::HLSLTree::FExpression* CompileMaterialInput(FMaterialHLSLGenerator& Generator,
	UE::HLSLTree::FScope& Scope,
	EMaterialProperty InputProperty,
	UMaterial* Material)
{
	using namespace UE::HLSLTree;

	FExpression* Expression = nullptr;
	if (Material->IsPropertyActive(InputProperty))
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

	return Expression;
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
		return Error(TEXT("Multiple connections to execution output"));
	}
	else
	{
		check(!CachedTree.ResultStatement);
		check(!CachedTree.ResultExpression);

		FExpression* AttributesExpression = nullptr;
		if (TargetMaterial)
		{
			const FStructField* PrevWPOField = CachedTree.GetMaterialAttributesType()->FindFieldByName(TEXT("PrevWorldPositionOffset"));
			if (TargetMaterial->bUseMaterialAttributes)
			{
				FMaterialInputDescription InputDescription;
				if (TargetMaterial->GetExpressionInputDescription(MP_MaterialAttributes, InputDescription))
				{
					check(InputDescription.Type == UE::Shader::EValueType::Struct);
					AttributesExpression = InputDescription.Input->TryAcquireHLSLExpression(*this, Scope);

					if (AttributesExpression)
					{
						const FString& WPOName = FMaterialAttributeDefinitionMap::GetAttributeName(MP_WorldPositionOffset);
						const FStructField* WPOField = CachedTree.GetMaterialAttributesType()->FindFieldByName(*WPOName);

						FRequestedType PrevRequestedType;
						PrevRequestedType.SetFieldRequested(WPOField);

						FExpression* PrevAttributesExpression = GetTree().GetPreviousFrame(AttributesExpression, PrevRequestedType);
						ensure(PrevAttributesExpression);
						FExpression* PrevWPOExpression = GetTree().NewExpression<FExpressionGetStructField>(CachedTree.GetMaterialAttributesType(), WPOField, PrevAttributesExpression);
						AttributesExpression = GetTree().NewExpression<FExpressionSetStructField>(CachedTree.GetMaterialAttributesType(), PrevWPOField, AttributesExpression, PrevWPOExpression);
					}
				}
			}
			else
			{
				AttributesExpression = GetTree().NewExpression<FExpressionConstant>(CachedTree.GetMaterialAttributesDefaultValue());
				for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
				{
					const EMaterialProperty Property = (EMaterialProperty)PropertyIndex;

					// We're only interesting in attributes that map to valid fields
					const UE::Shader::FStructField* AttributeField = CachedTree.GetMaterialAttributesType()->FindFieldByName(*FMaterialAttributeDefinitionMap::GetAttributeName(Property));
					if (AttributeField)
					{
						FExpression* InputExpression = CompileMaterialInput(*this, Scope, Property, TargetMaterial);
						if (InputExpression)
						{
							AttributesExpression = GetTree().NewExpression<FExpressionSetStructField>(
								CachedTree.GetMaterialAttributesType(),
								AttributeField,
								AttributesExpression,
								InputExpression);
							if (Property == MP_WorldPositionOffset)
							{
								FExpression* PrevWPOExpression = GetTree().GetPreviousFrame(InputExpression, ERequestedType::Vector3);
								ensure(PrevWPOExpression);
								AttributesExpression = GetTree().NewExpression<FExpressionSetStructField>(CachedTree.GetMaterialAttributesType(), PrevWPOField, AttributesExpression, PrevWPOExpression);
							}
						}
					}
				}
			}

			for (UMaterialExpressionCustomOutput* CustomOutput : CachedTree.MaterialCustomOutputs)
			{
				const int32 NumOutputs = CustomOutput->GetNumOutputs();
				const FString OutputName = CustomOutput->GetFunctionName();
				for (int32 OutputIndex = 0; OutputIndex < NumOutputs; ++OutputIndex)
				{
					TStringBuilder<256> FieldName;
					FieldName.Appendf(TEXT("%s%d"), *OutputName, OutputIndex);
					const FStructField* CustomOutputField = CachedTree.GetMaterialAttributesType()->FindFieldByName(FieldName.ToString());
					check(CustomOutputField);

					FExpression* CustomOutputExpression = AcquireExpression(Scope, CustomOutput, OutputIndex);
					AttributesExpression = GetTree().NewExpression<FExpressionSetStructField>(CachedTree.GetMaterialAttributesType(), CustomOutputField, AttributesExpression, CustomOutputExpression);
				}
			}
		}

		if (PreviewExpression)
		{
			if (!PreviewExpressionResult)
			{
				// If we didn't hit the preview expression while generating the material normally, then generate it now
				// Hardcoding output 0 as we don't have the UI to specify any other output
				const int32 OutputIndex = 0;
				PreviewExpressionResult = AcquireExpression(Scope, PreviewExpression, OutputIndex);
			}
			const FString& EmissiveColorName = FMaterialAttributeDefinitionMap::GetAttributeName(MP_EmissiveColor);
			const FStructField* EmissiveColorField = CachedTree.GetMaterialAttributesType()->FindFieldByName(*EmissiveColorName);

			// Get back into gamma corrected space, as DrawTile does not do this adjustment.
			FExpression* ExpressionEmissive = GetTree().NewPowClamped(PreviewExpressionResult, NewConstant(1.f / 2.2f));

			AttributesExpression = GetTree().NewExpression<FExpressionConstant>(CachedTree.GetMaterialAttributesDefaultValue());
			AttributesExpression = GetTree().NewExpression<FExpressionSetStructField>(CachedTree.GetMaterialAttributesType(), EmissiveColorField, AttributesExpression, ExpressionEmissive);
		}

		if (AttributesExpression)
		{
			FStatementReturn* ReturnStatement = GetTree().NewStatement<FStatementReturn>(Scope);
			ReturnStatement->Expression = AttributesExpression;
			CachedTree.ResultExpression = AttributesExpression;
			CachedTree.ResultStatement = ReturnStatement;
			bResult = true;
		}

		bGeneratedResult = true;
	}
	return bResult;
}

UE::HLSLTree::FScope* FMaterialHLSLGenerator::NewScope(UE::HLSLTree::FScope& Scope, EMaterialNewScopeFlag Flags)
{
	UE::HLSLTree::FScope* NewScope = GetTree().NewScope(Scope);
	if (!EnumHasAllFlags(Flags, EMaterialNewScopeFlag::NoPreviousScope))
	{
		NewScope->AddPreviousScope(Scope);
	}

	return NewScope;
}

UE::HLSLTree::FScope* FMaterialHLSLGenerator::NewOwnedScope(UE::HLSLTree::FStatement& Owner)
{
	UE::HLSLTree::FScope* NewScope = GetTree().NewOwnedScope(Owner);
	NewScope->AddPreviousScope(Owner.GetParentScope());
	return NewScope;
}

UE::HLSLTree::FScope* FMaterialHLSLGenerator::NewJoinedScope(UE::HLSLTree::FScope& Scope)
{
	UE::HLSLTree::FScope* NewScope = GetTree().NewScope(Scope);
	JoinedScopeStack.Add(NewScope);
	return NewScope;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::NewConstant(const UE::Shader::FValue& Value)
{
	return GetTree().NewConstant(Value);
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::NewTexCoord(int32 Index)
{
	using namespace UE::HLSLTree;
	return NewExternalInput(Material::MakeInputTexCoord(Index));
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::NewExternalInput(UE::HLSLTree::Material::EExternalInput Input)
{
	using namespace UE::HLSLTree;
	return GetTree().NewExpression<Material::FExpressionExternalInput>(Input);
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::NewSwizzle(const UE::HLSLTree::FSwizzleParameters& Params, UE::HLSLTree::FExpression* Input)
{
	return GetTree().NewExpression<UE::HLSLTree::FExpressionSwizzle>(Params, Input);
}

const UE::Shader::FTextureValue* FMaterialHLSLGenerator::AcquireTextureValue(const UE::Shader::FTextureValue& InValue)
{
	using namespace UE::Shader;
	
	// Need to move this to HLSLTreeEmit
	/*FString SamplerTypeError;
	if (!UMaterialExpressionTextureBase::VerifySamplerType(CompileTarget.FeatureLevel, CompileTarget.TargetPlatform, InValue.Texture, InValue.SamplerType, SamplerTypeError))
	{
		Errors.AddError(SamplerTypeError);
		return nullptr;
	}*/

	FXxHash64Builder Hasher;
	Hasher.Update(&InValue.Texture, sizeof(InValue.Texture));
	Hasher.Update(&InValue.SamplerType, sizeof(InValue.SamplerType));
	Hasher.Update(&InValue.ExternalTextureGuid, sizeof(InValue.ExternalTextureGuid));
	const FXxHash64 Hash = Hasher.Finalize();

	FTextureValue const* const* PrevValue = TextureValueMap.Find(Hash);
	if (PrevValue)
	{
		check(*PrevValue);
		check(**PrevValue == InValue);
		return *PrevValue;
	}

	FTextureValue* Value = new(GetTree().GetAllocator()) FTextureValue(InValue);
	TextureValueMap.Add(Hash, Value);
	return Value;
}

bool FMaterialHLSLGenerator::InternalError(FStringView ErrorMessage)
{
	if (CurrentErrorMessage.Len() > 0)
	{
		CurrentErrorMessage.AppendChar(TEXT('\n'));
	}
	CurrentErrorMessage.Append(ErrorMessage);
	return false;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::AcquireExpression(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex)
{
	using namespace UE::HLSLTree;
	FOwnerScope TreeOwnerScope(GetTree(), MaterialExpression);

	FExpression* Expression = nullptr;
	if (MaterialExpression->GenerateHLSLExpression(*this, Scope, OutputIndex, Expression))
	{
		if (MaterialExpression == PreviewExpression &&
			!PreviewExpressionResult)
		{
			PreviewExpressionResult = Expression;
		}
		return Expression;
	}
	
	check(!Expression);
	const FStringView ErrorMessage = UE::MemStack::AllocateStringView(GetTree().GetAllocator(), CurrentErrorMessage);
	CurrentErrorMessage.Reset();
	return GetTree().NewExpression<UE::HLSLTree::FExpressionError>(ErrorMessage);
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
			Error(TEXT("Invalid function input"));
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
			case FunctionInput_MaterialAttributes: DefaultValue = CachedTree.GetMaterialAttributesDefaultValue(); break;
			case FunctionInput_Texture2D:
			case FunctionInput_TextureCube:
			case FunctionInput_Texture2DArray:
			case FunctionInput_VolumeTexture:
			case FunctionInput_StaticBool:
			case FunctionInput_TextureExternal:
				Errorf(TEXT("Missing Preview connection for function input '%s'"), *MaterialExpression->InputName.ToString());
				return nullptr;
			default:
				Error(TEXT("Unknown input type"));
				return nullptr;
			}

			InputExpression = NewConstant(DefaultValue);
		}
	}

	return InputExpression;
}

bool FMaterialHLSLGenerator::GenerateStatements(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression)
{
	using namespace UE::HLSLTree;

	FStatementEntry& Entry = StatementMap.FindOrAdd(MaterialExpression);
	check(Entry.NumInputs >= 0);

	if (Entry.NumInputs >= MaterialExpression->NumExecutionInputs)
	{
		return Errorf(TEXT("Bad control flow, found %d inputs out of %d reported"), Entry.NumInputs, MaterialExpression->NumExecutionInputs);
	}
	if (Entry.NumInputs == MaxNumPreviousScopes)
	{
		return Errorf(TEXT("Bad control flow, too many execution inputs"));
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
				return Error(TEXT("Bad control flow"));
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

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::GenerateMaterialParameter(FName InParameterName, const FMaterialParameterMetadata& InParameterMeta, const UE::Shader::FValue& InDefaultValue)
{
	using namespace UE::Shader;

	FMaterialParameterMetadata ParameterMeta(InParameterMeta);
	FValue DefaultValue(InDefaultValue);

	FMaterialParameterMetadata OverrideParameterMeta;
	if (GetParameterOverrideValueForCurrentFunction(InParameterMeta.Value.Type, InParameterName, OverrideParameterMeta))
	{
		ParameterMeta.Value = OverrideParameterMeta.Value;
		ParameterMeta.ExpressionGuid = OverrideParameterMeta.ExpressionGuid;
		ParameterMeta.bUsedAsAtlasPosition = OverrideParameterMeta.bUsedAsAtlasPosition;
		ParameterMeta.ScalarAtlas = OverrideParameterMeta.ScalarAtlas;
		ParameterMeta.ScalarCurve = OverrideParameterMeta.ScalarCurve;

		if (DefaultValue.Type.IsTexture())
		{
			FTextureValue TextureValue(*DefaultValue.AsTexture());
			TextureValue.Texture = OverrideParameterMeta.Value.Texture;
			DefaultValue = AcquireTextureValue(TextureValue);
		}
		else
		{
			DefaultValue = OverrideParameterMeta.Value.AsShaderValue();
		}
	}

	return GetTree().NewExpression<UE::HLSLTree::Material::FExpressionParameter>(GetParameterInfo(InParameterName), ParameterMeta, DefaultValue);
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::GenerateFunctionCall(UE::HLSLTree::FScope& Scope,
	UMaterialFunctionInterface* MaterialFunction,
	EMaterialParameterAssociation InParameterAssociation,
	int32 InParameterIndex,
	TArrayView<UE::HLSLTree::FExpression*> ConnectedInputs,
	int32 OutputIndex)
{
	using namespace UE::HLSLTree;

	if (!MaterialFunction)
	{
		Error(TEXT("Missing material function"));
		return nullptr;
	}

	TArray<FFunctionExpressionInput> FunctionInputs;
	TArray<FFunctionExpressionOutput> FunctionOutputs;
	MaterialFunction->GetInputsAndOutputs(FunctionInputs, FunctionOutputs);

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

	EMaterialParameterAssociation ParameterAssociation = InParameterAssociation;
	int32 ParameterIndex = InParameterIndex;
	if (InParameterAssociation == GlobalParameter)
	{
		// If this is a global function, inherit the parameter association from the previous function
		const FFunctionCallEntry* PrevFunctionEntry = FunctionCallStack.Last();
		ParameterAssociation = PrevFunctionEntry->ParameterAssociation;
		ParameterIndex = PrevFunctionEntry->ParameterIndex;
	}

	FXxHash64 Hash;
	FFunctionInputArray LocalFunctionInputs;
	{
		FXxHash64Builder Hasher;
		Hasher.Update(&MaterialFunction, sizeof(UMaterialFunctionInterface*));
		Hasher.Update(&ParameterAssociation, sizeof(EMaterialParameterAssociation));
		Hasher.Update(&ParameterIndex, sizeof(int32));

		for (int32 InputIndex = 0; InputIndex < ConnectedInputs.Num(); ++InputIndex)
		{
			// FunctionInputs are the inputs from the UMaterialFunction object
			const FFunctionExpressionInput& FunctionInput = FunctionInputs[InputIndex];

			// ConnectedInputs are the inputs from the UMaterialFunctionCall object
			// We want to connect the UMaterialExpressionFunctionInput from the UMaterialFunction to whatever UMaterialExpression is passed to the UMaterialFunctionCall
			FExpression* ConnectedInput = ConnectedInputs[InputIndex];

			LocalFunctionInputs.Add(FunctionInput.ExpressionInput);
			Hasher.Update((uint8*)&ConnectedInput, sizeof(ConnectedInput));
		}
		Hash = Hasher.Finalize();
	}

	const bool bInlineFunction = !MaterialFunction->IsUsingControlFlow();
	TUniquePtr<FFunctionCallEntry>* ExistingFunctionCall = FunctionCallMap.Find(Hash);
	FFunctionCallEntry* FunctionCall = ExistingFunctionCall ? ExistingFunctionCall->Get() : nullptr;
	if (!FunctionCall)
	{
		// Generate an HLSL function object, if this is not an inline function call
		FFunction* HLSLFunction = !bInlineFunction ? GetTree().NewFunction() : nullptr;
		FunctionCall = new FFunctionCallEntry();
		FunctionCall->MaterialFunction = MaterialFunction;
		FunctionCall->ParameterAssociation = ParameterAssociation;
		FunctionCall->ParameterIndex = ParameterIndex;
		FunctionCall->HLSLFunction = HLSLFunction;
		FunctionCall->FunctionInputs = MoveTemp(LocalFunctionInputs);
		FunctionCall->ConnectedInputs = ConnectedInputs;
		FunctionCall->FunctionOutputs.Reserve(FunctionOutputs.Num());
		for (const FFunctionExpressionOutput& Output : FunctionOutputs)
		{
			FunctionCall->FunctionOutputs.Add(Output.ExpressionOutput);
		}

		FunctionCallMap.Emplace(Hash, FunctionCall);

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
			Result = GetTree().NewFunctionCall(Scope, HLSLFunction, OutputIndex);
		}
		else
		{
			Error(TEXT("Invalid function output"));
		}
	}
	verify(FunctionCallStack.Pop() == FunctionCall);
	if (Result)
	{
		Result = GetTree().NewExpression<Material::FExpressionFunctionCall>(Result, MaterialFunction);
	}
	return Result;
}

bool FMaterialHLSLGenerator::GetParameterOverrideValueForCurrentFunction(EMaterialParameterType ParameterType, FName ParameterName, FMaterialParameterMetadata& OutResult) const
{
	bool bResult = false;
	if (!ParameterName.IsNone())
	{
		// Give every function in the callstack on opportunity to override the parameter value
		// Parameters in outer functions take priority
		// For example, if a layer instance calls a function instance that includes an overriden parameter, we want to use the value from the layer instance rather than the function instance
		for (const FFunctionCallEntry* FunctionEntry : FunctionCallStack)
		{
			const UMaterialFunctionInterface* CurrentFunction = FunctionEntry->MaterialFunction;
			if (CurrentFunction)
			{
				if (CurrentFunction->GetParameterOverrideValue(ParameterType, ParameterName, OutResult))
				{
					bResult = true;
					break;
				}
			}
		}
	}
	return bResult;
}

FMaterialParameterInfo FMaterialHLSLGenerator::GetParameterInfo(const FName& ParameterName) const
{
	if (ParameterName.IsNone())
	{
		return FMaterialParameterInfo();
	}

	const FFunctionCallEntry* FunctionEntry = FunctionCallStack.Last();
	return FMaterialParameterInfo(ParameterName, FunctionEntry->ParameterAssociation, FunctionEntry->ParameterIndex);
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
