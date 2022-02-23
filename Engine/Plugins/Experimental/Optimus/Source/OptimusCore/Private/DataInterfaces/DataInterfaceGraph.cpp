// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaces/DataInterfaceGraph.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "OptimusDeformerInstance.h"
#include "OptimusVariableDescription.h"

namespace
{
	template<typename T>
	void ParametrizedAddParm(FShaderParametersMetadataBuilder& InOutBuilder, const TCHAR* InName)
	{
		InOutBuilder.AddParam<T>(InName);
	}

	void AddParamForType(FShaderParametersMetadataBuilder& InOutBuilder, TCHAR const* InName, FShaderValueTypeHandle const& InValueType)
	{
		using AddParamFuncType = TFunction<void(FShaderParametersMetadataBuilder&, const TCHAR*)>;

		static TMap<FShaderValueType, AddParamFuncType> AddParamFuncs =
		{
			{*FShaderValueType::Get(EShaderFundamentalType::Bool), &ParametrizedAddParm<bool>},
			{*FShaderValueType::Get(EShaderFundamentalType::Int), &ParametrizedAddParm<int32>},
			{*FShaderValueType::Get(EShaderFundamentalType::Int, 2), &ParametrizedAddParm<FIntPoint>},
			{*FShaderValueType::Get(EShaderFundamentalType::Int, 3), &ParametrizedAddParm<FIntVector>},
			{*FShaderValueType::Get(EShaderFundamentalType::Int, 4), &ParametrizedAddParm<FIntVector4>},
			{*FShaderValueType::Get(EShaderFundamentalType::Uint), &ParametrizedAddParm<uint32>},
			{*FShaderValueType::Get(EShaderFundamentalType::Uint, 2), &ParametrizedAddParm<FUintVector2>},
			{*FShaderValueType::Get(EShaderFundamentalType::Uint, 4), &ParametrizedAddParm<FUintVector4>},
			{*FShaderValueType::Get(EShaderFundamentalType::Float), &ParametrizedAddParm<float>},
			{*FShaderValueType::Get(EShaderFundamentalType::Float, 2), &ParametrizedAddParm<FVector2f>},
			{*FShaderValueType::Get(EShaderFundamentalType::Float, 3), &ParametrizedAddParm<FVector3f>},
			{*FShaderValueType::Get(EShaderFundamentalType::Float, 4), &ParametrizedAddParm<FVector4f>},
			{*FShaderValueType::Get(EShaderFundamentalType::Float, 4, 4), &ParametrizedAddParm<FMatrix44f>},
		};

		if (const AddParamFuncType* Entry = AddParamFuncs.Find(*InValueType))
		{
			(*Entry)(InOutBuilder, InName);
		}
	}
}

void UGraphDataInterface::Init(TArray<FGraphVariableDescription> const& InVariables)
{
	Variables = InVariables;

	FShaderParametersMetadataBuilder Builder;
	for (FGraphVariableDescription const& Variable : Variables)
	{
		AddParamForType(Builder, *Variable.Name, Variable.ValueType);
	}
	TSharedPtr<FShaderParametersMetadata> ShaderParameterMetadata(Builder.Build(FShaderParametersMetadata::EUseCase::ShaderParameterStruct, TEXT("UGraphDataInterface")));

	TArray<FShaderParametersMetadata::FMember> const& Members = ShaderParameterMetadata->GetMembers();
	for (int32 VariableIndex = 0; VariableIndex < Variables.Num(); ++VariableIndex)
	{
		check(Variables[VariableIndex].Name == Members[VariableIndex].GetName());
		Variables[VariableIndex].Offset = Members[VariableIndex].GetOffset();
	}

	ParameterBufferSize = ShaderParameterMetadata->GetSize();
}

void UGraphDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	for (FGraphVariableDescription const& Variable : Variables)
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = FString::Printf(TEXT("Read%s"), *Variable.Name);
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = Variable.ValueType;
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
}

void UGraphDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	FShaderParametersMetadataBuilder Builder;
	for (FGraphVariableDescription const& Variable : Variables)
	{
		AddParamForType(Builder, *Variable.Name, Variable.ValueType);
	}

	// Todo[CF]: This leaks! Provide an allocator to this function to collect stuff like this (and maybe UID TCHAR allocation).
	FShaderParametersMetadata* ShaderParameterMetadata = Builder.Build(FShaderParametersMetadata::EUseCase::ShaderParameterStruct, TEXT("UGraphDataInterface"));

	OutBuilder.AddNestedStruct(UID, ShaderParameterMetadata);
}

void UGraphDataInterface::GetHLSL(FString& OutHLSL) const
{
	// Need include for DI_LOCAL macro expansion.
	OutHLSL += TEXT("#include \"/Plugin/ComputeFramework/Private/ComputeKernelCommon.ush\"\n");
	// Add uniforms.
	for (FGraphVariableDescription const& Variable : Variables)
	{
		OutHLSL += FString::Printf(TEXT("float DI_LOCAL(%s);\n"), *Variable.Name);
	}
	// Add function getters.
	for (FGraphVariableDescription const& Variable : Variables)
	{
		OutHLSL += FString::Printf(TEXT("DI_IMPL_READ(Read%s, float, )\n{\n\treturn DI_LOCAL(%s);\n}\n"), *Variable.Name, *Variable.Name);
	}
}

void UGraphDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkinnedMeshComponent::StaticClass());
}

UComputeDataProvider* UGraphDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UGraphDataProvider* Provider = NewObject<UGraphDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkinnedMeshComponent = Cast<USkinnedMeshComponent>(InSourceObjects[0]);
		Provider->Variables = Variables;
		Provider->ParameterBufferSize = ParameterBufferSize;
	}

	return Provider;
}


void UGraphDataProvider::SetConstant(FString const& InVariableName, TArray<uint8> const& InValue)
{
	for (int32 VariableIndex = 0; VariableIndex < Variables.Num(); ++VariableIndex)
	{
		if (Variables[VariableIndex].Name == InVariableName)
		{
			if (ensure(Variables[VariableIndex].Value.Num() == InValue.Num()))
			{
				Variables[VariableIndex].Value = InValue;
				break;
			}
		}
	}
}

FComputeDataProviderRenderProxy* UGraphDataProvider::GetRenderProxy()
{
	UOptimusDeformerInstance* DeformerInstance = Cast<UOptimusDeformerInstance>(SkinnedMeshComponent->MeshDeformerInstance);

	return new FGraphDataProviderProxy(DeformerInstance, Variables, ParameterBufferSize);
}


FGraphDataProviderProxy::FGraphDataProviderProxy(UOptimusDeformerInstance const* DeformerInstance, TArray<FGraphVariableDescription> const& Variables, int32 ParameterBufferSize)
{
	// Get all variables from deformer instance and fill buffer.
	ParameterData.AddZeroed(ParameterBufferSize);

	if (DeformerInstance == nullptr)
	{
		return;
	}

	TArray<UOptimusVariableDescription*> const& VariableValues = DeformerInstance->GetVariables();
	for (FGraphVariableDescription const& Variable : Variables)
	{
		if (Variable.Value.Num())
		{
			// Use the constant value.
			FMemory::Memcpy(&ParameterData[Variable.Offset], Variable.Value.GetData(), Variable.Value.Num());
		}
		else
		{
			// Find value from variables on the deformer instance.
			// todo[CF]: Use a map for more efficient look up? Or something even faster like having a fixed location per variable?
			for (UOptimusVariableDescription const* VariableValue : VariableValues)
			{
				if (VariableValue != nullptr)
				{
					if (Variable.ValueType == VariableValue->DataType->ShaderValueType && Variable.Name == VariableValue->VariableName.GetPlainNameString())
					{
						FMemory::Memcpy(&ParameterData[Variable.Offset], VariableValue->ValueData.GetData(), VariableValue->ValueData.Num());
						break;
					}
				}
			}
		}
	}
}

void FGraphDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (ParameterData.Num() == 0)
	{
		// todo[CF]: Why can we end up here? Remove this condition if possible.
		return;
	}

	if (!ensure(ParameterData.Num() == InDispatchSetup.ParameterStructSizeForValidation))
	{
		return;
	}

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		void* ParameterBuffer = (void*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		FMemory::Memcpy(ParameterBuffer, ParameterData.GetData(), ParameterData.Num());
	}
}
