// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlField.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"
#include "UObject/UnrealType.h"

namespace RemoteControlFieldUtils
{
	void ResolveSegment(FName SegmentName, UStruct* Owner, void* ContainerAddress, FRCFieldResolvedData& OutResolvedData)
	{
		if (FProperty* FoundField = FindFProperty<FProperty>(Owner, SegmentName))
		{
			OutResolvedData.ContainerAddress = ContainerAddress;
			OutResolvedData.Field = FoundField;
			OutResolvedData.Struct = Owner;
		}
		else
		{
			OutResolvedData.ContainerAddress = nullptr;
			OutResolvedData.Field = nullptr;
			OutResolvedData.Struct = nullptr;
		}
	}
}

FRemoteControlField::FRemoteControlField(EExposedFieldType InType, FName InLabel, FRCFieldPathInfo FieldPathInfo, TArray<FString> InComponentHierarchy)
	: FieldType(InType)
	, FieldName(FieldPathInfo.GetFieldName())
	, Label(InLabel)
	, Id(FGuid::NewGuid())
	, FieldPathInfo(MoveTemp(FieldPathInfo))
	, ComponentHierarchy(MoveTemp(InComponentHierarchy))
{
}

TArray<UObject*> FRemoteControlField::ResolveFieldOwners(const TArray<UObject*>& SectionObjects) const
{
	TArray<UObject*> FieldOwners;
	FieldOwners.Reserve(SectionObjects.Num());

	for (UObject* Object : SectionObjects)
	{
		//If component hierarchy is not empty, we need to walk it to find the child object
		if (ComponentHierarchy.Num() > 0)
		{
			UObject* Outer = Object;
			for (const FString& Component : ComponentHierarchy)
			{
				if (UObject* ResolvedFieldOwner = FindObject<UObject>(Outer, *Component))
				{
					Outer = ResolvedFieldOwner;
				}
				else
				{
					// This can happen when one of the grouped actors has a component named DefaultSceneRoot and one has a component StaticMeshComponent.
					// @todo: Change to a log if this situation can occur under normal conditions. (ie. Blueprint reinstanced)
					ensureAlwaysMsgf(false, TEXT("Could not resolve field owner for field %s"), *Object->GetName());
					Outer = nullptr;
					break;
				}
			}
			
			if (Outer)
			{
				FieldOwners.Add(Outer);
			}
		}
		else
		{
			FieldOwners.Add(Object);
		}
	}

	return FieldOwners;
}

bool FRemoteControlField::operator==(const FRemoteControlField& InField) const
{
	return InField.Id == Id;
}

bool FRemoteControlField::operator==(FGuid InFieldId) const
{
	return InFieldId == Id;
}

uint32 GetTypeHash(const FRemoteControlField& InField)
{
	return GetTypeHash(InField.Id);
}

FRemoteControlProperty::FRemoteControlProperty(FName InLabel, FRCFieldPathInfo FieldPathInfo, TArray<FString> InComponentHierarchy)
	: FRemoteControlField(EExposedFieldType::Property, InLabel, MoveTemp(FieldPathInfo), MoveTemp(InComponentHierarchy))
{}

FRemoteControlFunction::FRemoteControlFunction(FName InLabel, FRCFieldPathInfo FieldPathInfo, UFunction* InFunction)
	: FRemoteControlField(EExposedFieldType::Function, InLabel, MoveTemp(FieldPathInfo), TArray<FString>())
	, Function(InFunction)
{
	FunctionArguments = MakeShared<FStructOnScope>(Function);
	Function->InitializeStruct(FunctionArguments->GetStructMemory());
}

bool FRemoteControlFunction::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading() || Ar.IsSaving())
	{
		Ar << *this;
	}
	return true;
}

FArchive& operator<<(FArchive& Ar, FRemoteControlFunction& RCFunction)
{
	FRemoteControlFunction::StaticStruct()->SerializeTaggedProperties(Ar, (uint8*)&RCFunction, FRemoteControlFunction::StaticStruct(), nullptr);

	if (Ar.IsLoading())
	{
		RCFunction.FunctionArguments = MakeShared<FStructOnScope>(RCFunction.Function);
	}

	if (ensure(RCFunction.Function))
	{
		RCFunction.Function->SerializeTaggedProperties(Ar, RCFunction.FunctionArguments->GetStructMemory(), RCFunction.Function, nullptr);
	}

	return Ar;
}

bool FRCFieldPathInfo::ResolveInternalRecursive(UStruct* OwnerType, void* ContainerAddress, int32 SegmentIndex)
{
	const bool bLastSegment = (SegmentIndex == Segments.Num() -1);

	//Resolve the desired segment
	FRCFieldPathSegment& Segment = Segments[SegmentIndex];
	RemoteControlFieldUtils::ResolveSegment(Segment.Name, OwnerType, ContainerAddress, Segment.ResolvedData);
	
	if (bLastSegment == false)
	{
		if (Segment.IsResolved())
		{
			const int32 ArrayIndex = Segment.ArrayIndex == INDEX_NONE ? 0 : Segment.ArrayIndex;
			//Not the last segment so we'll call ourself again digging into structures / arrays / containers

			FProperty* Property = Segment.ResolvedData.Field;
			if (FStructProperty* StructureProperty = CastField<FStructProperty>(Property))
			{
				return ResolveInternalRecursive(StructureProperty->Struct, StructureProperty->ContainerPtrToValuePtr<void>(ContainerAddress, ArrayIndex), SegmentIndex + 1);
			}
			else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				//Look for the kind of array this is. Since it's not the final segment, it must be a container thing
				if (FStructProperty* ArrayInnerStructureProperty = CastField<FStructProperty>(ArrayProperty->Inner))
				{
					FScriptArrayHelper_InContainer ArrayHelper(ArrayProperty, ContainerAddress);
					if (ArrayHelper.IsValidIndex(ArrayIndex))
					{
						return ResolveInternalRecursive(ArrayInnerStructureProperty->Struct, reinterpret_cast<void*>(ArrayHelper.GetRawPtr(ArrayIndex)), SegmentIndex + 1);
					}
				}
			}
			else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
			{
				//Look for the kind of set this is. Since it's not the final segment, it must be a container thing
				if (FStructProperty* SetInnerStructureProperty = CastField<FStructProperty>(SetProperty->ElementProp))
				{
					FScriptSetHelper_InContainer SetHelper(SetProperty, ContainerAddress);
					if (SetHelper.IsValidIndex(ArrayIndex))
					{
						return ResolveInternalRecursive(SetInnerStructureProperty->Struct, reinterpret_cast<void*>(SetHelper.GetElementPtr(ArrayIndex)), SegmentIndex + 1);
					}
				}
			}
			else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
			{
				//Look for the kind of set this is. Since it's not the final segment, it must be a container thing
				if (FStructProperty* SetInnerStructureProperty = CastField<FStructProperty>(MapProperty->ValueProp))
				{
					FScriptMapHelper_InContainer MapHelper(MapProperty, ContainerAddress);
					if (MapHelper.IsValidIndex(ArrayIndex))
					{
						return ResolveInternalRecursive(SetInnerStructureProperty->Struct, reinterpret_cast<void*>(MapHelper.GetValuePtr(ArrayIndex)), SegmentIndex + 1);
					}
				}
			}

			//Add support for missing types if required (SoftObjPtr, ObjectProperty, etc...)
			return false;
		}
	}
	
	return Segment.IsResolved();
}

FRCFieldPathInfo::FRCFieldPathInfo(const FString& PathInfo, bool bCleanDuplicates)
{
	TArray<FString> PathSegment;
	PathInfo.ParseIntoArray(PathSegment, TEXT("."));

	Segments.Reserve(PathSegment.Num());
	for (int32 Index = 0; Index < PathSegment.Num(); ++Index)
	{
		const FString& SegmentString = PathSegment[Index];
		FRCFieldPathSegment NewSegment(SegmentString);

		if (bCleanDuplicates && Index > 0 && NewSegment.ArrayIndex != INDEX_NONE)
		{
			FRCFieldPathSegment& PreviousSegment = Segments[Segments.Num() - 1];
			if (PreviousSegment.Name == NewSegment.Name)
			{
				//Skip duplicate entries if required for GeneratePathToProperty style (Array.Array[Index])
				PreviousSegment.ArrayIndex = NewSegment.ArrayIndex;
				continue;
			}
		}
		
		Segments.Emplace(MoveTemp(NewSegment));
	}

	PathHash = GetTypeHash(PathInfo);
}

bool FRCFieldPathInfo::Resolve(UObject* Owner)
{
	if (Owner == nullptr)
	{
		return false;
	}

	if (Segments.Num() <= 0)
	{
		return false;
	}

	void* ContainerAddress = reinterpret_cast<void*>(Owner);
	UStruct* Type = Owner->GetClass();
	return ResolveInternalRecursive(Type, ContainerAddress, 0);
}

bool FRCFieldPathInfo::IsResolved() const
{
	const int32 SegmentCount = GetSegmentCount();
	if (SegmentCount <= 0)
	{
		return false;
	}

	return GetFieldSegment(SegmentCount-1).IsResolved();
}

bool FRCFieldPathInfo::IsEqual(FStringView OtherPath) const
{
	return GetTypeHash(OtherPath) == PathHash;
}

bool FRCFieldPathInfo::IsEqual(const FRCFieldPathInfo& OtherPath) const
{
	return OtherPath.PathHash == PathHash;
}

FString FRCFieldPathInfo::ToString(int32 EndSegment /*= INDEX_NONE*/) const
{
	const int32 LastSegment = EndSegment == INDEX_NONE ? Segments.Num() : FMath::Min(Segments.Num(), EndSegment);
	FString FullPath; 
	for (int32 SegmentIndex = 0; SegmentIndex < LastSegment; ++SegmentIndex)
	{
		const FRCFieldPathSegment& Segment = GetFieldSegment(SegmentIndex);

		// Segment
		FullPath += Segment.ToString();

		// Delimiter
		if (SegmentIndex < GetSegmentCount() - 1)
		{
			FullPath += TEXT(".");
		}
	}

	return FullPath;
}

FString FRCFieldPathInfo::ToPathPropertyString(int32 EndSegment /*= INDEX_NONE*/) const
{
	const int32 LastSegment = EndSegment == INDEX_NONE ? Segments.Num() : FMath::Min(Segments.Num(), EndSegment);
	FString FullPath;
	for (int32 SegmentIndex = 0; SegmentIndex < LastSegment; ++SegmentIndex)
	{
		const FRCFieldPathSegment& Segment = GetFieldSegment(SegmentIndex);

		// Segment
		FullPath += Segment.ToString(true /*bDuplicateContainer*/);

		// Delimiter
		if (SegmentIndex < GetSegmentCount() - 1)
		{
			FullPath += TEXT(".");
		}
	}

	return FullPath;
}

const FRCFieldPathSegment& FRCFieldPathInfo::GetFieldSegment(int32 Index) const
{
	check(Segments.IsValidIndex(Index));
	return Segments[Index];
}

FRCFieldResolvedData FRCFieldPathInfo::GetResolvedData() const
{
	if (IsResolved())
	{
		return GetFieldSegment(GetSegmentCount() - 1).ResolvedData;
	}

	return FRCFieldResolvedData();
}

FName FRCFieldPathInfo::GetFieldName() const
{
	if (GetSegmentCount() <= 0)
	{
		return NAME_None;
	}

	return *GetFieldSegment(GetSegmentCount() - 1).ToString();
}

FRCFieldPathSegment::FRCFieldPathSegment(FStringView SegmentName)
{
	bool bValidSegment = false;

	int32 FieldNameEnd = MAX_int32;
	int32 OpenBracketIndex;
	if (SegmentName.FindChar('[', OpenBracketIndex))
	{
		if (OpenBracketIndex > 0)
		{
			FieldNameEnd = OpenBracketIndex;
			//Found an open bracket, find the closing one
			int32 CloseBracketIndex;
			if (SegmentName.FindChar(']', CloseBracketIndex) && (CloseBracketIndex > OpenBracketIndex + 1))
			{
				//Brackets found so take that index in the middle
				FStringView IndexString = SegmentName.Mid(OpenBracketIndex + 1, CloseBracketIndex - OpenBracketIndex - 1);
				ArrayIndex = FCString::Atoi(IndexString.GetData());
				bValidSegment = true;
			}
		}
	}
	else
	{
		bValidSegment = true;
	}

	if (bValidSegment)
	{
		FStringView FieldName = SegmentName.Mid(0, FieldNameEnd);
		Name = FName(FieldName);
	}
}

bool FRCFieldPathSegment::IsResolved() const
{
	return ResolvedData.Field != nullptr
		&& ResolvedData.ContainerAddress != nullptr
		&& ResolvedData.Struct != nullptr;
}

FString FRCFieldPathSegment::ToString(bool bDuplicateContainer) const
{
	if (ArrayIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("%s"), *Name.ToString());
	}
	else
	{
		//Special case for GeneratePathToProperty match
		if (bDuplicateContainer)
		{
			return FString::Printf(TEXT("%s.%s[%d]"), *Name.ToString(), *Name.ToString(), ArrayIndex);
		}
		else
		{
			return FString::Printf(TEXT("%s[%d]"), *Name.ToString(), ArrayIndex);
		}
	}
}

void FRCFieldPathSegment::ClearResolvedData()
{
	ResolvedData = FRCFieldResolvedData();
}

FPropertyChangedEvent FRCFieldPathInfo::ToPropertyChangedEvent(EPropertyChangeType::Type InChangeType) const
{
	check(IsResolved());

	FPropertyChangedEvent PropertyChangedEvent(GetFieldSegment(GetSegmentCount() - 1).ResolvedData.Field, InChangeType);

	// Set a containing 'struct' if we need to
	if (GetSegmentCount() > 1)
	{
		PropertyChangedEvent.SetActiveMemberProperty(GetFieldSegment(0).ResolvedData.Field);
	}

	return PropertyChangedEvent;
}

void FRCFieldPathInfo::ToEditPropertyChain(FEditPropertyChain& OutPropertyChain) const
{
	check(IsResolved());

	//Go over the segment chain to build the property changed chain skipping duplicates
	for (int32 Index = 0; Index < GetSegmentCount(); ++Index)
	{
		const FRCFieldPathSegment& Segment = GetFieldSegment(Index);
		OutPropertyChain.AddTail(Segment.ResolvedData.Field);
	}

	OutPropertyChain.SetActivePropertyNode(OutPropertyChain.GetTail()->GetValue());
	if (GetSegmentCount() > 1)
	{
		OutPropertyChain.SetActiveMemberPropertyNode(OutPropertyChain.GetHead()->GetValue());
	}
}
