// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FieldPath.cpp: Pointer to UObject asset, keeps extra information so that it is works even if the asset is not in memory
=============================================================================*/

#include "UObject/FieldPath.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "UObject/UObjectArray.h"

#if WITH_EDITORONLY_DATA
FFieldPath::FFieldPath(UField* InField, const FName& InPropertyTypeName)
{
	if (InField)
	{
		// Must be constructed from the equivalent UField class
		check(InField->GetClass()->GetFName() == InPropertyTypeName);
		GenerateFromUField(InField);
	}
}
#endif

void FFieldPath::Generate(FField* InField)
{
	Path.Empty();

	if (InField)
	{
		// Add names from the innermost to the outermost
		for (FFieldVariant Iter(InField); Iter.IsValid(); Iter = Iter.GetOwnerVariant())
		{
			Path.Add(Iter.GetFName());
		}
		ResolvedFieldOwner = GUObjectArray.ObjectToIndex(InField->GetOwnerUObject());
		SerialNumber = GlobalSerialNumber;
	}
	else
	{
		ClearCachedField();
	}
}

void FFieldPath::Generate(const TCHAR* InFieldPathString)
{
	// Expected format is: FullPackageName.Subobject[:Subobject:...]:FieldName
	check(InFieldPathString);
	
	{
		TCHAR NameBuffer[NAME_SIZE];
		int32 NameIndex = 0;

		// Construct names
		while (true)
		{
			if (*InFieldPathString == '.' || *InFieldPathString == SUBOBJECT_DELIMITER_CHAR || *InFieldPathString == '\0')
			{
				NameBuffer[NameIndex] = '\0';
				if (NameIndex > 0)
				{
					Path.Add(NameBuffer);
					NameIndex = 0;
				}
				if (*InFieldPathString == '\0')
				{
					break;
				}
			}
			else
			{
				NameBuffer[NameIndex++] = *InFieldPathString;
			}
			++InFieldPathString;
		}
	}

	if (Path.Num() > 1)
	{
		// Reverse the order
		for (int32 NameIndex = 0; NameIndex < (Path.Num() / 2); ++NameIndex)
		{
			Swap<FName>(Path[NameIndex], Path[Path.Num() - NameIndex - 1]);
		}
	}
}

FField* FFieldPath::TryToResolvePath(UStruct* InCurrentStruct, int32* OutOwnerIndex) const
{
	FField* Result = nullptr;

	// Resolve from the outermost to the innermost UObject
	UObject* LastOuter = nullptr;
	int32 PathIndex = Path.Num() - 1;
	for (; PathIndex > 0; --PathIndex)
	{
		UObject* Outer = StaticFindObjectFast(UObject::StaticClass(), LastOuter, Path[PathIndex]);		
		if (!Outer && PathIndex == (Path.Num() - 1) && InCurrentStruct)
		{
			// Try to resolve the package with the provided struct
			// Sometimes packages are renamed when loading
			UPackage* StructPackage = InCurrentStruct->GetOutermost();
			if (StructPackage->FileName == Path[PathIndex])
			{
				Outer = StructPackage;
			}
		}
		if (!Outer)
		{
			break;
		}
		LastOuter = Outer;				
	}
	if (UStruct* Owner = Cast<UStruct>(LastOuter))
	{
		check(PathIndex <= 1);
		Result = FindField<FField>(Owner, Path[PathIndex]);
		if (Result && PathIndex > 0)
		{
			// Nested property
			Result = Result->GetInnerFieldByName(Path[0]);
		}	
		
	}
	if (Result && OutOwnerIndex)
	{
		*OutOwnerIndex = GUObjectArray.ObjectToIndex(LastOuter);
	}

	return Result;
}

FString FFieldPath::ToString() const
{
	FString Result;
	// See FFieldPath::Generate for formatting specifics
	// Generate path from the outermost (last item) to the property (first item)
	for (int32 PathIndex = Path.Num() - 1; PathIndex >= 0; --PathIndex)
	{
		// @todo: this should be handled by some kind of a flag passed to this function
		FString ObjName = Path[PathIndex].ToString();
		if (PathIndex == (Path.Num() - 1) && ObjName.StartsWith(UDynamicClass::GetTempPackagePrefix(), ESearchCase::IgnoreCase))
		{
			ObjName.RemoveFromStart(UDynamicClass::GetTempPackagePrefix(), ESearchCase::IgnoreCase);
		}
		Result += ObjName;
		if (PathIndex > 0)
		{
			// Separator between the package name (last item) and the class (asset object - the second to last item is a '.', oterwise use SUBOBJECT_DELIMITER_CHAR)
			if (PathIndex == (Path.Num() - 1))
			{
				Result += '.';
			}
			else
			{
				Result += SUBOBJECT_DELIMITER_CHAR;
			}
		}
	}
	return Result;
}

#if WITH_EDITORONLY_DATA
void FFieldPath::GenerateFromUField(UField* InField)
{
	Path.Empty();
	ClearCachedField();
	for (UObject* Obj = InField; Obj; Obj = Obj->GetOuter())
	{
		Path.Add(Obj->GetFName());
	}	
}
#endif // WITH_EDITORONLY_DATA

int32 FFieldPath::GlobalSerialNumber = 0;

void FFieldPath::OnFieldDeleted()
{
	checkSlow(IsInGameThread());
	GlobalSerialNumber++;
}
