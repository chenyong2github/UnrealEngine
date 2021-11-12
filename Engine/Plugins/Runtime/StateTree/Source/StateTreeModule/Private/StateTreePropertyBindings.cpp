// Copyright Epic Games, Inc. All Rights Reserved.
#include "StateTreePropertyBindings.h"
#include "StateTreeTypes.h"

//----------------------------------------------------------------//
//  FStateTreePropertyBindings
//----------------------------------------------------------------//

void FStateTreePropertyBindings::Reset()
{
	SourceStructs.Reset();
	CopyBatches.Reset();
	PropertyBindings.Reset();
	PropertyPaths.Reset();
	PropertySegments.Reset();
	PropertyCopies.Reset();
	PropertyAccesses.Reset();
	PropertyIndirections.Reset();
	bBindingsResolved = false;
}

bool FStateTreePropertyBindings::ResolvePaths()
{
	PropertyIndirections.Reset();
	PropertyCopies.SetNum(PropertyBindings.Num());
	PropertyAccesses.SetNum(PropertyPaths.Num());

	bBindingsResolved = true;

	bool bResult = true;
	
	for (const FStateTreePropCopyBatch& Batch : CopyBatches)
	{
		for (int32 i = Batch.BindingsBegin; i != Batch.BindingsEnd; i++)
		{
			const FStateTreePropertyBinding& Binding = PropertyBindings[i];
			FStateTreePropCopy& Copy = PropertyCopies[i];

			Copy.SourceAccessIndex = Binding.SourcePathIndex;
			Copy.TargetAccessIndex = Binding.TargetPathIndex;
			Copy.SourceStructIndex = Binding.SourceStructIndex;
			Copy.Type = Binding.CopyType;

			const UStruct* SourceStruct = SourceStructs[Binding.SourceStructIndex].Struct;
			const UStruct* TargetStruct = Batch.TargetStruct.Struct;
			if (!SourceStruct || !TargetStruct)
			{
				bBindingsResolved = false;
				break;
			}

			// Resolve paths and validate the copy. Stops on first failure.
			bool bSuccess = true;
			bSuccess = bSuccess && ResolvePath(SourceStruct, PropertyPaths[Binding.SourcePathIndex], PropertyAccesses[Copy.SourceAccessIndex]);
			bSuccess = bSuccess && ResolvePath(TargetStruct, PropertyPaths[Binding.TargetPathIndex], PropertyAccesses[Copy.TargetAccessIndex]);
			bSuccess = bSuccess && ValidateCopy(Copy);
			if (!bSuccess)
			{
				// Resolving or validating failed, make the copy a nop.
				Copy.Type = EStateTreePropertyCopyType::None;
				bResult = false;
			}
		}
	}

	return bResult;
}

bool FStateTreePropertyBindings::ResolvePath(const UStruct* Struct, const FStateTreePropertyPath& Path, FStateTreePropertyAccess& OutAccess)
{
	if (!Struct)
	{
		UE_LOG(LogStateTree, Error, TEXT("%s: '%s' Invalid source struct."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathAsString(Path));
		return false;
	}

	const UStruct* CurrentStruct = Struct;
	FProperty* LeafProperty = nullptr;
	const int32 IndirectionsBegin = PropertyIndirections.Num();

	for (int32 i = Path.SegmentsBegin; i < Path.SegmentsEnd; i++)
	{
		const FStateTreePropertySegment& Segment = PropertySegments[i];
		FStateTreePropertyIndirection& Indirection = PropertyIndirections.AddDefaulted_GetRef();

		const bool bFinalSegment = i == (Path.SegmentsEnd - 1);

		if (!ensure(CurrentStruct))
		{
			UE_LOG(LogStateTree, Error, TEXT("%s: '%s' Invalid struct."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathAsString(Path, i, TEXT("<"), TEXT(">")));
			return false;
		}
		FProperty* Property = CurrentStruct->FindPropertyByName(Segment.Name);
		if (!Property)
		{
			// TODO: use core redirects to fix up the name.
			UE_LOG(LogStateTree, Error, TEXT("%s: Malformed path '%s', could not to find property '%s%s.%s'."),
				ANSI_TO_TCHAR(__FUNCTION__), *GetPathAsString(Path, i, TEXT("<"), TEXT(">")),
				CurrentStruct->GetPrefixCPP(), *CurrentStruct->GetName(), *Segment.Name.ToString());
			return false;
		}
		Indirection.ArrayIndex = Segment.ArrayIndex == INDEX_NONE ? 0 : Segment.ArrayIndex;
		Indirection.Type = Segment.Type;
		Indirection.Offset = Property->GetOffset_ForInternal() + Property->ElementSize * Indirection.ArrayIndex;

		// Check to see if it is an array access first.
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (FStructProperty* ArrayOfStructsProperty = CastField<FStructProperty>(ArrayProperty->Inner))
			{
				CurrentStruct = ArrayOfStructsProperty->Struct;
			}
			else if (FObjectPropertyBase* ArrayOfObjectsProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
			{
				CurrentStruct = ArrayOfObjectsProperty->PropertyClass;
			}
			Indirection.ArrayProperty = ArrayProperty;
		}
		// Leaf segments all get treated the same, plain, struct or object
		else if (bFinalSegment)
		{
			CurrentStruct = nullptr;
		}
		// Check to see if this is a simple structure (eg. not an array of structures)
		else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			CurrentStruct = StructProperty->Struct;
		}
		// Check to see if this is a simple object (eg. not an array of objects)
		else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			CurrentStruct = ObjectProperty->PropertyClass;
		}
		// Check to see if this is a simple weak object property (eg. not an array of weak objects).
		else if (FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property))
		{
			CurrentStruct = WeakObjectProperty->PropertyClass;
		}
		// Check to see if this is a simple soft object property (eg. not an array of soft objects).
		else if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			CurrentStruct = SoftObjectProperty->PropertyClass;
		}
		else
		{
			UE_LOG(LogStateTree, Error, TEXT("%s: Unsupported segment %s in path '%s'."),
			    ANSI_TO_TCHAR(__FUNCTION__), *Property->GetCPPType(), *GetPathAsString(Path, i, TEXT("<"), TEXT(">")));
		}

		if (bFinalSegment)
		{
			LeafProperty = Property;
		}
	}

	int32 IndirectionsEnd = PropertyIndirections.Num();

	// Collapse adjacent offset indirections
	for (int32 i = IndirectionsBegin; i < IndirectionsEnd; i++)
	{
		FStateTreePropertyIndirection& Indirection = PropertyIndirections[i];
		if (Indirection.Type == EStateTreePropertyAccessType::Offset && (i + 1) < IndirectionsEnd)
		{
			FStateTreePropertyIndirection& NextIndirection = PropertyIndirections[i + 1];
			if (NextIndirection.Type == EStateTreePropertyAccessType::Offset)
			{
				Indirection.Offset += NextIndirection.Offset;
				PropertyIndirections.RemoveAt(i + 1);
				i--;
				IndirectionsEnd--;
			}
		}
	}

	OutAccess.IndirectionsBegin = IndirectionsBegin;
	OutAccess.IndirectionsEnd = IndirectionsEnd;
	OutAccess.LeafProperty = LeafProperty;

	return true;
}

bool FStateTreePropertyBindings::ValidateCopy(FStateTreePropCopy& Copy) const
{
	const FProperty* SourceProperty = PropertyAccesses[Copy.SourceAccessIndex].LeafProperty;
	const FProperty* TargetProperty = PropertyAccesses[Copy.TargetAccessIndex].LeafProperty;

	if (!SourceProperty || !TargetProperty)
	{
		return false;
	}

	// Extract underlying types for enums
	if (const FEnumProperty* EnumPropertyA = CastField<const FEnumProperty>(SourceProperty))
	{
		SourceProperty = EnumPropertyA->GetUnderlyingProperty();
	}

	if (const FEnumProperty* EnumPropertyB = CastField<const FEnumProperty>(TargetProperty))
	{
		TargetProperty = EnumPropertyB->GetUnderlyingProperty();
	}

	bool bResult = true;
	switch (Copy.Type)
	{
	case EStateTreePropertyCopyType::CopyPlain:
		Copy.CopySize = PropertyAccesses[Copy.SourceAccessIndex].LeafProperty->ElementSize * PropertyAccesses[Copy.SourceAccessIndex].LeafProperty->ArrayDim;
		bResult = (SourceProperty->PropertyFlags & CPF_IsPlainOldData) != 0 && (TargetProperty->PropertyFlags & CPF_IsPlainOldData) != 0;
		break;
	case EStateTreePropertyCopyType::CopyComplex:
		bResult = true;
		break;
	case EStateTreePropertyCopyType::CopyBool:
		bResult = SourceProperty->IsA<FBoolProperty>() && TargetProperty->IsA<FBoolProperty>();
		break;
	case EStateTreePropertyCopyType::CopyStruct:
		bResult = SourceProperty->IsA<FStructProperty>() && TargetProperty->IsA<FStructProperty>();
		break;
	case EStateTreePropertyCopyType::CopyObject:
		bResult = SourceProperty->IsA<FObjectPropertyBase>() && TargetProperty->IsA<FObjectPropertyBase>();
		break;
	case EStateTreePropertyCopyType::CopyName:
		bResult = SourceProperty->IsA<FNameProperty>() && TargetProperty->IsA<FNameProperty>();
		break;
	case EStateTreePropertyCopyType::CopyFixedArray:
		bResult = SourceProperty->IsA<FArrayProperty>() && TargetProperty->IsA<FArrayProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteBoolToByte:
		bResult = SourceProperty->IsA<FBoolProperty>() && TargetProperty->IsA<FByteProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteBoolToInt32:
		bResult = SourceProperty->IsA<FBoolProperty>() && TargetProperty->IsA<FIntProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteBoolToUInt32:
		bResult = SourceProperty->IsA<FBoolProperty>() && TargetProperty->IsA<FUInt32Property>();
		break;
	case EStateTreePropertyCopyType::PromoteBoolToInt64:
		bResult = SourceProperty->IsA<FBoolProperty>() && TargetProperty->IsA<FInt64Property>();
		break;
	case EStateTreePropertyCopyType::PromoteBoolToFloat:
		bResult = SourceProperty->IsA<FBoolProperty>() && TargetProperty->IsA<FFloatProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteBoolToDouble:
		bResult = SourceProperty->IsA<FBoolProperty>() && TargetProperty->IsA<FDoubleProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteByteToInt32:
		bResult = SourceProperty->IsA<FByteProperty>() && TargetProperty->IsA<FIntProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteByteToUInt32:
		bResult = SourceProperty->IsA<FByteProperty>() && TargetProperty->IsA<FUInt32Property>();
		break;
	case EStateTreePropertyCopyType::PromoteByteToInt64:
		bResult = SourceProperty->IsA<FByteProperty>() && TargetProperty->IsA<FInt64Property>();
		break;
	case EStateTreePropertyCopyType::PromoteByteToFloat:
		bResult = SourceProperty->IsA<FByteProperty>() && TargetProperty->IsA<FFloatProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteByteToDouble:
		bResult = SourceProperty->IsA<FByteProperty>() && TargetProperty->IsA<FDoubleProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteInt32ToInt64:
		bResult = SourceProperty->IsA<FIntProperty>() && TargetProperty->IsA<FInt64Property>();
		break;
	case EStateTreePropertyCopyType::PromoteInt32ToFloat:
		bResult = SourceProperty->IsA<FIntProperty>() && TargetProperty->IsA<FFloatProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteInt32ToDouble:
		bResult = SourceProperty->IsA<FIntProperty>() && TargetProperty->IsA<FDoubleProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteFloatToDouble:
		bResult = SourceProperty->IsA<FFloatProperty>() && TargetProperty->IsA<FDoubleProperty>();
		break;
	case EStateTreePropertyCopyType::DemoteDoubleToFloat:
		bResult = SourceProperty->IsA<FDoubleProperty>() && TargetProperty->IsA<FFloatProperty>();
		break;
	case EStateTreePropertyCopyType::PromoteUInt32ToInt64:
		bResult = SourceProperty->IsA<FUInt32Property>() && TargetProperty->IsA<FInt64Property>();
		break;
	case EStateTreePropertyCopyType::PromoteUInt32ToFloat:
		bResult = SourceProperty->IsA<FUInt32Property>() && TargetProperty->IsA<FFloatProperty>();
		break;
	default:
		UE_LOG(LogStateTree, Error, TEXT("FStateTreePropertyBindings::ValidateCopy: Unhandled copy type %s between '%s' and '%s'"),
			*StaticEnum<EStateTreePropertyCopyType>()->GetValueAsString(Copy.Type), *SourceProperty->GetNameCPP(), *TargetProperty->GetNameCPP());
		bResult = false;
		break;
	}

	UE_CLOG(!bResult, LogStateTree, Error, TEXT("FStateTreePropertyBindings::ValidateCopy: Failed to validate copy type %s between '%s' and '%s'"),
		*StaticEnum<EStateTreePropertyCopyType>()->GetValueAsString(Copy.Type), *SourceProperty->GetNameCPP(), *TargetProperty->GetNameCPP());
	
	return bResult;
}

uint8* FStateTreePropertyBindings::GetAddress(FStateTreeDataView InStructView, const FStateTreePropertyAccess& InAccess) const
{
	check(InStructView.IsValid());

	uint8* Address = InStructView.GetMutableMemory();
	for (int32 i = InAccess.IndirectionsBegin; Address != nullptr && i < InAccess.IndirectionsEnd; i++)
	{
		const FStateTreePropertyIndirection& Indirection = PropertyIndirections[i];
		switch (Indirection.Type)
		{
		case EStateTreePropertyAccessType::Offset:
		{
			Address = Address + Indirection.Offset;
			break;
		}
		case EStateTreePropertyAccessType::Object:
		{
			UObject* Object = *reinterpret_cast<UObject**>(Address + Indirection.Offset);
			Address = reinterpret_cast<uint8*>(Object);
			break;
		}
		case EStateTreePropertyAccessType::WeakObject:
		{
			TWeakObjectPtr<UObject>& WeakObjectPtr = *reinterpret_cast<TWeakObjectPtr<UObject>*>(Address + Indirection.Offset);
			UObject* Object = WeakObjectPtr.Get();
			Address = reinterpret_cast<uint8*>(Object);
			break;
		}
		case EStateTreePropertyAccessType::SoftObject:
		{
			FSoftObjectPtr& SoftObjectPtr = *reinterpret_cast<FSoftObjectPtr*>(Address + Indirection.Offset);
			UObject* Object = SoftObjectPtr.Get();
			Address = reinterpret_cast<uint8*>(Object);
			break;
		}
		case EStateTreePropertyAccessType::IndexArray:
		{
			check(Indirection.ArrayProperty);
			FScriptArrayHelper Helper(Indirection.ArrayProperty, Address + Indirection.Offset);
			if (Helper.IsValidIndex(Indirection.ArrayIndex))
			{
				Address = reinterpret_cast<uint8*>(Helper.GetRawPtr(Indirection.ArrayIndex));
			}
			else
			{
				Address = nullptr;
			}
			break;
		}
		default:
			ensureMsgf(false, TEXT("FStateTreePropertyBindings::GetAddress: Unhandled indirection type %s for '%s'"),
				*StaticEnum<EStateTreePropertyAccessType>()->GetValueAsString(Indirection.Type), *InAccess.LeafProperty->GetNameCPP());
		}
	}

	return Address;
}

void FStateTreePropertyBindings::PerformCopy(const FStateTreePropCopy& Copy, const FProperty* SourceProperty, const uint8* SourceAddress, const FProperty* TargetProperty, uint8* TargetAddress) const
{
	check(SourceProperty);
	check(SourceAddress);
	check(TargetProperty);
	check(TargetAddress);

	switch (Copy.Type)
	{
	case EStateTreePropertyCopyType::CopyPlain:
		FMemory::Memcpy(TargetAddress, SourceAddress, Copy.CopySize);
		break;
	case EStateTreePropertyCopyType::CopyComplex:
		SourceProperty->CopyCompleteValue(TargetAddress, SourceAddress);
		break;
	case EStateTreePropertyCopyType::CopyBool:
		static_cast<const FBoolProperty*>(TargetProperty)->SetPropertyValue(TargetAddress, static_cast<const FBoolProperty*>(SourceProperty)->GetPropertyValue(SourceAddress));
		break;
	case EStateTreePropertyCopyType::CopyStruct:
		static_cast<const FStructProperty*>(TargetProperty)->Struct->CopyScriptStruct(TargetAddress, SourceAddress);
		break;
	case EStateTreePropertyCopyType::CopyObject:
		static_cast<const FObjectPropertyBase*>(TargetProperty)->SetObjectPropertyValue(TargetAddress, static_cast<const FObjectPropertyBase*>(SourceProperty)->GetObjectPropertyValue(SourceAddress));
		break;
	case EStateTreePropertyCopyType::CopyName:
		static_cast<const FNameProperty*>(TargetProperty)->SetPropertyValue(TargetAddress, static_cast<const FNameProperty*>(SourceProperty)->GetPropertyValue(SourceAddress));
		break;
	case EStateTreePropertyCopyType::CopyFixedArray:
	{
		// Copy into fixed sized array (EditFixedSize). Resizable arrays are copied as Complex, and regular fixed sizes arrays via the regular copies (dim specifies array size).
		const FArrayProperty* SourceArrayProperty = static_cast<const FArrayProperty*>(SourceProperty);
		const FArrayProperty* TargetArrayProperty = static_cast<const FArrayProperty*>(TargetProperty);
		FScriptArrayHelper SourceArrayHelper(SourceArrayProperty, SourceAddress);
		FScriptArrayHelper TargetArrayHelper(TargetArrayProperty, TargetAddress);
			
		const int32 MinSize = FMath::Min(SourceArrayHelper.Num(), TargetArrayHelper.Num());
		for (int32 ElementIndex = 0; ElementIndex < MinSize; ++ElementIndex)
		{
			SourceArrayProperty->Inner->CopySingleValue(TargetArrayHelper.GetRawPtr(ElementIndex), SourceArrayHelper.GetRawPtr(ElementIndex));
		}
		break;
	}
	case EStateTreePropertyCopyType::PromoteBoolToByte:
		*reinterpret_cast<uint8*>(TargetAddress) = (uint8)static_cast<const FBoolProperty*>(SourceProperty)->GetPropertyValue(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteBoolToInt32:
		*reinterpret_cast<int32*>(TargetAddress) = (int32)static_cast<const FBoolProperty*>(SourceProperty)->GetPropertyValue(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteBoolToUInt32:
		*reinterpret_cast<uint32*>(TargetAddress) = (uint32)static_cast<const FBoolProperty*>(SourceProperty)->GetPropertyValue(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteBoolToInt64:
		*reinterpret_cast<int64*>(TargetAddress) = (int64)static_cast<const FBoolProperty*>(SourceProperty)->GetPropertyValue(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteBoolToFloat:
		*reinterpret_cast<float*>(TargetAddress) = (float)static_cast<const FBoolProperty*>(SourceProperty)->GetPropertyValue(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteBoolToDouble:
		*reinterpret_cast<double*>(TargetAddress) = (double)static_cast<const FBoolProperty*>(SourceProperty)->GetPropertyValue(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteByteToInt32:
		*reinterpret_cast<int32*>(TargetAddress) = (int32)*reinterpret_cast<const uint8*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteByteToUInt32:
		*reinterpret_cast<uint32*>(TargetAddress) = (uint32)*reinterpret_cast<const uint8*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteByteToInt64:
		*reinterpret_cast<int64*>(TargetAddress) = (int64)*reinterpret_cast<const uint8*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteByteToFloat:
		*reinterpret_cast<float*>(TargetAddress) = (float)*reinterpret_cast<const uint8*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteByteToDouble:
		*reinterpret_cast<double*>(TargetAddress) = (double)*reinterpret_cast<const uint8*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteInt32ToInt64:
		*reinterpret_cast<int64*>(TargetAddress) = (int64)*reinterpret_cast<const int32*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteInt32ToFloat:
		*reinterpret_cast<float*>(TargetAddress) = (float)*reinterpret_cast<const int32*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteInt32ToDouble:
		*reinterpret_cast<double*>(TargetAddress) = (double)*reinterpret_cast<const int32*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteFloatToDouble:
		*reinterpret_cast<double*>(TargetAddress) = (double)*reinterpret_cast<const float*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::DemoteDoubleToFloat:
		*reinterpret_cast<float*>(TargetAddress) = (float)*reinterpret_cast<const double*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteUInt32ToInt64:
		*reinterpret_cast<int64*>(TargetAddress) = (int64)*reinterpret_cast<const uint32*>(SourceAddress);
		break;
	case EStateTreePropertyCopyType::PromoteUInt32ToFloat:
		*reinterpret_cast<float*>(TargetAddress) = (float)*reinterpret_cast<const uint32*>(SourceAddress);
		break;
	default:
		ensureMsgf(false, TEXT("FStateTreePropertyBindings::PerformCopy: Unhandled copy type %s between '%s' and '%s'"),
			*StaticEnum<EStateTreePropertyCopyType>()->GetValueAsString(Copy.Type), *SourceProperty->GetNameCPP(), *TargetProperty->GetNameCPP());
		break;
	}
}

void FStateTreePropertyBindings::CopyTo(TConstArrayView<FStateTreeDataView> SourceStructViews, const int32 TargetBatchIndex, FStateTreeDataView TargetStructView) const
{
	// This is made ensure so that the programmers have the change to catch it (it's usually programming error not to call ResolvePaths(), and it wont spam log for others.
	if (!ensureMsgf(bBindingsResolved, TEXT("Bindings must be resolved successfully before copying. See ResolvePaths()")))
	{
		return;
	}

	if (TargetBatchIndex == INDEX_NONE)
	{
		return;
	}

	check(CopyBatches.IsValidIndex(TargetBatchIndex));
	const FStateTreePropCopyBatch& Batch = CopyBatches[TargetBatchIndex];

	check(TargetStructView.IsValid());
	check(TargetStructView.GetStruct() == Batch.TargetStruct.Struct);

	for (int32 i = Batch.BindingsBegin; i != Batch.BindingsEnd; i++)
	{
		const FStateTreePropCopy& Copy = PropertyCopies[i];
		// Copies that fail to be resolved (i.e. property path does not resolve, types changed) will be marked as None, skip them.
		if (Copy.Type == EStateTreePropertyCopyType::None)
		{
			continue;
		}
		const FStateTreePropertyAccess& SourceAccess = PropertyAccesses[Copy.SourceAccessIndex];
		const FStateTreePropertyAccess& TargetAccess = PropertyAccesses[Copy.TargetAccessIndex];
		check(SourceStructViews[Copy.SourceStructIndex].GetStruct() == SourceStructs[Copy.SourceStructIndex].Struct);
		const uint8* SourceAddress = GetAddress(SourceStructViews[Copy.SourceStructIndex], SourceAccess);
		uint8* TargetAddress = GetAddress(TargetStructView, TargetAccess);
		if (SourceAddress && TargetAddress)
		{
			PerformCopy(Copy, SourceAccess.LeafProperty, SourceAddress, TargetAccess.LeafProperty, TargetAddress);
		}
	}
}

void FStateTreePropertyBindings::DebugPrintInternalLayout(FString& OutString) const
{
	/** Array of expected source structs. */
	OutString += FString::Printf(TEXT("\nBindableStructDesc (%d)\n  [ %-40s | %-40s ]\n"), SourceStructs.Num(), TEXT("Type"), TEXT("Name"));
	for (const FStateTreeBindableStructDesc& BindableStructDesc : SourceStructs)
	{
		OutString += FString::Printf(TEXT("  | %-40s | %-40s |\n"),
									 BindableStructDesc.Struct ? *BindableStructDesc.Struct->GetName() : TEXT("null"),
									 *BindableStructDesc.Name.ToString());
	}

	/** Array of copy batches. */
	OutString += FString::Printf(TEXT("\nCopyBatches (%d)\n  [ %-40s | %-40s | %-8s [%-3s:%-3s[ ]\n"), CopyBatches.Num(),
		TEXT("Target Type"), TEXT("Target Name"), TEXT("Bindings"), TEXT("Beg"), TEXT("End"));
	for (const FStateTreePropCopyBatch& CopyBatch : CopyBatches)
	{
		OutString += FString::Printf(TEXT("  | %-40s | %-40s | %8s [%3d:%-3d[ |\n"),
									 CopyBatch.TargetStruct.Struct ? *CopyBatch.TargetStruct.Struct->GetName() : TEXT("null"),
									 *CopyBatch.TargetStruct.Name.ToString(),
									 TEXT(""), CopyBatch.BindingsBegin, CopyBatch.BindingsEnd);
	}

	/** Array of property bindings, resolved into arrays of copies before use. */
	OutString += FString::Printf(TEXT("\nPropertyBindings (%d)\n  [ %-8s | %-8s | %-10s | %-50s ]\n"), PropertyBindings.Num(),
	TEXT("Src Path"), TEXT("Tgt Path"), TEXT("Src Struct"), TEXT("Copy Type"));
	for (const FStateTreePropertyBinding& PropertyBinding : PropertyBindings)
	{
		OutString += FString::Printf(TEXT("  | %8d | %8d | %10d | %-50s |\n"),
									 PropertyBinding.SourcePathIndex, PropertyBinding.TargetPathIndex, PropertyBinding.SourceStructIndex,
									 *StaticEnum<EStateTreePropertyCopyType>()->GetValueAsString(PropertyBinding.CopyType));
	}

	/** Array of property bindings, indexed by property paths. */
	OutString += FString::Printf(TEXT("\nPropertyPaths (%d)\n  [ %-4s [%3s:%3s[ |\n"), PropertyPaths.Num(),
		TEXT("Seg."), TEXT("Beg"), TEXT("End"));
	for (const FStateTreePropertyPath& PropertyPath : PropertyPaths)
	{
		OutString += FString::Printf(TEXT("  | %4s [%3d:%-3d[ |\n"), TEXT(""), PropertyPath.SegmentsBegin, PropertyPath.SegmentsEnd);
	}

	/** Array of property segments, indexed by property paths. */
	OutString += FString::Printf(TEXT("\nPropertySegments (%d)\n  [ %-20s | %3s | %-40s ]\n"), PropertySegments.Num(),
		TEXT("Name"), TEXT("Idx"), TEXT("Access Type"));
	for (const FStateTreePropertySegment& PropertySegment : PropertySegments)
	{
		OutString += FString::Printf(TEXT("  | %-20s | %3d | %-40s |\n"),
									 *PropertySegment.Name.ToString(),
									 PropertySegment.ArrayIndex, *StaticEnum<EStateTreePropertyAccessType>()->GetValueAsString(PropertySegment.Type));
	}

	/** Array of property copies */
	OutString += FString::Printf(TEXT("\nPropertyCopies (%d)\n  [ %-7s | %-7s | %-7s | %-50s | %-4s ]\n"), PropertyCopies.Num(),
		TEXT("Src Idx"), TEXT("Tgt Idx"), TEXT("Struct"), TEXT("Copy Type"), TEXT("Size"));
	for (const FStateTreePropCopy& PropertyCopy : PropertyCopies)
	{
		OutString += FString::Printf(TEXT("  | %7d | %7d | %7d | %-50s | %4d |\n"),
					PropertyCopy.SourceAccessIndex, PropertyCopy.TargetAccessIndex, PropertyCopy.SourceStructIndex,
					*StaticEnum<EStateTreePropertyCopyType>()->GetValueAsString(PropertyCopy.Type),
					PropertyCopy.CopySize);
	}

	/** Array of property accesses, indexed by copies*/
	OutString += FString::Printf(TEXT("\nPropertyAccesses (%d)\n  [ %-8s [%-4s:%-4s [ |\n"), PropertyAccesses.Num(),
		TEXT("Indirect"), TEXT("Beg"), TEXT("End"));
	for (const FStateTreePropertyAccess& PropertyAccess : PropertyAccesses)
	{
		OutString += FString::Printf(TEXT("  | %8s [%4d:%-4d [ |\n"), TEXT(""), PropertyAccess.IndirectionsBegin, PropertyAccess.IndirectionsEnd);
	}

	/** Array of property indirections, indexed by accesses*/
	OutString += FString::Printf(TEXT("\nPropertyIndirections (%d)\n  [ %-4s | %-4s | %-40s ] \n"), PropertyIndirections.Num(),
		TEXT("Idx"), TEXT("Off."), TEXT("Access Type"));
	for (const FStateTreePropertyIndirection& PropertyIndirection : PropertyIndirections)
	{
		OutString += FString::Printf(TEXT("  | %4d | %4d | %-40s |\n"),
					PropertyIndirection.ArrayIndex, PropertyIndirection.Offset,*StaticEnum<EStateTreePropertyAccessType>()->GetValueAsString(PropertyIndirection.Type));
	}
}

FString FStateTreePropertyBindings::GetPathAsString(const FStateTreePropertyPath& Path, const int32 HighlightedSegment, const TCHAR* HighlightPrefix, const TCHAR* HighlightPostfix)
{
	FString Result;
	for (int32 i = Path.SegmentsBegin; i < Path.SegmentsEnd; i++)
	{
		const FStateTreePropertySegment& Segment = PropertySegments[i];
		if (i > 0)
		{
			Result += TEXT(".");
		}
		
		if (i == HighlightedSegment && HighlightPrefix)
		{
			Result += HighlightPrefix;
		}

		Result += Segment.Name.ToString();

		if (i == HighlightedSegment && HighlightPostfix)
		{
			Result += HighlightPostfix;
		}
	}
	return Result;
}


//----------------------------------------------------------------//
//  FStateTreeEditorPropertyPath
//----------------------------------------------------------------//

FString FStateTreeEditorPropertyPath::ToString(const int32 HighlightedSegment, const TCHAR* HighlightPrefix, const TCHAR* HighlightPostfix) const
{
	FString Result;
	for (int32 i = 0; i < Path.Num(); i++)
	{
		if (i > 0)
		{
			Result += TEXT(".");
		}
		if (i == HighlightedSegment && HighlightPrefix)
		{
			Result += HighlightPrefix;
		}

		Result += Path[i];

		if (i == HighlightedSegment && HighlightPostfix)
		{
			Result += HighlightPostfix;
		}
	}
	return Result;
}

bool FStateTreeEditorPropertyPath::operator==(const FStateTreeEditorPropertyPath& RHS) const
{
	if (StructID != RHS.StructID)
	{
		return false;
	}

	if (Path.Num() != RHS.Path.Num())
	{
		return false;
	}

	for (int32 i = 0; i < Path.Num(); i++)
	{
		if (Path[i] != RHS.Path[i])
		{
			return false;
		}
	}

	return true;
}
