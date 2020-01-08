// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClassViewerFilter.h"

#include "UnloadedBlueprintData.h"
#include "Engine/Brush.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "AssetRegistryModule.h"
#include "PropertyHandle.h"

EFilterReturn::Type FClassViewerFilterFuncs::IfInChildOfClassesSet(TSet< const UClass* >& InSet, const UClass* InClass)
{
	check(InClass);

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			if (InClass->IsChildOf(*CurClassIt))
			{
				return EFilterReturn::Passed;
			}
		}

		return EFilterReturn::Failed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfInChildOfClassesSet(TSet< const UClass* >& InSet, const TSharedPtr< const IUnloadedBlueprintData > InClass)
{
	check(InClass.IsValid());

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			if (InClass->IsChildOf(*CurClassIt))
			{
				return EFilterReturn::Passed;
			}
		}

		return EFilterReturn::Failed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfMatchesAllInChildOfClassesSet(TSet< const UClass* >& InSet, const UClass* InClass)
{
	check(InClass);

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			if (!InClass->IsChildOf(*CurClassIt))
			{
				// Since it doesn't match one, it fails.
				return EFilterReturn::Failed;
			}
		}

		// It matches all of them, so it passes.
		return EFilterReturn::Passed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfMatchesAllInChildOfClassesSet(TSet< const UClass* >& InSet, const TSharedPtr< const IUnloadedBlueprintData > InClass)
{
	check(InClass.IsValid());

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			if (!InClass->IsChildOf(*CurClassIt))
			{
				// Since it doesn't match one, it fails.
				return EFilterReturn::Failed;
			}
		}

		// It matches all of them, so it passes.
		return EFilterReturn::Passed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfMatchesAll_ObjectsSetIsAClass(TSet< const UObject* >& InSet, const UClass* InClass)
{
	check(InClass);

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			if (!(*CurClassIt)->IsA(InClass))
			{
				// Since it doesn't match one, it fails.
				return EFilterReturn::Failed;
			}
		}

		// It matches all of them, so it passes.
		return EFilterReturn::Passed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfMatchesAll_ObjectsSetIsAClass(TSet< const UObject* >& InSet, const TSharedPtr< const IUnloadedBlueprintData > InClass)
{
	check(InClass.IsValid());

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			if (!(*CurClassIt)->IsA(UBlueprintGeneratedClass::StaticClass()))
			{
				// Since it doesn't match one, it fails.
				return EFilterReturn::Failed;
			}
		}

		// It matches all of them, so it passes.
		return EFilterReturn::Passed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfMatchesAll_ClassesSetIsAClass(TSet< const UClass* >& InSet, const UClass* InClass)
{
	check(InClass);

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			const UObject* Object = *CurClassIt;
			if (!Object->IsA(InClass))
			{
				// Since it doesn't match one, it fails.
				return EFilterReturn::Failed;
			}
		}

		// It matches all of them, so it passes.
		return EFilterReturn::Passed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfMatchesAll_ClassesSetIsAClass(TSet< const UClass* >& InSet, const TSharedPtr< const IUnloadedBlueprintData > InClass)
{
	check(InClass.IsValid());

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			const UObject* Object = *CurClassIt;
			if (!Object->IsA(UBlueprintGeneratedClass::StaticClass()))
			{
				// Since it doesn't match one, it fails.
				return EFilterReturn::Failed;
			}
		}

		// It matches all of them, so it passes.
		return EFilterReturn::Passed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfMatches_ClassesSetIsAClass(TSet< const UClass* >& InSet, const UClass* InClass)
{
	check(InClass);

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			const UObject* Object = *CurClassIt;
			if (Object->IsA(InClass))
			{
				return EFilterReturn::Passed;
			}
		}

		return EFilterReturn::Failed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfMatches_ClassesSetIsAClass(TSet< const UClass* >& InSet, const TSharedPtr< const IUnloadedBlueprintData > InClass)
{
	check(InClass.IsValid());

	if (InSet.Num())
	{
		// If a class is a child of any classes on this list, it will be allowed onto the list, unless it also appears on a disallowed list.
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			const UObject* Object = *CurClassIt;
			if (Object->IsA(UBlueprintGeneratedClass::StaticClass()))
			{
				return EFilterReturn::Passed;
			}
		}

		return EFilterReturn::Failed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfInClassesSet(TSet< const UClass* >& InSet, const UClass* InClass)
{
	check(InClass);

	if (InSet.Num())
	{
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			if (InClass == *CurClassIt)
			{
				return EFilterReturn::Passed;
			}
		}
		return EFilterReturn::Failed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

EFilterReturn::Type FClassViewerFilterFuncs::IfInClassesSet(TSet< const UClass* >& InSet, const TSharedPtr< const IUnloadedBlueprintData > InClass)
{
	check(InClass.IsValid());

	if (InSet.Num())
	{
		for (auto CurClassIt = InSet.CreateConstIterator(); CurClassIt; ++CurClassIt)
		{
			const TSharedPtr<const FUnloadedBlueprintData> UnloadedBlueprintData = StaticCastSharedPtr<const FUnloadedBlueprintData>(InClass);
			if (*UnloadedBlueprintData->GetClassViewerNode().Pin()->GetClassName() == (*CurClassIt)->GetName())
			{
				return EFilterReturn::Passed;
			}
		}
		return EFilterReturn::Failed;
	}

	// Since there are none on this list, return that there is no items.
	return EFilterReturn::NoItems;
}

/** Checks if a particular class is a brush.
*	@param InClass				The Class to check.
*	@return Returns true if the class is a brush.
*/
static bool IsBrush(const UClass* InClass)
{
	return InClass->IsChildOf(ABrush::StaticClass());
}

static bool IsBrush(const TSharedRef<const IUnloadedBlueprintData>& InBlueprintData)
{
	return InBlueprintData->IsChildOf(ABrush::StaticClass());
}

/** Checks if a particular class is placeable.
*	@param InClass				The Class to check.
*	@return Returns true if the class is placeable.
*/
static bool IsPlaceable(const UClass* InClass)
{
	return !InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_NotPlaceable) 
		&& InClass->IsChildOf(AActor::StaticClass());
}

static bool IsPlaceable(const TSharedRef<const IUnloadedBlueprintData>& InBlueprintData)
{
	return !InBlueprintData->HasAnyClassFlags(CLASS_Abstract | CLASS_NotPlaceable)
		&& InBlueprintData->IsChildOf(AActor::StaticClass());
}

/** Util class to checks if a particular class can be made into a Blueprint, ignores deprecation
 *
 * @param InClass	The class to verify can be made into a Blueprint
 * @return			true if the class can be made into a Blueprint
 */
static bool CanCreateBlueprintOfClass(UClass* InClass)
{
	// Temporarily remove the deprecated flag so we can check if it is valid for
	bool bIsClassDeprecated = InClass->HasAnyClassFlags(CLASS_Deprecated);
	InClass->ClassFlags &= ~CLASS_Deprecated;

	bool bCanCreateBlueprintOfClass = FKismetEditorUtilities::CanCreateBlueprintOfClass(InClass);

	// Reassign the deprecated flag if it was previously assigned
	if (bIsClassDeprecated)
	{
		InClass->ClassFlags |= CLASS_Deprecated;
	}

	return bCanCreateBlueprintOfClass;
}

/** Checks if a node is a blueprint base or not.
*	@param	InNode	The node to check if it is a blueprint base.
*	@return			true if the class is a blueprint base.
*/
static bool CheckIfBlueprintBase(const TSharedRef<const IUnloadedBlueprintData>& InBlueprintData)
{
	if (InBlueprintData->IsNormalBlueprintType())
	{
		bool bAllowDerivedBlueprints = false;
		GConfig->GetBool(TEXT("Kismet"), TEXT("AllowDerivedBlueprints"), /*out*/ bAllowDerivedBlueprints, GEngineIni);

		return bAllowDerivedBlueprints;
	}
	return false;
}

/** Checks if the TestString passes the filter.
*	@param InTestString			The string to test against the filter.
*	@param InTextFilter			Compiled text filter to apply.
*
*	@return	true if it passes the filter.
*/
static bool PassesTextFilter(const FString& InTestString, const TSharedRef<FTextFilterExpressionEvaluator>& InTextFilter)
{
	class FClassFilterContext : public ITextFilterExpressionContext
	{
	public:
		explicit FClassFilterContext(const FString& InStr)
			: StrPtr(&InStr)
		{
		}

		virtual bool TestBasicStringExpression(const FTextFilterString& InValue, const ETextFilterTextComparisonMode InTextComparisonMode) const override
		{
			return TextFilterUtils::TestBasicStringExpression(*StrPtr, InValue, InTextComparisonMode);
		}

		virtual bool TestComplexExpression(const FName& InKey, const FTextFilterString& InValue, const ETextFilterComparisonOperation InComparisonOperation, const ETextFilterTextComparisonMode InTextComparisonMode) const override
		{
			return false;
		}

	private:
		const FString* StrPtr;
	};

	return InTextFilter->TestTextFilter(FClassFilterContext(InTestString));
}

FClassViewerFilter::FClassViewerFilter(const FClassViewerInitializationOptions& InInitOptions) :
	TextFilter(MakeShared<FTextFilterExpressionEvaluator>(ETextFilterExpressionEvaluatorMode::BasicString)),
	FilterFunctions(MakeShared<FClassViewerFilterFuncs>()),
	AssetRegistry(FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get())
{
	// Create a game-specific filter, if the referencing property/assets were supplied
	if (GEditor)
	{
		FAssetReferenceFilterContext AssetReferenceFilterContext;
		AssetReferenceFilterContext.ReferencingAssets = InInitOptions.AdditionalReferencingAssets;
		if (InInitOptions.PropertyHandle.IsValid())
		{
			TArray<UObject*> ReferencingObjects;
			InInitOptions.PropertyHandle->GetOuterObjects(ReferencingObjects);
			for (UObject* ReferencingObject : ReferencingObjects)
			{
				AssetReferenceFilterContext.ReferencingAssets.Add(FAssetData(ReferencingObject));
			}
		}
		AssetReferenceFilter = GEditor->MakeAssetReferenceFilter(AssetReferenceFilterContext);
	}
}

bool FClassViewerFilter::IsNodeAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<FClassViewerNode>& InNode, const bool bCheckTextFilter)
{
	if (InNode->Class.IsValid())
	{
		return IsClassAllowed(InInitOptions, InNode->Class.Get(), FilterFunctions, bCheckTextFilter);
	}
	else if (InInitOptions.bShowUnloadedBlueprints && InNode->UnloadedBlueprintData.IsValid())
	{
		return IsUnloadedClassAllowed(InInitOptions, InNode->UnloadedBlueprintData.ToSharedRef(), FilterFunctions, bCheckTextFilter);
	}

	return false;
}

bool FClassViewerFilter::IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<class FClassViewerFilterFuncs> InFilterFuncs)
{
	const bool bCheckTextFilter = true;
	return IsClassAllowed(InInitOptions, InClass, InFilterFuncs, bCheckTextFilter);
}

bool FClassViewerFilter::IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<class FClassViewerFilterFuncs> InFilterFuncs, const bool bCheckTextFilter)
{
	if (InInitOptions.bIsActorsOnly && !InClass->IsChildOf(AActor::StaticClass()))
	{
		return false;
	}

	const bool bPassesBlueprintBaseFilter = !InInitOptions.bIsBlueprintBaseOnly || CanCreateBlueprintOfClass(const_cast<UClass*>(InClass));
	const bool bPassesEditorClassFilter = !InInitOptions.bEditorClassesOnly || IsEditorOnlyObject(InClass);

	// Determine if we allow any developer folder classes, if so determine if this class is in one of the allowed developer folders.
	static const FString DeveloperPathWithSlash = FPackageName::FilenameToLongPackageName(FPaths::GameDevelopersDir());
	static const FString UserDeveloperPathWithSlash = FPackageName::FilenameToLongPackageName(FPaths::GameUserDeveloperDir());
	const FString GeneratedClassPathString = InClass->GetPathName();

	const UClassViewerSettings* ClassViewerSettings = GetDefault<UClassViewerSettings>();

	bool bPassesDeveloperFilter = true;
	EClassViewerDeveloperType AllowedDeveloperType = ClassViewerSettings->DeveloperFolderType;
	if (AllowedDeveloperType == EClassViewerDeveloperType::CVDT_None)
	{
		bPassesDeveloperFilter = !GeneratedClassPathString.StartsWith(DeveloperPathWithSlash);
	}
	else if (AllowedDeveloperType == EClassViewerDeveloperType::CVDT_CurrentUser)
	{
		if (GeneratedClassPathString.StartsWith(DeveloperPathWithSlash))
		{
			bPassesDeveloperFilter = GeneratedClassPathString.StartsWith(UserDeveloperPathWithSlash);
		}
	}
	
	// The INI files declare classes and folders that are considered internal only. Does this class match any of those patterns?
	// INI path: /Script/ClassViewer.ClassViewerProjectSettings
	bool bPassesInternalFilter = true;
	if (!ClassViewerSettings->DisplayInternalClasses)
	{
		for (const FDirectoryPath& DirPath : InternalPaths)
		{
			if (GeneratedClassPathString.StartsWith(DirPath.Path))
			{
				bPassesInternalFilter = false;
				break;
			}
		}

		if (bPassesInternalFilter)
		{
			for (const UClass* Class : InternalClasses)
			{
				if (InClass->IsChildOf(Class))
				{
					bPassesInternalFilter = false;
					break;
				}
			}
		}
	}

	// The INI files can contain a list of globally allowed classes - if it does, then only classes whose names match will be shown.
	bool bPassesAllowedClasses = true;
	if (ClassViewerSettings->AllowedClasses.Num() > 0)
	{
		if (!ClassViewerSettings->AllowedClasses.Contains(GeneratedClassPathString))
		{
			bPassesAllowedClasses = false;
		}
	}

	bool bPassesPlaceableFilter = true;
	if (InInitOptions.bIsPlaceableOnly)
	{
		bPassesPlaceableFilter = IsPlaceable(InClass) && 
			(InInitOptions.Mode == EClassViewerMode::ClassPicker || !IsBrush(InClass));
	}

	bool bPassesCustomFilter = true;
	if (InInitOptions.ClassFilter.IsValid())
	{
		bPassesCustomFilter = InInitOptions.ClassFilter->IsClassAllowed(InInitOptions, InClass, FilterFunctions);
	}

	const bool bPassesTextFilter = PassesTextFilter(InClass->GetName(), TextFilter);

	bool bPassesAssetReferenceFilter = true;
	if (AssetReferenceFilter.IsValid() && !InClass->IsNative())
	{
		bPassesAssetReferenceFilter = AssetReferenceFilter->PassesFilter(FAssetData(InClass));
	}

	bool bPassesFilter = bPassesAllowedClasses && bPassesPlaceableFilter && bPassesBlueprintBaseFilter
		&& bPassesDeveloperFilter && bPassesInternalFilter && bPassesEditorClassFilter 
		&& bPassesCustomFilter && (!bCheckTextFilter || bPassesTextFilter) && bPassesAssetReferenceFilter;

	return bPassesFilter;
}

bool FClassViewerFilter::IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs)
{
	const bool bCheckTextFilter = true;
	return IsUnloadedClassAllowed(InInitOptions, InUnloadedClassData, InFilterFuncs, bCheckTextFilter);
}

bool FClassViewerFilter::IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs, const bool bCheckTextFilter)
{
	if (InInitOptions.bIsActorsOnly && !InUnloadedClassData->IsChildOf(AActor::StaticClass()))
	{
		return false;
	}

	const bool bIsBlueprintBase = CheckIfBlueprintBase(InUnloadedClassData);
	const bool bPassesBlueprintBaseFilter = !InInitOptions.bIsBlueprintBaseOnly || bIsBlueprintBase;

	// unloaded blueprints cannot be editor-only
	const bool bPassesEditorClassFilter = !InInitOptions.bEditorClassesOnly;

	// Determine if we allow any developer folder classes, if so determine if this class is in one of the allowed developer folders.
	static const FString DeveloperPathWithSlash = FPackageName::FilenameToLongPackageName(FPaths::GameDevelopersDir());
	static const FString UserDeveloperPathWithSlash = FPackageName::FilenameToLongPackageName(FPaths::GameUserDeveloperDir());
	FString GeneratedClassPathString = InUnloadedClassData->GetClassPath().ToString();

	bool bPassesDeveloperFilter = true;

	const UClassViewerSettings* ClassViewerSettings = GetDefault<UClassViewerSettings>();
	EClassViewerDeveloperType AllowedDeveloperType = ClassViewerSettings->DeveloperFolderType;
	if (AllowedDeveloperType == EClassViewerDeveloperType::CVDT_None)
	{
		bPassesDeveloperFilter = !GeneratedClassPathString.StartsWith(DeveloperPathWithSlash);
	}
	else if (AllowedDeveloperType == EClassViewerDeveloperType::CVDT_CurrentUser)
	{
		if (GeneratedClassPathString.StartsWith(DeveloperPathWithSlash))
		{
			bPassesDeveloperFilter = GeneratedClassPathString.StartsWith(UserDeveloperPathWithSlash);
		}
	}

	// The INI files declare classes and folders that are considered internal only. Does this class match any of those patterns?
	// INI path: /Script/ClassViewer.ClassViewerProjectSettings
	bool bPassesInternalFilter = true;
	if (!ClassViewerSettings->DisplayInternalClasses)
	{
		for (const FDirectoryPath& DirPath : InternalPaths)
		{
			if (GeneratedClassPathString.StartsWith(DirPath.Path))
			{
				bPassesInternalFilter = false;
				break;
			}
		}
	}

	// The INI files can contain a list of globally allowed classes - if it does, then only classes whose names match will be shown.
	bool bPassesAllowedClasses = true;
	if (ClassViewerSettings->AllowedClasses.Num() > 0)
	{
		if (!ClassViewerSettings->AllowedClasses.Contains(GeneratedClassPathString))
		{
			bPassesAllowedClasses = false;
		}
	}

	bool bPassesPlaceableFilter = true;
	if (InInitOptions.bIsPlaceableOnly)
	{
		bPassesPlaceableFilter = IsPlaceable(InUnloadedClassData) &&
			(InInitOptions.Mode == EClassViewerMode::ClassPicker || !IsBrush(InUnloadedClassData));
	}

	bool bPassesCustomFilter = true;
	if (InInitOptions.ClassFilter.IsValid())
	{
		bPassesCustomFilter = InInitOptions.ClassFilter->IsUnloadedClassAllowed(InInitOptions, InUnloadedClassData, FilterFunctions);
	}

	const bool bPassesTextFilter = PassesTextFilter(*InUnloadedClassData->GetClassName().Get(), TextFilter);

	bool bPassesAssetReferenceFilter = true;
	if (AssetReferenceFilter.IsValid() && bIsBlueprintBase)
	{
		FName BlueprintPath = FName(*GeneratedClassPathString.LeftChop(2)); // Chop off _C
		bPassesAssetReferenceFilter = AssetReferenceFilter->PassesFilter(AssetRegistry.GetAssetByObjectPath(BlueprintPath));
	}

	bool bPassesFilter = bPassesPlaceableFilter && bPassesBlueprintBaseFilter
		&& bPassesDeveloperFilter && bPassesInternalFilter && bPassesEditorClassFilter
		&& bPassesCustomFilter && (!bCheckTextFilter || bPassesTextFilter) && bPassesAssetReferenceFilter;

	return bPassesFilter;
}
