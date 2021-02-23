// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

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
