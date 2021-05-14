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
#include "MaterialHLSLTree.h"
#include "ShaderCore.h"
#include "HLSLTree/HLSLTree.h"
#include "HLSLTree/HLSLTreeCommon.h"
#include "Containers/LazyPrintf.h"

FMaterialHLSLGenerator::FMaterialHLSLGenerator(UMaterial* InTargetMaterial, const FMaterialCompileTargetParameters& InCompileTarget, UE::HLSLTree::FTree& InOutTree)
	: CompileTarget(InCompileTarget)
	, TargetMaterial(InTargetMaterial)
	, TargetMaterialFunction(nullptr)
	, HLSLTree(&InOutTree)
	, bGeneratedResult(false)
{
}

FMaterialHLSLGenerator::FMaterialHLSLGenerator(UMaterialFunctionInterface* InTargetMaterialFunction, const FMaterialCompileTargetParameters& InCompileTarget, UE::HLSLTree::FTree& InOutTree)
	: CompileTarget(InCompileTarget)
	, TargetMaterial(nullptr)
	, TargetMaterialFunction(InTargetMaterialFunction)
	, HLSLTree(&InOutTree)
	, bGeneratedResult(false)
{
}

void FMaterialHLSLGenerator::AcquireErrors(TArray<FString>& OutCompileErrors, TArray<UMaterialExpression*>& OutErrorExpressions)
{
	OutCompileErrors = MoveTemp(CompileErrors);
	OutErrorExpressions = MoveTemp(ErrorExpressions);
}

EMaterialGenerateHLSLStatus FMaterialHLSLGenerator::Error(const FString& Message)
{
	UMaterialExpression* ExpressionToError = nullptr;
	FString ErrorString;

	if (ExpressionStack.Num() > 0)
	{
		UMaterialExpression* ErrorExpression = ExpressionStack.Last().Expression;
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
					Expression = Generator.NewConstant(Scope, InputDescription.ConstantValue);
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
						Expression = InputDescription.Input->AcquireHLSLExpressionWithCast(Generator, Scope, InputDescription.Type);
					}
					if (!Expression)
					{
						Expression = Generator.NewTexCoord(Scope, TexCoordIndex);
					}
				}
				else
				{
					Expression = InputDescription.Input->AcquireHLSLExpressionWithCast(Generator, Scope, InputDescription.Type);
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

	return AttributesExpression;
}

UE::HLSLTree::FStatement* FMaterialHLSLGenerator::NewResult(UE::HLSLTree::FScope& Scope)
{
	UE::HLSLTree::FStatement* Result = nullptr;
	if (bGeneratedResult)
	{
		Error(TEXT("Multiple connections to execution output"));
	}
	else
	{
		if (TargetMaterial)
		{
			UE::HLSLTree::FExpression* AttributesExpression = nullptr;
			if (TargetMaterial->bUseMaterialAttributes)
			{
				FMaterialInputDescription InputDescription;
				if (TargetMaterial->GetExpressionInputDescription(MP_MaterialAttributes, InputDescription))
				{
					check(InputDescription.Type == UE::Shader::EValueType::MaterialAttributes);
					AttributesExpression = InputDescription.Input->AcquireHLSLExpression(*this, Scope);
				}
			}
			else
			{
				AttributesExpression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionDefaultMaterialAttributes>(Scope);
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
				Result = ReturnStatement;
			}
		}
		else
		{
			check(false);
		}
		bGeneratedResult = true;
	}
	return Result;
}

UE::HLSLTree::FExpressionConstant* FMaterialHLSLGenerator::NewConstant(UE::HLSLTree::FScope& Scope, const UE::Shader::FValue& Value)
{
	UE::HLSLTree::FExpressionConstant* Expression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionConstant>(Scope, Value);
	return Expression;
}

UE::HLSLTree::FExpressionExternalInput* FMaterialHLSLGenerator::NewTexCoord(UE::HLSLTree::FScope& Scope, int32 Index)
{
	UE::HLSLTree::FExpressionExternalInput* Expression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionExternalInput>(Scope, UE::HLSLTree::MakeInputTexCoord(Index));
	return Expression;
}

UE::HLSLTree::FExpressionSwizzle* FMaterialHLSLGenerator::NewSwizzle(UE::HLSLTree::FScope& Scope, const UE::HLSLTree::FSwizzleParameters& Params, UE::HLSLTree::FExpression* Input)
{
	UE::HLSLTree::FExpressionSwizzle* Expression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionSwizzle>(Scope, Params, Input);
	return Expression;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::NewCast(UE::HLSLTree::FScope& Scope, UE::Shader::EValueType Type, UE::HLSLTree::FExpression* Input, UE::HLSLTree::ECastFlags Flags)
{
	UE::HLSLTree::FExpressionCast* Expression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionCast>(Scope, Type, Input, Flags);
	return Expression;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::NewFunctionInput(UE::HLSLTree::FScope& Scope, int32 InputIndex, UMaterialExpressionFunctionInput* MaterialFunctionInput)
{
	UE::Shader::EValueType ExpressionType = UE::Shader::EValueType::Void;
	switch (MaterialFunctionInput->InputType)
	{
	case FunctionInput_Scalar: ExpressionType = UE::Shader::EValueType::Float1; break;
	case FunctionInput_Vector2: ExpressionType = UE::Shader::EValueType::Float2; break;
	case FunctionInput_Vector3: ExpressionType = UE::Shader::EValueType::Float3; break;
	case FunctionInput_Vector4: ExpressionType = UE::Shader::EValueType::Float4; break;
	case FunctionInput_Texture2D:
	case FunctionInput_TextureCube:
	case FunctionInput_Texture2DArray:
	case FunctionInput_VolumeTexture:
	case FunctionInput_StaticBool:
	case FunctionInput_MaterialAttributes:
	case FunctionInput_TextureExternal:
		// TODO
		break;
	}
	check(ExpressionType != UE::Shader::EValueType::Void);

	UE::HLSLTree::FExpressionFunctionInput* Expression = HLSLTree->NewExpression<UE::HLSLTree::FExpressionFunctionInput>(Scope, MaterialFunctionInput->InputName, ExpressionType, InputIndex);
	const FExpressionKey Key(MaterialFunctionInput, 0);
	ExpressionMap.Add(Key, Expression);

	return Expression;
}

UE::HLSLTree::FLocalDeclaration* FMaterialHLSLGenerator::AcquireLocalDeclaration(UE::HLSLTree::FScope& Scope, UE::Shader::EValueType Type, const FName& Name)
{
	UE::HLSLTree::FLocalDeclaration*& Declaration = LocalDeclarationMap.FindOrAdd(Name);
	if (!Declaration)
	{
		Declaration = HLSLTree->NewLocalDeclaration(Scope, Type, Name);
	}
	else
	{
		if (Declaration->Type == Type)
		{
			Scope.UseDeclaration(Declaration);
		}
		else
		{
			Errorf(TEXT("Local %s first accessed as type %s, now type %s"), *Name.ToString(),
				UE::Shader::GetValueTypeDescription(Declaration->Type).Name,
				UE::Shader::GetValueTypeDescription(Type).Name);
			return nullptr;
		}
	}
	return Declaration;
}

UE::HLSLTree::FParameterDeclaration* FMaterialHLSLGenerator::AcquireParameterDeclaration(UE::HLSLTree::FScope& Scope, const FName& Name, const UE::Shader::FValue& DefaultValue)
{
	UE::HLSLTree::FParameterDeclaration*& Declaration = ParameterDeclarationMap.FindOrAdd(Name);
	if (!Declaration)
	{
		Declaration = HLSLTree->NewParameterDeclaration(Scope, Name, DefaultValue);
	}
	return Declaration;
}

UE::HLSLTree::FTextureParameterDeclaration* FMaterialHLSLGenerator::AcquireTextureDeclaration(UE::HLSLTree::FScope& Scope, const UE::HLSLTree::FTextureDescription& Value)
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
		Declaration = HLSLTree->NewTextureParameterDeclaration(Scope, FName(), Value);
	}
	return Declaration;
}

UE::HLSLTree::FTextureParameterDeclaration* FMaterialHLSLGenerator::AcquireTextureParameterDeclaration(UE::HLSLTree::FScope& Scope, const FName& Name, const UE::HLSLTree::FTextureDescription& DefaultValue)
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
		Declaration = HLSLTree->NewTextureParameterDeclaration(Scope, Name, DefaultValue);
	}
	return Declaration;
}

UE::HLSLTree::FFunctionCall* FMaterialHLSLGenerator::AcquireFunctionCall(UE::HLSLTree::FScope& Scope, UMaterialFunctionInterface* Function, TArrayView<UE::HLSLTree::FExpression*> Inputs)
{
	FSHA1 Hasher;
	for (UE::HLSLTree::FExpression* Input : Inputs)
	{
		Hasher.Update((uint8*)&Input, sizeof(UE::HLSLTree::FExpression*));
	}
	Hasher.Final();

	FFunctionCallKey Key;
	Key.Function = Function;
	Hasher.GetHash(Key.InputHash.Hash);

	UE::HLSLTree::FFunctionCall** PrevFunctionCall = FunctionCallMap.Find(Key);
	UE::HLSLTree::FFunctionCall* FunctionCall = nullptr;
	if (!PrevFunctionCall)
	{
		const FMaterialHLSLTree& FunctionTree = Function->AcquireHLSLTree(*this);
		FunctionCall = FunctionTree.GenerateFunctionCall(*this, Scope, Inputs);
		FunctionCallMap.Add(Key, FunctionCall);
	}
	else
	{
		FunctionCall = *PrevFunctionCall;
		Scope.UseFunctionCall(FunctionCall);
	}
	return FunctionCall;
}

UE::HLSLTree::FExpression* FMaterialHLSLGenerator::AcquireExpression(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex)
{
	const FExpressionKey Key(MaterialExpression, OutputIndex);
	UE::HLSLTree::FExpression** PrevExpression = ExpressionMap.Find(Key);
	UE::HLSLTree::FExpression* Expression = nullptr;
	if (!PrevExpression)
	{
		ExpressionStack.Add(Key);
		const EMaterialGenerateHLSLStatus Status = MaterialExpression->GenerateHLSLExpression(*this, Scope, OutputIndex, Expression);
		verify(ExpressionStack.Pop() == Key);
		ExpressionMap.Add(Key, Expression);
	}
	else
	{
		Expression = *PrevExpression;
		Scope.UseExpression(Expression);
	}
	return Expression;
}

UE::HLSLTree::FTextureParameterDeclaration* FMaterialHLSLGenerator::AcquireTextureDeclaration(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression, int32 OutputIndex)
{
	// No need to cache at this level, TextureDeclarations are cached at a lower level, as they're generated by UMaterialExpression
	UE::HLSLTree::FTextureParameterDeclaration* TextureDeclaration = nullptr;
	const EMaterialGenerateHLSLStatus Status = MaterialExpression->GenerateHLSLTexture(*this, Scope, OutputIndex, TextureDeclaration);
	return TextureDeclaration;
}

UE::HLSLTree::FStatement* FMaterialHLSLGenerator::AcquireStatement(UE::HLSLTree::FScope& Scope, UMaterialExpression* MaterialExpression)
{
	UE::HLSLTree::FStatement** PrevStatement = StatementMap.Find(MaterialExpression);
	UE::HLSLTree::FStatement* Statement = nullptr;
	if (!PrevStatement)
	{
		const FExpressionKey Key(MaterialExpression);
		ExpressionStack.Add(Key);
		const EMaterialGenerateHLSLStatus Status = MaterialExpression->GenerateHLSLStatement(*this, Scope, Statement);
		verify(ExpressionStack.Pop() == Key);
		StatementMap.Add(MaterialExpression, Statement);
	}
	else
	{
		Statement = *PrevStatement;
		if (Statement && !Scope.TryMoveStatement(Statement))
		{
			// Could not move existing statement to the given scope
			Error(TEXT("Invalid control flow"));
			return nullptr;
		}
	}

	return Statement;
}

#endif // WITH_EDITOR
