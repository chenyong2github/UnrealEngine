// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyAccessEditor.h"
#include "PropertyAccess.h"
#include "PropertyPathHelpers.h"
#include "Algo/Transform.h"
#include "IPropertyAccessEditor.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

#define LOCTEXT_NAMESPACE "PropertyAccessEditor"

struct FPropertyAccessEditorSystem
{
	struct FResolveSegmentsContext
	{
		FResolveSegmentsContext(const UStruct* InStruct, TArrayView<FString> InPath, FPropertyAccessPath& InAccessPath)
			: Struct(InStruct)
			, CurrentStruct(InStruct)
			, Path(InPath)
			, AccessPath(InAccessPath)
		{}

		// Starting struct
		const UStruct* Struct;

		// Current struct
		const UStruct* CurrentStruct;

		// Path as FStrings with optional array markup
		TArrayView<FString> Path;

		// The access path we are building
		FPropertyAccessPath& AccessPath;

		// Output segments
		TArray<FPropertyAccessSegment> Segments;

		// The last error message produced
		FText ErrorMessage;

		// The current segment index (or that at which the last error occurred)
		int32 SegmentIndex;

		// Whether this is the final segment
		bool bFinalSegment;
	};

	// The result of a segment resolve operation
	enum class ESegmentResolveResult
	{
		Failed,

		SucceededInternal,

		SucceededExternal,
	};

	static ESegmentResolveResult ResolveSegments_CheckProperty(FPropertyAccessSegment& InSegment, FProperty* InProperty, FResolveSegmentsContext& InContext)
	{
		ESegmentResolveResult Result = ESegmentResolveResult::SucceededInternal;

		InSegment.Property = InProperty;

		// Check to see if it is an array first, as arrays get handled the same for 'leaf' and 'branch' nodes
		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty);
		if(ArrayProperty != nullptr && InSegment.ArrayIndex != INDEX_NONE)
		{
			// It is an array, now check to see if this is an array of structures
			if(FStructProperty* ArrayOfStructsProperty = CastField<FStructProperty>(ArrayProperty->Inner))
			{
				InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::ArrayOfStructs;
				InSegment.Struct = ArrayOfStructsProperty->Struct;
			}
			// if it's not an array of structs, maybe it's an array of objects
			else if(FObjectPropertyBase* ArrayOfObjectsProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
			{
				InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::ArrayOfObjects;
				InSegment.Struct = ArrayOfObjectsProperty->PropertyClass;
				if(!InContext.bFinalSegment)
				{
					Result = ESegmentResolveResult::SucceededExternal;
				}
			}
			else
			{
				InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::Array;
			}
		}
		// Leaf segments all get treated the same, plain, struct or object
		else if(InContext.bFinalSegment)
		{
			InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::Leaf;
			InSegment.Struct = nullptr;
		}
		// Check to see if this is a simple structure (eg. not an array of structures)
		else if(FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
		{
			InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::Struct;
			InSegment.Struct = StructProperty->Struct;
		}
		// Check to see if this is a simple object (eg. not an array of objects)
		else if(FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InProperty))
		{
			InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::Object;
			InSegment.Struct = ObjectProperty->PropertyClass;
			if(!InContext.bFinalSegment)
			{
				Result = ESegmentResolveResult::SucceededExternal;
			}
		}
		// Check to see if this is a simple weak object property (eg. not an array of weak objects).
		else if(FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(InProperty))
		{
			InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::WeakObject;
			InSegment.Struct = WeakObjectProperty->PropertyClass;
			if(!InContext.bFinalSegment)
			{
				Result = ESegmentResolveResult::SucceededExternal;
			}
		}
		// Check to see if this is a simple soft object property (eg. not an array of soft objects).
		else if(FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(InProperty))
		{
			InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::SoftObject;
			InSegment.Struct = SoftObjectProperty->PropertyClass;
			if(!InContext.bFinalSegment)
			{
				Result = ESegmentResolveResult::SucceededExternal;
			}
		}
		else
		{
			return ESegmentResolveResult::Failed;
		}

		static const FName PropertyEventMetadata("PropertyEvent");
		if(InProperty->HasMetaData(PropertyEventMetadata))
		{
			InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::Event;
		}

		return Result;
	}

	static ESegmentResolveResult ResolveSegments_CheckFunction(FPropertyAccessSegment& InSegment, UFunction* InFunction, FResolveSegmentsContext& InContext)
	{
		InSegment.Function = InFunction;
		InSegment.Flags |= (uint16)EPropertyAccessSegmentFlags::Function;

		// Functions are always 'getters', so we need to verify their form
		if(InFunction->NumParms != 1)
		{
			InContext.ErrorMessage = FText::Format(LOCTEXT("FunctionHasTooManyParameters", "Function '{0}' has too many parameters in property path for @@"), FText::FromName(InFunction->GetFName()));
			return ESegmentResolveResult::Failed;
		}

		FProperty* ReturnProperty = InFunction->GetReturnProperty();
		if(ReturnProperty == nullptr)
		{
			InContext.ErrorMessage = FText::Format(LOCTEXT("FunctionHasNoReturnValue", "Function '{0}' has no return value in property path for @@"), FText::FromName(InFunction->GetFName()));
			return ESegmentResolveResult::Failed;
		}

		// Treat the function's return value as the struct/class we want to use for the next segment
		ESegmentResolveResult Result = ResolveSegments_CheckProperty(InSegment, ReturnProperty, InContext);
		if(Result != ESegmentResolveResult::Failed)
		{
			// See if a function's thread safety means it should be considered 'external'.
			// Note that this logic means that an external (ie. thread unsafe) object dereference returned from ResolveSegments_CheckProperty
			// can be overridden here if the function that returns the value promises that it is thread safe to access that object.
			// An example of this is would be something like accessing the main anim BP from a linked anim BP, where it is 'safe' to access
			// the other object while running animation updated on a worker thread.
			const UClass* FunctionClass = InFunction->GetOuterUClass();
			const bool bThreadSafe = InFunction->HasMetaData(TEXT("BlueprintThreadSafe")) || (FunctionClass && FunctionClass->HasMetaData(TEXT("BlueprintThreadSafe")) && !InFunction->HasMetaData(TEXT("NotBlueprintThreadSafe")));
			Result = bThreadSafe ? ESegmentResolveResult::SucceededInternal : ESegmentResolveResult::SucceededExternal;
		}

		return Result;
	}

	// Called at compile time to build out a segments array
	static EPropertyAccessResolveResult ResolveSegments(FResolveSegmentsContext& InContext)
	{
		check(InContext.Struct);

		if(InContext.Path.Num() > 0)
		{
			EPropertyAccessResolveResult Result = EPropertyAccessResolveResult::SucceededInternal;

			for(int32 SegmentIndex = 0; SegmentIndex < InContext.Path.Num(); ++SegmentIndex)
			{
				const FString& SegmentString = InContext.Path[SegmentIndex];

				FPropertyAccessSegment& Segment = InContext.Segments.AddDefaulted_GetRef();
				const TCHAR* PropertyNamePtr = nullptr;
				int32 PropertyNameLength = 0;
				int32 ArrayIndex = INDEX_NONE;
				PropertyPathHelpers::FindFieldNameAndArrayIndex(SegmentString.Len(), *SegmentString, PropertyNameLength, &PropertyNamePtr, ArrayIndex);
				ensure(PropertyNamePtr != nullptr);
				FString PropertyNameString(PropertyNameLength, PropertyNamePtr);
				Segment.Name = FName(*PropertyNameString, FNAME_Find);
				Segment.ArrayIndex = ArrayIndex;

				InContext.SegmentIndex = SegmentIndex;
				InContext.bFinalSegment = SegmentIndex == InContext.Path.Num() - 1;

				if(InContext.CurrentStruct == nullptr)
				{
					InContext.ErrorMessage = LOCTEXT("MalformedPath", "Malformed property path for @@");
					return EPropertyAccessResolveResult::Failed;
				}

				// Obtain the property/function from the given structure definition
				FFieldVariant Field = FindUFieldOrFProperty(InContext.CurrentStruct, Segment.Name);
				if(!Field.IsValid())
				{
					InContext.ErrorMessage = FText::Format(LOCTEXT("InvalidField", "Invalid field '{0}' found in property path for @@"), FText::FromName(Segment.Name));
					return EPropertyAccessResolveResult::Failed;
				}

				if(FProperty* Property = Field.Get<FProperty>())
				{
					ESegmentResolveResult PropertyResult = ResolveSegments_CheckProperty(Segment, Property, InContext);
					if(PropertyResult == ESegmentResolveResult::SucceededExternal)
					{
						Result = EPropertyAccessResolveResult::SucceededExternal;
					}
					else if(PropertyResult == ESegmentResolveResult::Failed)
					{
						return EPropertyAccessResolveResult::Failed;
					}
				}
				else if(UFunction* Function = Field.Get<UFunction>())
				{
					ESegmentResolveResult FunctionResult = ResolveSegments_CheckFunction(Segment, Function, InContext);
					if(FunctionResult == ESegmentResolveResult::SucceededExternal)
					{
						Result = EPropertyAccessResolveResult::SucceededExternal;
					}
					else if(FunctionResult == ESegmentResolveResult::Failed)
					{
						return EPropertyAccessResolveResult::Failed;
					}
				}

				InContext.CurrentStruct = Segment.Struct;
			}

			return InContext.Segments.Num() > 0 ? Result : EPropertyAccessResolveResult::Failed;
		}
		else
		{
			InContext.ErrorMessage = LOCTEXT("InvalidPath", "Invalid path found for @@");
			return EPropertyAccessResolveResult::Failed;
		}
	}

	static EPropertyAccessResolveResult ResolveLeafProperty(const UStruct* InStruct, TArrayView<FString> InPath, FProperty*& OutProperty, int32& OutArrayIndex)
	{
		FPropertyAccessPath AccessPath;
		FResolveSegmentsContext Context(InStruct, InPath, AccessPath);
		EPropertyAccessResolveResult Result = ResolveSegments(Context);
		if(Result != EPropertyAccessResolveResult::Failed)
		{
			const FPropertyAccessSegment& LeafSegment = Context.Segments.Last();
			OutProperty = LeafSegment.Property.Get();
			OutArrayIndex = LeafSegment.ArrayIndex;
			return Result;
		}

		return Result;
	}

	static EPropertyAccessCopyType GetCopyType(const FPropertyAccessSegment& InSrcSegment, const FPropertyAccessSegment& InDestSegment)
	{
		FProperty* SrcProperty = InSrcSegment.Property.Get();
		check(SrcProperty);

		if(FArrayProperty* SrcArrayProperty = CastField<FArrayProperty>(SrcProperty))
		{
			// use the array's inner property if we are not trying to copy the whole array
			if(InSrcSegment.ArrayIndex != INDEX_NONE)
			{
				SrcProperty = SrcArrayProperty->Inner;
			}
		}

		FProperty* DestProperty = InDestSegment.Property.Get();
		check(DestProperty);

		if(FArrayProperty* DestArrayProperty = CastField<FArrayProperty>(DestProperty))
		{
			// use the array's inner property if we are not trying to copy the whole array
			if(InDestSegment.ArrayIndex != INDEX_NONE)
			{
				DestProperty = DestArrayProperty->Inner;
			}
		}

		EPropertyAccessCompatibility Compatibility = PropertyAccess::GetPropertyCompatibility(SrcProperty, DestProperty);
		if(Compatibility == EPropertyAccessCompatibility::Compatible)
		{
			if (CastField<FNameProperty>(DestProperty))
			{
				return EPropertyAccessCopyType::Name;
			}
			else if (CastField<FBoolProperty>(DestProperty))
			{
				return EPropertyAccessCopyType::Bool;
			}
			else if (CastField<FStructProperty>(DestProperty))
			{
				return EPropertyAccessCopyType::Struct;
			}
			else if (CastField<FObjectPropertyBase>(DestProperty))
			{
				return EPropertyAccessCopyType::Object;
			}
			else if (CastField<FArrayProperty>(DestProperty) && DestProperty->HasAnyPropertyFlags(CPF_EditFixedSize))
			{
				// only apply array copying rules if the destination array is fixed size, otherwise it will be 'complex'
				return EPropertyAccessCopyType::Array;
			}
			else if(DestProperty->PropertyFlags & CPF_IsPlainOldData)
			{
				return EPropertyAccessCopyType::Plain;
			}
			else
			{
				return EPropertyAccessCopyType::Complex;
			}
		}
		else if(Compatibility == EPropertyAccessCompatibility::Promotable)
		{
			if(SrcProperty->IsA<FBoolProperty>())
			{
				if(DestProperty->IsA<FByteProperty>())
				{
					return EPropertyAccessCopyType::PromoteBoolToByte;
				}
				else if(DestProperty->IsA<FIntProperty>())
				{
					return EPropertyAccessCopyType::PromoteBoolToInt32;
				}
				else if(DestProperty->IsA<FInt64Property>())
				{
					return EPropertyAccessCopyType::PromoteBoolToInt64;
				}
				else if(DestProperty->IsA<FFloatProperty>())
				{
					return EPropertyAccessCopyType::PromoteBoolToFloat;
				}
			}
			else if(SrcProperty->IsA<FByteProperty>())
			{
				if(DestProperty->IsA<FIntProperty>())
				{
					return EPropertyAccessCopyType::PromoteByteToInt32;
				}
				else if(DestProperty->IsA<FInt64Property>())
				{
					return EPropertyAccessCopyType::PromoteByteToInt64;
				}
				else if(DestProperty->IsA<FFloatProperty>())
				{
					return EPropertyAccessCopyType::PromoteByteToFloat;
				}
			}
			else if(SrcProperty->IsA<FIntProperty>())
			{
				if(DestProperty->IsA<FInt64Property>())
				{
					return EPropertyAccessCopyType::PromoteInt32ToInt64;
				}
				else if(DestProperty->IsA<FFloatProperty>())
				{
					return EPropertyAccessCopyType::PromoteInt32ToFloat;
				}
			}
		}

		checkf(false, TEXT("Couldnt determine property copy type (%s -> %s)"), *SrcProperty->GetName(), *DestProperty->GetName());

		return EPropertyAccessCopyType::None;
	}

	static bool CompileCopy(const UStruct* InStruct, FPropertyAccessLibrary& InLibrary, FPropertyAccessLibraryCompiler::FQueuedCopy& OutCopy)
	{
		FPropertyAccessPath SrcAccessPath;
		FPropertyAccessPath DestAccessPath;

		FResolveSegmentsContext SrcContext(InStruct, OutCopy.SourcePath, SrcAccessPath);
		FResolveSegmentsContext DestContext(InStruct, OutCopy.DestPath, DestAccessPath);

		OutCopy.SourceResult = ResolveSegments(SrcContext);
		OutCopy.SourceErrorText = SrcContext.ErrorMessage;
		OutCopy.DestResult = ResolveSegments(DestContext);
		OutCopy.DestErrorText = SrcContext.ErrorMessage;

		if(OutCopy.SourceResult != EPropertyAccessResolveResult::Failed && OutCopy.DestResult != EPropertyAccessResolveResult::Failed)
		{
			const bool bExternal = OutCopy.SourceResult == EPropertyAccessResolveResult::SucceededExternal ||  OutCopy.DestResult == EPropertyAccessResolveResult::SucceededExternal;

			// Decide on batch type
			EPropertyAccessCopyBatch CopyBatch;
			if(bExternal)
			{
				CopyBatch = OutCopy.BatchType == EPropertyAccessBatchType::Unbatched ? EPropertyAccessCopyBatch::ExternalUnbatched : EPropertyAccessCopyBatch::ExternalBatched;
			}
			else
			{
				CopyBatch = OutCopy.BatchType == EPropertyAccessBatchType::Unbatched ? EPropertyAccessCopyBatch::InternalUnbatched : EPropertyAccessCopyBatch::InternalBatched;
			}

			OutCopy.BatchIndex = InLibrary.CopyBatches[(__underlying_type(EPropertyAccessCopyBatch))CopyBatch].Copies.Num();
			FPropertyAccessCopy& Copy = InLibrary.CopyBatches[(__underlying_type(EPropertyAccessCopyBatch))CopyBatch].Copies.AddDefaulted_GetRef();
			Copy.AccessIndex = InLibrary.SrcPaths.Num();
			Copy.DestAccessStartIndex = InLibrary.DestPaths.Num();
			Copy.DestAccessEndIndex = InLibrary.DestPaths.Num() + 1;
			Copy.Type = GetCopyType(SrcContext.Segments.Last(), DestContext.Segments.Last());

			SrcAccessPath.PathSegmentStartIndex = InLibrary.PathSegments.Num();
			SrcAccessPath.PathSegmentCount = SrcContext.Segments.Num();
			InLibrary.SrcPaths.Add(SrcAccessPath);
			InLibrary.PathSegments.Append(SrcContext.Segments);

			DestAccessPath.PathSegmentStartIndex = InLibrary.PathSegments.Num();
			DestAccessPath.PathSegmentCount = DestContext.Segments.Num();
			InLibrary.DestPaths.Add(DestAccessPath);
			InLibrary.PathSegments.Append(DestContext.Segments);

			return true;
		}

		return false;
	}
};

namespace PropertyAccess
{
	EPropertyAccessResolveResult ResolveLeafProperty(const UStruct* InStruct, TArrayView<FString> InPath, FProperty*& OutProperty, int32& OutArrayIndex)
	{
		return ::FPropertyAccessEditorSystem::ResolveLeafProperty(InStruct, InPath, OutProperty, OutArrayIndex);
	}

	EPropertyAccessCompatibility GetPropertyCompatibility(const FProperty* InPropertyA, const FProperty* InPropertyB)
	{
		if(InPropertyA == InPropertyB)
		{
			return EPropertyAccessCompatibility::Compatible;
		}

		if(InPropertyA == nullptr || InPropertyB == nullptr)
		{
			return EPropertyAccessCompatibility::Incompatible;
		}

		// Special case for object properties
		if(InPropertyA->IsA<FObjectPropertyBase>() && InPropertyB->IsA<FObjectPropertyBase>())
		{
			return EPropertyAccessCompatibility::Compatible;
		}

		// Extract underlying types for enums
		if(const FEnumProperty* EnumPropertyA = CastField<const FEnumProperty>(InPropertyA))
		{
			InPropertyA = EnumPropertyA->GetUnderlyingProperty();
		}

		if(const FEnumProperty* EnumPropertyB = CastField<const FEnumProperty>(InPropertyB))
		{
			InPropertyB = EnumPropertyB->GetUnderlyingProperty();
		}

		if(InPropertyA->SameType(InPropertyB))
		{
			return EPropertyAccessCompatibility::Compatible;
		}
		else
		{
			// Not directly compatible, check for promotions
			if(InPropertyA->IsA<FBoolProperty>())
			{
				if(InPropertyB->IsA<FByteProperty>() || InPropertyB->IsA<FIntProperty>() || InPropertyB->IsA<FInt64Property>() || InPropertyB->IsA<FFloatProperty>())
				{
					return EPropertyAccessCompatibility::Promotable;
				}
			}
			else if(InPropertyA->IsA<FByteProperty>())
			{
				if(InPropertyB->IsA<FIntProperty>() || InPropertyB->IsA<FInt64Property>() || InPropertyB->IsA<FFloatProperty>())
				{
					return EPropertyAccessCompatibility::Promotable;
				}
			}
			else if(InPropertyA->IsA<FIntProperty>())
			{
				if(InPropertyB->IsA<FInt64Property>() || InPropertyB->IsA<FFloatProperty>())
				{
					return EPropertyAccessCompatibility::Promotable;
				}
			}
		}

		return EPropertyAccessCompatibility::Incompatible;
	}

	EPropertyAccessCompatibility GetPinTypeCompatibility(const FEdGraphPinType& InPinTypeA, const FEdGraphPinType& InPinTypeB)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if(Schema->ArePinTypesCompatible(InPinTypeA, InPinTypeB))
		{
			return EPropertyAccessCompatibility::Compatible;
		}
		else
		{
			// Not directly compatible, check for promotions
			if(InPinTypeA.PinCategory == UEdGraphSchema_K2::PC_Boolean)
			{
				if(InPinTypeB.PinCategory == UEdGraphSchema_K2::PC_Byte || InPinTypeB.PinCategory == UEdGraphSchema_K2::PC_Int || InPinTypeB.PinCategory == UEdGraphSchema_K2::PC_Int64 || InPinTypeB.PinCategory == UEdGraphSchema_K2::PC_Float)
				{
					return EPropertyAccessCompatibility::Promotable;
				}
			}
			else if(InPinTypeA.PinCategory == UEdGraphSchema_K2::PC_Byte)
			{
				if(InPinTypeB.PinCategory == UEdGraphSchema_K2::PC_Int || InPinTypeB.PinCategory == UEdGraphSchema_K2::PC_Int64 || InPinTypeB.PinCategory == UEdGraphSchema_K2::PC_Float)
				{
					return EPropertyAccessCompatibility::Promotable;
				}
			}
			else if(InPinTypeA.PinCategory == UEdGraphSchema_K2::PC_Int)
			{
				if(InPinTypeB.PinCategory == UEdGraphSchema_K2::PC_Int64 || InPinTypeB.PinCategory == UEdGraphSchema_K2::PC_Float)
				{
					return EPropertyAccessCompatibility::Promotable;
				}
			}
		}

		return EPropertyAccessCompatibility::Incompatible;
	}

	void MakeStringPath(const TArray<FBindingChainElement>& InBindingChain, TArray<FString>& OutStringPath)
	{
		Algo::Transform(InBindingChain, OutStringPath, [](const FBindingChainElement& InElement)
		{
			if(FProperty* Property = InElement.Field.Get<FProperty>())
			{
				if(InElement.ArrayIndex == INDEX_NONE)
				{
					return Property->GetName();
				}
				else
				{
					return FString::Printf(TEXT("%s[%d]"), *Property->GetName(), InElement.ArrayIndex);
				}
			}
			else if(UFunction* Function = InElement.Field.Get<UFunction>())
			{
				return Function->GetName();
			}
			else
			{
				check(false);
				return FString();
			}
		});
	}
}

FPropertyAccessLibraryCompiler::FPropertyAccessLibraryCompiler()
	: Library(nullptr)
	, Class(nullptr)
{
}

void FPropertyAccessLibraryCompiler::BeginCompilation(const UClass* InClass)
{
	if(Class && Library)
	{
		*Library = FPropertyAccessLibrary();
	}
}

int32 FPropertyAccessLibraryCompiler::AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InAssociatedObject)
{
	FQueuedCopy QueuedCopy;
	QueuedCopy.SourcePath = InSourcePath;
	QueuedCopy.DestPath = InDestPath;
	QueuedCopy.BatchType = InBatchType;
	QueuedCopy.AssociatedObject = InAssociatedObject;

	QueuedCopies.Add(MoveTemp(QueuedCopy));

	return QueuedCopies.Num() - 1;
}

bool FPropertyAccessLibraryCompiler::FinishCompilation()
{
	if(Class && Library)
	{
		bool bResult = true;
		for(int32 CopyIndex = 0; CopyIndex < QueuedCopies.Num(); ++CopyIndex)
		{
			FQueuedCopy& Copy = QueuedCopies[CopyIndex];
			bResult &= ::FPropertyAccessEditorSystem::CompileCopy(Class, *Library, Copy);
			CopyMap.Add(CopyIndex, Copy.BatchIndex);
		}

		// Always rebuild the library even if we detected a 'failure'. Otherwise we could fail to copy data for both
		// valid and invalid copies 
		PropertyAccess::PostLoadLibrary(*Library);

		return bResult;
	}

	return false;
}

void FPropertyAccessLibraryCompiler::IterateErrors(TFunctionRef<void(const FText&, UObject*)> InFunction) const
{
	if(Class && Library)
	{
		for(const FQueuedCopy& Copy : QueuedCopies)
		{
			if(Copy.SourceResult == EPropertyAccessResolveResult::Failed)
			{
				InFunction(Copy.SourceErrorText, Copy.AssociatedObject);
			}

			if(Copy.DestResult == EPropertyAccessResolveResult::Failed)
			{
				InFunction(Copy.DestErrorText, Copy.AssociatedObject);
			}
		}
	}
}

int32 FPropertyAccessLibraryCompiler::MapCopyIndex(int32 InIndex) const
{
	if(const int32* FoundIndex = CopyMap.Find(InIndex))
	{
		return *FoundIndex;
	}

	return INDEX_NONE;
}

void FPropertyAccessLibraryCompiler::Setup(const UClass* InClass, FPropertyAccessLibrary* InLibrary)
{
	Class = InClass;
	Library = InLibrary;
}

#undef LOCTEXT_NAMESPACE