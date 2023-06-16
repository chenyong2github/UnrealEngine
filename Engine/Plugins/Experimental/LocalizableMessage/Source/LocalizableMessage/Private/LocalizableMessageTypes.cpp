// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalizableMessageTypes.h"

#include "ILocalizableMessageModule.h"
#include "LocalizableMessageBaseParameters.h"
#include "LocalizableMessageProcessor.h"
#include "LocalizationContext.h"

namespace LocalizableMessageTypes
{
	FLocalizableMessageProcessor::FScopedRegistrations RegisteredLocalizationTypes;

	FText Int_LocalizeValue(const FLocalizableMessageParameterInt& Localizable, const FLocalizationContext& LocalizationContext)
	{
		return FText::AsNumber(Localizable.Value, nullptr, LocalizationContext.GetLocaleOverride());
	}
	FText Float_LocalizeValue(const FLocalizableMessageParameterFloat& Localizable, const FLocalizationContext& LocalizationContext)
	{
		return FText::AsNumber(Localizable.Value, nullptr, LocalizationContext.GetLocaleOverride());
	}
	FText String_LocalizeValue(const FLocalizableMessageParameterString& Localizable, const FLocalizationContext& LocalizationContext)
	{
		return FText::AsCultureInvariant(Localizable.Value);
	}

	void RegisterTypes()
	{
		ILocalizableMessageModule& LocalizableMessageModule = ILocalizableMessageModule::Get();
		FLocalizableMessageProcessor& Processor = LocalizableMessageModule.GetLocalizableMessageProcessor();

		Processor.RegisterLocalizableType<FLocalizableMessageParameterInt>(&Int_LocalizeValue,RegisteredLocalizationTypes);
		Processor.RegisterLocalizableType<FLocalizableMessageParameterFloat>(&Float_LocalizeValue,RegisteredLocalizationTypes);
		Processor.RegisterLocalizableType<FLocalizableMessageParameterString>(&String_LocalizeValue, RegisteredLocalizationTypes);
	}

	void UnregisterTypes()
	{
		ILocalizableMessageModule& LocalizableMessageModule = ILocalizableMessageModule::Get();
		FLocalizableMessageProcessor& Processor = LocalizableMessageModule.GetLocalizableMessageProcessor();

		Processor.UnregisterLocalizableTypes(RegisteredLocalizationTypes);
	}
}