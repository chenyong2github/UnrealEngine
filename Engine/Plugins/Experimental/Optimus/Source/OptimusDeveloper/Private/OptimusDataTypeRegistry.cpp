// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataTypeRegistry.h"

#include "OptimusDeveloperModule.h"
#include "Types/OptimusType_MeshAttribute.h"
#include "Types/OptimusType_MeshSkinWeights.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UnrealType.h"


void FOptimusDataTypeRegistry::RegisterBuiltinTypes()
{
	// Register standard UE types and their mappings to the compute framework types.
	FOptimusDataTypeRegistry& Registry = FOptimusDataTypeRegistry::Get();

	// NOTE: The pin categories should match the PC_* ones in EdGraphSchema_K2.cpp for the 
	// fundamental types.
	// FIXME: Turn this into an array and separate out to own file.
	const bool bShowElements = true;
	const bool bHideElements = false;


	// bool -> bool
	Registry.RegisterType(
	    *FBoolProperty::StaticClass(),
	    FShaderValueType::Get(EShaderFundamentalType::Bool),
	    FName(TEXT("bool")), {},
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// int -> int
	Registry.RegisterType(
	    *FIntProperty::StaticClass(),
	    FShaderValueType::Get(EShaderFundamentalType::Int),
	    FName(TEXT("int")), {}, 
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// float -> float
	Registry.RegisterType(
	    *FFloatProperty::StaticClass(),
	    FShaderValueType::Get(EShaderFundamentalType::Float),
	    FName(TEXT("float")), {}, 
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// FVector2D -> float2
	Registry.RegisterType(
	    TBaseStructure<FVector2D>::Get(),
	    FShaderValueType::Get(EShaderFundamentalType::Float, 2),
	    {},
	    bShowElements,
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// FVector -> float3
	Registry.RegisterType(
		TBaseStructure<FVector>::Get(),
	    FShaderValueType::Get(EShaderFundamentalType::Float, 3),
		{},
		bShowElements,
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// FLinearColor -> float4
	Registry.RegisterType(
	    TBaseStructure<FLinearColor>::Get(),
	    FShaderValueType::Get(EShaderFundamentalType::Float, 4),
	    {},
	    bShowElements,
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// FRotator -> float3x3
	Registry.RegisterType(
	    TBaseStructure<FRotator>::Get(),
	    FShaderValueType::Get(EShaderFundamentalType::Float, 3, 3),
	    {},
	    bShowElements,
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// FTransform -> float4x4
	Registry.RegisterType(
	    TBaseStructure<FTransform>::Get(),
	    FShaderValueType::Get(EShaderFundamentalType::Float, 4, 4),
	    {},
	    bHideElements,
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

// 	Registry.RegisterType(
// 	    TBaseStructure<FTransform>::Get(),
// 	    FShaderValueType::Get(FName("transform"), 
// 			{{FName("trn"), FShaderValueType::Get(EShaderFundamentalType::Float, 3)},
// 			 {FName("scl"), FShaderValueType::Get(EShaderFundamentalType::Float, 3)},
// 			 {FName("rot"), FShaderValueType::Get(EShaderFundamentalType::Float, 4)}}),
// 	    {},
// 	    bShowElements,
// 	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// String types
	Registry.RegisterType(
	    *FNameProperty::StaticClass(),
	    FShaderValueTypeHandle(),
	    FName(TEXT("name")), 
		{},
	    EOptimusDataTypeUsageFlags::Variable);

	Registry.RegisterType(
	    *FStrProperty::StaticClass(),
	    FShaderValueTypeHandle(),
	    FName(TEXT("string")),
	    {},
	    EOptimusDataTypeUsageFlags::Variable);

	// UObject types
	Registry.RegisterType(
		USkeletalMesh::StaticClass(),
	    FLinearColor::White,
	    EOptimusDataTypeUsageFlags::Variable);

	Registry.RegisterType(
	    UOptimusType_MeshAttribute::StaticClass(),
	    FLinearColor(0.4f, 0.4f, 0.8f, 1.0f),
	    EOptimusDataTypeUsageFlags::Node);

	Registry.RegisterType(
	    UOptimusType_MeshSkinWeights::StaticClass(),
	    FLinearColor(0.4f, 0.8f, 0.8f, 1.0f),
	    EOptimusDataTypeUsageFlags::Node);

	Registry.RegisterType(
	    USkeleton::StaticClass(),
	    FLinearColor(0.4f, 0.8f, 0.4f, 1.0f),
	    EOptimusDataTypeUsageFlags::Node);
}


FOptimusDataTypeRegistry::~FOptimusDataTypeRegistry() = default;


FOptimusDataTypeRegistry& FOptimusDataTypeRegistry::Get()
{
	static FOptimusDataTypeRegistry Singleton;

	return Singleton;
}

bool FOptimusDataTypeRegistry::RegisterType(
	FName InTypeName, 
	TFunction<void(FOptimusDataType&)> InFillFunc
	)
{
	if (InTypeName == NAME_None)
	{
		UE_LOG(LogOptimusDeveloper, Error, TEXT("Invalid type name."));
		return false;
	}

	if (RegisteredTypes.Contains(InTypeName))
	{
		UE_LOG(LogOptimusDeveloper, Error, TEXT("Type '%s' is already registered."), *InTypeName.ToString());
		return false;
	}

	FOptimusDataType* DataType = new FOptimusDataType;
	InFillFunc(*DataType);
	RegisteredTypes.Emplace(InTypeName, DataType);
	RegistrationOrder.Add(InTypeName);
	return true;
}


bool FOptimusDataTypeRegistry::RegisterType(
	const FFieldClass &InFieldType,
	FShaderValueTypeHandle InShaderValueType,
	FName InPinCategory,
	TOptional<FLinearColor> InPinColor,
	EOptimusDataTypeUsageFlags InUsageFlags
	)
{
	return RegisterType(InFieldType.GetFName(), [&](FOptimusDataType& InDataType) {
		InDataType.TypeName = InFieldType.GetFName();
		InDataType.ShaderValueType = InShaderValueType;
		InDataType.TypeCategory = InPinCategory;
		if (InPinColor.IsSet())
		{
			InDataType.bHasCustomPinColor = true;
			InDataType.CustomPinColor = *InPinColor;
		}
		InDataType.UsageFlags = InUsageFlags;
	});
}


bool FOptimusDataTypeRegistry::RegisterType(
    UScriptStruct *InStructType,
    FShaderValueTypeHandle InShaderValueType,
    TOptional<FLinearColor> InPinColor,
    bool bInShowElements,
    EOptimusDataTypeUsageFlags InUsageFlags
	)
{
	if (ensure(InStructType))
	{
		// If showing elements, the sub-elements have to be registered already.
		if (bInShowElements)
		{
			for (const FProperty* Property : TFieldRange<FProperty>(InStructType))
			{
				if (FindType(*Property) == nullptr)
				{
					UE_LOG(LogOptimusDeveloper, Error, TEXT("Found un-registered sub-element '%s' when registering '%s'"),
						*(Property->GetClass()->GetName()), *InStructType->GetName());
					return false;
				}
			}
		}

		FName TypeName(*FString::Printf(TEXT("F%s"), *InStructType->GetName()));

		return RegisterType(TypeName, [&](FOptimusDataType& InDataType) {
			InDataType.TypeName = TypeName;
			InDataType.ShaderValueType = InShaderValueType;
			InDataType.TypeCategory = FName(TEXT("struct"));
			InDataType.TypeObject = InStructType;
			if (InPinColor.IsSet())
			{
				InDataType.bHasCustomPinColor = true;
				InDataType.CustomPinColor = *InPinColor;
			}
			InDataType.UsageFlags = InUsageFlags;
			InDataType.TypeFlags |= EOptimusDataTypeFlags::IsStructType;
			if (bInShowElements)
			{
				InDataType.TypeFlags |= EOptimusDataTypeFlags::ShowElements;
			}
		});
	}
	else
	{
		return false;
	}
}


bool FOptimusDataTypeRegistry::RegisterType(
	UClass* InClassType, 
    TOptional<FLinearColor> InPinColor,
	EOptimusDataTypeUsageFlags InUsageFlags
	)
{
	if (ensure(InClassType))
	{
		FName TypeName(*FString::Printf(TEXT("U%s"), *InClassType->GetName()));

		return RegisterType(TypeName, [&](FOptimusDataType& InDataType) {
				InDataType.TypeName = TypeName;
				InDataType.TypeCategory = FName(TEXT("object"));
				InDataType.TypeObject = InClassType;
				InDataType.bHasCustomPinColor = true;
			    if (InPinColor.IsSet())
			    {
				    InDataType.bHasCustomPinColor = true;
				    InDataType.CustomPinColor = *InPinColor;
			    }
				InDataType.UsageFlags = InUsageFlags;
			});
	}
	else
	{
		return false;
	}
}

bool FOptimusDataTypeRegistry::RegisterType(
    FName InTypeName,
    FShaderValueTypeHandle InShaderValueType,
    FName InPinCategory,
    UObject* InPinSubCategory,
    FLinearColor InPinColor,
    EOptimusDataTypeUsageFlags InUsageFlags
	)
{
	if (EnumHasAnyFlags(InUsageFlags, EOptimusDataTypeUsageFlags::Variable))
	{
		UE_LOG(LogOptimusDeveloper, Error, TEXT("Can't register '%s' for use in variables when there is no associated native type."), *InTypeName.ToString());
		return false;
	}

	return RegisterType(InTypeName, [&](FOptimusDataType& InDataType) {
		InDataType.TypeName = InTypeName;
		InDataType.ShaderValueType = InShaderValueType;
		InDataType.TypeCategory = InPinCategory;
		InDataType.TypeObject = InPinSubCategory;
		InDataType.bHasCustomPinColor = true;
		InDataType.CustomPinColor = InPinColor;
		InDataType.UsageFlags = InUsageFlags;
	});
}


TArray<FOptimusDataTypeHandle> FOptimusDataTypeRegistry::GetAllTypes() const
{
	TArray<FOptimusDataTypeHandle> Result;
	for (const FName& TypeName : RegistrationOrder)
	{
		Result.Add(RegisteredTypes[TypeName]);
	}
	return Result;
}


FOptimusDataTypeHandle FOptimusDataTypeRegistry::FindType(const FProperty& InProperty) const
{
	if (const FStructProperty* StructProperty = CastField<const FStructProperty>(&InProperty))
	{
		FName TypeName(*FString::Printf(TEXT("F%s"), *StructProperty->Struct->GetName()));
		return FindType(TypeName);
	}
	else if (const FObjectProperty* ObjectProperty = CastField<const FObjectProperty>(&InProperty))
	{
		FName TypeName(*FString::Printf(TEXT("U%s"), *ObjectProperty->PropertyClass->GetName()));
		return FindType(TypeName);
	}
	else
	{
		return FindType(*InProperty.GetClass());
	}

#if 0
	const FProperty* PropertyForType = &InProperty;
	const FArrayProperty* ArrayProperty = CastField<const FArrayProperty>(PropertyForType);
	if (ArrayProperty)
	{
		PropertyForType = ArrayProperty->Inner;
	}

	if (const FStructProperty* StructProperty = CastField<const FStructProperty>(PropertyForType))
	{
		TypeClass = StructProperty->Struct->GetClass();
	}
	else if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(PropertyForType))
	{
		TypeClass = EnumProperty->GetEnum()->GetClass();
	}
	else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(PropertyForType))
	{
		TypeClass = ByteProperty->Enum->GetClass();
	}
#endif
}


FOptimusDataTypeHandle FOptimusDataTypeRegistry::FindType(const FFieldClass& InFieldType) const
{
	return FindType(InFieldType.GetFName());
}


FOptimusDataTypeHandle FOptimusDataTypeRegistry::FindType(FName InTypeName) const
{
	const FOptimusDataTypeHandle* ResultPtr = RegisteredTypes.Find(InTypeName);
	return ResultPtr ? *ResultPtr : FOptimusDataTypeHandle();
}


void FOptimusDataTypeRegistry::UnregisterAllTypes()
{
	FOptimusDataTypeRegistry& Registry = FOptimusDataTypeRegistry::Get();

	Registry.RegisteredTypes.Reset();
}
