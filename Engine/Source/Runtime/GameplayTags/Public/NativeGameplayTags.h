// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

#define DECLARE_NATIVE_GAMEPLAY_TAG_SOURCE(ClassType)						\
public:																		\
	static const TSharedRef<const ClassType>& Get()							\
	{																		\
		static TSharedRef<const ClassType> Tags = MakeShared<ClassType>();	\
		return Tags;														\
	}																		\
																			\
	virtual void Register()	const override									\
	{																		\
		UGameplayTagsManager& Manager = UGameplayTagsManager::Get();		\
		Manager.AddNativeGameplayTagSource(#ClassType, Get());				\
	}																		\
																			\
	virtual void Unregister() const override								\
	{																		\
		UGameplayTagsManager& Manager = UGameplayTagsManager::Get();		\
		Manager.RemoveNativeGameplayTagSource(#ClassType);					\
	}

/**
 * Useful for making a dedicated groups of native tags that are registered
 * and unregistered as a block.  You define it as follows,
 * 
 *	struct FMyTagsForThings : public FNativeGameplayTagSource
 *	{
 *		DECLARE_NATIVE_GAMEPLAY_TAG_SOURCE(FMyTagsForThings)
 *
 *		FGameplayTag TagForThing1 = Add(TEXT("Thing.One"));
 * 		FGameplayTag TagForThing2 = Add(TEXT("Thing.Two"));
 *		// Add more tags here...
 *	};
 * 
 * During your module startup you can register them together in your startup by
 * calling FMyTagsForThings::Get()->Register(), and Unregister on module shutdown.
 */
class GAMEPLAYTAGS_API FNativeGameplayTagSource
{
public:
	FNativeGameplayTagSource() { }
	virtual ~FNativeGameplayTagSource() { }

	virtual void Register() const = 0;
	virtual void Unregister() const = 0;

protected:
	/**
	 * Call this during the struct member initialization to create the tags.
	 */
	FGameplayTag Add(FName TagName, const FString& TagDevComment = TEXT("(Native)"));

private:
	TArray<FGameplayTagTableRow> NativeTags;

	friend class UGameplayTagsManager;
};

enum class ENativeGameplayTagToken { PRIVATE_USE_MACRO_INSTEAD };

/**
 * Declares a native gameplay tag that is defined in a cpp with UE_DEFINE_GAMEPLAY_TAG to allow other modules or code to use the created tag variable.
 */
#define UE_DECLARE_GAMEPLAY_TAG_EXTERN(TagName) extern FNativeGameplayTag TagName;

/**
 * Defines a native gameplay tag that is externally declared in a header to allow other modules or code to use the created tag variable.
 */
#define UE_DEFINE_GAMEPLAY_TAG(TagName, Tag) FNativeGameplayTag TagName(UE_PLUGIN_NAME, UE_MODULE_NAME, Tag, TEXT(""), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);

/**
 * Defines a native gameplay tag such that it's only available to the cpp file you define it in.
 */
#define UE_DEFINE_GAMEPLAY_TAG_STATIC(TagName, Tag) static FNativeGameplayTag TagName(UE_PLUGIN_NAME, UE_MODULE_NAME, Tag, TEXT(""), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);

/**
 * Holds a gameplay tag that was registered during static construction of the module, and will
 * be unregistered when the module unloads.  Each registration is based on the native tag pointer
 * so even if two modules register the same tag and one is unloaded, the tag will still be registered
 * by the other one.
 */
class GAMEPLAYTAGS_API FNativeGameplayTag : public FNoncopyable
{
public:
	FNativeGameplayTag(FName PluginName, FName ModuleName, FName TagName, const FString& TagDevComment, ENativeGameplayTagToken);
	~FNativeGameplayTag();

	operator const FGameplayTag& () const { return InternalTag; }
	operator FGameplayTag() const { return InternalTag; }

private:
	FGameplayTag InternalTag;

#if WITH_EDITORONLY_DATA
	FString DeveloperComment;
#endif

	static TSet<const class FNativeGameplayTag*> RegisteredNativeTags;

	friend class UGameplayTagsManager;
};
