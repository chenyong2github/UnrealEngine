// Copyright Epic Games, Inc. All Rights Reserved.
#include "StateTreePropertyBindingCompiler.h"
#include "IPropertyAccessEditor.h"
#include "PropertyPathHelpers.h"
#include "StateTreeTypes.h"
#include "StateTreePropertyBindings.h"

bool FStateTreePropertyBindingCompiler::Init(FStateTreePropertyBindings& InPropertyBindings, FStateTreeCompilerLog& InLog)
{
	Log = &InLog;
	PropertyBindings = &InPropertyBindings;
	PropertyBindings->Reset();
	SourceStructs.Reset();
	return true;
}

bool FStateTreePropertyBindingCompiler::CompileBatch(const FStateTreeBindableStructDesc& TargetStruct, TConstArrayView<FStateTreeEditorPropertyBinding> EditorPropertyBindings, int32& OutBatchIndex)
{
	check(Log);
	check(PropertyBindings);
	OutBatchIndex = INDEX_NONE;

	StoreSourceStructs();
	
	int32 BindingsBegin = PropertyBindings->PropertyBindings.Num();

	for (const FStateTreeEditorPropertyBinding& EditorBinding : EditorPropertyBindings)
	{
		if (EditorBinding.TargetPath.StructID != TargetStruct.ID)
		{
			continue;
		}
		// Source must be in the source array
		const FGuid SourceStructID = EditorBinding.SourcePath.StructID;
		const int32 SourceStructIdx = SourceStructs.IndexOfByPredicate([SourceStructID](const FStateTreeBindableStructDesc& Struct)
			{
				return (Struct.ID == SourceStructID);
			});
		if (SourceStructIdx == INDEX_NONE)
		{
			Log->Reportf(EMessageSeverity::Error, TargetStruct,
				TEXT("Could not find a binding source."));
			return false;
		}
		const FStateTreeBindableStructDesc& SourceStruct = SourceStructs[SourceStructIdx];

		FStateTreePropertyBinding& NewBinding = PropertyBindings->PropertyBindings.AddDefaulted_GetRef();

		// Resolve paths
		FResolvedPathResult SourceResult;
		if (!ResolvePropertyPath(TargetStruct, SourceStruct, EditorBinding.SourcePath, SourceResult))
		{
			Log->Reportf(EMessageSeverity::Error, TargetStruct,
				TEXT("Could not resolve path to '%s:%s'."),
				*SourceStruct.Name.ToString(), *EditorBinding.SourcePath.ToString());
			return false;
		}

		// Destination container is set to 0, it is assumed to be passed in when doing the batch copy.
		FResolvedPathResult TargetResult;
		if (!ResolvePropertyPath(TargetStruct, TargetStruct, EditorBinding.TargetPath, TargetResult))
		{
			Log->Reportf(EMessageSeverity::Error, TargetStruct,
				TEXT("Could not resolve path to '%s:%s'."),
				*TargetStruct.Name.ToString(), *EditorBinding.TargetPath.ToString());
			return false;
		}

		NewBinding.SourcePathIndex = SourceResult.PathIndex;
		NewBinding.TargetPathIndex = TargetResult.PathIndex;
		NewBinding.SourceStructIndex = SourceStructIdx;
		NewBinding.CopyType = GetCopyType(SourceResult.LeafProperty, SourceResult.LeafArrayIndex, TargetResult.LeafProperty, TargetResult.LeafArrayIndex);
	}

	const int32 BindingsEnd = PropertyBindings->PropertyBindings.Num();
	if (BindingsBegin != BindingsEnd)
	{
		FStateTreePropCopyBatch& Batch = PropertyBindings->CopyBatches.AddDefaulted_GetRef();
		Batch.TargetStruct = TargetStruct;
		Batch.BindingsBegin = BindingsBegin;
		Batch.BindingsEnd = BindingsEnd;
		OutBatchIndex = PropertyBindings->CopyBatches.Num() - 1;
	}

	return true;
}

void FStateTreePropertyBindingCompiler::Finalize()
{
	StoreSourceStructs();
}

int32 FStateTreePropertyBindingCompiler::AddSourceStruct(const FStateTreeBindableStructDesc& SourceStruct)
{
	SourceStructs.Add(SourceStruct);
	return SourceStructs.Num() - 1;
}

int32 FStateTreePropertyBindingCompiler::GetSourceStructIndexByID(const FGuid& ID) const
{
	return SourceStructs.IndexOfByPredicate([ID](const FStateTreeBindableStructDesc& Structs) { return (Structs.ID == ID); });
}

EStateTreePropertyCopyType FStateTreePropertyBindingCompiler::GetCopyType(const FProperty* SourceProperty, const int32 SourceArrayIndex, const FProperty* TargetProperty, const int32 TargetArrayIndex)
{
	if (const FArrayProperty* SourceArrayProperty = CastField<FArrayProperty>(SourceProperty))
	{
		// use the array's inner property if we are not trying to copy the whole array
		if (SourceArrayIndex != INDEX_NONE)
		{
			SourceProperty = SourceArrayProperty->Inner;
		}
	}

	if (const FArrayProperty* TargetArrayProperty = CastField<FArrayProperty>(TargetProperty))
	{
		// use the array's inner property if we are not trying to copy the whole array
		if (TargetArrayIndex != INDEX_NONE)
		{
			TargetProperty = TargetArrayProperty->Inner;
		}
	}

	const EPropertyAccessCompatibility Compatibility = GetPropertyCompatibility(SourceProperty, TargetProperty);

	// Extract underlying types for enums
	if (const FEnumProperty* EnumPropertyA = CastField<const FEnumProperty>(SourceProperty))
	{
		SourceProperty = EnumPropertyA->GetUnderlyingProperty();
	}

	if (const FEnumProperty* EnumPropertyB = CastField<const FEnumProperty>(TargetProperty))
	{
		TargetProperty = EnumPropertyB->GetUnderlyingProperty();
	}

	if (Compatibility == EPropertyAccessCompatibility::Compatible)
	{
		if (CastField<FNameProperty>(TargetProperty))
		{
			return EStateTreePropertyCopyType::CopyName;
		}
		else if (CastField<FBoolProperty>(TargetProperty))
		{
			return EStateTreePropertyCopyType::CopyBool;
		}
		else if (CastField<FStructProperty>(TargetProperty))
		{
			return EStateTreePropertyCopyType::CopyStruct;
		}
		else if (CastField<FObjectPropertyBase>(TargetProperty))
		{
			return EStateTreePropertyCopyType::CopyObject;
		}
		else if (CastField<FArrayProperty>(TargetProperty) && TargetProperty->HasAnyPropertyFlags(CPF_EditFixedSize))
		{
			// only apply array copying rules if the destination array is fixed size, otherwise it will be 'complex'
			return EStateTreePropertyCopyType::CopyFixedArray;
		}
		else if (TargetProperty->PropertyFlags & CPF_IsPlainOldData)
		{
			return EStateTreePropertyCopyType::CopyPlain;
		}
		else
		{
			return EStateTreePropertyCopyType::CopyComplex;
		}
	}
	else if (Compatibility == EPropertyAccessCompatibility::Promotable)
	{
		if (SourceProperty->IsA<FBoolProperty>())
		{
			if (TargetProperty->IsA<FByteProperty>())
			{
				return EStateTreePropertyCopyType::PromoteBoolToByte;
			}
			else if (TargetProperty->IsA<FIntProperty>())
			{
				return EStateTreePropertyCopyType::PromoteBoolToInt32;
			}
			else if (TargetProperty->IsA<FUInt32Property>())
			{
				return EStateTreePropertyCopyType::PromoteBoolToUInt32;
			}
			else if (TargetProperty->IsA<FInt64Property>())
			{
				return EStateTreePropertyCopyType::PromoteBoolToInt64;
			}
			else if (TargetProperty->IsA<FFloatProperty>())
			{
				return EStateTreePropertyCopyType::PromoteBoolToFloat;
			}
			else if (TargetProperty->IsA<FDoubleProperty>())
			{
				return EStateTreePropertyCopyType::PromoteBoolToDouble;
			}
		}
		else if (SourceProperty->IsA<FByteProperty>())
		{
			if (TargetProperty->IsA<FIntProperty>())
			{
				return EStateTreePropertyCopyType::PromoteByteToInt32;
			}
			else if (TargetProperty->IsA<FUInt32Property>())
			{
				return EStateTreePropertyCopyType::PromoteByteToUInt32;
			}
			else if (TargetProperty->IsA<FInt64Property>())
			{
				return EStateTreePropertyCopyType::PromoteByteToInt64;
			}
			else if (TargetProperty->IsA<FFloatProperty>())
			{
				return EStateTreePropertyCopyType::PromoteByteToFloat;
			}
			else if (TargetProperty->IsA<FDoubleProperty>())
			{
				return EStateTreePropertyCopyType::PromoteByteToDouble;
			}
		}
		else if (SourceProperty->IsA<FIntProperty>())
		{
			if (TargetProperty->IsA<FInt64Property>())
			{
				return EStateTreePropertyCopyType::PromoteInt32ToInt64;
			}
			else if (TargetProperty->IsA<FFloatProperty>())
			{
				return EStateTreePropertyCopyType::PromoteInt32ToFloat;
			}
			else if (TargetProperty->IsA<FDoubleProperty>())
			{
				return EStateTreePropertyCopyType::PromoteInt32ToDouble;
			}
		}
		else if (SourceProperty->IsA<FFloatProperty>())
		{
			if (TargetProperty->IsA<FDoubleProperty>())
			{
				return EStateTreePropertyCopyType::PromoteFloatToDouble;
			}
		}
		else if (SourceProperty->IsA<FDoubleProperty>())
		{
			if (TargetProperty->IsA<FFloatProperty>())
			{
				return EStateTreePropertyCopyType::DemoteDoubleToFloat;
			}
		}
		else if (SourceProperty->IsA<FUInt32Property>())
		{
			if (TargetProperty->IsA<FInt64Property>())
			{
				return EStateTreePropertyCopyType::PromoteUInt32ToInt64;
			}
			else if (TargetProperty->IsA<FFloatProperty>())
			{
				return EStateTreePropertyCopyType::PromoteUInt32ToFloat;
			}
		}
	}

	ensureMsgf(false, TEXT("Couldnt determine property copy type (%s -> %s)"), *SourceProperty->GetNameCPP(), *TargetProperty->GetNameCPP());

	return EStateTreePropertyCopyType::None;
}

bool FStateTreePropertyBindingCompiler::ResolvePropertyPath(const FStateTreeBindableStructDesc& InOwnerStructDesc, const FStateTreeBindableStructDesc& InStructDesc, const FStateTreeEditorPropertyPath& InPath, FResolvedPathResult& OutResult)
{
	const int32 PathIndex = PropertyBindings->PropertyPaths.Num();
	FStateTreePropertyPath& Path = PropertyBindings->PropertyPaths.AddDefaulted_GetRef();
	Path.SegmentsBegin = PropertyBindings->PropertySegments.Num();

	const bool bResult = ResolvePropertyPath(InStructDesc, InPath, PropertyBindings->PropertySegments, OutResult.LeafProperty, OutResult.LeafArrayIndex, Log, &InOwnerStructDesc);

	Path.SegmentsEnd = PropertyBindings->PropertySegments.Num();
	OutResult.PathIndex = PathIndex;

	return bResult;
}

bool FStateTreePropertyBindingCompiler::ResolvePropertyPath(const FStateTreeBindableStructDesc& InStructDesc, const FStateTreeEditorPropertyPath& InPath,
															TArray<FStateTreePropertySegment>& OutSegments, const FProperty*& OutLeafProperty, int32& OutLeafArrayIndex,
															FStateTreeCompilerLog* InLog, const FStateTreeBindableStructDesc* InLogContextStruct)
{
	if (!InPath.IsValid())
	{
		if (InLog != nullptr && InLogContextStruct != nullptr)
		{
			InLog->Reportf(EMessageSeverity::Error, *InLogContextStruct,
					TEXT("Invalid path '%s:%s'."),
					*InStructDesc.Name.ToString(), *InPath.ToString());
		}
		return false;
	}

	const UStruct* CurrentStruct = InStructDesc.Struct;
	const FProperty* LeafProperty = nullptr;
	int32 LeafArrayIndex = INDEX_NONE;
	bool bResult = InPath.Path.Num() > 0;

	for (int32 SegmentIndex = 0; SegmentIndex < InPath.Path.Num(); SegmentIndex++)
	{
		const FString& SegmentString = InPath.Path[SegmentIndex];
		const TCHAR* PropertyNamePtr = nullptr;
		int32 PropertyNameLength = 0;
		int32 ArrayIndex = INDEX_NONE;
		PropertyPathHelpers::FindFieldNameAndArrayIndex(SegmentString.Len(), *SegmentString, PropertyNameLength, &PropertyNamePtr, ArrayIndex);
		ensure(PropertyNamePtr != nullptr);
		FString PropertyNameString(PropertyNameLength, PropertyNamePtr);
		const FName PropertyName = FName(*PropertyNameString, FNAME_Find);

		const bool bFinalSegment = SegmentIndex == (InPath.Path.Num() - 1);

		if (CurrentStruct == nullptr)
		{
			if (InLog != nullptr && InLogContextStruct != nullptr)
			{
				InLog->Reportf(EMessageSeverity::Error, *InLogContextStruct,
						TEXT("Malformed path '%s:%s'."),
						*InStructDesc.Name.ToString(), *InPath.ToString(SegmentIndex, TEXT("<"), TEXT(">")));
			}
			bResult = false;
			break;
		}

		const FProperty* Property = CurrentStruct->FindPropertyByName(PropertyName);
		if (Property == nullptr)
		{
			// TODO: use core redirects to fix up the name.
			if (InLog != nullptr && InLogContextStruct != nullptr)
			{
				InLog->Reportf(EMessageSeverity::Error, *InLogContextStruct,
						TEXT("Malformed path '%s:%s', could not find property '%s%s.%s'."),
						*InStructDesc.Name.ToString(), *InPath.ToString(SegmentIndex, TEXT("<"), TEXT(">")),
						CurrentStruct->GetPrefixCPP(), *CurrentStruct->GetName(), *PropertyName.ToString());
			}
			bResult = false;
			break;
		}

		FStateTreePropertySegment& Segment = OutSegments.AddDefaulted_GetRef();
		Segment.Name = PropertyName;
		Segment.ArrayIndex = ArrayIndex;

		// Check to see if it is an array access first
		const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
		if (ArrayProperty != nullptr && ArrayIndex != INDEX_NONE)
		{
			// It is an array, now check to see if this is an array of structures
			if (const FStructProperty* ArrayOfStructsProperty = CastField<FStructProperty>(ArrayProperty->Inner))
			{
				Segment.Type = EStateTreePropertyAccessType::IndexArray;
				CurrentStruct = ArrayOfStructsProperty->Struct;
			}
			// if it's not an array of structs, maybe it's an array of objects
			else if (const FObjectPropertyBase* ArrayOfObjectsProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
			{
				Segment.Type = EStateTreePropertyAccessType::IndexArray;
				CurrentStruct = ArrayOfObjectsProperty->PropertyClass;

				if (!bFinalSegment)
				{
					// Object arrays need an object dereference adding if non-leaf
					FStateTreePropertySegment& ExtraSegment = OutSegments.AddDefaulted_GetRef();
					ExtraSegment.ArrayIndex = 0;
					ExtraSegment.Type = EStateTreePropertyAccessType::Object;
					const FProperty* InnerProperty = ArrayProperty->Inner;
					if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InnerProperty))
					{
						ExtraSegment.Type = EStateTreePropertyAccessType::Object;
					}
					else if (const FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(InnerProperty))
					{
						ExtraSegment.Type = EStateTreePropertyAccessType::WeakObject;
					}
					else if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(InnerProperty))
					{
						ExtraSegment.Type = EStateTreePropertyAccessType::SoftObject;
					}
				}
			}
			else
			{
				Segment.Type = EStateTreePropertyAccessType::IndexArray;
				Segment.ArrayIndex = ArrayIndex;
				CurrentStruct = nullptr;
			}
		}
		// Leaf segments all get treated the same, plain, array, struct or object. Copy type is figured out separately.
		else if (bFinalSegment)
		{
			Segment.Type = EStateTreePropertyAccessType::Offset;
			CurrentStruct = nullptr;
		}
		// Check to see if this is a simple structure (eg. not an array of structures)
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			Segment.Type = EStateTreePropertyAccessType::Offset;
			CurrentStruct = StructProperty->Struct;
		}
		// Check to see if this is a simple object (eg. not an array of objects)
		else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			Segment.Type = EStateTreePropertyAccessType::Object;
			CurrentStruct = ObjectProperty->PropertyClass;
		}
		// Check to see if this is a simple weak object property (eg. not an array of weak objects).
		else if (const FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property))
		{
			Segment.Type = EStateTreePropertyAccessType::WeakObject;
			CurrentStruct = WeakObjectProperty->PropertyClass;
		}
		// Check to see if this is a simple soft object property (eg. not an array of soft objects).
		else if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			Segment.Type = EStateTreePropertyAccessType::SoftObject;
			CurrentStruct = SoftObjectProperty->PropertyClass;
		}
		else
		{
			if (InLog != nullptr && InLogContextStruct != nullptr)
			{
				InLog->Reportf(EMessageSeverity::Error, *InLogContextStruct,
						TEXT("Unsupported segment %s in path '%s:%s'."),
						*InStructDesc.Name.ToString(), *InPath.ToString(SegmentIndex, TEXT("<"), TEXT(">")),
						*Property->GetCPPType(), *InStructDesc.Name.ToString(), *InPath.ToString(SegmentIndex, TEXT("<"), TEXT(">")));
			}
			bResult = false;
			break;
		}

		if (bFinalSegment)
		{
			LeafProperty = Property;
			LeafArrayIndex = ArrayIndex;
		}
	}

	if (!bResult)
	{
		return false;
	}

	OutLeafProperty = LeafProperty;
	OutLeafArrayIndex = LeafArrayIndex;

	return true;
}

EPropertyAccessCompatibility FStateTreePropertyBindingCompiler::GetPropertyCompatibility(const FProperty* InPropertyA, const FProperty* InPropertyB)
{
	if (InPropertyA == InPropertyB)
	{
		return EPropertyAccessCompatibility::Compatible;
	}

	if (InPropertyA == nullptr || InPropertyB == nullptr)
	{
		return EPropertyAccessCompatibility::Incompatible;
	}

	// Special case for object properties
	if (InPropertyA->IsA<FObjectPropertyBase>() && InPropertyB->IsA<FObjectPropertyBase>())
	{
		return EPropertyAccessCompatibility::Compatible;
	}

	// Extract underlying types for enums
	if (const FEnumProperty* EnumPropertyA = CastField<const FEnumProperty>(InPropertyA))
	{
		InPropertyA = EnumPropertyA->GetUnderlyingProperty();
	}

	if (const FEnumProperty* EnumPropertyB = CastField<const FEnumProperty>(InPropertyB))
	{
		InPropertyB = EnumPropertyB->GetUnderlyingProperty();
	}

	if (InPropertyA->SameType(InPropertyB))
	{
		return EPropertyAccessCompatibility::Compatible;
	}
	else
	{
		// Not directly compatible, check for promotions
		if (InPropertyA->IsA<FBoolProperty>())
		{
			if (InPropertyB->IsA<FByteProperty>() || InPropertyB->IsA<FIntProperty>() || InPropertyB->IsA<FUInt32Property>() || InPropertyB->IsA<FInt64Property>() || InPropertyB->IsA<FFloatProperty>())
			{
				return EPropertyAccessCompatibility::Promotable;
			}
		}
		else if (InPropertyA->IsA<FByteProperty>())
		{
			if (InPropertyB->IsA<FIntProperty>() || InPropertyB->IsA<FUInt32Property>() || InPropertyB->IsA<FInt64Property>() || InPropertyB->IsA<FFloatProperty>())
			{
				return EPropertyAccessCompatibility::Promotable;
			}
		}
		else if (InPropertyA->IsA<FIntProperty>())
		{
			if (InPropertyB->IsA<FInt64Property>() || InPropertyB->IsA<FFloatProperty>())
			{
				return EPropertyAccessCompatibility::Promotable;
			}
		}
		else if (InPropertyA->IsA<FUInt32Property>())
		{
			if (InPropertyB->IsA<FInt64Property>() || InPropertyB->IsA<FFloatProperty>())
			{
				return EPropertyAccessCompatibility::Promotable;
			}
		}
	}

	return EPropertyAccessCompatibility::Incompatible;
}

void FStateTreePropertyBindingCompiler::StoreSourceStructs()
{
	// Check that existing structs are compatible
	check(PropertyBindings->SourceStructs.Num() <= SourceStructs.Num());
	for (int32 i = 0; i < PropertyBindings->SourceStructs.Num(); i++)
	{
		check(PropertyBindings->SourceStructs[i] == SourceStructs[i]);
	}

	// Add new
	if (SourceStructs.Num() > PropertyBindings->SourceStructs.Num())
	{
		for (int32 i = PropertyBindings->SourceStructs.Num(); i < SourceStructs.Num(); i++)
		{
			PropertyBindings->SourceStructs.Add(SourceStructs[i]);
		}
	}
}
