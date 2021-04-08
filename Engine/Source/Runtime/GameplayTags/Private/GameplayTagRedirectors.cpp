// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTagRedirectors.h"
#include "GameplayTagsSettings.h"
#include "Misc/ConfigCacheIni.h"

FGameplayTagRedirectors& FGameplayTagRedirectors::Get()
{
	static FGameplayTagRedirectors Singleton;
	return Singleton;
}

FGameplayTagRedirectors::FGameplayTagRedirectors()
{
	UGameplayTagsSettings* MutableDefault = GetMutableDefault<UGameplayTagsSettings>();

	// Check the deprecated location
	bool bFoundDeprecated = false;
	FConfigSection* PackageRedirects = GConfig->GetSectionPrivate(TEXT("/Script/Engine.Engine"), false, true, GEngineIni);

	if (PackageRedirects)
	{
		for (FConfigSection::TIterator It(*PackageRedirects); It; ++It)
		{
			if (It.Key() == TEXT("+GameplayTagRedirects"))
			{
				FName OldTagName = NAME_None;
				FName NewTagName;

				if (FParse::Value(*It.Value().GetValue(), TEXT("OldTagName="), OldTagName))
				{
					if (FParse::Value(*It.Value().GetValue(), TEXT("NewTagName="), NewTagName))
					{
						FGameplayTagRedirect Redirect;
						Redirect.OldTagName = OldTagName;
						Redirect.NewTagName = NewTagName;

						MutableDefault->GameplayTagRedirects.AddUnique(Redirect);

						bFoundDeprecated = true;
					}
				}
			}
		}
	}

	if (bFoundDeprecated)
	{
		UE_LOG(LogGameplayTags, Error, TEXT("GameplayTagRedirects is in a deprecated location, after editing GameplayTags developer settings you must remove these manually"));
	}

	// Check settings object
	for (const FGameplayTagRedirect& Redirect : MutableDefault->GameplayTagRedirects)
	{
		FName OldTagName = Redirect.OldTagName;
		FName NewTagName = Redirect.NewTagName;

		if (ensureMsgf(!TagRedirects.Contains(OldTagName), TEXT("Old tag %s is being redirected to more than one tag. Please remove all the redirections except for one."), *OldTagName.ToString()))
		{
			//FGameplayTag OldTag = RequestGameplayTag(OldTagName, false); //< This only succeeds if OldTag is in the Table!
			//if (OldTag.IsValid())
			//{
			//	FGameplayTagContainer MatchingChildren = RequestGameplayTagChildren(OldTag);

			//	FString Msg = FString::Printf(TEXT("Old tag (%s) which is being redirected still exists in the table!  Generally you should "
			//		TEXT("remove the old tags from the table when you are redirecting to new tags, or else users will ")
			//		TEXT("still be able to add the old tags to containers.")), *OldTagName.ToString());

			//	if (MatchingChildren.Num() == 0)
			//	{
			//		UE_LOG(LogGameplayTags, Warning, TEXT("%s"), *Msg);
			//	}
			//	else
			//	{
			//		Msg += TEXT("\nSuppressed warning due to redirected tag being a single component that matched other hierarchy elements.");
			//		UE_LOG(LogGameplayTags, Log, TEXT("%s"), *Msg);
			//	}
			//}

			//FGameplayTag NewTag = (NewTagName != NAME_None) ? RequestGameplayTag(NewTagName, false) : FGameplayTag();

			// Attempt to find multiple redirect hops and flatten the redirection so we only need to redirect once
			// to resolve the update.  Includes a basic infinite recursion guard, in case the redirects loop.
			int32 IterationsLeft = 10;
			while (NewTagName != NAME_None)
			{
				bool bFoundRedirect = false;

				// See if it got redirected again
				for (const FGameplayTagRedirect& SecondRedirect : MutableDefault->GameplayTagRedirects)
				{
					if (SecondRedirect.OldTagName == NewTagName)
					{
						NewTagName = SecondRedirect.NewTagName;
						bFoundRedirect = true;
						break;
					}
				}
				IterationsLeft--;

				if (!bFoundRedirect)
				{
					break;
				}

				if (IterationsLeft <= 0)
				{
					UE_LOG(LogGameplayTags, Warning, TEXT("Invalid new tag %s!  Cannot replace old tag %s."), *Redirect.NewTagName.ToString(), *Redirect.OldTagName.ToString());
					break;
				}
			}

			// Populate the map
			TagRedirects.Add(OldTagName, FGameplayTag(NewTagName));
		}
	}
}

const FGameplayTag* FGameplayTagRedirectors::RedirectTag(const FName& InTagName) const
{
	if (const FGameplayTag* NewTag = TagRedirects.Find(InTagName))
	{
		return NewTag;
	}

	return nullptr;
}