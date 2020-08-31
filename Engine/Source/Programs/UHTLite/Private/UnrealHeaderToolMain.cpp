// Copyright Epic Games, Inc. All Rights Reserved.


#include "UnrealHeaderTool.h"
#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "Misc/CompilationResult.h"
#include "UnrealHeaderToolGlobals.h"
#include "RequiredProgramMainCPPInclude.h"
#include "ClassMaps.h"
#include "IScriptGeneratorPluginInterface.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "HeaderParser.h"
#include "Features/IModularFeatures.h"
#include "Manifest.h"
#include "UnrealTypeDefinitionInfo.h"
#include "StringUtils.h"
#include "FileLineException.h"
#include "NativeClassExporter.h"

IMPLEMENT_APPLICATION(UnrealHeaderTool, "UnrealHeaderTool");

DEFINE_LOG_CATEGORY(LogCompile);

bool GUHTWarningLogged = false;
bool GUHTErrorLogged = false;

// UHTLite TODO: Refactor globals!
extern FManifest GManifest;
extern ECompilationResult::Type GCompilationResult;
extern FGraphEventArray GAsyncFileTasks;
extern double GMacroizeTime;
extern TArray<FString> ChangeMessages;
extern bool bWriteContents;
extern bool bVerifyContents;

static void PerformSimplifiedClassParse(UPackage* InParent, const TCHAR* FileName, const TCHAR* Buffer, struct FPerHeaderData& PerHeaderData, FClassDeclarations& ClassDeclarations);
static void ProcessInitialClassParse(FPerHeaderData& PerHeaderData, FTypeDefinitionInfoMap& TypeDefinitionInfoMap);

#pragma region Plugins
/** Get all script plugins based on ini setting */
void GetScriptPlugins(TArray<IScriptGeneratorPluginInterface*>& ScriptPlugins)
{
	FScopedDurationTimer PluginTimeTracker(GPluginOverheadTime);

	ScriptPlugins = IModularFeatures::Get().GetModularFeatureImplementations<IScriptGeneratorPluginInterface>(TEXT("ScriptGenerator"));
	UE_LOG(LogCompile, Log, TEXT("Found %d script generator plugins."), ScriptPlugins.Num());

	// Check if we can use these plugins and initialize them
	for (int32 PluginIndex = ScriptPlugins.Num() - 1; PluginIndex >= 0; --PluginIndex)
	{
		IScriptGeneratorPluginInterface* ScriptGenerator = ScriptPlugins[PluginIndex];
		bool bSupportedPlugin = ScriptGenerator->SupportsTarget(GManifest.TargetName);
		if (bSupportedPlugin)
		{
			// Find the right output directory for this plugin base on its target (Engine-side) plugin name.
			FString GeneratedCodeModuleName = ScriptGenerator->GetGeneratedCodeModuleName();
			const FManifestModule* GeneratedCodeModule = NULL;
			FString OutputDirectory;
			FString IncludeBase;
			for (const FManifestModule& Module : GManifest.Modules)
			{
				if (Module.Name == GeneratedCodeModuleName)
				{
					GeneratedCodeModule = &Module;
				}
			}
			if (GeneratedCodeModule)
			{
				UE_LOG(LogCompile, Log, TEXT("Initializing script generator \'%s\'"), *ScriptGenerator->GetGeneratorName());
				ScriptGenerator->Initialize(GManifest.RootLocalPath, GManifest.RootBuildPath, GeneratedCodeModule->GeneratedIncludeDirectory, GeneratedCodeModule->IncludeBase);
			}
			else
			{
				// Can't use this plugin
				UE_LOG(LogCompile, Log, TEXT("Unable to determine output directory for %s. Cannot export script glue with \'%s\'"), *GeneratedCodeModuleName, *ScriptGenerator->GetGeneratorName());
				bSupportedPlugin = false;
			}
		}
		if (!bSupportedPlugin)
		{
			UE_LOG(LogCompile, Log, TEXT("Script generator \'%s\' not supported for target: %s"), *ScriptGenerator->GetGeneratorName(), *GManifest.TargetName);
			ScriptPlugins.RemoveAt(PluginIndex);
		}
	}
}
#pragma endregion Plugins

/**
 * Tries to resolve super classes for classes defined in the given
 * module.
 *
 * @param Package Modules package.
 */
void ResolveSuperClasses(UPackage* Package, const FTypeDefinitionInfoMap& TypeDefinitionInfoMap)
{
	TArray<UObject*> Objects;
	GetObjectsWithPackage(Package, Objects);

	for (UObject* Object : Objects)
	{
		if (!Object->IsA<UClass>() || Object->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		UClass* DefinedClass = Cast<UClass>(Object);

		if (DefinedClass->HasAnyClassFlags(CLASS_Intrinsic | CLASS_NoExport))
		{
			continue;
		}

		const FSimplifiedParsingClassInfo& ParsingInfo = TypeDefinitionInfoMap[DefinedClass]->GetUnrealSourceFile().GetDefinedClassParsingInfo(DefinedClass);

		const FString& BaseClassName = ParsingInfo.GetBaseClassName();
		const FString& BaseClassNameStripped = GetClassNameWithPrefixRemoved(BaseClassName);

		if (!BaseClassNameStripped.IsEmpty() && !DefinedClass->GetSuperClass())
		{
			UClass* FoundBaseClass = FindObject<UClass>(Package, *BaseClassNameStripped);

			if (FoundBaseClass == nullptr)
			{
				FoundBaseClass = FindObject<UClass>(ANY_PACKAGE, *BaseClassNameStripped);
			}

			if (FoundBaseClass == nullptr)
			{
				// Don't know its parent class. Raise error.
				FError::Throwf(TEXT("Couldn't find parent type for '%s' named '%s' in current module (Package: %s) or any other module parsed so far."), *DefinedClass->GetName(), *BaseClassName, *GetNameSafe(Package));
			}

			DefinedClass->SetSuperStruct(FoundBaseClass);
			DefinedClass->ClassCastFlags |= FoundBaseClass->ClassCastFlags;
		}
	}
}

ECompilationResult::Type PreparseModules(const FString& ModuleInfoPath, int32& NumFailures, FUnrealSourceFiles& UnrealSourceFilesMap, FTypeDefinitionInfoMap& TypeDefinitionInfoMap, FPublicSourceFileSet& PublicSourceFileSet, TMap<UPackage*, const FManifestModule*>& PackageToManifestModuleMap, FClassDeclarations& ClassDeclarations)
{
	// Three passes.  1) Public 'Classes' headers (legacy)  2) Public headers   3) Private headers
	enum EHeaderFolderTypes
	{
		PublicClassesHeaders = 0,
		PublicHeaders = 1,
		PrivateHeaders,

		FolderType_Count
	};

	ECompilationResult::Type Result = ECompilationResult::Succeeded;

#if !PLATFORM_EXCEPTIONS_DISABLED
	FGraphEventArray ExceptionTasks;

	auto LogException = [&Result, &NumFailures, &ExceptionTasks](FString&& Filename, int32 Line, const FString& Message)
	{
		auto LogExceptionTask = [&Result, &NumFailures, Filename = MoveTemp(Filename), Line, Message]()
		{
			TGuardValue<ELogTimes::Type> DisableLogTimes(GPrintLogTimes, ELogTimes::None);

			FString FormattedErrorMessage = FString::Printf(TEXT("%s(%d): Error: %s\r\n"), *Filename, Line, *Message);
			Result = ECompilationResult::OtherCompilationError;

			UE_LOG(LogCompile, Log, TEXT("%s"), *FormattedErrorMessage);
			GWarn->Log(ELogVerbosity::Error, FormattedErrorMessage);

			++NumFailures;
		};

		if (IsInGameThread())
		{
			LogExceptionTask();
		}
		else
		{
			FGraphEventRef EventRef = FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(LogExceptionTask), TStatId(), nullptr, ENamedThreads::GameThread);

			static FCriticalSection ExceptionCS;
			FScopeLock Lock(&ExceptionCS);
			ExceptionTasks.Add(EventRef);
		}
	};
#endif

	for (FManifestModule& Module : GManifest.Modules)
	{
		if (Result != ECompilationResult::Succeeded)
		{
			break;
		}

		// Force regeneration of all subsequent modules, otherwise data will get corrupted.
		Module.ForceRegeneration();

		UPackage* Package = Cast<UPackage>(StaticFindObjectFast(UPackage::StaticClass(), NULL, FName(*Module.LongPackageName), false, false));
		if (Package == NULL)
		{
			Package = CreatePackage(NULL, *Module.LongPackageName);
		}
		// Set some package flags for indicating that this package contains script
		// NOTE: We do this even if we didn't have to create the package, because CoreUObject is compiled into UnrealHeaderTool and we still
		//       want to make sure our flags get set
		Package->SetPackageFlags(PKG_ContainsScript | PKG_Compiling);
		Package->ClearPackageFlags(PKG_ClientOptional | PKG_ServerSideOnly);

		if (Module.OverrideModuleType == EPackageOverrideType::None)
		{
			switch (Module.ModuleType)
			{
			case EBuildModuleType::GameEditor:
			case EBuildModuleType::EngineEditor:
				Package->SetPackageFlags(PKG_EditorOnly);
				break;

			case EBuildModuleType::GameDeveloper:
			case EBuildModuleType::EngineDeveloper:
				Package->SetPackageFlags(PKG_Developer);
				break;

			case EBuildModuleType::GameUncooked:
			case EBuildModuleType::EngineUncooked:
				Package->SetPackageFlags(PKG_UncookedOnly);
				break;
			}
		}
		else
		{
			// If the user has specified this module to have another package flag, then OR it on
			switch (Module.OverrideModuleType)
			{
			case EPackageOverrideType::EditorOnly:
				Package->SetPackageFlags(PKG_EditorOnly);
				break;

			case EPackageOverrideType::EngineDeveloper:
			case EPackageOverrideType::GameDeveloper:
				Package->SetPackageFlags(PKG_Developer);
				break;

			case EPackageOverrideType::EngineUncookedOnly:
			case EPackageOverrideType::GameUncookedOnly:
				Package->SetPackageFlags(PKG_UncookedOnly);
				break;
			}
		}

		// Add new module or overwrite whatever we had loaded, that data is obsolete.
		PackageToManifestModuleMap.Add(Package, &Module);

		double ThisModulePreparseTime = 0.0;
		int32 NumHeadersPreparsed = 0;
		FDurationTimer ThisModuleTimer(ThisModulePreparseTime);
		ThisModuleTimer.Start();

		// Pre-parse the headers
		for (int32 PassIndex = 0; PassIndex < FolderType_Count && Result == ECompilationResult::Succeeded; ++PassIndex)
		{
			EHeaderFolderTypes CurrentlyProcessing = (EHeaderFolderTypes)PassIndex;

			// We'll make an ordered list of all UObject headers we care about.
			// @todo uht: Ideally 'dependson' would not be allowed from public -> private, or NOT at all for new style headers
			const TArray<FString>& UObjectHeaders =
				(CurrentlyProcessing == PublicClassesHeaders) ? Module.PublicUObjectClassesHeaders :
				(CurrentlyProcessing == PublicHeaders) ? Module.PublicUObjectHeaders :
				Module.PrivateUObjectHeaders;
			if (!UObjectHeaders.Num())
			{
				continue;
			}

			NumHeadersPreparsed += UObjectHeaders.Num();

			TArray<FString> HeaderFiles;
			HeaderFiles.SetNum(UObjectHeaders.Num());

			{
				SCOPE_SECONDS_COUNTER_UHT(LoadHeaderContentFromFile);
#if	UHT_USE_PARALLEL_FOR
				ParallelFor(UObjectHeaders.Num(), [&](int32 Index)
#else
				for (int Index = 0; Index < UObjectHeaders.Num(); Index++)
#endif
				{
					const FString& RawFilename = UObjectHeaders[Index];

#if !PLATFORM_EXCEPTIONS_DISABLED
					try
#endif
					{
						const FString FullFilename = FPaths::ConvertRelativePathToFull(ModuleInfoPath, RawFilename);

						if (!FFileHelper::LoadFileToString(HeaderFiles[Index], *FullFilename))
						{
							FError::Throwf(TEXT("UnrealHeaderTool was unable to load source file '%s'"), *FullFilename);
						}
					}
#if !PLATFORM_EXCEPTIONS_DISABLED
					catch (TCHAR* ErrorMsg)
					{
						FString AbsFilename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*RawFilename);
						LogException(MoveTemp(AbsFilename), 1, ErrorMsg);
					}
#endif
				}
#if	UHT_USE_PARALLEL_FOR
				);
#endif
			}

#if !PLATFORM_EXCEPTIONS_DISABLED
			FTaskGraphInterface::Get().WaitUntilTasksComplete(ExceptionTasks);
#endif

			if (Result != ECompilationResult::Succeeded)
			{
				continue;
			}

			TArray<FPerHeaderData> PerHeaderData;
			PerHeaderData.SetNum(UObjectHeaders.Num());

#if	UHT_USE_PARALLEL_FOR
			ParallelFor(UObjectHeaders.Num(), [&](int32 Index)
#else
			for (int Index = 0; Index < UObjectHeaders.Num(); Index++)
#endif
			{
				const FString& RawFilename = UObjectHeaders[Index];

#if !PLATFORM_EXCEPTIONS_DISABLED
				try
#endif
				{
					PerformSimplifiedClassParse(Package, *RawFilename, *HeaderFiles[Index], PerHeaderData[Index], ClassDeclarations);
				}
#if !PLATFORM_EXCEPTIONS_DISABLED
				catch (const FFileLineException& Ex)
				{
					FString AbsFilename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Ex.Filename);
					LogException(MoveTemp(AbsFilename), Ex.Line, Ex.Message);
				}
				catch (TCHAR* ErrorMsg)
				{
					FString AbsFilename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*RawFilename);
					LogException(MoveTemp(AbsFilename), 1, ErrorMsg);
				}
#endif
			}
#if	UHT_USE_PARALLEL_FOR
			);
#endif

#if !PLATFORM_EXCEPTIONS_DISABLED
			FTaskGraphInterface::Get().WaitUntilTasksComplete(ExceptionTasks);
#endif

			if (Result != ECompilationResult::Succeeded)
			{
				continue;
			}

			for (int32 Index = 0; Index < UObjectHeaders.Num(); ++Index)
			{
				const FString& RawFilename = UObjectHeaders[Index];

#if !PLATFORM_EXCEPTIONS_DISABLED
				try
#endif
				{
					// Import class.
					const FString FullFilename = FPaths::ConvertRelativePathToFull(ModuleInfoPath, RawFilename);

					ProcessInitialClassParse(PerHeaderData[Index], TypeDefinitionInfoMap);
					TSharedRef<FUnrealSourceFile> UnrealSourceFile = PerHeaderData[Index].UnrealSourceFile.ToSharedRef();
					FUnrealSourceFile* UnrealSourceFilePtr = &UnrealSourceFile.Get();
					UnrealSourceFilesMap.Add(FPaths::GetCleanFilename(RawFilename), UnrealSourceFile);

					if (CurrentlyProcessing == PublicClassesHeaders)
					{
						PublicSourceFileSet.Add(UnrealSourceFilePtr);
					}

					// Save metadata for the class path, both for it's include path and relative to the module base directory
					if (FullFilename.StartsWith(Module.BaseDirectory))
					{
						// Get the path relative to the module directory
						const TCHAR* ModuleRelativePath = *FullFilename + Module.BaseDirectory.Len();

						UnrealSourceFile->SetModuleRelativePath(ModuleRelativePath);

						// Calculate the include path
						const TCHAR* IncludePath = ModuleRelativePath;

						// Walk over the first potential slash
						if (*IncludePath == TEXT('/'))
						{
							IncludePath++;
						}

						// Does this module path start with a known include path location? If so, we can cut that part out of the include path
						static const TCHAR PublicFolderName[] = TEXT("Public/");
						static const TCHAR PrivateFolderName[] = TEXT("Private/");
						static const TCHAR ClassesFolderName[] = TEXT("Classes/");
						if (FCString::Strnicmp(IncludePath, PublicFolderName, UE_ARRAY_COUNT(PublicFolderName) - 1) == 0)
						{
							IncludePath += (UE_ARRAY_COUNT(PublicFolderName) - 1);
						}
						else if (FCString::Strnicmp(IncludePath, PrivateFolderName, UE_ARRAY_COUNT(PrivateFolderName) - 1) == 0)
						{
							IncludePath += (UE_ARRAY_COUNT(PrivateFolderName) - 1);
						}
						else if (FCString::Strnicmp(IncludePath, ClassesFolderName, UE_ARRAY_COUNT(ClassesFolderName) - 1) == 0)
						{
							IncludePath += (UE_ARRAY_COUNT(ClassesFolderName) - 1);
						}

						// Add the include path
						if (*IncludePath != 0)
						{
							UnrealSourceFile->SetIncludePath(MoveTemp(IncludePath));
						}
					}
				}
#if !PLATFORM_EXCEPTIONS_DISABLED
				catch (const FFileLineException& Ex)
				{
					FString AbsFilename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Ex.Filename);
					LogException(MoveTemp(AbsFilename), Ex.Line, Ex.Message);
				}
				catch (TCHAR* ErrorMsg)
				{
					FString AbsFilename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*RawFilename);
					LogException(MoveTemp(AbsFilename), 1, ErrorMsg);
				}
#endif
			}
			if (Result == ECompilationResult::Succeeded && NumFailures != 0)
			{
				Result = ECompilationResult::OtherCompilationError;
			}
		}

		// Don't resolve superclasses for module when loading from makefile.
		// Data is only partially loaded at this point.
#if !PLATFORM_EXCEPTIONS_DISABLED
		try
#endif
		{
			ResolveSuperClasses(Package, TypeDefinitionInfoMap);
		}
#if !PLATFORM_EXCEPTIONS_DISABLED
		catch (TCHAR* ErrorMsg)
		{
			TGuardValue<ELogTimes::Type> DisableLogTimes(GPrintLogTimes, ELogTimes::None);

			FString FormattedErrorMessage = FString::Printf(TEXT("Error: %s\r\n"), ErrorMsg);

			Result = GCompilationResult;

			UE_LOG(LogCompile, Log, TEXT("%s"), *FormattedErrorMessage);
			GWarn->Log(ELogVerbosity::Error, FormattedErrorMessage);

			++NumFailures;
		}
#endif

		ThisModuleTimer.Stop();
		UE_LOG(LogCompile, Log, TEXT("Preparsed module %s containing %i files(s) in %.2f secs."), *Module.LongPackageName, NumHeadersPreparsed, ThisModulePreparseTime);
	}

	return Result;
}

ECompilationResult::Type UnrealHeaderTool_Main(const FString& ModuleInfoFilename)
{
	double MainTime = 0.0;
	FDurationTimer MainTimer(MainTime);
	MainTimer.Start();

	check(GIsUCCMakeStandaloneHeaderGenerator);
	ECompilationResult::Type Result = ECompilationResult::Succeeded;

	FString ModuleInfoPath = FPaths::GetPath(ModuleInfoFilename);

	// Load the manifest file, giving a list of all modules to be processed, pre-sorted by dependency ordering
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		GManifest = FManifest::LoadFromFile(ModuleInfoFilename);
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (const TCHAR* Ex)
	{
		UE_LOG(LogCompile, Error, TEXT("Failed to load manifest file '%s': %s"), *ModuleInfoFilename, Ex);
		return GCompilationResult;
	}
#endif

	// Counters.
	int32 NumFailures = 0;
	double TotalModulePreparseTime = 0.0;
	double TotalParseAndCodegenTime = 0.0;

	// UHTLite NOTE: Used in PreparseModules() and UnrealHeaderTool_Main().
	// UHTLite NOTE: Written to in FHeaderParser::CompileStructDeclaration() and FHeaderParser::CompileEnum().
	// UHTLite NOTE: Needs a mutex?
	FTypeDefinitionInfoMap TypeDefinitionInfoMap;

	// UHTLite NOTE: Data is written once in PreparseModules() and is then read-only.
	FUnrealSourceFiles   UnrealSourceFilesMap;
	FPublicSourceFileSet PublicSourceFileSet;
	TMap<UPackage*, const FManifestModule*> PackageToManifestModuleMap;

	// UHTLite NOTE: Classes are added in PreparseModules(), but class flags are modified during header parsing!
	FClassDeclarations ClassDeclarations;

	// UHTLite NOTE: This data is cross-module! It is read-only during code generation.
	TMap<UEnum*, EUnderlyingEnumType>      EnumUnderlyingTypes;
	FRWLock                                EnumUnderlyingTypesLock;
	TMap<UClass*, FArchiveTypeDefinePair>  ClassSerializerMap;
	FRWLock                                ClassSerializerMapLock;

	{
		FDurationTimer TotalModulePreparseTimer(TotalModulePreparseTime);
		TotalModulePreparseTimer.Start();
		Result = PreparseModules(ModuleInfoPath, NumFailures, UnrealSourceFilesMap, TypeDefinitionInfoMap, PublicSourceFileSet, PackageToManifestModuleMap, ClassDeclarations);
		TotalModulePreparseTimer.Stop();
	}
	// Do the actual parse of the headers and generate for them
	if (Result == ECompilationResult::Succeeded)
	{
		FScopedDurationTimer ParseAndCodeGenTimer(TotalParseAndCodegenTime);

		TMap<UPackage*, TArray<UClass*>> ClassesByPackageMap;
		ClassesByPackageMap.Reserve(GManifest.Modules.Num());

		// Verify that all script declared superclasses exist.
		for (UClass* ScriptClass : TObjectRange<UClass>())
		{
			ClassesByPackageMap.FindOrAdd(ScriptClass->GetOutermost()).Add(ScriptClass);

			// UHTLite NOTE: Not required for code-gen. Will be refactored later.
			/*
			const UClass* ScriptSuperClass = ScriptClass->GetSuperClass();

			if (ScriptSuperClass && !ScriptSuperClass->HasAnyClassFlags(CLASS_Intrinsic) && TypeDefinitionInfoMap.Contains(ScriptClass) && !TypeDefinitionInfoMap.Contains(ScriptSuperClass))
			{
				class FSuperClassContextSupplier : public FContextSupplier
				{
				public:
					FSuperClassContextSupplier(const UClass* Class)
						: DefinitionInfo(TypeDefinitionInfoMap[Class])
					{ }

					virtual FString GetContext() override
					{
						FString Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*DefinitionInfo->GetUnrealSourceFile().GetFilename());
						int32 LineNumber = DefinitionInfo->GetLineNumber();
						return FString::Printf(TEXT("%s(%i)"), *Filename, LineNumber);
					}
				private:
					TSharedRef<FUnrealTypeDefinitionInfo> DefinitionInfo;
				} ContextSupplier(ScriptClass);

				FContextSupplier* OldContext = GWarn->GetContext();

				TGuardValue<ELogTimes::Type> DisableLogTimes(GPrintLogTimes, ELogTimes::None);

				GWarn->SetContext(&ContextSupplier);
				GWarn->Log(ELogVerbosity::Error, FString::Printf(TEXT("Error: Superclass %s of class %s not found"), *ScriptSuperClass->GetName(), *ScriptClass->GetName()));
				GWarn->SetContext(OldContext);

				Result = ECompilationResult::OtherCompilationError;
				++NumFailures;
			}
			*/
		}

		if (Result == ECompilationResult::Succeeded)
		{
#pragma region Plugins
			TArray<IScriptGeneratorPluginInterface*> ScriptPlugins;
			// Can only export scripts for game targets
			if (GManifest.IsGameTarget)
			{
				GetScriptPlugins(ScriptPlugins);
			}
#pragma endregion Plugins

			// UHTLite NOTE: TypeDefinitionInfoMap needs a mutex before this loop can be threaded!
			for (const FManifestModule& Module : GManifest.Modules)
			{
				if (UPackage* Package = Cast<UPackage>(StaticFindObjectFast(UPackage::StaticClass(), NULL, FName(*Module.LongPackageName), false, false)))
				{
					FClasses AllClasses(ClassesByPackageMap.Find(Package));
					AllClasses.Validate();

					Result = FHeaderParser::ParseAllHeadersInside(AllClasses,
						GWarn,
						Package,
						Module,
#pragma region Plugins
						ScriptPlugins,
#pragma endregion Plugins
						UnrealSourceFilesMap,
						TypeDefinitionInfoMap,
						PublicSourceFileSet,
						PackageToManifestModuleMap,
						ClassDeclarations,
						EnumUnderlyingTypes,
						EnumUnderlyingTypesLock,
						ClassSerializerMap,
						ClassSerializerMapLock);

					if (Result != ECompilationResult::Succeeded)
					{
						++NumFailures;
						break;
					}
				}
			}

#pragma region Plugins
			{
				FScopedDurationTimer PluginTimeTracker(GPluginOverheadTime);
				for (IScriptGeneratorPluginInterface* ScriptGenerator : ScriptPlugins)
				{
					ScriptGenerator->FinishExport();
				}
			}

			// Get a list of external dependencies from each enabled plugin
			FString ExternalDependencies;
			for (IScriptGeneratorPluginInterface* ScriptPlugin : ScriptPlugins)
			{
				TArray<FString> PluginExternalDependencies;
				ScriptPlugin->GetExternalDependencies(PluginExternalDependencies);

				for (const FString& PluginExternalDependency : PluginExternalDependencies)
				{
					ExternalDependencies += PluginExternalDependency + LINE_TERMINATOR;
				}
			}

			FFileHelper::SaveStringToFile(ExternalDependencies, *GManifest.ExternalDependenciesFile);
#pragma endregion Plugins
		}
	}

	// Avoid TArray slack for meta data.
	GScriptHelper.Shrink();

	// Finish all async file tasks before stopping the clock
	FTaskGraphInterface::Get().WaitUntilTasksComplete(GAsyncFileTasks);

	MainTimer.Stop();

	UE_LOG(LogCompile, Log, TEXT("Preparsing %i modules took %.2f seconds"), GManifest.Modules.Num(), TotalModulePreparseTime);
	UE_LOG(LogCompile, Log, TEXT("Parsing took %.2f seconds"), TotalParseAndCodegenTime - GHeaderCodeGenTime);
	UE_LOG(LogCompile, Log, TEXT("Code generation took %.2f seconds"), GHeaderCodeGenTime);
	UE_LOG(LogCompile, Log, TEXT("ScriptPlugin overhead was %.2f seconds"), GPluginOverheadTime);
	UE_LOG(LogCompile, Log, TEXT("Macroize time was %.2f seconds"), GMacroizeTime);

	FUnrealHeaderToolStats& Stats = FUnrealHeaderToolStats::Get();
	for (const TPair<FName, double>& Pair : Stats.Counters)
	{
		FString CounterName = Pair.Key.ToString();
		UE_LOG(LogCompile, Log, TEXT("%s timer was %.3f seconds"), *CounterName, Pair.Value);
	}

	UE_LOG(LogCompile, Log, TEXT("Total time was %.2f seconds"), MainTime);

	if (bWriteContents)
	{
		UE_LOG(LogCompile, Log, TEXT("********************************* Wrote reference generated code to ReferenceGeneratedCode."));
	}
	else if (bVerifyContents)
	{
		UE_LOG(LogCompile, Log, TEXT("********************************* Wrote generated code to VerifyGeneratedCode and compared to ReferenceGeneratedCode"));
		for (FString& Msg : ChangeMessages)
		{
			UE_LOG(LogCompile, Error, TEXT("%s"), *Msg);
		}
		TArray<FString> RefFileNames;
		IFileManager::Get().FindFiles(RefFileNames, *(FPaths::ProjectSavedDir() / TEXT("ReferenceGeneratedCode/*.*")), true, false);
		TArray<FString> VerFileNames;
		IFileManager::Get().FindFiles(VerFileNames, *(FPaths::ProjectSavedDir() / TEXT("VerifyGeneratedCode/*.*")), true, false);
		if (RefFileNames.Num() != VerFileNames.Num())
		{
			UE_LOG(LogCompile, Error, TEXT("Number of generated files mismatch ref=%d, ver=%d"), RefFileNames.Num(), VerFileNames.Num());
		}
	}

	RequestEngineExit(TEXT("UnrealHeaderTool finished"));

	if (Result != ECompilationResult::Succeeded || NumFailures > 0)
	{
		return ECompilationResult::OtherCompilationError;
	}

	return Result;
}

UClass* ProcessParsedClass(bool bClassIsAnInterface, TArray<FHeaderProvider>& DependentOn, const FString& ClassName, const FString& BaseClassName, UObject* InParent, EObjectFlags Flags)
{
	FString ClassNameStripped = GetClassNameWithPrefixRemoved(*ClassName);

	// All classes must start with a valid unreal prefix
	if (!FHeaderParser::ClassNameHasValidPrefix(ClassName, ClassNameStripped))
	{
		FError::Throwf(TEXT("Invalid class name '%s'. The class name must have an appropriate prefix added (A for Actors, U for other classes)."), *ClassName);
	}

	if (FHeaderParser::IsReservedTypeName(ClassNameStripped))
	{
		FError::Throwf(TEXT("Invalid class name '%s'. Cannot use a reserved name ('%s')."), *ClassName, *ClassNameStripped);
	}

	// Ensure the base class has any valid prefix and exists as a valid class. Checking for the 'correct' prefix will occur during compilation
	FString BaseClassNameStripped;
	if (!BaseClassName.IsEmpty())
	{
		BaseClassNameStripped = GetClassNameWithPrefixRemoved(BaseClassName);
		if (!FHeaderParser::ClassNameHasValidPrefix(BaseClassName, BaseClassNameStripped))
		{
			FError::Throwf(TEXT("No prefix or invalid identifier for base class %s.\nClass names must match Unreal prefix specifications (e.g., \"UObject\" or \"AActor\")"), *BaseClassName);
		}
	}

	//UE_LOG(LogCompile, Log, TEXT("Class: %s extends %s"),*ClassName,*BaseClassName);
	// Handle failure and non-class headers.
	if (BaseClassName.IsEmpty() && (ClassName != TEXT("UObject")))
	{
		FError::Throwf(TEXT("Class '%s' must inherit UObject or a UObject-derived class"), *ClassName);
	}

	if (ClassName == BaseClassName)
	{
		FError::Throwf(TEXT("Class '%s' cannot inherit from itself"), *ClassName);
	}

	// In case the file system and the class disagree on the case of the
	// class name replace the fname with the one from the script class file
	// This is needed because not all source control systems respect the
	// original filename's case
	FName ClassNameReplace(*ClassName, FNAME_Replace_Not_Safe_For_Threading);

	// Use stripped class name for processing and replace as we did above
	FName ClassNameStrippedReplace(*ClassNameStripped, FNAME_Replace_Not_Safe_For_Threading);

	UClass* ResultClass = FindObject<UClass>(InParent, *ClassNameStripped);

	// if we aren't generating headers, then we shouldn't set misaligned object, since it won't get cleared

	const static bool bVerboseOutput = FParse::Param(FCommandLine::Get(), TEXT("VERBOSE"));

	if (ResultClass == nullptr || !ResultClass->IsNative())
	{
		// detect if the same class name is used in multiple packages
		if (ResultClass == nullptr)
		{
			UClass* ConflictingClass = FindObject<UClass>(ANY_PACKAGE, *ClassNameStripped, true);
			if (ConflictingClass != nullptr)
			{
				UE_LOG_WARNING_UHT(TEXT("Duplicate class name: %s also exists in file %s"), *ClassName, *ConflictingClass->GetOutermost()->GetName());
			}
		}

		// Create new class.
		ResultClass = new(EC_InternalUseOnlyConstructor, InParent, *ClassNameStripped, Flags) UClass(FObjectInitializer(), nullptr);

		// add CLASS_Interface flag if the class is an interface
		// NOTE: at this pre-parsing/importing stage, we cannot know if our super class is an interface or not,
		// we leave the validation to the main header parser
		if (bClassIsAnInterface)
		{
			ResultClass->ClassFlags |= CLASS_Interface;
		}

		if (bVerboseOutput)
		{
			UE_LOG(LogCompile, Log, TEXT("Imported: %s"), *ResultClass->GetFullName());
		}
	}

	if (bVerboseOutput)
	{
		for (const FHeaderProvider& Dependency : DependentOn)
		{
			UE_LOG(LogCompile, Log, TEXT("\tAdding %s as a dependency"), *Dependency.ToString());
		}
	}

	return ResultClass;
}

void PerformSimplifiedClassParse(UPackage* InParent, const TCHAR* FileName, const TCHAR* Buffer, FPerHeaderData& PerHeaderData, FClassDeclarations& ClassDeclarations)
{
	// Parse the header to extract the information needed
	FUHTStringBuilder ClassHeaderTextStrippedOfCppText;

	FHeaderParser::SimplifiedClassParse(FileName, Buffer, /*out*/ PerHeaderData.ParsedClassArray, /*out*/ PerHeaderData.DependsOn, ClassHeaderTextStrippedOfCppText, ClassDeclarations);

	FUnrealSourceFile* UnrealSourceFilePtr = new FUnrealSourceFile(InParent, FileName, MoveTemp(ClassHeaderTextStrippedOfCppText));
	PerHeaderData.UnrealSourceFile = MakeShareable(UnrealSourceFilePtr);
}

void ProcessInitialClassParse(FPerHeaderData& PerHeaderData, FTypeDefinitionInfoMap& TypeDefinitionInfoMap)
{
	TSharedRef<FUnrealSourceFile> UnrealSourceFile = PerHeaderData.UnrealSourceFile.ToSharedRef();
	UPackage* InParent = UnrealSourceFile->GetPackage();
	for (FSimplifiedParsingClassInfo& ParsedClassInfo : PerHeaderData.ParsedClassArray)
	{
		UClass* ResultClass = ProcessParsedClass(ParsedClassInfo.IsInterface(), PerHeaderData.DependsOn, ParsedClassInfo.GetClassName(), ParsedClassInfo.GetBaseClassName(), InParent, RF_Public | RF_Standalone);

		// UHTLite NOTE: Not required for code-gen. Will be refactored later.
		/*
		GStructToSourceLine.Add(ResultClass, MakeTuple(UnrealSourceFile, ParsedClassInfo.GetClassDefLine()));
		*/

		FScope::AddTypeScope(ResultClass, &UnrealSourceFile->GetScope().Get());

		TypeDefinitionInfoMap.Add(ResultClass, MakeShared<FUnrealTypeDefinitionInfo>(UnrealSourceFile.Get(), ParsedClassInfo.GetClassDefLine()));
		UnrealSourceFile->AddDefinedClass(ResultClass, MoveTemp(ParsedClassInfo));
	}

	for (FHeaderProvider& DependsOnElement : PerHeaderData.DependsOn)
	{
		UnrealSourceFile->GetIncludes().AddUnique(DependsOnElement);
	}
}

/**
 * Application entry point
 *
 * @param	ArgC	Command-line argument count
 * @param	ArgV	Argument strings
 */
INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	FTaskTagScope Scope(ETaskTag::EGameThread);
	FString CmdLine;

	for (int32 Arg = 0; Arg < ArgC; Arg++)
	{
		FString LocalArg = ArgV[Arg];
		if (LocalArg.Contains(TEXT(" "), ESearchCase::CaseSensitive))
		{
			CmdLine += TEXT("\"");
			CmdLine += LocalArg;
			CmdLine += TEXT("\"");
		}
		else
		{
			CmdLine += LocalArg;
		}

		if (Arg + 1 < ArgC)
		{
			CmdLine += TEXT(" ");
		}
	}

	FString ShortCmdLine = FCommandLine::RemoveExeName(*CmdLine);
	ShortCmdLine.TrimStartInline();	

	// Get game name from the commandline. It will later be used to load the correct ini files.
	FString ModuleInfoFilename;
	if (ShortCmdLine.Len() && **ShortCmdLine != TEXT('-'))
	{
		const TCHAR* CmdLinePtr = *ShortCmdLine;

		// Parse the game name or project filename.  UHT reads the list of plugins from there in case one of the plugins is UHT plugin.
		FString GameName = FParse::Token(CmdLinePtr, false);

		// This parameter is the absolute path to the file which contains information about the modules
		// that UHT needs to generate code for.
		ModuleInfoFilename = FParse::Token(CmdLinePtr, false );
	}

#if !NO_LOGGING
	const static bool bVerbose = FParse::Param(*CmdLine,TEXT("VERBOSE"));
	if (bVerbose)
	{
		UE_SET_LOG_VERBOSITY(LogCompile, Verbose);
	}
#endif

	// Make sure the engine is properly cleaned up whenever we exit this function
	ON_SCOPE_EXIT
	{
		FEngineLoop::AppPreExit();
		FEngineLoop::AppExit();
	};

	GIsUCCMakeStandaloneHeaderGenerator = true;
	if (GEngineLoop.PreInit(*ShortCmdLine) != 0)
	{
		UE_LOG(LogCompile, Error, TEXT("Failed to initialize the engine (PreInit failed)."));
		return ECompilationResult::CrashOrAssert;
	}

	// Log full command line for UHT as UBT overrides LogInit verbosity settings
	UE_LOG(LogCompile, Log, TEXT("UHT Command Line: %s"), *CmdLine);

	if (ModuleInfoFilename.IsEmpty())
	{
		if (!FPlatformMisc::IsDebuggerPresent())
		{
			UE_LOG(LogCompile, Error, TEXT( "Missing module info filename on command line" ));
			return ECompilationResult::OtherCompilationError;
		}

		// If we have a debugger, let's use a pre-existing manifest file to streamline debugging
		// without the user having to faff around trying to get a UBT-generated manifest
		ModuleInfoFilename = FPaths::ConvertRelativePathToFull(FPlatformProcess::BaseDir(), TEXT("../../Source/Programs/UnrealHeaderTool/Resources/UHTDebugging.manifest"));
	}

	ECompilationResult::Type Result = UnrealHeaderTool_Main(ModuleInfoFilename);

	if (Result == ECompilationResult::Succeeded && (GUHTErrorLogged || (GUHTWarningLogged && GWarn->TreatWarningsAsErrors)))
	{
		Result = ECompilationResult::OtherCompilationError;
	}

	return Result;
}
