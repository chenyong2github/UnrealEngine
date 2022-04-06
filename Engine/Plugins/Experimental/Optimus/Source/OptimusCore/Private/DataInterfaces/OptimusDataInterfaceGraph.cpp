// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceGraph.h"

#include "OptimusDeformerInstance.h"
#include "OptimusVariableDescription.h"

#include "Components/SkinnedMeshComponent.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"

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

void UOptimusGraphDataInterface::Init(TArray<FOptimusGraphVariableDescription> const& InVariables)
{
	Variables = InVariables;

	FShaderParametersMetadataBuilder Builder;
	for (FOptimusGraphVariableDescription const& Variable : Variables)
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

void UOptimusGraphDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.Reserve(OutFunctions.Num() + Variables.Num());
	for (FOptimusGraphVariableDescription const& Variable : Variables)
	{
		OutFunctions.AddDefaulted_GetRef()
			.SetName(FString::Printf(TEXT("Read%s"), *Variable.Name))
			.AddReturnType(Variable.ValueType);
	}
}

void UOptimusGraphDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	FShaderParametersMetadataBuilder Builder;
	for (FOptimusGraphVariableDescription const& Variable : Variables)
	{
		AddParamForType(Builder, *Variable.Name, Variable.ValueType);
	}

	// Todo[CF]: This leaks! Provide an allocator to this function to collect stuff like this (and maybe UID TCHAR allocation).
	FShaderParametersMetadata* ShaderParameterMetadata = Builder.Build(FShaderParametersMetadata::EUseCase::ShaderParameterStruct, TEXT("UGraphDataInterface"));

	OutBuilder.AddNestedStruct(UID, ShaderParameterMetadata);
}

void UOptimusGraphDataInterface::GetHLSL(FString& OutHLSL) const
{
	// Need include for DI_LOCAL macro expansion.
	OutHLSL += TEXT("#include \"/Plugin/ComputeFramework/Private/ComputeKernelCommon.ush\"\n");
	// Add uniforms.
	for (FOptimusGraphVariableDescription const& Variable : Variables)
	{
		OutHLSL += FString::Printf(TEXT("float DI_LOCAL(%s);\n"), *Variable.Name);
	}
	// Add function getters.
	for (FOptimusGraphVariableDescription const& Variable : Variables)
	{
		OutHLSL += FString::Printf(TEXT("DI_IMPL_READ(Read%s, float, )\n{\n\treturn DI_LOCAL(%s);\n}\n"), *Variable.Name, *Variable.Name);
	}
}

void UOptimusGraphDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkinnedMeshComponent::StaticClass());
}

UComputeDataProvider* UOptimusGraphDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UOptimusGraphDataProvider* Provider = NewObject<UOptimusGraphDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkinnedMeshComponent = Cast<USkinnedMeshComponent>(InSourceObjects[0]);
		Provider->Variables = Variables;
		Provider->ParameterBufferSize = ParameterBufferSize;
	}

	return Provider;
}


void UOptimusGraphDataProvider::SetConstant(FString const& InVariableName, TArray<uint8> const& InValue)
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

FComputeDataProviderRenderProxy* UOptimusGraphDataProvider::GetRenderProxy()
{
	UOptimusDeformerInstance* DeformerInstance = Cast<UOptimusDeformerInstance>(SkinnedMeshComponent->MeshDeformerInstance);

	return new FOptimusGraphDataProviderProxy(DeformerInstance, Variables, ParameterBufferSize);
}


FOptimusGraphDataProviderProxy::FOptimusGraphDataProviderProxy(UOptimusDeformerInstance const* DeformerInstance, TArray<FOptimusGraphVariableDescription> const& Variables, int32 ParameterBufferSize)
{
	// Get all variables from deformer instance and fill buffer.
	ParameterData.AddZeroed(ParameterBufferSize);

	if (DeformerInstance == nullptr)
	{
		return;
	}

	TArray<UOptimusVariableDescription*> const& VariableValues = DeformerInstance->GetVariables();
	for (FOptimusGraphVariableDescription const& Variable : Variables)
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

void FOptimusGraphDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
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
