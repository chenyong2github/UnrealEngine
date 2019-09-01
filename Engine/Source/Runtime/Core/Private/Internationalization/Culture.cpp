// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Internationalization/Culture.h"

#if UE_ENABLE_ICU
#include "Internationalization/ICUCulture.h"
#else
#include "Internationalization/LegacyCulture.h"
#endif

FCultureRef FCulture::Create(TUniquePtr<FCultureImplementation>&& InImplementation)
{
	check(InImplementation.IsValid());
	return MakeShareable(new FCulture(MoveTemp(InImplementation)));
}

FCulture::FCulture(TUniquePtr<FCultureImplementation>&& InImplementation)
	: Implementation(MoveTemp(InImplementation))
	, CachedDisplayName(Implementation->GetDisplayName())
	, CachedEnglishName(Implementation->GetEnglishName())
	, CachedName(Implementation->GetName())
	, CachedNativeName(Implementation->GetNativeName())
	, CachedUnrealLegacyThreeLetterISOLanguageName(Implementation->GetUnrealLegacyThreeLetterISOLanguageName())
	, CachedThreeLetterISOLanguageName(Implementation->GetThreeLetterISOLanguageName())
	, CachedTwoLetterISOLanguageName(Implementation->GetTwoLetterISOLanguageName())
	, CachedNativeLanguage(Implementation->GetNativeLanguage())
	, CachedRegion(Implementation->GetRegion())
	, CachedNativeRegion(Implementation->GetNativeRegion())
	, CachedScript(Implementation->GetScript())
	, CachedVariant(Implementation->GetVariant())
{ 
}

FCulture::~FCulture()
{
	// Explicit destructor due to forward declaration of FCultureImplementation
}

const FString& FCulture::GetDisplayName() const
{
	return CachedDisplayName;
}

const FString& FCulture::GetEnglishName() const
{
	return CachedEnglishName;
}

int FCulture::GetKeyboardLayoutId() const
{
	return Implementation->GetKeyboardLayoutId();
}

int FCulture::GetLCID() const
{
	return Implementation->GetLCID();
}

TArray<FString> FCulture::GetPrioritizedParentCultureNames() const
{
	return GetPrioritizedParentCultureNames(GetTwoLetterISOLanguageName(), GetScript(), GetRegion());
}

TArray<FString> FCulture::GetPrioritizedParentCultureNames(const FString& LanguageCode, const FString& ScriptCode, const FString& RegionCode)
{
	TArray<FString> LocaleTagCombinations;

	if (!ScriptCode.IsEmpty() && !RegionCode.IsEmpty())
	{
		LocaleTagCombinations.Add(CreateCultureName(LanguageCode, ScriptCode, RegionCode));
	}

	if (!RegionCode.IsEmpty())
	{
		LocaleTagCombinations.Add(CreateCultureName(LanguageCode, FString(), RegionCode));
	}

	if (!ScriptCode.IsEmpty())
	{
		LocaleTagCombinations.Add(CreateCultureName(LanguageCode, ScriptCode, FString()));
	}

	LocaleTagCombinations.Add(LanguageCode);

	return LocaleTagCombinations;
}

FString FCulture::CreateCultureName(const FString& LanguageCode, const FString& ScriptCode, const FString& RegionCode)
{
	if (!ScriptCode.IsEmpty() && !RegionCode.IsEmpty())
	{
		FString CultureName;
		CultureName.Reserve(LanguageCode.Len() + ScriptCode.Len() + RegionCode.Len() + 2);
		CultureName += LanguageCode;
		CultureName += TEXT('-');
		CultureName += ScriptCode;
		CultureName += TEXT('-');
		CultureName += RegionCode;
		return CultureName;
	}

	if (!RegionCode.IsEmpty())
	{
		FString CultureName;
		CultureName.Reserve(LanguageCode.Len() + RegionCode.Len() + 1);
		CultureName += LanguageCode;
		CultureName += TEXT('-');
		CultureName += RegionCode;
		return CultureName;
	}

	if (!ScriptCode.IsEmpty())
	{
		FString CultureName;
		CultureName.Reserve(LanguageCode.Len() + ScriptCode.Len() + 1);
		CultureName += LanguageCode;
		CultureName += TEXT('-');
		CultureName += ScriptCode;
		return CultureName;
	}

	return LanguageCode;
}

FString FCulture::GetCanonicalName(const FString& Name)
{
	return FCultureImplementation::GetCanonicalName(Name);
}

const FString& FCulture::GetName() const
{
	return CachedName;
}

const FString& FCulture::GetNativeName() const
{
	return CachedNativeName;
}

const FString& FCulture::GetUnrealLegacyThreeLetterISOLanguageName() const
{
	return CachedUnrealLegacyThreeLetterISOLanguageName;
}

const FString& FCulture::GetThreeLetterISOLanguageName() const
{
	return CachedThreeLetterISOLanguageName;
}

const FString& FCulture::GetTwoLetterISOLanguageName() const
{
	return CachedTwoLetterISOLanguageName;
}

const FString& FCulture::GetNativeLanguage() const
{
	return CachedNativeLanguage;
}

const FString& FCulture::GetRegion() const
{
	return CachedRegion;
}

const FString& FCulture::GetNativeRegion() const
{
	return CachedNativeRegion;
}

const FString& FCulture::GetScript() const
{
	return CachedScript;
}

const FString& FCulture::GetVariant() const
{
	return CachedVariant;
}

const FDecimalNumberFormattingRules& FCulture::GetDecimalNumberFormattingRules() const
{
	return Implementation->GetDecimalNumberFormattingRules();
}

const FDecimalNumberFormattingRules& FCulture::GetPercentFormattingRules() const
{
	return Implementation->GetPercentFormattingRules();
}

const FDecimalNumberFormattingRules& FCulture::GetCurrencyFormattingRules(const FString& InCurrencyCode) const
{
	return Implementation->GetCurrencyFormattingRules(InCurrencyCode);
}

/**
 * Get the correct plural form to use for the given number
 * ICU only supports int32 and double, so we cast larger int values to double to try and keep as much precision as possible
 */

#define DEF_GETPLURALFORM_CAST(T1, T2) ETextPluralForm FCulture::GetPluralForm(T1 Val, const ETextPluralType PluralType) const { return GetPluralForm((T2)Val, PluralType); }
DEF_GETPLURALFORM_CAST(float, double)
DEF_GETPLURALFORM_CAST(int8, int32)
DEF_GETPLURALFORM_CAST(int16, int32)
DEF_GETPLURALFORM_CAST(int64, double)
DEF_GETPLURALFORM_CAST(uint8, int32)
DEF_GETPLURALFORM_CAST(uint16, int32)
DEF_GETPLURALFORM_CAST(uint32, double)
DEF_GETPLURALFORM_CAST(uint64, double)
#undef DEF_GETPLURALFORM_CAST

ETextPluralForm FCulture::GetPluralForm(int32 Val, const ETextPluralType PluralType) const
{
	if (Val < 0)
	{
		// Must be positive
		Val *= -1;
	}
	return Implementation->GetPluralForm(Val, PluralType);
}

ETextPluralForm FCulture::GetPluralForm(double Val, const ETextPluralType PluralType) const
{
	if (FMath::IsNegativeDouble(Val))
	{
		// Must be positive
		Val *= -1.0;
	}
	return Implementation->GetPluralForm(Val, PluralType);
}

const TArray<ETextPluralForm>& FCulture::GetValidPluralForms(const ETextPluralType PluralType) const
{
	return Implementation->GetValidPluralForms(PluralType);
}

void FCulture::HandleCultureChanged()
{
	// Re-cache the DisplayName, as this may change when the active culture is changed
	CachedDisplayName = Implementation->GetDisplayName();
}
