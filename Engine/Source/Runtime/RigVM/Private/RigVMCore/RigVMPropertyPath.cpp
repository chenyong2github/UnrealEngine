// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMPropertyPath.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FRigVMPropertyPath FRigVMPropertyPath::Empty;

FRigVMPropertyPath::FRigVMPropertyPath()
: Path()
, Segments()
{
}

FRigVMPropertyPath::FRigVMPropertyPath(const FProperty* InProperty, const FString& InSegmentPath)
: Path()
, Segments()
{
	check(InProperty);
	check(!InSegmentPath.IsEmpty())

	const FProperty* Property = InProperty;
	FString WorkPath = InSegmentPath;
	WorkPath = WorkPath.Replace(TEXT("["), TEXT("."));
	WorkPath = WorkPath.Replace(TEXT("]"), TEXT("."));
	WorkPath = WorkPath.Replace(TEXT(".."), TEXT("."));
	WorkPath.TrimCharInline('.', nullptr);

	while(!WorkPath.IsEmpty())
	{
		FString PathSegment, PathRemainder;
		if(!WorkPath.Split(TEXT("."), &PathSegment, &PathRemainder))
		{
			PathSegment = WorkPath;
			PathRemainder.Empty();
		}

		FRigVMPropertyPathSegment Segment;

		if(const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			Segment.Type = ERigVMPropertyPathSegmentType::StructMember;

			if(const FProperty* MemberProperty = StructProperty->Struct->FindPropertyByName(*PathSegment))
			{
				Segment.Name = MemberProperty->GetFName();
				Segment.Index = MemberProperty->GetOffset_ForInternal();
				Property = Segment.Property = MemberProperty;
			}
			else
			{
				Segments.Empty();
				return;
			}
		}
		else if(const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			Segment.Type = ERigVMPropertyPathSegmentType::ArrayElement;
			const FProperty* ElementProperty = ArrayProperty->Inner;
			Segment.Name = ElementProperty->GetFName();
			Segment.Index = FCString::Atoi(*PathSegment);
			Segment.Property = ArrayProperty;
			Property = ElementProperty;
		}
		else if(const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			check(MapProperty->KeyProp->IsA<FNameProperty>());
			
			Segment.Type = ERigVMPropertyPathSegmentType::MapValue;
			const FProperty* ValueProperty = MapProperty->ValueProp;
			Segment.Name = *PathSegment;
			Segment.Index = INDEX_NONE;
			Segment.Property = MapProperty;
			Property = ValueProperty;
		}
		else
		{
			Segments.Empty();
			return;
		}

		Segments.Add(Segment);
		WorkPath = PathRemainder;
	}

	Path.Empty();

	for(const FRigVMPropertyPathSegment& Segment : Segments)
	{
		switch(Segment.Type)
		{
			case ERigVMPropertyPathSegmentType::StructMember:
			{
				if(!Path.IsEmpty())
				{
					Path += TEXT(".");
				}
				Path += Segment.Name.ToString();
				break;
			}
			case ERigVMPropertyPathSegmentType::ArrayElement:
			{
				Path += TEXT("[") + FString::FromInt(Segment.Index) + TEXT("]");
				break;
			}
			case ERigVMPropertyPathSegmentType::MapValue:
			{
				Path += TEXT("[") + Segment.Name.ToString() + TEXT("]");
				break;
			}
			default:
			{
				break;
			}
		}
	}
}

FRigVMPropertyPath::FRigVMPropertyPath(const FRigVMPropertyPath& InOther)
: Path(InOther.Path)
, Segments(InOther.Segments)
{
}

uint8* FRigVMPropertyPath::GetData_Internal(uint8* InPtr) const
{
	for(const FRigVMPropertyPathSegment& Segment : Segments)
	{
		switch(Segment.Type)
		{
			case ERigVMPropertyPathSegmentType::StructMember:
			{
				// index represents the offset needed
				// from container to struct member
				InPtr += Segment.Property->GetOffset_ForInternal();
				break;
			}
			case ERigVMPropertyPathSegmentType::ArrayElement:
			{
				FScriptArrayHelper ArrayHelper(CastFieldChecked<FArrayProperty>(Segment.Property), InPtr);
				InPtr = ArrayHelper.GetRawPtr(Segment.Index);
				break;
			}
			case ERigVMPropertyPathSegmentType::MapValue:
			{
				FScriptMapHelper MapHelper(CastFieldChecked<FMapProperty>(Segment.Property), InPtr);
				InPtr = MapHelper.FindValueFromHash(&Segment.Name);;
				break;
			}
			default:
			{
				break;
			}
		}
	}

	return InPtr;
}

