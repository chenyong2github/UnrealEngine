// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalizableMessageProcessor.h"

#include "Internationalization/Text.h"
#include "LocalizableMessage.h"
#include "LocalizationContext.h"

FLocalizableMessageProcessor::FLocalizableMessageProcessor()
{

}

FLocalizableMessageProcessor::~FLocalizableMessageProcessor()
{
	ensure(LocalizeValueMapping.IsEmpty());
}

FText FLocalizableMessageProcessor::Localize(const FLocalizableMessage& Message, const FLocalizationContext& Context)
{
	// todo: look up the localized string via Key
	// for now we only use the DefaultText in the message
	FText SubstitutionResult;
	FFormatNamedArguments FormatArguments;

	for (const FLocalizableMessageParameterEntry& Substitution : Message.Substitutions)
	{
		if (ensure(Substitution.Value.IsValid()))
		{
			if (const LocalizeValueFnc* Functor = LocalizeValueMapping.Find(Substitution.Value.GetScriptStruct()->GetFName()))
			{
				SubstitutionResult = (*Functor)(Substitution.Value, Context);

				if (SubstitutionResult.IsEmpty() == false)
				{
					TextFormatUtil::FormatNamed(FormatArguments, Substitution.Key, SubstitutionResult);
				}
			}
			else
			{
				ensureMsgf(false, TEXT("Localization type %s not registered in Localization Processor."), *Substitution.Value.GetScriptStruct()->GetFName().ToString());
			}
		}
		else
		{
			ensureMsgf(false, TEXT("Message contained null substitution."));
		}
	}

	// an unfortunate number of allocations and copies here
	return FText::Format(FTextFormat::FromString(Message.DefaultText), FormatArguments);
}

void FLocalizableMessageProcessor::UnregisterLocalizableTypes(FScopedRegistrations& ScopedRegistrations)
{
	for (const FName& Registration : ScopedRegistrations.Registrations)
	{
		LocalizeValueMapping.Remove(Registration);
	}

	ScopedRegistrations.Registrations.Reset();
}
