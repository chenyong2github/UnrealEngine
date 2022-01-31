// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataTypeRegistry.h"

#include "OptimusCoreModule.h"

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

template<typename SourceT, typename DestT>
static bool ConvertPropertyValuePOD(
	TArrayView<const uint8> InRawValue,
	TArray<uint8>& OutShaderValue
	)
{
	if (!ensure(InRawValue.Num() == sizeof(SourceT)))
	{
		return false;
	}
	const int32 Offset = OutShaderValue.Num();
	OutShaderValue.AddUninitialized(sizeof(DestT));
	*reinterpret_cast<DestT*>(OutShaderValue.GetData() + Offset) = static_cast<DestT>(*reinterpret_cast<const SourceT*>(InRawValue.GetData()));
	return true;
}


void FOptimusDataTypeRegistry::RegisterBuiltinTypes()
{
	// Register standard UE types and their mappings to the compute framework types.
	FOptimusDataTypeRegistry& Registry = FOptimusDataTypeRegistry::Get();

	// NOTE: The pin categories should match the PC_* ones in EdGraphSchema_K2.cpp for the 
	// fundamental types.
	// FIXME: Turn this into an array and separate out to own file.
	constexpr bool bShowElements = true;
	constexpr bool bHideElements = false;


	// bool -> bool
	Registry.RegisterType(
	    *FBoolProperty::StaticClass(),
	    FText::FromString(TEXT("Bool")),
	    FShaderValueType::Get(EShaderFundamentalType::Bool),
		[](UStruct *InScope, FName InName) {
		    auto Prop = new FBoolProperty(InScope, InName, RF_Public);
		    Prop->SetBoolSize(sizeof(bool), true);
			return Prop;
		},
		ConvertPropertyValuePOD<bool, int32>,
		FName(TEXT("bool")), {},
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// int -> int
	Registry.RegisterType(
	    *FIntProperty::StaticClass(),
	    FText::FromString(TEXT("Int")),
	    FShaderValueType::Get(EShaderFundamentalType::Int),
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FIntProperty(InScope, InName, RF_Public);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
		    return Prop;
	    },
		ConvertPropertyValuePOD<int32, int32>,
		FName(TEXT("int")), {}, 
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// int -> int
	Registry.RegisterType(
		*FUInt32Property::StaticClass(),
	    FText::FromString(TEXT("Unsigned Int")),
		FShaderValueType::Get(EShaderFundamentalType::Uint),
		[](UStruct* InScope, FName InName) {
			auto Prop = new FUInt32Property(InScope, InName, RF_Public);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		},
		ConvertPropertyValuePOD<uint32, uint32>,
		FName(TEXT("uint")), FLinearColor(0.0275f, 0.733, 0.820f, 1.0f), 
		EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);
	
	// float -> float
	Registry.RegisterType(
	    *FFloatProperty::StaticClass(),
	    FText::FromString(TEXT("Float")),
	    FShaderValueType::Get(EShaderFundamentalType::Float),
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FFloatProperty(InScope, InName, RF_Public);
		    Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
#if WITH_EDITOR
	    	Prop->SetMetaData(TEXT("UIMin"), TEXT("0.0"));
	    	Prop->SetMetaData(TEXT("UIMax"), TEXT("1.0"));
	    	Prop->SetMetaData(TEXT("SupportDynamicSliderMinValue"), TEXT("true"));
	    	Prop->SetMetaData(TEXT("SupportDynamicSliderMaxValue"), TEXT("true"));
#endif
		    return Prop;
	    },
		ConvertPropertyValuePOD<float, float>,
		FName(TEXT("float")), {}, 
	    EOptimusDataTypeUsageFlags::Resource | EOptimusDataTypeUsageFlags::Variable);

	// double -> float 
	Registry.RegisterType(
	    *FDoubleProperty::StaticClass(),
	    FText::FromString(TEXT("Double")),
	    FShaderValueType::Get(EShaderFundamentalType::Float),
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FDoubleProperty(InScope, InName, RF_Public);
		    Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
#if WITH_EDITOR
	    	Prop->SetMetaData(TEXT("UIMin"), TEXT("0.0"));
			Prop->SetMetaData(TEXT("UIMax"), TEXT("1.0"));
			Prop->SetMetaData(TEXT("SupportDynamicSliderMinValue"), TEXT("true"));
			Prop->SetMetaData(TEXT("SupportDynamicSliderMaxValue"), TEXT("true"));
#endif
			return Prop;
	    },
		ConvertPropertyValuePOD<double, float>,
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

	// FVector4 -> float4
	Registry.RegisterType(
	    TBaseStructure<FVector4>::Get(),
	    FShaderValueType::Get(EShaderFundamentalType::Float, 4),
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
	    FText::FromString(TEXT("Name")),
	    FShaderValueTypeHandle(),
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FNameProperty(InScope, InName, RF_Public);
		    Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
		    return Prop;
	    },
	    {},
	    /*
	    [](const uint8* InRawValue, TArray<uint8>& OutShaderValue) -> const uint8 * {
			const int32 Offset = OutShaderValue.Num();
			OutShaderValue.AddUninitialized(4);
			*reinterpret_cast<int32*>(OutShaderValue.GetData() + Offset) = (*reinterpret_cast<const FName*>(InRawValue)).GetComparisonIndex();
			return InRawValue + sizeof(FName);
		},
		*/
	    FName(TEXT("name")), 
		{},
	    EOptimusDataTypeUsageFlags::Variable);

	Registry.RegisterType(
	    *FStrProperty::StaticClass(),
	    FText::FromString(TEXT("String")),
	    FShaderValueTypeHandle(),
	    [](UStruct* InScope, FName InName) {
		    auto Prop = new FStrProperty(InScope, InName, RF_Public);
		    Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
		    return Prop;
	    },
	    {},	// No conversion function.	
	    FName(TEXT("string")),
	    {},
	    EOptimusDataTypeUsageFlags::Variable);

	// UObject types
	/*
	Registry.RegisterType(
		USkeletalMesh::StaticClass(),
	    FLinearColor::White,
	    EOptimusDataTypeUsageFlags::Variable);
	*/

	// HLSL types
	Registry.RegisterType(
		FName("3x4 Float"),
		FText::FromString(TEXT("Matrix 3x4")),
		FShaderValueType::Get(EShaderFundamentalType::Float, 3, 4),
		FName("float3x4"),
		nullptr,
		FLinearColor(0.7f, 0.3f, 0.4f, 1.0f),
		EOptimusDataTypeUsageFlags::Resource);

	// FIXME: Add type aliases (e.g. "3x4 Float" above should really be "float3x4")
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
	PropertyCreateFuncT InPropertyCreateFunc,
	PropertyValueConvertFuncT InPropertyValueConvertFunc
	)
{
	if (InTypeName == NAME_None)
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Invalid type name."));
		return false;
	}

	if (RegisteredTypes.Contains(InTypeName))
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Type '%s' is already registered."), *InTypeName.ToString());
		return false;
	}

	auto DataType = MakeShared<FOptimusDataType>();

	FTypeInfo Info{DataType, InPropertyCreateFunc, InPropertyValueConvertFunc};
	InFillFunc(*DataType);
	RegisteredTypes.Add(InTypeName, Info);
	RegistrationOrder.Add(InTypeName);
	return true;
}


bool FOptimusDataTypeRegistry::RegisterType(
	const FFieldClass &InFieldType,
	const FText& InDisplayName,
	FShaderValueTypeHandle InShaderValueType,
    PropertyCreateFuncT InPropertyCreateFunc,
	PropertyValueConvertFuncT InPropertyValueConvertFunc,
	FName InPinCategory,
	TOptional<FLinearColor> InPinColor,
	EOptimusDataTypeUsageFlags InUsageFlags
	)
{
	return RegisterType(InFieldType.GetFName(), [&](FOptimusDataType& InDataType) {
		InDataType.TypeName = InFieldType.GetFName();
		InDataType.DisplayName = InDisplayName;
		InDataType.ShaderValueType = InShaderValueType;
		InDataType.TypeCategory = InPinCategory;
		if (InPinColor.IsSet())
		{
			InDataType.bHasCustomPinColor = true;
			InDataType.CustomPinColor = *InPinColor;
		}
		InDataType.UsageFlags = InUsageFlags;
	},
	InPropertyCreateFunc,
	InPropertyValueConvertFunc
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
					UE_LOG(LogOptimusCore, Error, TEXT("Found un-registered sub-element '%s' when registering '%s'"),
						*(Property->GetClass()->GetName()), *InStructType->GetName());
					return false;
				}
			}
		}

		FName TypeName(*FString::Printf(TEXT("F%s"), *InStructType->GetName()));

		PropertyCreateFuncT PropertyCreateFunc;
		PropertyValueConvertFuncT PropertyValueConvertFunc;
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

			PropertyValueConvertFunc = [InStructType, this](TArrayView<const uint8> InRawValue, TArray<uint8>& OutShaderValue) -> bool
			{
				const uint8* RawValuePtr = InRawValue.GetData();
				int32 BytesRemaining = InRawValue.Num();
				
				for (const FProperty* Property : TFieldRange<FProperty>(InStructType))
				{
					FOptimusDataTypeHandle TypeHandle = FindType(*Property);
					if (!TypeHandle.IsValid())
					{
						UE_LOG(LogOptimusCore, Error, TEXT("Found un-registered sub-element '%s' when converting '%s'"),
							*(Property->GetClass()->GetName()), *InStructType->GetName());
						return false;
					}

					auto ConversionFunc = FindPropertyValueConvertFunc(TypeHandle->TypeName);
					if (!ConversionFunc)
					{
						UE_LOG(LogOptimusCore, Error, TEXT("Sub-element '%s' has no conversion when converting '%s'"),
							*(Property->GetClass()->GetName()), *InStructType->GetName());
						return false;
					}
					if (!ConversionFunc(TArrayView<const uint8>(RawValuePtr, Property->GetSize()), OutShaderValue))
					{
						return false;
					}
					RawValuePtr += Property->GetSize();
					BytesRemaining -= Property->GetSize();
					if (!ensure(BytesRemaining >= 0))
					{
						return false;
					}
				}
				return true;
			};
		}

		return RegisterType(TypeName, [&](FOptimusDataType& InDataType) {
			InDataType.TypeName = TypeName;
#if WITH_EDITOR
			InDataType.DisplayName = InStructType->GetDisplayNameText();
#else
			InDataType.DisplayName = FText::FromName(InStructType->GetFName());
#endif
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
			}, PropertyCreateFunc, PropertyValueConvertFunc);
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
#if WITH_EDITOR
				InDataType.DisplayName = InClassType->GetDisplayNameText();
#else
				InDataType.DisplayName = FText::FromName(InClassType->GetFName());
#endif
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
	const FText& InDisplayName,
    FShaderValueTypeHandle InShaderValueType,
    FName InPinCategory,
    UObject* InPinSubCategory,
    FLinearColor InPinColor,
    EOptimusDataTypeUsageFlags InUsageFlags
	)
{
	if (EnumHasAnyFlags(InUsageFlags, EOptimusDataTypeUsageFlags::Variable))
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Can't register '%s' for use in variables when there is no associated native type."), *InTypeName.ToString());
		return false;
	}

	return RegisterType(InTypeName, [&](FOptimusDataType& InDataType) {
		InDataType.TypeName = InTypeName;
		InDataType.DisplayName = InDisplayName;
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


FOptimusDataTypeRegistry::PropertyCreateFuncT FOptimusDataTypeRegistry::FindPropertyCreateFunc(
	FName InTypeName
	) const
{
	const FTypeInfo* InfoPtr = RegisteredTypes.Find(InTypeName);
	if (!InfoPtr)
	{
		UE_LOG(LogOptimusCore, Fatal, TEXT("CreateProperty: Invalid type name."));
		return nullptr;
	}

	return InfoPtr->PropertyCreateFunc;
}


FOptimusDataTypeRegistry::PropertyValueConvertFuncT FOptimusDataTypeRegistry::FindPropertyValueConvertFunc(
	FName InTypeName
	) const
{
	const FTypeInfo* InfoPtr = RegisteredTypes.Find(InTypeName);
	if (!InfoPtr)
	{
		UE_LOG(LogOptimusCore, Fatal, TEXT("CreateProperty: Invalid type name."));
		return nullptr;
	}

	return InfoPtr->PropertyValueConvertFunc;
}
