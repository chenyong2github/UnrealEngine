// Copyright Epic Games, Inc. All Rights Reserved.

#include "Param/ParamHelpers.h"
#include "Param/ParamType.h"
#include "Param/ParamTypeHandle.h"
#include "Templates/SubclassOf.h"

namespace UE::AnimNext
{

FParamHelpers::ECopyResult FParamHelpers::Copy(const FAnimNextParamType& InSourceType, const FAnimNextParamType& InTargetType, TConstArrayView<uint8> InSourceMemory, TArrayView<uint8> InTargetMemory)
{
	if(InSourceType == InTargetType)
	{
		switch(InSourceType.ContainerType)
		{
		case FAnimNextParamType::EContainerType::Array:
			{
				const TScriptArray<FHeapAllocator>* SourceArray = reinterpret_cast<const TScriptArray<FHeapAllocator>*>(InSourceMemory.GetData());
				TScriptArray<FHeapAllocator>* TargetArray = reinterpret_cast<TScriptArray<FHeapAllocator>*>(InTargetMemory.GetData());

				const int32 NumElements = SourceArray->Num(); 
				const size_t ValueTypeSize = InSourceType.GetValueTypeSize();
				const size_t ValueTypeAlignment = InSourceType.GetValueTypeSize();

				// Reallocate target array
				TargetArray->SetNumUninitialized(NumElements, ValueTypeSize, ValueTypeAlignment);
				check(TargetArray->GetAllocatedSize(ValueTypeSize) == SourceArray->GetAllocatedSize(ValueTypeSize));

				// Perform the copy according to value type
				switch (InSourceType.ValueType)
				{
				case FAnimNextParamType::EValueType::None:
					ensureMsgf(false, TEXT("Trying to copy parameter of type None"));
					break;
				case FAnimNextParamType::EValueType::Bool:
				case FAnimNextParamType::EValueType::Byte:
				case FAnimNextParamType::EValueType::Int32:
				case FAnimNextParamType::EValueType::Int64:
				case FAnimNextParamType::EValueType::Float:
				case FAnimNextParamType::EValueType::Double:
				case FAnimNextParamType::EValueType::Name:
				case FAnimNextParamType::EValueType::Enum:
					{
						FMemory::Memcpy(TargetArray->GetData(), SourceArray->GetData(), SourceArray->GetAllocatedSize(ValueTypeSize));
					}
					break;
				case FAnimNextParamType::EValueType::String:
					{
						const FString* SourceString = static_cast<const FString*>(SourceArray->GetData());
						FString* TargetString = static_cast<FString*>(TargetArray->GetData());
						for(int32 Index = 0; Index < NumElements; ++Index)
						{
							*TargetString = *SourceString;
							++TargetString;
							++SourceString;
						}
					}
					break;
				case FAnimNextParamType::EValueType::Text:
					{
						const FText* SourceText = static_cast<const FText*>(SourceArray->GetData());
						FText* TargetText = static_cast<FText*>(TargetArray->GetData());
						for(int32 Index = 0; Index < NumElements; ++Index)
						{
							*TargetText = *SourceText;
							++TargetText;
							++SourceText;
						}
					}
					break;
				case FAnimNextParamType::EValueType::Struct:
					if(UScriptStruct* ScriptStruct = Cast<UScriptStruct>(InSourceType.ValueTypeObject.Get()))
					{
						ScriptStruct->CopyScriptStruct(TargetArray->GetData(), SourceArray->GetData(), NumElements);
					}
					else
					{
						checkf(false, TEXT("Error: FParameterHelpers::Copy: Unknown Struct Type"));
					}
					break;
				case FAnimNextParamType::EValueType::Object:
					{
						const TObjectPtr<UObject>* SourceObjectPtr = reinterpret_cast<const TObjectPtr<UObject>*>(InSourceMemory.GetData());
						TObjectPtr<UObject>* TargetObjectPtr = reinterpret_cast<TObjectPtr<UObject>*>(InTargetMemory.GetData());
						for(int32 Index = 0; Index < NumElements; ++Index)
						{
							*TargetObjectPtr = *SourceObjectPtr;
							++TargetObjectPtr;
							++SourceObjectPtr;
						}
					}
					break;
				case FAnimNextParamType::EValueType::SoftObject:
					{
						const TSoftObjectPtr<UObject>* SourceSoftObjectPtr = reinterpret_cast<const TSoftObjectPtr<UObject>*>(InSourceMemory.GetData());
						TSoftObjectPtr<UObject>* TargetSoftObjectPtr = reinterpret_cast<TSoftObjectPtr<UObject>*>(InTargetMemory.GetData());
						for(int32 Index = 0; Index < NumElements; ++Index)
						{
							*TargetSoftObjectPtr = *SourceSoftObjectPtr;
							++TargetSoftObjectPtr;
							++SourceSoftObjectPtr;
						}
					}
					break;
				case FAnimNextParamType::EValueType::Class:
					{
						const TSubclassOf<UObject>* SourceClassPtr = reinterpret_cast<const TSubclassOf<UObject>*>(InSourceMemory.GetData());
						TSubclassOf<UObject>* TargetClassPtr = reinterpret_cast<TSubclassOf<UObject>*>(InTargetMemory.GetData());
						for(int32 Index = 0; Index < NumElements; ++Index)
						{
							*TargetClassPtr = *SourceClassPtr;
							++TargetClassPtr;
							++SourceClassPtr;
						}
					}
					break;
				case FAnimNextParamType::EValueType::SoftClass:
					{
						const TSoftClassPtr<UObject>* SourceClassPtr = reinterpret_cast<const TSoftClassPtr<UObject>*>(InSourceMemory.GetData());
						TSoftClassPtr<UObject>* TargetClassPtr = reinterpret_cast<TSoftClassPtr<UObject>*>(InTargetMemory.GetData());
						for(int32 Index = 0; Index < NumElements; ++Index)
						{
							*TargetClassPtr = *SourceClassPtr;
							++TargetClassPtr;
							++SourceClassPtr;
						}
					}
					break;
				default:
					checkf(false, TEXT("Error: FParameterHelpers::Copy of unknown type"));
					break;
				}
			}
			break;
		case FAnimNextParamType::EContainerType::None:
			{
				switch (InSourceType.ValueType)
				{
				case FAnimNextParamType::EValueType::None:
					ensureMsgf(false, TEXT("Trying to copy parameter of type None"));
					break;
				case FAnimNextParamType::EValueType::Bool:
				case FAnimNextParamType::EValueType::Byte:
				case FAnimNextParamType::EValueType::Int32:
				case FAnimNextParamType::EValueType::Int64:
				case FAnimNextParamType::EValueType::Float:
				case FAnimNextParamType::EValueType::Double:
				case FAnimNextParamType::EValueType::Name:
				case FAnimNextParamType::EValueType::Enum:
					{
						const int32 ParamAlignment = InSourceType.GetAlignment();
						const int32 ParamSize = InSourceType.GetSize();
						const int32 ParamAllocSize = Align(ParamSize, ParamAlignment);
						check(InTargetMemory.Num() <= ParamAllocSize);
						check(InSourceMemory.Num() <= ParamAllocSize);
						check(IsAligned(InTargetMemory.GetData(), ParamAlignment));
						check(IsAligned(InSourceMemory.GetData(), ParamAlignment));

						FMemory::Memcpy(InTargetMemory.GetData(), InSourceMemory.GetData(), ParamAllocSize);
					}
					break;
				case FAnimNextParamType::EValueType::String:
					{
						const FString* SourceString = reinterpret_cast<const FString*>(InSourceMemory.GetData());
						FString* TargetString = reinterpret_cast<FString*>(InTargetMemory.GetData());
						*TargetString = *SourceString;
					}
					break;
				case FAnimNextParamType::EValueType::Text:
					{
						const FText* SourceText = reinterpret_cast<const FText*>(InSourceMemory.GetData());
						FText* TargetText = reinterpret_cast<FText*>(InTargetMemory.GetData());
						*TargetText = *SourceText;
					}
					break;
				case FAnimNextParamType::EValueType::Struct:
					if(UScriptStruct* ScriptStruct = Cast<UScriptStruct>(InSourceType.ValueTypeObject.Get()))
					{
						const int32 StructSize = ScriptStruct->GetStructureSize();
						check(StructSize <= InTargetMemory.Num());

						ScriptStruct->CopyScriptStruct(InTargetMemory.GetData(), InSourceMemory.GetData(), 1);
					}
					else
					{
						checkf(false, TEXT("Error: FParameterHelpers::Copy: Unknown Struct Type"));
					}
					break;
				case FAnimNextParamType::EValueType::Object:
					{
						const TObjectPtr<UObject>* SourceObjectPtr = reinterpret_cast<const TObjectPtr<UObject>*>(InSourceMemory.GetData());
						TObjectPtr<UObject>* TargetObjectPtr = reinterpret_cast<TObjectPtr<UObject>*>(InTargetMemory.GetData());
						*TargetObjectPtr = *SourceObjectPtr;
					}
					break;
				case FAnimNextParamType::EValueType::SoftObject:
					{
						const TSoftObjectPtr<UObject>* SourceSoftObjectPtr = reinterpret_cast<const TSoftObjectPtr<UObject>*>(InSourceMemory.GetData());
						TSoftObjectPtr<UObject>* TargetSoftObjectPtr = reinterpret_cast<TSoftObjectPtr<UObject>*>(InTargetMemory.GetData());
						*TargetSoftObjectPtr = *SourceSoftObjectPtr;
					}
					break;
				case FAnimNextParamType::EValueType::Class:
					{
						const TSubclassOf<UObject>* SourceClassPtr = reinterpret_cast<const TSubclassOf<UObject>*>(InSourceMemory.GetData());
						TSubclassOf<UObject>* TargetClassPtr = reinterpret_cast<TSubclassOf<UObject>*>(InTargetMemory.GetData());
						*TargetClassPtr = *SourceClassPtr;
					}
					break;
				case FAnimNextParamType::EValueType::SoftClass:
					{
						const TSoftClassPtr<UObject>* SourceClassPtr = reinterpret_cast<const TSoftClassPtr<UObject>*>(InSourceMemory.GetData());
						TSoftClassPtr<UObject>* TargetClassPtr = reinterpret_cast<TSoftClassPtr<UObject>*>(InTargetMemory.GetData());
						*TargetClassPtr = *SourceClassPtr;
					}
					break;
				default:
					checkf(false, TEXT("Error: FParameterHelpers::Copy of unknown type"));
					break;
				}
			}
			break;
		}
		
		return ECopyResult::Succeeded;
	}
	else
	{
		/** TODO: mismatched/conversions etc. */
		check(false);
		return ECopyResult::Failed;
	}
}


FParamHelpers::ECopyResult FParamHelpers::Copy(const FParamTypeHandle& InSourceTypeHandle, const FParamTypeHandle& InTargetTypeHandle, TConstArrayView<uint8> InSourceMemory, TArrayView<uint8> InTargetMemory)
{
	if(InSourceTypeHandle == InTargetTypeHandle)
	{
		auto SimpleCopy = [&InTargetMemory, &InSourceMemory](int32 InParamSize, int32 InAlignment)
		{
			const int32 ParamAllocSize = Align(InParamSize, InAlignment);
			check(InTargetMemory.Num() <= ParamAllocSize);
			check(InSourceMemory.Num() <= ParamAllocSize);
			check(IsAligned(InTargetMemory.GetData(), InAlignment));
			check(IsAligned(InSourceMemory.GetData(), InAlignment));

			FMemory::Memcpy(InTargetMemory.GetData(), InSourceMemory.GetData(), ParamAllocSize);

			return ECopyResult::Succeeded;
		};

		switch(InSourceTypeHandle.GetParameterType())
		{
		case FParamTypeHandle::EParamType::None:
			ensureMsgf(false, TEXT("Trying to copy parameter of type None"));
			break;
		case FParamTypeHandle::EParamType::Bool:
			return SimpleCopy(sizeof(bool), alignof(bool));
		case FParamTypeHandle::EParamType::Byte:
			return SimpleCopy(sizeof(uint8), alignof(uint8));
		case FParamTypeHandle::EParamType::Int32:
			return SimpleCopy(sizeof(int32), alignof(int32));
		case FParamTypeHandle::EParamType::Int64:
			return SimpleCopy(sizeof(int64), alignof(int64));
		case FParamTypeHandle::EParamType::Float:
			return SimpleCopy(sizeof(float), alignof(float));
		case FParamTypeHandle::EParamType::Double:
			return SimpleCopy(sizeof(double), alignof(double));
		case FParamTypeHandle::EParamType::Name:
			return SimpleCopy(sizeof(FName), alignof(FName));
		case FParamTypeHandle::EParamType::String:
			{
				const FString* SourceString = reinterpret_cast<const FString*>(InSourceMemory.GetData());
				FString* TargetString = reinterpret_cast<FString*>(InTargetMemory.GetData());
				*TargetString = *SourceString;
				return ECopyResult::Succeeded;
			}
		case FParamTypeHandle::EParamType::Text:
			{
				const FText* SourceText = reinterpret_cast<const FText*>(InSourceMemory.GetData());
				FText* TargetText = reinterpret_cast<FText*>(InTargetMemory.GetData());
				*TargetText = *SourceText;
			}
		case FParamTypeHandle::EParamType::Vector:
			return SimpleCopy(sizeof(FVector), alignof(FVector));
		case FParamTypeHandle::EParamType::Vector4:
			return SimpleCopy(sizeof(FVector4), alignof(FVector4));
		case FParamTypeHandle::EParamType::Quat:
			return SimpleCopy(sizeof(FQuat), alignof(FQuat));
		case FParamTypeHandle::EParamType::Transform:
			return SimpleCopy(sizeof(FTransform), alignof(FTransform));
		case FParamTypeHandle::EParamType::Custom:
			{
				FAnimNextParamType Type = InSourceTypeHandle.GetType();
				return Copy(Type, Type, InSourceMemory, InTargetMemory);
			}
		default:
			checkf(false, TEXT("Error: FParameterHelpers::Copy of unknown type"));
			break;
		}
	}

	/** TODO: mismatched/conversions etc. */
	check(false);
	return ECopyResult::Failed;
}

}