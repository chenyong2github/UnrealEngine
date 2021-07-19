// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataTypeRegistry.h"

#include "OptimusDeveloperModule.h"
#include "Types/OptimusType_MeshAttribute.h"
#include "Types/OptimusType_MeshSkinWeights.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UnrealType.h"


static bool IsStructHashable(const UScriptStruct* InStructType)
{
	if (InStructType->IsNative())
	{
		return InStructType->GetCppStructOps() && InStructType->GetCppStructOps()->HasGetTypeHash();
	}
	else
	{
		for (TFieldIterator<FProperty> It(InStructType); It; ++It)
		{
			if (CastField<FBoolProperty>(*It))
			{
				continue;
			}
			else if (!It->HasAllPropertyFlags(CPF_HasGetValueTypeHash))
			{
				return false;
			}
		}
		return true;
	}
}


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
		[](UStruct *InScope, FName InName) {
		    auto Prop = new FBoolProperty(InScope, InName, RF_Public);
		    Prop->SetBoolSize(sizeof(bool), true);
			return Prop;
		},
	    FName(TEXT("bool")), {},
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// int -> int
	Registry.RegisterType(
	    *FIntProperty::StaticClass(),
	    FShaderValueType::Get(EShaderFundamentalType::Int),
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FIntProperty(InScope, InName, RF_Public);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
		    return Prop;
	    },
	    FName(TEXT("int")), {}, 
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// int -> int
	Registry.RegisterType(
		*FUInt32Property::StaticClass(),
		FShaderValueType::Get(EShaderFundamentalType::Uint),
		[](UStruct* InScope, FName InName) {
			auto Prop = new FUInt32Property(InScope, InName, RF_Public);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		},
		FName(TEXT("uint")), FLinearColor(0.0275f, 0.733, 0.820f, 1.0f), 
		EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);
	
	// float -> float
	Registry.RegisterType(
	    *FFloatProperty::StaticClass(),
	    FShaderValueType::Get(EShaderFundamentalType::Float),
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FFloatProperty(InScope, InName, RF_Public);
		    Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
		    return Prop;
	    },
	    FName(TEXT("float")), {}, 
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// double -> float 
	Registry.RegisterType(
	    *FDoubleProperty::StaticClass(),
	    FShaderValueType::Get(EShaderFundamentalType::Float),
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FDoubleProperty(InScope, InName, RF_Public);
		    Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
		    return Prop;
	    },
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
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FNameProperty(InScope, InName, RF_Public);
		    Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
		    return Prop;
	    },
	    FName(TEXT("name")), 
		{},
	    EOptimusDataTypeUsageFlags::Variable);

	Registry.RegisterType(
	    *FStrProperty::StaticClass(),
	    FShaderValueTypeHandle(),
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FStrProperty(InScope, InName, RF_Public);
		    Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
		    return Prop;
	    },
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

	// HLSL types
	Registry.RegisterType(
		FName("3x4 Float"),
		FShaderValueType::Get(EShaderFundamentalType::Float, 3, 4),
		FName("float3x4"),
		nullptr,
		FLinearColor(0.7f, 0.3f, 0.4f, 1.0f),
		EOptimusDataTypeUsageFlags::Resource);	
}


FOptimusDataTypeRegistry::~FOptimusDataTypeRegistry() = default;


FOptimusDataTypeRegistry& FOptimusDataTypeRegistry::Get()
{
	static FOptimusDataTypeRegistry Singleton;

	return Singleton;
}

bool FOptimusDataTypeRegistry::RegisterType(
	FName InTypeName, 
	TFunction<void(FOptimusDataType&)> InFillFunc,
	PropertyCreateFuncT InPropertyCreateFunc
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

	auto DataType = MakeShared<FOptimusDataType>();

	FTypeInfo Info{DataType, InPropertyCreateFunc};
	InFillFunc(*DataType);
	RegisteredTypes.Add(InTypeName, Info);
	RegistrationOrder.Add(InTypeName);
	return true;
}


bool FOptimusDataTypeRegistry::RegisterType(
	const FFieldClass &InFieldType,
	FShaderValueTypeHandle InShaderValueType,
    PropertyCreateFuncT InPropertyCreateFunc,
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
	},
	InPropertyCreateFunc
	);
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

		PropertyCreateFuncT PropertyCreateFunc;
		if (EnumHasAnyFlags(InUsageFlags, EOptimusDataTypeUsageFlags::Variable))
		{
			const bool bIsHashable = IsStructHashable(InStructType);

			PropertyCreateFunc = [bIsHashable, InStructType](UStruct* InScope, FName InName) -> FProperty *
			{
				auto Prop = new FStructProperty(InScope, InName, RF_Public);
				Prop->Struct = InStructType;
				if (bIsHashable)
				{
					Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				}
				return Prop;
			};
		}

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
			}, PropertyCreateFunc);
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

		PropertyCreateFuncT PropertyCreateFunc;
		if (EnumHasAnyFlags(InUsageFlags, EOptimusDataTypeUsageFlags::Variable))
		{
			PropertyCreateFunc = [InClassType](UStruct* InScope, FName InName) -> FProperty* {
				auto Prop = new FObjectProperty(InScope, InName, RF_Public);
				Prop->SetPropertyClass(InClassType);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			};
		}

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
			}, PropertyCreateFunc);
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
		Result.Add(RegisteredTypes[TypeName].Handle);
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
	const FTypeInfo* InfoPtr = RegisteredTypes.Find(InTypeName);
	return InfoPtr ? InfoPtr->Handle : FOptimusDataTypeHandle();
}


FOptimusDataTypeHandle FOptimusDataTypeRegistry::FindType(FShaderValueTypeHandle InValueType) const
{
	for (const FName& TypeName : RegistrationOrder)
	{
		const FOptimusDataTypeHandle Handle = RegisteredTypes[TypeName].Handle;
		if (Handle->ShaderValueType == InValueType)
		{
			return Handle;
		}	
	}
	return {};
}


void FOptimusDataTypeRegistry::UnregisterAllTypes()
{
	FOptimusDataTypeRegistry& Registry = FOptimusDataTypeRegistry::Get();

	Registry.RegisteredTypes.Reset();
}


FProperty* FOptimusDataTypeRegistry::CreateProperty(
	FName InTypeName, 
	UStruct* InScope, 
	FName InName
	) const
{
	const FTypeInfo* InfoPtr = RegisteredTypes.Find(InTypeName);
	if (!InfoPtr)
	{
		UE_LOG(LogOptimusDeveloper, Fatal, TEXT("CreateProperty: Invalid type name."));
		return nullptr;
	}

	if (!InfoPtr->PropertyCreateFunc)
	{
		return nullptr;
	}

	return InfoPtr->PropertyCreateFunc(InScope, InName);
}
