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

class GAMEPLAYTAGS_API FNativeGameplayTagSource
{
public:
	FNativeGameplayTagSource() { }
	virtual ~FNativeGameplayTagSource() { }

	virtual void Register() const = 0;
	virtual void Unregister() const = 0;

protected:
	FGameplayTag Add(FName TagName, const FString& TagDevComment = TEXT("(Native)"));

private:
	TArray<FGameplayTagTableRow> NativeTags;

	friend class UGameplayTagsManager;
};

enum class ENativeGameplayTagToken { PRIVATE_USE_MACRO_INSTEAD };

#define UE_DECLARE_GAMEPLAY_TAG_EXTERN(TagName) extern FNativeGameplayTag TagName;
#define UE_DEFINE_GAMEPLAY_TAG(TagName, Tag) FNativeGameplayTag TagName(UE_PLUGIN_NAME, UE_MODULE_NAME, Tag, TEXT(""), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
#define UE_DEFINE_GAMEPLAY_TAG_STATIC(TagName, Tag) static FNativeGameplayTag TagName(UE_PLUGIN_NAME, UE_MODULE_NAME, Tag, TEXT(""), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);

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
