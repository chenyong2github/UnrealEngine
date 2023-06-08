// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalizableMessageLibrary.h"
#include "LocalizationContext.h"
#include "LocalizableMessageProcessor.h"
#include "ILocalizableMessageModule.h"
#include "Internationalization/Internationalization.h"

FText ULocalizableMessageLibrary::Conv_LocalizableMessageToText(const UObject* WorldContextObject, const int32& Message)
{
	// This will never be called, the exec version below will be hit instead
	check(0);
	return FText::GetEmpty();
}

DEFINE_FUNCTION(ULocalizableMessageLibrary::execConv_LocalizableMessageToText)
{
	P_GET_OBJECT(UObject, WorldContextObject)
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	void* MessagePtr = Stack.MostRecentPropertyAddress;
	FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

	P_FINISH;

	FText Result = FText::GetEmpty();
	if (StructProp && MessagePtr)
	{
		P_NATIVE_BEGIN
		FLocalizationContext LocContext(WorldContextObject, FInternationalization::Get().GetCurrentCulture());
		FLocalizableMessageProcessor& Processor = ILocalizableMessageModule::Get().GetLocalizableMessageProcessor();
		Result = Processor.Localize(*reinterpret_cast<FLocalizableMessage*>(MessagePtr), LocContext);
		P_NATIVE_END
	}
	else
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AccessViolation,
			NSLOCTEXT("LocalizableMessageLibrary", "MissingMessageProperty", "Failed to resolve the input parameter for LocalizableMessageToText.")
		);
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}

	*(FText*)RESULT_PARAM = Result;
}
