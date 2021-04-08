// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyHasNameFilter.h"

EFilterResult::Type UPropertyHasNameFilter::IsPropertyValid(const FIsPropertyValidParams& Params) const
{
	class FNameMatcher
	{
	public:

		FNameMatcher(ENameMatchingRule::Type MatchingRule, const TFieldPath<FProperty>& Property)
            :
			MatchingRule(MatchingRule),
			PropertyName(Property->GetName())
		{}

		bool MatchesName(const FString& AllowedName) const
		{
			switch(MatchingRule)
			{
				case ENameMatchingRule::MatchesExactly:
					return MatchesExactly(AllowedName);
				case ENameMatchingRule::MatchesIgnoreCase:
					return MatchesIgnoreCase(AllowedName);
				case ENameMatchingRule::ContainsExactly:
					return ContainsExactly(AllowedName);
				case ENameMatchingRule::ContainsIgnoreCase:
					return ContainsIgnoreCase(AllowedName);
				default:
					ensure(false);
					return false;
			}
		}
	
	private:
		
		ENameMatchingRule::Type MatchingRule;
		const FString PropertyName;

		bool MatchesExactly(const FString& AllowedName) const
		{
			return PropertyName.Equals(AllowedName, ESearchCase::CaseSensitive);
		}

		bool MatchesIgnoreCase(const FString& AllowedName) const
		{
			return PropertyName.Equals(AllowedName, ESearchCase::IgnoreCase);
		}

		bool ContainsExactly(const FString& AllowedName) const
		{
			return PropertyName.Find(AllowedName, ESearchCase::CaseSensitive) != INDEX_NONE;
		}

		bool ContainsIgnoreCase(const FString& AllowedName) const
		{
			return PropertyName.Find(AllowedName, ESearchCase::IgnoreCase) != INDEX_NONE;
		}
	};

	const FNameMatcher NameMatcher(NameMatchingRule, Params.Property);
	for (const FString& AllowedPropertyName : AllowedNames)
	{
		if (NameMatcher.MatchesName(AllowedPropertyName))
		{
			return EFilterResult::Include;
		}
	}
	return EFilterResult::Exclude;
}
