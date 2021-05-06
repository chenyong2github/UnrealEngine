// Copyright Epic Games, Inc. All Rights Reserved.

#include "UncontrolledChangelist.h"

#include "Dom/JsonObject.h"
#include "ISourceControlModule.h"
#include "Misc/Guid.h"

#define LOCTEXT_NAMESPACE "UncontrolledChangelists"

const FGuid FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_GUID = FGuid(0, 0, 0, 0);
const FText FUncontrolledChangelist::DEFAULT_UNCONTROLLED_CHANGELIST_NAME = LOCTEXT("DefaultUncontrolledChangelist", "Default Uncontrolled Changelist");

FUncontrolledChangelist::FUncontrolledChangelist()
	: Guid(FGuid::NewGuid())
	, Name()
{
}

FUncontrolledChangelist::FUncontrolledChangelist(const FGuid& InGuid, const FString& InName)
	: Guid(InGuid)
	, Name(InName)
{
}

void FUncontrolledChangelist::Serialize(TSharedRef<FJsonObject> OutJsonObject) const
{
	OutJsonObject->SetStringField(GUID_NAME, Guid.ToString());
	OutJsonObject->SetStringField(NAME_NAME, Name);
}

bool FUncontrolledChangelist::Deserialize(const TSharedRef<FJsonObject> InJsonValue)
{
	FString TempStr;

	if (!InJsonValue->TryGetStringField(GUID_NAME, TempStr))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Cannot get field %s."), GUID_NAME);
		return false;
	}

	if (!FGuid::Parse(TempStr, Guid))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Cannot parse Guid %s."), *TempStr);
		return false;
	}

	if (!InJsonValue->TryGetStringField(NAME_NAME, Name))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Cannot get field %s."), NAME_NAME);
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
