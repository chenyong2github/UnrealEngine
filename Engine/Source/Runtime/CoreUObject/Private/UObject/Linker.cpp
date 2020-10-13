// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Linker.cpp: Unreal object linker.
=============================================================================*/

#include "UObject/Linker.h"
#include "Containers/StringView.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "UObject/UnrealType.h"
#include "UObject/LinkerLoad.h"
#include "Misc/SecureHash.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "UObject/CoreRedirects.h"
#include "UObject/LinkerManager.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/DebugSerializationFlags.h"
#include "UObject/ObjectResource.h"
#include "Algo/Transform.h"

DEFINE_LOG_CATEGORY(LogLinker);

#define LOCTEXT_NAMESPACE "Linker"

/*-----------------------------------------------------------------------------
	Helper functions.
-----------------------------------------------------------------------------*/
namespace Linker
{
	FORCEINLINE bool IsCorePackage(const FName& PackageName)
	{
		return PackageName == NAME_Core || PackageName == GLongCorePackageName;
	}
}

/**
 * Type hash implementation. 
 *
 * @param	Ref		Reference to hash
 * @return	hash value
 */
uint32 GetTypeHash( const FDependencyRef& Ref  )
{
	return PointerHash(Ref.Linker) ^ Ref.ExportIndex;
}

/*----------------------------------------------------------------------------
	FCompressedChunk.
----------------------------------------------------------------------------*/

FCompressedChunk::FCompressedChunk()
:	UncompressedOffset(0)
,	UncompressedSize(0)
,	CompressedOffset(0)
,	CompressedSize(0)
{
}

/** I/O function */
FArchive& operator<<(FArchive& Ar,FCompressedChunk& Chunk)
{
	Ar << Chunk.UncompressedOffset;
	Ar << Chunk.UncompressedSize;
	Ar << Chunk.CompressedOffset;
	Ar << Chunk.CompressedSize;
	return Ar;
}

void operator<<(FStructuredArchive::FSlot Slot, FCompressedChunk& Chunk)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	Record << SA_VALUE(TEXT("UncompressedOffset"), Chunk.UncompressedOffset);
	Record << SA_VALUE(TEXT("UncompressedSize"), Chunk.UncompressedSize);
	Record << SA_VALUE(TEXT("CompressedOffset"), Chunk.CompressedOffset);
	Record << SA_VALUE(TEXT("CompressedSize"), Chunk.CompressedSize);
}

/*----------------------------------------------------------------------------
	Items stored in Unreal files.
----------------------------------------------------------------------------*/

FGenerationInfo::FGenerationInfo(int32 InExportCount, int32 InNameCount)
: ExportCount(InExportCount), NameCount(InNameCount)
{}

/** I/O functions
 * we use a function instead of operator<< so we can pass in the package file summary for version tests, since archive version hasn't been set yet
 */
void FGenerationInfo::Serialize(FArchive& Ar, const struct FPackageFileSummary& Summary)
{
	Ar << ExportCount << NameCount;
}

void FGenerationInfo::Serialize(FStructuredArchive::FSlot Slot, const struct FPackageFileSummary& Summary)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	Record << SA_VALUE(TEXT("ExportCount"), ExportCount) << SA_VALUE(TEXT("NameCount"), NameCount);
}

#if WITH_EDITORONLY_DATA
extern int32 GLinkerAllowDynamicClasses;
#endif

void FLinkerTables::SerializeSearchableNamesMap(FArchive& Ar)
{
	SerializeSearchableNamesMap(FStructuredArchiveFromArchive(Ar).GetSlot());
}

void FLinkerTables::SerializeSearchableNamesMap(FStructuredArchive::FSlot Slot)
{
#if WITH_EDITOR
	FArchive::FScopeSetDebugSerializationFlags S(Slot.GetUnderlyingArchive(), DSF_IgnoreDiff, true);
#endif

	if (Slot.GetUnderlyingArchive().IsSaving())
	{
		// Sort before saving to keep order consistent
		SearchableNamesMap.KeySort(TLess<FPackageIndex>());

		for (TPair<FPackageIndex, TArray<FName> >& Pair : SearchableNamesMap)
		{
			Pair.Value.Sort(FNameLexicalLess());
		}
	}

	// Default Map serialize works fine
	Slot << SearchableNamesMap;
}

FName FLinker::GetExportClassName( int32 i )
{
	if (ExportMap.IsValidIndex(i))
	{
		FObjectExport& Export = ExportMap[i];
		if( !Export.ClassIndex.IsNull() )
		{
			return ImpExp(Export.ClassIndex).ObjectName;
		}
#if WITH_EDITORONLY_DATA
		else if (GLinkerAllowDynamicClasses && (Export.DynamicType == FObjectExport::EDynamicType::DynamicType))
		{
			static FName NAME_BlueprintGeneratedClass(TEXT("BlueprintGeneratedClass"));
			return NAME_BlueprintGeneratedClass;
		}
#else
		else if (Export.DynamicType == FObjectExport::EDynamicType::DynamicType)
		{
			return GetDynamicTypeClassName(*GetExportPathName(i));
		}
#endif
	}
	return NAME_Class;
}

/*----------------------------------------------------------------------------
	FLinker.
----------------------------------------------------------------------------*/
FLinker::FLinker(ELinkerType::Type InType, UPackage* InRoot, const TCHAR* InFilename)
: LinkerType(InType)
, LinkerRoot( InRoot )
, Filename( InFilename )
, FilterClientButNotServer(false)
, FilterServerButNotClient(false)
, ScriptSHA(nullptr)
{
	check(LinkerRoot);
	check(InFilename);

	if( !GIsClient && GIsServer)
	{
		FilterClientButNotServer = true;
	}
	if( GIsClient && !GIsServer)
	{
		FilterServerButNotClient = true;
	}
}

void FLinker::Serialize( FArchive& Ar )
{
	// This function is only used for counting memory, actual serialization uses a different path
	if( Ar.IsCountingMemory() )
	{
		// Can't use CountBytes as ExportMap is array of structs of arrays.
		Ar << ImportMap;
		Ar << ExportMap;
		Ar << DependsMap;
		Ar << SoftPackageReferenceList;
		Ar << GatherableTextDataMap;
		Ar << SearchableNamesMap;
	}
}

void FLinker::AddReferencedObjects(FReferenceCollector& Collector)
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		Collector.AddReferencedObject(*(UObject**)&LinkerRoot);
	}
#endif
}

// FLinker interface.
/**
 * Return the path name of the UObject represented by the specified import. 
 * (can be used with StaticFindObject)
 * 
 * @param	ImportIndex	index into the ImportMap for the resource to get the name for
 *
 * @return	the path name of the UObject represented by the resource at ImportIndex
 */
FString FLinker::GetImportPathName(int32 ImportIndex)
{
	FString Result;
	for (FPackageIndex LinkerIndex = FPackageIndex::FromImport(ImportIndex); !LinkerIndex.IsNull();)
	{
		const FObjectResource& Resource = ImpExp(LinkerIndex);
		bool bSubobjectDelimiter=false;

		if (Result.Len() > 0 && GetClassName(LinkerIndex) != NAME_Package
			&& (Resource.OuterIndex.IsNull() || GetClassName(Resource.OuterIndex) == NAME_Package) )
		{
			bSubobjectDelimiter = true;
		}

		// don't append a dot in the first iteration
		if ( Result.Len() > 0 )
		{
			if ( bSubobjectDelimiter )
			{
				Result = FString(SUBOBJECT_DELIMITER) + Result;
			}
			else
			{
				Result = FString(TEXT(".")) + Result;
			}
		}

		Result = Resource.ObjectName.ToString() + Result;
		LinkerIndex = Resource.OuterIndex;
	}
	return Result;
}

/**
 * Return the path name of the UObject represented by the specified export.
 * (can be used with StaticFindObject)
 * 
 * @param	ExportIndex				index into the ExportMap for the resource to get the name for
 * @param	FakeRoot				Optional name to replace use as the root package of this object instead of the linker
 * @param	bResolveForcedExports	if true, the package name part of the return value will be the export's original package,
 *									not the name of the package it's currently contained within.
 *
 * @return	the path name of the UObject represented by the resource at ExportIndex
 */
FString FLinker::GetExportPathName(int32 ExportIndex, const TCHAR* FakeRoot,bool bResolveForcedExports/*=false*/)
{
	FString Result;

	bool bForcedExport = false;
	bool bHasOuterImport = false;
	for ( FPackageIndex LinkerIndex = FPackageIndex::FromExport(ExportIndex); !LinkerIndex.IsNull(); LinkerIndex = ImpExp(LinkerIndex).OuterIndex )
	{ 
		bHasOuterImport |= LinkerIndex.IsImport();
		const FObjectResource& Resource = ImpExp(LinkerIndex);

		// don't append a dot in the first iteration
		if ( Result.Len() > 0 )
		{
			// if this export is not a UPackage but this export's Outer is a UPackage, we need to use subobject notation
			if ((Resource.OuterIndex.IsNull() || GetClassName(Resource.OuterIndex) == NAME_Package)
			  && GetClassName(LinkerIndex) != NAME_Package)
			{
				Result = FString(SUBOBJECT_DELIMITER) + Result;
			}
			else
			{
				Result = FString(TEXT(".")) + Result;
			}
		}
		Result = Resource.ObjectName.ToString() + Result;
		bForcedExport = bForcedExport || (LinkerIndex.IsExport() ? Exp(LinkerIndex).bForcedExport : false);
	}

	if ((bForcedExport && FakeRoot == nullptr && bResolveForcedExports) ||
		// if the export we are building the path of has an import in its outer chain, no need to append the LinkerRoot path
		bHasOuterImport )
	{
		// Result already contains the correct path name for this export
		return Result;
	}

	return (FakeRoot ? FakeRoot : LinkerRoot->GetPathName()) + TEXT(".") + Result;
}

FString FLinker::GetImportFullName(int32 ImportIndex)
{
	return ImportMap[ImportIndex].ClassName.ToString() + TEXT(" ") + GetImportPathName(ImportIndex);
}

FString FLinker::GetExportFullName(int32 ExportIndex, const TCHAR* FakeRoot,bool bResolveForcedExports/*=false*/)
{
	FPackageIndex ClassIndex = ExportMap[ExportIndex].ClassIndex;
	FName ClassName = ClassIndex.IsNull() ? FName(NAME_Class) : ImpExp(ClassIndex).ObjectName;

	return ClassName.ToString() + TEXT(" ") + GetExportPathName(ExportIndex, FakeRoot, bResolveForcedExports);
}

FPackageIndex FLinker::ResourceGetOutermost(FPackageIndex LinkerIndex) const
{
	const FObjectResource* Res = &ImpExp(LinkerIndex);
	while (!Res->OuterIndex.IsNull())
	{
		LinkerIndex = Res->OuterIndex;
		Res = &ImpExp(LinkerIndex);
	}
	return LinkerIndex;
}

bool FLinker::ResourceIsIn(FPackageIndex LinkerIndex, FPackageIndex OuterIndex) const
{
	LinkerIndex = ImpExp(LinkerIndex).OuterIndex;
	while (!LinkerIndex.IsNull())
	{
		LinkerIndex = ImpExp(LinkerIndex).OuterIndex;
		if (LinkerIndex == OuterIndex)
		{
			return true;
		}
	}
	return false;
}

bool FLinker::DoResourcesShareOutermost(FPackageIndex LinkerIndexLHS, FPackageIndex LinkerIndexRHS) const
{
	return ResourceGetOutermost(LinkerIndexLHS) == ResourceGetOutermost(LinkerIndexRHS);
}

bool FLinker::ImportIsInAnyExport(int32 ImportIndex) const
{
	FPackageIndex LinkerIndex = ImportMap[ImportIndex].OuterIndex;
	while (!LinkerIndex.IsNull())
	{
		LinkerIndex = ImpExp(LinkerIndex).OuterIndex;
		if (LinkerIndex.IsExport())
		{
			return true;
		}
	}
	return false;

}

bool FLinker::AnyExportIsInImport(int32 ImportIndex) const
{
	FPackageIndex OuterIndex = FPackageIndex::FromImport(ImportIndex);
	for (int32 ExportIndex = 0; ExportIndex < ExportMap.Num(); ++ExportIndex)
	{
		if (ResourceIsIn(FPackageIndex::FromExport(ExportIndex), OuterIndex))
		{
			return true;
		}
	}
	return false;
}

bool FLinker::AnyExportShareOuterWithImport(int32 ImportIndex) const
{
	FPackageIndex Import = FPackageIndex::FromImport(ImportIndex);
	for (int32 ExportIndex = 0; ExportIndex < ExportMap.Num(); ++ExportIndex)
	{
		if (ExportMap[ExportIndex].OuterIndex.IsImport()
			&& DoResourcesShareOutermost(FPackageIndex::FromExport(ExportIndex), Import))
		{
			return true;
		}
	}
	return false;
}

/**
 * Tell this linker to start SHA calculations
 */
void FLinker::StartScriptSHAGeneration()
{
	// create it if needed
	if (ScriptSHA == NULL)
	{
		ScriptSHA = new FSHA1;
	}

	// make sure it's reset
	ScriptSHA->Reset();
}

/**
 * If generating a script SHA key, update the key with this script code
 *
 * @param ScriptCode Code to SHAify
 */
void FLinker::UpdateScriptSHAKey(const TArray<uint8>& ScriptCode)
{
	// if we are doing SHA, update it
	if (ScriptSHA && ScriptCode.Num())
	{
		ScriptSHA->Update((uint8*)ScriptCode.GetData(), ScriptCode.Num());
	}
}

/**
 * After generating the SHA key for all of the 
 *
 * @param OutKey Storage for the key bytes (20 bytes)
 */
void FLinker::GetScriptSHAKey(uint8* OutKey)
{
	check(ScriptSHA);

	// finish up the calculation, and return it
	ScriptSHA->Final();
	ScriptSHA->GetHash(OutKey);
}

FLinker::~FLinker()
{
	// free any SHA memory
	delete ScriptSHA;
}



/*-----------------------------------------------------------------------------
	Global functions
-----------------------------------------------------------------------------*/

void ResetLoaders(UObject* InPkg)
{
	if (IsAsyncLoading())
	{
		UE_LOG(LogLinker, Log, TEXT("ResetLoaders(%s) is flushing async loading"), *GetPathNameSafe(InPkg));
	}

	// Make sure we're not in the middle of loading something in the background.
	FlushAsyncLoading();
	FLinkerManager::Get().ResetLoaders(InPkg);
}

void DeleteLoaders()
{
	FLinkerManager::Get().DeleteLinkers();
}

void DeleteLoader(FLinkerLoad* Loader)
{
	FLinkerManager::Get().RemoveLinker(Loader);
}

static void LogGetPackageLinkerError(FUObjectSerializeContext* LoadContext, const TCHAR* InFilename, const FText& InErrorMessage, UObject* InOuter, uint32 LoadFlags)
{
	static FName NAME_LoadErrors("LoadErrors");
	struct Local
	{
		/** Helper function to output more detailed error info if available */
		static void OutputErrorDetail(FUObjectSerializeContext* InLoadContext, const FName& LogName)
		{
			FUObjectSerializeContext* LoadContextToReport = InLoadContext;
			if (LoadContextToReport && LoadContextToReport->SerializedObject && LoadContextToReport->SerializedImportLinker)
			{
				FMessageLog LoadErrors(LogName);

				TSharedRef<FTokenizedMessage> Message = LoadErrors.Info();
				Message->AddToken(FTextToken::Create(LOCTEXT("FailedLoad_Message", "Failed to load")));
				Message->AddToken(FAssetNameToken::Create(LoadContextToReport->SerializedImportLinker->GetImportPathName(LoadContextToReport->SerializedImportIndex)));
				Message->AddToken(FTextToken::Create(LOCTEXT("FailedLoad_Referenced", "Referenced by")));
				Message->AddToken(FUObjectToken::Create(LoadContextToReport->SerializedObject));
			}
		}
	};

	FLinkerLoad* SerializedPackageLinker = LoadContext ? LoadContext->SerializedPackageLinker : nullptr;
	UObject* SerializedObject = LoadContext ? LoadContext->SerializedObject : nullptr;
	FString LoadingFile = InFilename ? InFilename : InOuter ? *InOuter->GetName() : TEXT("NULL");
	
	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("LoadingFile"), FText::FromString(LoadingFile));
	Arguments.Add(TEXT("ErrorMessage"), InErrorMessage);

	FText FullErrorMessage = FText::Format(LOCTEXT("FailedLoad", "Failed to load '{LoadingFile}': {ErrorMessage}"), Arguments);
	if (SerializedPackageLinker || SerializedObject)
	{
		FLinkerLoad* LinkerToUse = SerializedPackageLinker;
		if (!LinkerToUse)
		{
			LinkerToUse = SerializedObject->GetLinker();
		}
		FString LoadedByFile = LinkerToUse ? *LinkerToUse->Filename : SerializedObject->GetOutermost()->GetName();
		FullErrorMessage = FText::FromString(FAssetMsg::GetAssetLogString(*LoadedByFile, FullErrorMessage.ToString()));
	}

	FMessageLog LoadErrors(NAME_LoadErrors);

	if( GIsEditor && !IsRunningCommandlet() )
	{
		// if we don't want to be warned, skip the load warning
		// Display log error regardless LoadFlag settings
		if (LoadFlags & (LOAD_NoWarn | LOAD_Quiet))
		{
			SET_WARN_COLOR(COLOR_RED);
			UE_LOG(LogLinker, Log, TEXT("%s"), *FullErrorMessage.ToString());
			CLEAR_WARN_COLOR();
		}
		else
		{
			SET_WARN_COLOR(COLOR_RED);
			UE_LOG(LogLinker, Warning, TEXT("%s"), *FullErrorMessage.ToString());
			CLEAR_WARN_COLOR();
			// we only want to output errors that content creators will be able to make sense of,
			// so any errors we cant get links out of we will just let be output to the output log (above)
			// rather than clog up the message log

			if(InFilename != NULL && InOuter != NULL)
			{
				FString PackageName;
				if (!FPackageName::TryConvertFilenameToLongPackageName(InFilename, PackageName))
				{
					PackageName = InFilename;
				}
				FString OuterPackageName;
				if (!FPackageName::TryConvertFilenameToLongPackageName(InOuter->GetPathName(), OuterPackageName))
				{
					OuterPackageName = InOuter->GetPathName();
				}
				// Output the summary error & the filename link. This might be something like "..\Content\Foo.upk Out of Memory"
				TSharedRef<FTokenizedMessage> Message = LoadErrors.Error();
				Message->AddToken(FAssetNameToken::Create(PackageName));
				Message->AddToken(FTextToken::Create(FText::FromString(TEXT(":"))));
				Message->AddToken(FTextToken::Create(FullErrorMessage));
				Message->AddToken(FAssetNameToken::Create(OuterPackageName));
			}

			Local::OutputErrorDetail(LoadContext, NAME_LoadErrors);
		}
	}
	else
	{
		bool bLogMessageEmitted = false;
		// @see ResavePackagesCommandlet
		if( FParse::Param(FCommandLine::Get(),TEXT("SavePackagesThatHaveFailedLoads")) == true )
		{
			LoadErrors.Warning(FullErrorMessage);
		}
		else
		{
			// Gracefully handle missing packages
			bLogMessageEmitted = SafeLoadError(InOuter, LoadFlags, *FullErrorMessage.ToString());
		}

		// Only print out the message if it was not already handled by SafeLoadError
		if (!bLogMessageEmitted)
		{
			if (LoadFlags & (LOAD_NoWarn | LOAD_Quiet))
			{
				SET_WARN_COLOR(COLOR_RED);
				UE_LOG(LogLinker, Log, TEXT("%s"), *FullErrorMessage.ToString());
				CLEAR_WARN_COLOR();
			}
			else
			{
				SET_WARN_COLOR(COLOR_RED);
				UE_LOG(LogLinker, Warning, TEXT("%s"), *FullErrorMessage.ToString());
				CLEAR_WARN_COLOR();
				Local::OutputErrorDetail(LoadContext, NAME_LoadErrors);
			}
		}
	}
}

/** Customized version of FPackageName::DoesPackageExist that takes dynamic native class packages into account */
static bool DoesPackageExistForGetPackageLinker(const FString& LongPackageName, const FGuid* Guid, FString& OutFilename)
{
	if (
#if WITH_EDITORONLY_DATA
		GLinkerAllowDynamicClasses && 
#endif
		GetConvertedDynamicPackageNameToTypeName().Contains(*LongPackageName))
	{
		OutFilename = FPackageName::LongPackageNameToFilename(LongPackageName);
		return true;
	}
	else
	{
		return FPackageName::DoesPackageExist(LongPackageName, Guid, &OutFilename);
	}
}

FString GetPrestreamPackageLinkerName(const TCHAR* InLongPackageName, bool bExistSkip)
{
	FString NewFilename;
	if (InLongPackageName)
	{
		FString PackageName(InLongPackageName);
		if (!FPackageName::TryConvertFilenameToLongPackageName(InLongPackageName, PackageName))
		{
			return FString();
		}
		UPackage* ExistingPackage = bExistSkip ? FindObject<UPackage>(nullptr, *PackageName) : nullptr;
		if (ExistingPackage)
		{
			return FString(); // we won't load this anyway, don't prestream
		}
		
		const bool DoesNativePackageExist = DoesPackageExistForGetPackageLinker(PackageName, nullptr, NewFilename);

		if ( !DoesNativePackageExist )
		{
			return FString();
		}
	}
	return NewFilename;
}

//
// Find or create the linker for a package.
//
FLinkerLoad* GetPackageLinker
(
	UPackage*		InOuter,
	const TCHAR*	InLongPackageName,
	uint32			LoadFlags,
	UPackageMap*	Sandbox,
	FGuid*			CompatibleGuid,
	FArchive*		InReaderOverride,
	FUObjectSerializeContext** InOutLoadContext,
	FLinkerLoad*	ImportLinker,
	const FLinkerInstancingContext* InstancingContext
)
{
	FUObjectSerializeContext* InExistingContext = InOutLoadContext ? *InOutLoadContext : nullptr;

	// See if the linker is already loaded.
	if (FLinkerLoad* Result = FLinkerLoad::FindExistingLinkerForPackage(InOuter))
	{
		if (InExistingContext && Result->GetSerializeContext() && Result->GetSerializeContext() != InExistingContext)
		{
			if (!Result->GetSerializeContext()->HasStartedLoading())
			{
				Result->SetSerializeContext(InExistingContext);
			}
		}
		return Result;
	}

	FString PackageNameToCreate;
	UPackage* TargetPackage = nullptr;
	if (!InLongPackageName)
	{
		// Resolve filename from package name.
		if (!InOuter)
		{
			// try to recover from this instead of throwing, it seems recoverable just by doing this
			LogGetPackageLinkerError(InExistingContext, InLongPackageName, LOCTEXT("PackageResolveFailed", "Can't resolve asset name"), InOuter, LoadFlags);
			return nullptr;
		}
		PackageNameToCreate = InOuter->GetName();
		TargetPackage = InOuter;

		// Process any package redirects
		{
			const FCoreRedirectObjectName NewPackageName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(NAME_None, NAME_None, *PackageNameToCreate));
			NewPackageName.PackageName.ToString(PackageNameToCreate);
		}
	}
	else
	{
		if (!FPackageName::TryConvertFilenameToLongPackageName(InLongPackageName, PackageNameToCreate))
		{
			// try to recover from this instead of throwing, it seems recoverable just by doing this
			LogGetPackageLinkerError(InExistingContext, InLongPackageName, LOCTEXT("PackageResolveFailed", "Can't resolve asset name"), InOuter, LoadFlags);
			return nullptr;
		}

		// Process any package redirects
		{
			const FCoreRedirectObjectName NewPackageName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Package, FCoreRedirectObjectName(NAME_None, NAME_None, *PackageNameToCreate));
			NewPackageName.PackageName.ToString(PackageNameToCreate);
		}

		if (InOuter)
		{
			TargetPackage = InOuter;
		}
		else
		{
			TargetPackage = FindObject<UPackage>(nullptr, *PackageNameToCreate);
			if (TargetPackage && TargetPackage->GetOuter() != nullptr)
			{
				TargetPackage = nullptr;
			}
		}
	}

	if (TargetPackage && TargetPackage->HasAnyPackageFlags(PKG_InMemoryOnly))
	{
		// This is a memory-only in package and so it has no linker and this is ok.
		return nullptr;
	}

	// The editor must not redirect packages for localization. We also shouldn't redirect script or in-memory packages (in-memory packages exited earlier so we don't need to check here).
	FString PackageNameToLoad = PackageNameToCreate;
	if (!(GIsEditor || FPackageName::IsScriptPackage(PackageNameToLoad)))
	{
		// Allow delegates to resolve the path
		PackageNameToLoad = FPackageName::GetDelegateResolvedPackagePath(PackageNameToLoad);
		PackageNameToLoad = FPackageName::GetLocalizedPackagePath(PackageNameToLoad);
	}

	// Verify that the file exists.
	FString NewFilename;
	const bool bDoesPackageExist = DoesPackageExistForGetPackageLinker(PackageNameToLoad, CompatibleGuid, NewFilename);
	if (!bDoesPackageExist)
	{
		// Issue a warning if the caller didn't request nowarn/quiet, and the package isn't marked as known to be missing.
		bool bIssueWarning = (LoadFlags & (LOAD_NoWarn | LOAD_Quiet)) == 0 && !FLinkerLoad::IsKnownMissingPackage(InLongPackageName);

		if (bIssueWarning)
		{
			// try to recover from this instead of throwing, it seems recoverable just by doing this
			LogGetPackageLinkerError(InExistingContext, InLongPackageName, LOCTEXT("FileNotFoundShort", "Can't find file."), InOuter, LoadFlags);
		}
		return nullptr;
	}

	UPackage* CreatedPackage = nullptr;
	if (!TargetPackage)
	{
#if WITH_EDITORONLY_DATA
		// Make sure the package name matches the name on disk
		FPackageName::FixPackageNameCase(PackageNameToCreate, FPathViews::GetExtension(NewFilename));
#endif
		// Create the package with the provided long package name.
		CreatedPackage = CreatePackage(*PackageNameToCreate);
		if (!CreatedPackage)
		{
			LogGetPackageLinkerError(InExistingContext, InLongPackageName, LOCTEXT("FilenameToPackageShort", "Can't convert filename to asset name"), InOuter, LoadFlags);
			return nullptr;
		}
		if (LoadFlags & LOAD_PackageForPIE)
		{
			CreatedPackage->SetPackageFlags(PKG_PlayInEditor);
		}
		TargetPackage = CreatedPackage;
	}
	if (InOuter != TargetPackage)
	{
		if (FLinkerLoad* Result = FLinkerLoad::FindExistingLinkerForPackage(TargetPackage))
		{
			if (InExistingContext)
			{
				if ((Result->GetSerializeContext() && Result->GetSerializeContext()->HasStartedLoading() && InExistingContext->GetBeginLoadCount() == 1) ||
					(IsInAsyncLoadingThread() && Result->GetSerializeContext()))
				{
					// Use the context associated with the linker because it has already started loading objects (or we're in ALT where each package needs its own context)
					*InOutLoadContext = Result->GetSerializeContext();
				}
				else
				{
					if (Result->GetSerializeContext() && Result->GetSerializeContext() != InExistingContext)
					{
						// Make sure the objects already loaded with the context associated with the existing linker
						// are copied to the context provided for this function call to make sure they all get loaded ASAP
						InExistingContext->AddUniqueLoadedObjects(Result->GetSerializeContext()->PRIVATE_GetObjectsLoadedInternalUseOnly());
					}
					// Replace the linker context with the one passed into this function
					Result->SetSerializeContext(InExistingContext);
				}
			}
			return Result;
		}
	}

	// Create new linker.
	// we will already have found the filename above
	check(NewFilename.Len() > 0);
	TRefCountPtr<FUObjectSerializeContext> LoadContext(InExistingContext ? InExistingContext : FUObjectThreadContext::Get().GetSerializeContext());
	FLinkerLoad* Result = FLinkerLoad::CreateLinker(LoadContext, TargetPackage, *NewFilename, LoadFlags, InReaderOverride, ImportLinker ? &ImportLinker->GetInstancingContext() : InstancingContext);

	if (!Result && CreatedPackage)
	{
		CreatedPackage->MarkPendingKill();
	}

	return Result;
}

FLinkerLoad* LoadPackageLinker(UPackage* InOuter, const TCHAR* InLongPackageName, uint32 LoadFlags, UPackageMap* Sandbox, FGuid* CompatibleGuid, FArchive* InReaderOverride, TFunctionRef<void(FLinkerLoad* LoadedLinker)> LinkerLoadedCallback)
{
	FLinkerLoad* Linker = nullptr;
	TRefCountPtr<FUObjectSerializeContext> LoadContext(FUObjectThreadContext::Get().GetSerializeContext());
	BeginLoad(LoadContext);
	{
		FUObjectSerializeContext* InOutLoadContext = LoadContext;
		Linker = GetPackageLinker(InOuter, InLongPackageName, LoadFlags, Sandbox, CompatibleGuid, InReaderOverride, &InOutLoadContext);
		if (InOutLoadContext != LoadContext)
		{
			// The linker already existed and was associated with another context
			LoadContext->DecrementBeginLoadCount();
			LoadContext = InOutLoadContext;
			LoadContext->IncrementBeginLoadCount();
		}
	}
	// Allow external code to work with the linker before EndLoad()
	LinkerLoadedCallback(Linker);
	EndLoad(Linker ? Linker->GetSerializeContext() : LoadContext.GetReference());
	return Linker;
}

FLinkerLoad* LoadPackageLinker(UPackage* InOuter, const TCHAR* InLongPackageName, uint32 LoadFlags, UPackageMap* Sandbox, FGuid* CompatibleGuid, FArchive* InReaderOverride)
{
	return LoadPackageLinker(InOuter, InLongPackageName, LoadFlags, Sandbox, CompatibleGuid, InReaderOverride, [](FLinkerLoad* InLinker) {});
}


void ResetLoadersForSave(UObject* InOuter, const TCHAR* Filename)
{
	UPackage* Package = dynamic_cast<UPackage*>(InOuter);
	ResetLoadersForSave(Package, Filename);
}

void ResetLoadersForSave(UPackage* Package, const TCHAR* Filename)
{
	FLinkerLoad* Loader = FLinkerLoad::FindExistingLinkerForPackage(Package);
	if( Loader )
	{
		// Compare absolute filenames to see whether we're trying to save over an existing file.
		if( FPaths::ConvertRelativePathToFull(Filename) == FPaths::ConvertRelativePathToFull( Loader->Filename ) )
		{
			// Detach all exports from the linker and dissociate the linker.
			ResetLoaders( Package );
		}
	}
}

void ResetLoadersForSave(TArrayView<FPackageSaveInfo> InPackages)
{
	TSet<FLinkerLoad*> LinkersToReset;
	Algo::TransformIf(InPackages, LinkersToReset,
		[](const FPackageSaveInfo& InPackageSaveInfo)
		{
			FLinkerLoad* Loader = FLinkerLoad::FindExistingLinkerForPackage(InPackageSaveInfo.Package);
			return Loader && FPaths::ConvertRelativePathToFull(InPackageSaveInfo.Filename) == FPaths::ConvertRelativePathToFull(Loader->Filename);
		},
		[](const FPackageSaveInfo& InPackageSaveInfo)
		{
			return FLinkerLoad::FindExistingLinkerForPackage(InPackageSaveInfo.Package);
		});
	FlushAsyncLoading();
	FLinkerManager::Get().ResetLoaders(LinkersToReset);
}

void EnsureLoadingComplete(UPackage* Package)
{
	FLinkerManager::Get().EnsureLoadingComplete(Package);
}

#undef LOCTEXT_NAMESPACE
