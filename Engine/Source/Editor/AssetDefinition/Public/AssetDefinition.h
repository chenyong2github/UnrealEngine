// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/ScopedSlowTask.h"
#include "Toolkits/IToolkit.h"

#include "AssetDefinition.generated.h"

struct FAssetFilterData;
class IToolkitHost;
class UThumbnailInfo;
struct FSlateBrush;
class ISourceControlRevision;
class SWidget;

UENUM()
enum class EAssetActivationMethod : uint8
{
	DoubleClicked,
	Opened,
	Previewed
};

UENUM()
enum class EAssetCommandResult : uint8
{
	Handled,
	Unhandled
};

UENUM()
enum class EAssetOpenMethod : uint8
{
	Edit,
	View,
	//Preview
};

enum class EAssetMergeResult : uint8
{
	Unknown,
	Completed,
	Cancelled,
};

struct FAssetArgs
{
	FAssetArgs() { }
	FAssetArgs(TConstArrayView<FAssetData> InAssets) : Assets(InAssets) { }
	
	TConstArrayView<FAssetData> Assets;

	template<typename ExpectedObjectType>
	TArray<ExpectedObjectType*> LoadObjects(const TSet<FName>& LoadTags = {}) const
	{
		FScopedSlowTask SlowTask(Assets.Num());
	
		TArray<ExpectedObjectType*> LoadedObjects;
		LoadedObjects.Reserve(Assets.Num());
		
		for (const FAssetData& Asset : Assets)
		{
			SlowTask.EnterProgressFrame(1, FText::FromString(Asset.GetObjectPathString()));
			
			if (Asset.IsInstanceOf(ExpectedObjectType::StaticClass()))
			{
				if (ExpectedObjectType* ExpectedType = Cast<ExpectedObjectType>(Asset.GetAsset(LoadTags)))
				{
					LoadedObjects.Add(ExpectedType);
				}
			}	
		}
		
		return LoadedObjects;
	}
	
	template<typename ExpectedObjectType>
    ExpectedObjectType* LoadFirstValid(const TSet<FName>& LoadTags = {}) const
    {   	
    	for (const FAssetData& Asset : Assets)
    	{
    		if (Asset.IsInstanceOf(ExpectedObjectType::StaticClass()))
    		{
    			if (ExpectedObjectType* ExpectedType = Cast<ExpectedObjectType>(Asset.GetAsset(LoadTags)))
    			{
    				return ExpectedType;
    			}
    		}	
    	}
    	
    	return nullptr;
    }
};

struct FAssetOpenArgs : public FAssetArgs
{
	EAssetOpenMethod OpenMethod;
	TSharedPtr<IToolkitHost> ToolkitHost;

	EToolkitMode::Type GetToolkitMode() const { return ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone; }
};

struct FAssetActivateArgs : public FAssetArgs
{
	EAssetActivationMethod ActivationMethod;
};

struct FAssetSourceFileArgs : public FAssetArgs
{
	FAssetSourceFileArgs(TConstArrayView<FAssetData> InAssets) : FAssetArgs(InAssets) { }
};

struct FAssetSourceFile
{
	FString DisplayLabelName;
	FString RelativeFilename;
};

struct FAssetMergeResults
{
	UPackage* MergedPackage = nullptr;
	EAssetMergeResult Result = EAssetMergeResult::Unknown;
};

DECLARE_DELEGATE_OneParam(FOnAssetMergeResolved, const FAssetMergeResults& Results);

struct FAssetMergeArgs
{
	FAssetData LocalAsset;
	TOptional<FAssetData> BaseAsset;
	TOptional<FAssetData> RemoteAsset;
	
	FOnAssetMergeResolved ResolutionCallback;
};

struct FAssetSupportResponse
{
public:
	static FAssetSupportResponse Supported()
	{
		return FAssetSupportResponse(true, FText::GetEmpty());
	}

	static FAssetSupportResponse NotSupported()
	{
		return FAssetSupportResponse(false, FText::GetEmpty());
	}

	static FAssetSupportResponse Error(const FText& ErrorText)
	{
		return FAssetSupportResponse(false, ErrorText);
	}

	bool IsSupported() const { return bSupported; }
	const FText& GetErrorText() const { return ErrorText; }

private:
	FAssetSupportResponse(bool InSupported, const FText InError)
		: bSupported(InSupported)
		, ErrorText(InError)
	{
	}

private:
	bool bSupported;
	FText ErrorText;
};

/* Revision information for a single revision of a file in source control */
USTRUCT(BlueprintType)
struct FRevisionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Asset Revision")
	FString		Revision;

	UPROPERTY(BlueprintReadWrite, Category="Asset Revision")
	int32		Changelist = -1;

	UPROPERTY(BlueprintReadWrite, Category="Asset Revision")
	FDateTime	Date;

	static inline FRevisionInfo InvalidRevision()
	{
		static const FRevisionInfo Ret = { TEXT(""), -1, FDateTime() };
		return Ret;
	}
};

struct FAssetDiffArgs
{
	UObject* OldAsset = nullptr;
	FRevisionInfo OldRevision;

	UObject* NewAsset = nullptr;
	FRevisionInfo NewRevision;
};

struct FAssetOpenSupportArgs
{
	EAssetOpenMethod OpenMethod = EAssetOpenMethod::Edit;
};

/**
 * The asset category path is how we know how to build menus around assets.  For example, Basic is generally the ones
 * we expose at the top level, where as everything else is a category with a pull out menu, and the subcategory would
 * be where it gets placed in a submenu inside of there.
 */
struct ASSETDEFINITION_API FAssetCategoryPath
{
	FAssetCategoryPath(const FText& InCategory);
	FAssetCategoryPath(FText InCategory, FText InSubCategory);
	FAssetCategoryPath(const FAssetCategoryPath& InCategory, const FText& SubCategory);
	FAssetCategoryPath(TConstArrayView<FText> InCategoryPath);

	FName GetCategory() const { return CategoryPath[0].Key; }
	FText GetCategoryText() const { return CategoryPath[0].Value; }
	
	bool HasSubCategory() const { return CategoryPath.Num() > 1; }
	FName GetSubCategory() const { return HasSubCategory() ? CategoryPath[1].Key : NAME_None; }
	FText GetSubCategoryText() const { return HasSubCategory() ? CategoryPath[1].Value : FText::GetEmpty(); }
	
	FAssetCategoryPath operator / (const FText& SubCategory) const { return FAssetCategoryPath(*this, SubCategory); }
	
private:
	TArray<TPair<FName, FText>> CategoryPath;
};

/**
 * These are just some common asset categories.  You're not at all limited to these, and can register an "Advanced"
 * category with the IAssetTools::RegisterAdvancedAssetCategory.
 */
struct ASSETDEFINITION_API EAssetCategoryPaths
{
	// This category is special, "Basic" assets appear at the very top level and are not placed into any submenu.
	// Arguably the basic category should not exist and should instead be user configurable on what they feel should be
	// top level assets.
	static FAssetCategoryPath Basic;
	
	static FAssetCategoryPath Animation;
	static FAssetCategoryPath Audio;
	static FAssetCategoryPath Blueprint;
	static FAssetCategoryPath Foliage;
	static FAssetCategoryPath Gameplay;
	static FAssetCategoryPath Input;
	static FAssetCategoryPath Material;
	static FAssetCategoryPath Misc;
	static FAssetCategoryPath Physics;
	static FAssetCategoryPath Texture;
	static FAssetCategoryPath UI;
	static FAssetCategoryPath Cinematics;
};

struct FAssetOpenSupport
{
public:
	FAssetOpenSupport(EAssetOpenMethod InOpenMethod, bool bInSupported)
		: OpenMethod(InOpenMethod)
		, IsSupported(bInSupported)
	{
	}
	
	FAssetOpenSupport(EAssetOpenMethod InOpenMethod, bool bInSupported, EToolkitMode::Type InRequiredToolkitMode)
		: OpenMethod(InOpenMethod)
		, IsSupported(bInSupported)
		, RequiredToolkitMode(InRequiredToolkitMode)
	{
	}
	
	EAssetOpenMethod OpenMethod;
	bool IsSupported;
	TOptional<EToolkitMode::Type> RequiredToolkitMode;
};


class UAssetDefinitionRegistry;

enum class EIncludeClassInFilter : uint8
{
	IfClassIsNotAbstract,
	Always
};

/**
 * Asset Definitions represent top level assets that are known to the editor.
 */
UCLASS(Abstract)
class ASSETDEFINITION_API UAssetDefinition : public UObject
{
	GENERATED_BODY()

public:
	UAssetDefinition();

	//Begin UObject
	virtual void PostCDOContruct() override;
	//End UObject
	
public:
	
	/** Returns the name of this type */
	virtual FText GetAssetDisplayName() const PURE_VIRTUAL(UAssetDefinition::GetAssetDisplayName, ensureMsgf(false && "NotImplemented", TEXT("NotImplemented")); return FText(); );

	/**
	 * Returns the name of this type, but allows overriding the default on a specific instance of the asset.  This
	 * is handy for cases like UAssetData which are of course all UAssetData, but a given instance of the asset
	 * is really a specific instance of some UAssetData class, and being able to override that on the instance is handy
	 * for readability at the Content Browser level.
	 */
	virtual FText GetAssetDisplayName(const FAssetData& AssetData) const { return GetAssetDisplayName(); }

	/** Get the supported class of this type. */
	virtual TSoftClassPtr<UObject> GetAssetClass() const PURE_VIRTUAL(UAssetDefinition::GetAssetClass, ensureMsgf(false && "NotImplemented", TEXT("NotImplemented")); return TSoftClassPtr<UClass>(); );

	/** Returns the color associated with this type */
	virtual FLinearColor GetAssetColor() const PURE_VIRTUAL(UAssetDefinition::GetAssetColor, ensureMsgf(false && "NotImplemented", TEXT("NotImplemented")); return FColor::Red; );

	/** Returns additional tooltip information for the specified asset, if it has any. */
	virtual FText GetAssetDescription(const FAssetData& AssetData) const { return FText::GetEmpty(); }

	/** Gets a list of categories this asset is in, these categories are used to help organize */
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const;
	
public:
	// Common Operations
	virtual TArray<FAssetData> PrepareToActivateAssets(const FAssetActivateArgs& ActivateArgs) const
	{
		return TArray<FAssetData>(ActivateArgs.Assets);
	}
	
	/** Get open support for the method.  Includes required information before we call OpenAsset. */
	virtual FAssetOpenSupport GetAssetOpenSupport(const FAssetOpenSupportArgs& OpenSupportArgs) const
	{
		return FAssetOpenSupport(OpenSupportArgs.OpenMethod,OpenSupportArgs.OpenMethod == EAssetOpenMethod::Edit); 
	}
	
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const PURE_VIRTUAL(UAssetDefinition::OpenAsset, ensureMsgf(false && "NotImplemented", TEXT("NotImplemented")); return EAssetCommandResult::Unhandled; );

	virtual EAssetCommandResult ActivateAssets(const FAssetActivateArgs& ActivateArgs) const 
	{
		return EAssetCommandResult::Unhandled;
	}

	
	// Common Queries
	virtual FAssetSupportResponse CanRename(const FAssetData& InAsset) const
	{
		return FAssetSupportResponse::Supported();
	}

	virtual FAssetSupportResponse CanDuplicate(const FAssetData& InAsset) const
	{
		return FAssetSupportResponse::Supported();
	}

	virtual FAssetSupportResponse CanLocalize(const FAssetData& InAsset) const
	{
		return FAssetSupportResponse::Supported();
	}

	
	// Importing
	virtual bool CanImport() const { return false; }

	
	// Merging
	virtual bool CanMerge() const { return false; }
	virtual EAssetCommandResult Merge(const FAssetMergeArgs& MergeArgs) const { return EAssetCommandResult::Unhandled; }
	

	// Filtering
	virtual EAssetCommandResult GetFilters(TArray<FAssetFilterData>& OutFilters) const;

	
	// Extras
	virtual FText GetObjectDisplayNameText(UObject* Object) const
	{
		return FText::FromString(Object->GetName());
	}

	// Source Files
	virtual EAssetCommandResult GetSourceFiles(const FAssetSourceFileArgs& SourceFileArgs, TArray<FAssetSourceFile>& OutSourceAssets) const
	{
		return EAssetCommandResult::Unhandled;
	}

	// Diffing Assets
	virtual EAssetCommandResult PerformAssetDiff(const FAssetDiffArgs& DiffArgs) const
	{
		return EAssetCommandResult::Unhandled;
	}

	// Thumbnails

	/** Returns the thumbnail info for the specified asset, if it has one. This typically requires loading the asset.  */
	virtual UThumbnailInfo* LoadThumbnailInfo(const FAssetData& InAssetData) const
	{
		return nullptr;
	}

	/** Returns thumbnail brush unique for the given asset data.  Returning null falls back to class thumbnail brush. */
	virtual const FSlateBrush* GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
	{
		return nullptr;
	}

	/** Returns icon brush unique for the given asset data.  Returning null falls back to class icon brush. */
	virtual const FSlateBrush* GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const
	{
		return nullptr;
	}
	
	/** Optionally returns a custom widget to overlay on top of this assets' thumbnail */
	virtual TSharedPtr<SWidget> GetThumbnailOverlay(const FAssetData& InAssetData) const
	{
		return TSharedPtr<SWidget>();
	}

	// DEVELOPER NOTE:
	// Originally this class was based on the IAssetTypeActions implementation.  Several of the functions on there
	// were created organically and added without a larger discussion about if such a thing belonged on those classes.
	//
	// For example, IAssetTypeActions::ShouldForceWorldCentric was needed for a single asset, but we didn't instead
	// implement GetAssetOpenSupport, which merges the needs of ShouldForceWorldCentric, and SupportsOpenedMethod.
	// 
	// Another example, is IAssetTypeActions::SetSupported and IAssetTypeActions::IsSupported.  These were concepts
	// that could have lived in a map on the registry and never needed to be stored on the actual IAssetTypeActions.
	//
	// So, please do not add new functions to this class if it can be helped.  The AssetDefinitions are intended to be
	// a basic low level representation of top level assets for the Content Browser and other editor tools to do
	// some basic interaction with them, or learn some basic common details about them.
	//
	// If you must add a new function to this class, some requests,
	// 1. Can it be added as a parameter to an existing Argument struct for an existing function?  If so, please do that.
	// 2. Can it be added as part of the return structure of an existing function?  If so, please do that.
	// 3. If you add a new function, please create a struct for the Args.  We'll be able to upgrade things easier.
	//    Please continue to use EAssetCommandResult and FAssetSupportResponse, for those kinds of commands.

protected:
	virtual bool CanRegisterStatically() const;

protected:
	EIncludeClassInFilter IncludeClassInFilter = EIncludeClassInFilter::IfClassIsNotAbstract;

	friend class UAssetDefinitionRegistry;
};