// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "UObject/CoreOnlineFwd.h"


struct FAvatarInfo : public TSharedFromThis<FAvatarInfo>
{
public:
	FAvatarInfo(const FUniqueNetIdPtr& InUserId)
		: UserId(InUserId.ToSharedRef())
	{ }

	FAvatarInfo(const FUniqueNetIdPtr InUserId, TMap<FString, FString>&& InAvatarInfoPairs)
		: UserId(InUserId.ToSharedRef()), AvatarInfoPairs(MoveTemp(InAvatarInfoPairs))
	{ }

	FAvatarInfo(const FAvatarInfo& AvatarInfo)
		: UserId(AvatarInfo.UserId), AvatarInfoPairs(AvatarInfo.AvatarInfoPairs)
	{ }

	bool Equals(const FAvatarInfo& Other) const
	{
		if (UserId.Get() != Other.UserId.Get())
		{
			return false;
		}
		if (AvatarInfoPairs.Num() != Other.AvatarInfoPairs.Num())
		{
			return false;
		}

		for (const TPair<FString, FString>& Pair : AvatarInfoPairs)
		{
			if (const FString* OtherValue = Other.AvatarInfoPairs.Find(Pair.Key))
			{
				if (!Pair.Value.Equals(*OtherValue))
				{
					return false;
				}
			}
			else
			{
				return false;
			}
			
		}

		return true;
	}

	FUniqueNetIdRef UserId;
	TMap<FString, FString> AvatarInfoPairs;
};

/**
 * Interface for a class that can provide support for querying information about an avatar associated with a user by UniqueNetId
 */
class IAvatarProvider : public IModularFeature
{
public:
	DECLARE_DELEGATE_OneParam(FOnQueryAvatarInfoComplete, const FString& /*ErrorStr*/)

	virtual ~IAvatarProvider() = default;

	/**
	 * Get the name of the modular feature, to be used to get the implementations
	 * @return name of the modular feature
	 */
	static FName GetModularFeatureName()
	{
		static FName FeatureName = FName(TEXT("AvatarProvider"));
		return FeatureName;
	}

	virtual void QueryAvatarInfo(const FUniqueNetId& LocalUserId, const TArray<FUniqueNetIdRef>& InUserIds, const FOnQueryAvatarInfoComplete& CompletionDelegate) = 0;

	virtual TSharedPtr<const FAvatarInfo> GetAvatarInfo(const FUniqueNetId& InUserId) const = 0;
};
