// Copyright Epic Games, Inc. All Rights Reserved.
#include "InstancedStruct.h"

void FInstancedStruct::InitializeAs(const UScriptStruct* InScriptStruct, const uint8* InStructMemory /*= nullptr*/)
{
	Reset();

	if (!InScriptStruct)
	{
		// InScriptStruct == nullptr signifies an empty, unset FInstancedStruct instance. No further work required.
		return;
	}

	SetScriptStruct(InScriptStruct);

	const int32 RequiredSize = InScriptStruct->GetStructureSize();
	SetMemory((uint8*)FMemory::Malloc(FMath::Max(1, RequiredSize)));

	InScriptStruct->InitializeStruct(GetMutableMemory());

	if (InStructMemory)
	{
		InScriptStruct->CopyScriptStruct(GetMutableMemory(), InStructMemory);
	}
}

void FInstancedStruct::Reset()
{
	if (uint8* Memory = GetMutableMemory())
	{
		DestroyScriptStruct();
		FMemory::Free(Memory);
	}
	FBaseStruct::Reset();
}

bool FInstancedStruct::Serialize(FArchive& Ar)
{
	UScriptStruct* NonConstStruct = const_cast<UScriptStruct*>(GetScriptStruct());

	enum class EVersion : uint8
	{
		InitialVersion = 0,
		// -----<new versions can be added above this line>-----
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	EVersion Version = EVersion::LatestVersion;

	// Temporary code to introduce versioning and load old data
	// The goal is to remove this  by bumping the version in a near future.
	bool bUseVersioning = true;

#if WITH_EDITOR
	if (!Ar.IsCooking())
	{
		// Keep archive position to use legacy serialization if the header is not found
		const int64 HeaderOffset = Ar.Tell();

		// Some random pattern to differentiate old data
		const uint32 NewVersionHeader = 0xABABABAB;
		uint32 Header = NewVersionHeader;
		Ar << Header;

		if (Ar.IsLoading())
		{
			if (Header != NewVersionHeader)
			{
				// Not a valid header so go back and process with legacy loading
				Ar.Seek(HeaderOffset);
				bUseVersioning = false;
				UE_LOG(LogLoad, Verbose, TEXT("Loading FInstancedStruct using legacy serialization"));
			}
		}
	}
#endif // WITH_EDITOR

	if (bUseVersioning)
	{
		Ar << Version;
	}

	if (Version > EVersion::LatestVersion)
	{
		UE_LOG(LogCore, Error, TEXT("Invalid Version: %hhu"), Version);
		Ar.SetError();
		return false;
	}

	if (Ar.IsLoading())
	{
		// UScriptStruct type
		UScriptStruct* NewNonConstStruct = nullptr;
		Ar << NewNonConstStruct;
		NonConstStruct = ReinitializeAs(NewNonConstStruct);

		// Size of the serialized memory
		int32 SerialSize = 0; 
		if (bUseVersioning)
		{
			Ar << SerialSize;
		}

		// Serialized memory
		if (NonConstStruct == nullptr && SerialSize > 0)
		{
			// A null struct indicates an old struct or an unsupported one for the current target.
			// In this case we manually seek in the archive to skip its serialized content. 
			// We don't want to rely on TaggedSerialization that will mark an error in the archive that
			// may cause other serialization to fail (e.g. FArchive& operator<<(FArchive& Ar, TArray& A))
			UE_LOG(LogCore, Warning, TEXT("Unable to find serialized UScriptStruct -> Advance %u bytes in the archive and reset to empty FInstancedStruct"), SerialSize);
			Ar.Seek(Ar.Tell() + SerialSize);
		}
		else if (NonConstStruct != nullptr && ensureMsgf(GetMutableMemory() != nullptr, TEXT("A valid script struct should always have allocated memory")))
		{
			NonConstStruct->SerializeItem(Ar, GetMutableMemory(), /* Defaults */ nullptr);
		}
	}
	else if (Ar.IsSaving())
	{
		// UScriptStruct type
		Ar << NonConstStruct;
	
		// Size of the serialized memory (reserve location)
		const int64 SizeOffset = Ar.Tell(); // Position to write the actual size after struct serialization
		int32 SerialSize = 0;
		Ar << SerialSize;
		
		// Serialized memory
		const int64 InitialOffset = Ar.Tell(); // Position before struct serialization to compute its serial size
		if (NonConstStruct != nullptr && ensureMsgf(GetMutableMemory() != nullptr, TEXT("A valid script struct should always have allocated memory")))
		{
			NonConstStruct->SerializeItem(Ar, GetMutableMemory(), /* Defaults */ nullptr);
		}
		const int64 FinalOffset = Ar.Tell(); // Keep current offset to reset the archive pos after write the serial size

		// Size of the serialized memory
		Ar.Seek(SizeOffset);	// Go back in the archive to write the actual size
		SerialSize = (int32)(FinalOffset - InitialOffset);
		Ar << SerialSize;
		Ar.Seek(FinalOffset);	// Reset archive to its position
	}

	return true;
}

bool FInstancedStruct::ExportTextItem(FString& ValueStr, FInstancedStruct const& DefaultValue, class UObject* Parent, int32 PortFlags, class UObject* ExportRootScope) const
{
	UScriptStruct* NonConstStruct = const_cast<UScriptStruct*>(GetScriptStruct());
	if (NonConstStruct == nullptr || GetMemory() == nullptr)
	{
		return false;
	}
	
	ValueStr += NonConstStruct->GetPathName();

	NonConstStruct->ExportText(ValueStr, GetMemory(), GetMemory(), nullptr, EPropertyPortFlags::PPF_None, nullptr);
	return true;
}

bool FInstancedStruct::ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText, FArchive* InSerializingArchive /*= nullptr*/)
{
	FString StructPath;
	const TCHAR* NewBuffer = FPropertyHelpers::ReadToken(Buffer, /* out */ StructPath, /* dotted names */ true);
	if (NewBuffer == nullptr)
	{
		return false;
	}

	Buffer = NewBuffer;
		
	UScriptStruct* NonConstStruct = ReinitializeAs(FindObject<UScriptStruct>(nullptr, *StructPath, false));
	if (NonConstStruct == nullptr)
	{
		return false;
	}

	const TCHAR* Result = NonConstStruct->ImportText(Buffer, GetMutableMemory(), nullptr, EPropertyPortFlags::PPF_None, nullptr, NonConstStruct->GetName());
	if (Result != nullptr)
	{
		Buffer = Result;
	}

	return true;
}

UScriptStruct* FInstancedStruct::ReinitializeAs(const UScriptStruct* InScriptStruct)
{
	UScriptStruct* NonConstStruct = const_cast<UScriptStruct*>(GetScriptStruct());

	if (InScriptStruct != NonConstStruct)
	{
		InitializeAs(InScriptStruct, nullptr);
		NonConstStruct = const_cast<UScriptStruct*>(GetScriptStruct());
	}

	return NonConstStruct;
}

bool FInstancedStruct::Identical(const FInstancedStruct* Other, uint32 PortFlags) const
{
	// Only empty is considered equal
	return Other != nullptr && GetMemory() == nullptr && Other->GetMemory() == nullptr && GetScriptStruct() == nullptr && Other->GetScriptStruct() == nullptr;
}

void FInstancedStruct::AddStructReferencedObjects(class FReferenceCollector& Collector)
{
	if (const UScriptStruct* ScriptStructPtr = GetScriptStruct())
	{
		Collector.AddReferencedObject(ScriptStructPtr);

		if (ScriptStructPtr->StructFlags & STRUCT_AddStructReferencedObjects)
		{
			ScriptStructPtr->GetCppStructOps()->AddStructReferencedObjects()(GetMutableMemory(), Collector);
		}
		else
		{
			// The iterator will recursively loop through all structs in structs too.
			for (TPropertyValueIterator<const FObjectProperty> It(ScriptStructPtr, GetMemory()); It; ++It)
			{
				UObject** ObjectPtr = static_cast<UObject**>(const_cast<void*>(It.Value()));
				Collector.AddReferencedObject(*ObjectPtr);
			}
		}
	}
}


