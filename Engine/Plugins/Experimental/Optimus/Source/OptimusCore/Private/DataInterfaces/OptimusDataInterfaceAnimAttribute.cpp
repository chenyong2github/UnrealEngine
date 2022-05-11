// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceAnimAttribute.h"

#include "OptimusDataDomain.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusHelpers.h"
#include "OptimusValueContainer.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ComputeFramework/ShaderParameterMetadataAllocation.h"
#include "Animation/BuiltInAttributeTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "ShaderParameterMetadataBuilder.h"

static const FString PinNameDelimiter = TEXT(" - ");
static const FString HlslIdDelimiter = TEXT("_");



FOptimusAnimAttributeDescription& FOptimusAnimAttributeDescription::Init(UOptimusAnimAttributeDataInterface* InOwner,const FString& InName, FName InBoneName,
	const FOptimusDataTypeRef& InDataType)
{
	Name = InName;
	BoneName = InBoneName;
	DataType = InDataType;
	DefaultValue = UOptimusValueContainer::MakeValueContainer(InOwner, InDataType);

	// Caller should ensure that the name is unique
	HlslId = InName;
	PinName = *InName;
	
	return *this;
}


void FOptimusAnimAttributeDescription::UpdatePinNameAndHlslId(bool bInIncludeBoneName, bool bInIncludeTypeName)
{
	PinName = *GetFormattedId(PinNameDelimiter, bInIncludeBoneName, bInIncludeTypeName);
	HlslId = GetFormattedId(HlslIdDelimiter, bInIncludeBoneName, bInIncludeTypeName);
}


FString FOptimusAnimAttributeDescription::GetFormattedId(
	const FString& InDelimiter, bool bInIncludeBoneName, bool bInIncludeTypeName) const
{
	FString UniqueId;
			
	if (bInIncludeBoneName)
	{
		if (BoneName != NAME_None)
		{
			 UniqueId += BoneName.ToString();
			 UniqueId += InDelimiter;
		}
	}
			
	if (bInIncludeTypeName)
	{
		 UniqueId += DataType.Resolve()->DisplayName.ToString();
		 UniqueId += InDelimiter;
	}

	UniqueId += Name;	

	return  UniqueId;
}

UOptimusAnimAttributeDataInterface::UOptimusAnimAttributeDataInterface()
{
}


#if WITH_EDITOR
void UOptimusAnimAttributeDataInterface::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	
	const FName BasePropertyName = (PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None);
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);
	
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
	{
		const int32 ChangedIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_STRING_CHECKED(FOptimusAnimAttributeArray, InnerArray));
		
		bool bHasAttributeIdChanged =
			(PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusAnimAttributeDescription, Name)) ||
			(PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusAnimAttributeDescription, BoneName))||
			(PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusDataTypeRef, TypeName));
				
		if (bHasAttributeIdChanged)
		{
			if(ensure(AttributeArray.IsValidIndex(ChangedIndex)))
			{
				FOptimusAnimAttributeDescription& ChangedAttribute = AttributeArray[ChangedIndex];

				if (ChangedAttribute.Name.IsEmpty())
				{
					ChangedAttribute.Name = TEXT("EmptyName");
				}
				
				for (int32 Index = 0; Index < AttributeArray.Num(); Index++)
				{
					const FOptimusAnimAttributeDescription& Attribute = AttributeArray[Index];
					if (Index != ChangedIndex)
					{
						if (Attribute.Name == ChangedAttribute.Name &&
							Attribute.BoneName == ChangedAttribute.BoneName &&
							Attribute.DataType == ChangedAttribute.DataType )
						{
							// This particular change caused a Id clash, resolve it by changing the attribute name
							ChangedAttribute.Name = GetUnusedAttributeName(ChangedAttribute.Name);
						}
					}
				}

				UpdateAttributePinNamesAndHlslIds();
			}
		}

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(FOptimusDataTypeRef, TypeName))
		{
			FOptimusAnimAttributeDescription& ChangedAttribute = AttributeArray[ChangedIndex];

			// Update the default value container accordingly
			ChangedAttribute.DefaultValue = UOptimusValueContainer::MakeValueContainer(this, ChangedAttribute.DataType);
		}
	}
	else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
	{
		const int32 ChangedIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_STRING_CHECKED(FOptimusAnimAttributeArray, InnerArray));
		FOptimusAnimAttributeDescription& Attribute = AttributeArray[ChangedIndex];
		
		// Default to a float attribute
		Attribute.Init(this, GetUnusedAttributeName(TEXT("EmptyName")), NAME_None,
			FOptimusDataTypeRegistry::Get().FindType(*FFloatProperty::StaticClass()));
	}
	else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
	{
		const int32 ChangedIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_STRING_CHECKED(FOptimusAnimAttributeArray, InnerArray));
		FOptimusAnimAttributeDescription& Attribute = AttributeArray[ChangedIndex];
		
		Attribute.Name = GetUnusedAttributeName(Attribute.Name);
		Attribute.UpdatePinNameAndHlslId();
	}
}
#endif


FString UOptimusAnimAttributeDataInterface::GetDisplayName() const
{
	return TEXT("Animation Attributes");
}

TArray<FOptimusCDIPinDefinition> UOptimusAnimAttributeDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;

	for (int32 Index = 0; Index < AttributeArray.Num(); Index++)
	{
		const FOptimusAnimAttributeDescription& Attribute = AttributeArray[Index];
		Defs.Add({Attribute.PinName, FString::Printf(TEXT("Read%s"), *Attribute.HlslId)});
	}
	return Defs;
}

void UOptimusAnimAttributeDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	for (int32 Index = 0; Index < AttributeArray.Num(); Index++)
	{
		const FOptimusAnimAttributeDescription& Attribute = AttributeArray[Index];

		OutFunctions.AddDefaulted_GetRef()
		.SetName(FString::Printf(TEXT("Read%s"), *Attribute.HlslId))
		.AddReturnType(Attribute.DataType->ShaderValueType);
	}
}

void UOptimusAnimAttributeDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	FShaderParametersMetadataBuilder Builder;

	for (int32 Index = 0; Index < AttributeArray.Num(); Index++)
	{
		const FOptimusAnimAttributeDescription& Attribute = AttributeArray[Index];
		Optimus::AddParamForType(Builder, *Attribute.HlslId, Attribute.DataType->ShaderValueType);
	}

	FShaderParametersMetadata* ShaderParameterMetadata = Builder.Build(FShaderParametersMetadata::EUseCase::ShaderParameterStruct, TEXT("UAnimAttributeDataInterface"));

	InOutAllocations.ShaderParameterMetadatas.Add(ShaderParameterMetadata);

	// Add the generated nested struct to our builder.
	InOutBuilder.AddNestedStruct(UID, ShaderParameterMetadata);
}

void UOptimusAnimAttributeDataInterface::GetHLSL(FString& OutHLSL) const
{
	// Need include for DI_LOCAL macro expansion.
	OutHLSL += TEXT("#include \"/Plugin/ComputeFramework/Private/ComputeKernelCommon.ush\"\n");
	
	for (int32 Index = 0; Index < AttributeArray.Num(); Index++)
	{
		const FOptimusAnimAttributeDescription& Attribute = AttributeArray[Index];

		const FString TypeName = Attribute.DataType->ShaderValueType->ToString();

		if (ensure(!TypeName.IsEmpty()))
		{
			// Add uniforms.
			OutHLSL += FString::Printf(TEXT("%s DI_LOCAL(%s);\n"), *TypeName, *Attribute.HlslId);
				
			// Add function getters.
			OutHLSL += FString::Printf(TEXT("DI_IMPL_READ(Read%s, %s, )\n{\n\treturn DI_LOCAL(%s);\n}\n"),
				*Attribute.HlslId,
				*TypeName,
				*Attribute.HlslId);
		}
	}
}

UComputeDataProvider* UOptimusAnimAttributeDataInterface::CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const
{
	UOptimusAnimAttributeDataProvider* Provider = NewObject<UOptimusAnimAttributeDataProvider>();
	Provider->Init(Cast<USkeletalMeshComponent>(InBinding), AttributeArray.InnerArray);
	return Provider;
}

const FOptimusAnimAttributeDescription& UOptimusAnimAttributeDataInterface::AddAnimAttribute(const FString& InName, FName InBoneName,
	const FOptimusDataTypeRef& InDataType)
{
	return AttributeArray.InnerArray.AddDefaulted_GetRef()
		.Init(this, GetUnusedAttributeName(InName), InBoneName, InDataType);
}

FString UOptimusAnimAttributeDataInterface::GetUnusedAttributeName(const FString& InName) const
{
	TMap<FString, int32> AttributeNames;
	for (int32 Index = 0; Index < AttributeArray.Num(); Index++)
	{
		const FOptimusAnimAttributeDescription& Attribute = AttributeArray[Index];
						
		AttributeNames.FindOrAdd(Attribute.Name);
	}

	int32 Suffix = 0;
	FString NewName = InName;
	while (AttributeNames.Contains(NewName))
	{
		NewName = FString::Printf(TEXT("%s_%d"), *InName, Suffix);
		Suffix++;
	}

	return NewName;
}

void UOptimusAnimAttributeDataInterface::UpdateAttributePinNamesAndHlslIds()
{
	const int32 NumAttributes = AttributeArray.Num(); 

	TMap<FString, TArray<int32>> AttributesByName;

	for (int32 Index = 0; Index < NumAttributes; Index++)
	{
		const FOptimusAnimAttributeDescription& Attribute = AttributeArray[Index];
		AttributesByName.FindOrAdd(Attribute.Name).Add(Index);
	}

	for (const TTuple<FString, TArray<int32>>& AttributeGroup : AttributesByName)
	{
		// For attributes that share the same name, prepend type name or bone name
		// or both to make sure pin names are unique
		bool bMoreThanOneTypes = false;
		bool bMoreThanOneBones = false;

		TOptional<FOptimusDataTypeRef> LastType;
		TOptional<FName> LastBone;
		
		for (int32 Index : AttributeGroup.Value)
		{
			const FOptimusAnimAttributeDescription& Attribute = AttributeArray[Index];
			
			if (!LastBone.IsSet())
			{
				LastBone = Attribute.BoneName;
			}
			else if (Attribute.BoneName!= LastBone.GetValue())
			{
				bMoreThanOneBones = true;
			}
			
			if (!LastType.IsSet())
			{
				LastType = Attribute.DataType;
			}
			else if (Attribute.DataType != LastType.GetValue())
			{
				bMoreThanOneTypes = true;
			}

			if (bMoreThanOneBones && bMoreThanOneTypes)
			{
				break;
			}
		}
		
		for (int32 Index : AttributeGroup.Value)
		{
			FOptimusAnimAttributeDescription& Attribute = AttributeArray[Index];

			Attribute.UpdatePinNameAndHlslId(bMoreThanOneBones, bMoreThanOneTypes);
		}
	}
}


FOptimusAnimAttributeRuntimeData::FOptimusAnimAttributeRuntimeData(
	const FOptimusAnimAttributeDescription& InDescription)
{
	Name = *InDescription.Name;
	BoneName = InDescription.BoneName;
	DataType = InDescription.DataType;

	if (ensure(InDescription.DefaultValue->GetValueType() == DataType))
	{
		CachedDefaultValue = InDescription.DefaultValue->GetShaderValue();
	}

	Offset = 0;
	CachedBoneIndex = 0;
}

void UOptimusAnimAttributeDataProvider::Init(
	USkeletalMeshComponent* InSkeletalMesh,
	TArray<FOptimusAnimAttributeDescription> InAttributeArray
	)
{
	SkeletalMesh = InSkeletalMesh;

	// Convert description to runtime data
	for (const FOptimusAnimAttributeDescription& Attribute : InAttributeArray)
	{
		AttributeRuntimeData.Add(Attribute);
	}

	for (FOptimusAnimAttributeRuntimeData& Attribute : AttributeRuntimeData)
	{
		// Skip this step in case that there is no skeletal mesh, this can happen if
		// the preview scene does not have a preview mesh assigned
		if (SkeletalMesh && SkeletalMesh->SkeletalMesh)
		{
			if (Attribute.BoneName != NAME_None)
			{
				Attribute.CachedBoneIndex = SkeletalMesh->SkeletalMesh->GetRefSkeleton().FindBoneIndex(Attribute.BoneName);
			}
			else
			{
				// default to look for the attribute on the root bone
				Attribute.CachedBoneIndex = 0;
			}
		}
	}

	// Compute offset within the shader parameter buffer for each attribute
	FShaderParametersMetadataBuilder Builder;

	for (FOptimusAnimAttributeDescription& Attribute : InAttributeArray)
	{
		Optimus::AddParamForType(Builder, *Attribute.Name, Attribute.DataType->ShaderValueType);
	}

	const FShaderParametersMetadata* ShaderParameterMetadata = Builder.Build(FShaderParametersMetadata::EUseCase::ShaderParameterStruct, TEXT("UAnimAttributeDataInterface"));

	// Similar to UOptimusGraphDataInterface
	TArray<FShaderParametersMetadata::FMember> const& Members = ShaderParameterMetadata->GetMembers();
	for (int32 Index = 0; Index < AttributeRuntimeData.Num(); ++Index)
	{
		check(AttributeRuntimeData[Index].Name == Members[Index].GetName());
		AttributeRuntimeData[Index].Offset = Members[Index].GetOffset();
	}

	// Total Buffer size, used for validation
	AttributeBufferSize = ShaderParameterMetadata->GetSize();

	// Pre-allocate memory for attribute values
	AttributeBuffer.AddDefaulted(AttributeBufferSize);
}

bool UOptimusAnimAttributeDataProvider::IsValid() const
{
	return SkeletalMesh != nullptr;
}

FComputeDataProviderRenderProxy* UOptimusAnimAttributeDataProvider::GetRenderProxy()
{
	// AttributeBuffer should have been allocated in UOptimusAnimAttributeDataProvider::Init
	check(AttributeBuffer.Num() == AttributeBufferSize);
	
	const UE::Anim::FMeshAttributeContainer& AttributeContainer = SkeletalMesh->GetCustomAttributes();
	
	for (int32 Index = 0; Index < AttributeRuntimeData.Num(); ++Index)
	{
		const FOptimusAnimAttributeRuntimeData& AttributeData = AttributeRuntimeData[Index];
		const UE::Anim::FAttributeId Id = {AttributeData.Name, FCompactPoseBoneIndex(AttributeData.CachedBoneIndex)} ;
		const int32 Offset = AttributeData.Offset;
				
		bool bIsSupportedType = false;
		bool bIsValueSet = false;

		const FShaderValueType& ShaderValueType = *AttributeData.DataType->ShaderValueType;

		if (ShaderValueType == *FShaderValueType::Get(EShaderFundamentalType::Int))
		{
			bIsSupportedType = true;
			if (const FIntegerAnimationAttribute* Attribute = AttributeContainer.Find<FIntegerAnimationAttribute>(Id))
			{
				bIsValueSet = true;
				*((int32*)(&AttributeBuffer[Offset])) = Attribute->Value;
			}
		}
		else if (ShaderValueType == *FShaderValueType::Get(EShaderFundamentalType::Float))
		{
			bIsSupportedType = true;
			if (const FFloatAnimationAttribute* Attribute = AttributeContainer.Find<FFloatAnimationAttribute>(Id))
			{
				bIsValueSet = true;
				*((float*)(&AttributeBuffer[Offset])) = Attribute->Value;
			}
		}
		else if (ShaderValueType == *FShaderValueType::Get(EShaderFundamentalType::Float, 4, 4))
		{
			bIsSupportedType = true;
			if (const FTransformAnimationAttribute* Attribute = AttributeContainer.Find<FTransformAnimationAttribute>(Id))
			{
				bIsValueSet = true;
				*((FMatrix44f*)(&AttributeBuffer[Offset])) = Optimus::ConvertFTransformToFMatrix44f(Attribute->Value);
			}
		}
		else if (ShaderValueType == *FShaderValueType::Get(EShaderFundamentalType::Float, 3))
		{
			bIsSupportedType = true;
			if (const FVectorAnimationAttribute* Attribute = AttributeContainer.Find<FVectorAnimationAttribute>(Id))
			{
				bIsValueSet = true;
				*((FVector3f*)(&AttributeBuffer[Offset])) = FVector3f(Attribute->Value);
			}
		}
		else if (ShaderValueType == *FShaderValueType::Get(EShaderFundamentalType::Float, 4))
		{
			bIsSupportedType = true;
			if (const FQuaternionAnimationAttribute* Attribute = AttributeContainer.Find<FQuaternionAnimationAttribute>(Id))
			{
				bIsValueSet = true;
				*((FQuat4f*)(&AttributeBuffer[Offset])) = FQuat4f(Attribute->Value);
			}
		}

		// Use the default value if the attribute was not found
		if (bIsSupportedType && !bIsValueSet)
		{
			const uint8* DefaultValuePtr = AttributeData.CachedDefaultValue.GetData();
			const uint32 DefaultValueSize = AttributeData.CachedDefaultValue.Num();
			FMemory::Memcpy(&AttributeBuffer[AttributeData.Offset], DefaultValuePtr, DefaultValueSize);
		}	
	}
	
	return new FOptimusAnimAttributeDataProviderProxy(AttributeBuffer, AttributeBufferSize);
}

FOptimusAnimAttributeDataProviderProxy::FOptimusAnimAttributeDataProviderProxy(TArray<uint8> InAttributeBuffer,
	int32 InAttributeBufferSize) :
	AttributeBuffer(InAttributeBuffer) ,
	AttributeBufferSize(InAttributeBufferSize)
{
}

void FOptimusAnimAttributeDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == AttributeBufferSize))
	{
		return;
	}

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		uint8* ParameterBuffer = (InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		
		FMemory::Memcpy(ParameterBuffer, AttributeBuffer.GetData(), InDispatchSetup.ParameterStructSizeForValidation);
	}
}
