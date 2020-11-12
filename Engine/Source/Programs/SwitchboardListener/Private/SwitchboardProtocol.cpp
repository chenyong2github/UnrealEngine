// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardProtocol.h"

#include "SwitchboardPacket.h"
#include "SwitchboardTasks.h"
#include "SyncStatus.h"

#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"


FString CreateMessage(const FString& InStateDescription, bool bInState, const TMap<FString, FString>& InAdditionalFields)
{
	FString Message;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Message);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue(InStateDescription, bInState); // TODO: Phase out this field and replace with two below because parser now needs to check all possibilities.
	JsonWriter->WriteValue(TEXT("command"), InStateDescription);
	JsonWriter->WriteValue(TEXT("bAck"), bInState);
	for (const auto& Value : InAdditionalFields)
	{
		JsonWriter->WriteValue(Value.Key, Value.Value);
	}
	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();
	return Message;
}

FString CreateTaskDeclinedMessage(const FSwitchboardTask& InTask, const FString& InErrorMessage, const TMap<FString, FString>& InAdditionalFields)
{
	TMap<FString, FString> AdditionalFields = InAdditionalFields;

	AdditionalFields.Add(TEXT("id"), InTask.TaskID.ToString());
	AdditionalFields.Add(TEXT("error"), InErrorMessage);

	return CreateMessage(InTask.Name, false, AdditionalFields);
}

FString CreateCommandAcceptedMessage(const FGuid& InMessageID)
{
	return CreateMessage(TEXT("command accepted"), true, { { TEXT("id"), InMessageID.ToString() } });
}

FString CreateCommandDeclinedMessage(const FGuid& InMessageID, const FString& InErrorMessage)
{
	return CreateMessage(TEXT("command accepted"), false, { { TEXT("id"), InMessageID.ToString() }, {TEXT("error"), InErrorMessage} });
}

FString CreateSyncStatusMessage(const FSyncStatus& SyncStatus)
{
	FString SyncStatusJsonString;
	const bool bJsonStringOk = FJsonObjectConverter::UStructToJsonObjectString(SyncStatus, SyncStatusJsonString);

	check(bJsonStringOk);

	FString Message;

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Message);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue(TEXT("get sync status"), true); // TODO: Phase out this field and replace with two below because parser now needs to check all possibilities.
	JsonWriter->WriteValue(TEXT("command"), TEXT("get sync status"));
	JsonWriter->WriteValue(TEXT("bAck"), true);
	JsonWriter->WriteRawJSONValue(TEXT("syncStatus"), SyncStatusJsonString);
	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();

	return Message;
}


FString CreateReceiveFileFromClientCompletedMessage(const FString& InDestinationPath)
{
	return CreateMessage(TEXT("send file complete"), true, { { TEXT("destination"), InDestinationPath } });
}

FString CreateReceiveFileFromClientFailedMessage(const FString& InDestinationPath, const FString& InError)
{
	return CreateMessage(TEXT("send file complete"), false, { { TEXT("destination"), InDestinationPath }, { TEXT("error"), InError } });
}

FString CreateSendFileToClientCompletedMessage(const FString& InSourcePath, const FString& InFileContent)
{
	return CreateMessage(TEXT("receive file complete"), true, { { TEXT("source"), InSourcePath }, { TEXT("content"), InFileContent } });
}

FString CreateSendFileToClientFailedMessage(const FString& InSourcePath, const FString& InError)
{
	return CreateMessage(TEXT("receive file complete"), false, { { TEXT("source"), InSourcePath }, { TEXT("error"), InError } });
}

bool CreateTaskFromCommand(const FString& InCommand, const FIPv4Endpoint& InEndpoint, TUniquePtr<FSwitchboardTask>& OutTask, bool& bOutEcho)
{
	TSharedRef<TJsonReader<TCHAR>> Reader = FJsonStringReader::Create(InCommand);

	TSharedPtr<FJsonObject> JsonData;

	if (!FJsonSerializer::Deserialize(Reader, JsonData))
	{
		return false;
	}

	TSharedPtr<FJsonValue> CommandField = JsonData->TryGetField(TEXT("command"));
	TSharedPtr<FJsonValue> IdField = JsonData->TryGetField(TEXT("id"));

	if (!CommandField.IsValid() || !IdField.IsValid())
	{
		return false;
	}

	FGuid MessageID;

	if (!FGuid::Parse(IdField->AsString(), MessageID))
	{
		return false;
	}

	// Should we echo this command in the output log?
	{
		TSharedPtr<FJsonValue> EchoField = JsonData->TryGetField(TEXT("bEcho"));
		bOutEcho = EchoField.IsValid() ? EchoField->AsBool() : true;
	}

	const FString CommandName = CommandField->AsString().ToLower();
	if (CommandName == TEXT("start"))
	{
		TSharedPtr<FJsonValue> ExeField = JsonData->TryGetField(TEXT("exe"));
		TSharedPtr<FJsonValue> ArgsField = JsonData->TryGetField(TEXT("args"));
		TSharedPtr<FJsonValue> NameField = JsonData->TryGetField(TEXT("name"));
		TSharedPtr<FJsonValue> CallerField = JsonData->TryGetField(TEXT("caller"));
		TSharedPtr<FJsonValue> WorkingDirField = JsonData->TryGetField(TEXT("working_dir"));
		TSharedPtr<FJsonValue> UpdateClientsWithStdoutField = JsonData->TryGetField(TEXT("bUpdateClientsWithStdout"));
		TSharedPtr<FJsonValue> ForceWindowFocusField = JsonData->TryGetField(TEXT("bForceWindowFocus"));

		OutTask = MakeUnique<FSwitchboardStartTask>(
			MessageID,
			InEndpoint,
			ExeField->AsString(),
			ArgsField->AsString(),
			NameField->AsString(),
			CallerField->AsString(),
			WorkingDirField->AsString(),
			UpdateClientsWithStdoutField->AsBool(),
			ForceWindowFocusField->AsBool()
		);

		return true;
	}
	else if (CommandName == TEXT("kill"))
	{
		TSharedPtr<FJsonValue> UUIDField = JsonData->TryGetField(TEXT("uuid"));

		FGuid ProgramID;
		if (FGuid::Parse(UUIDField->AsString(), ProgramID))
		{
			OutTask = MakeUnique<FSwitchboardKillTask>(MessageID, InEndpoint, ProgramID);
			return true;
		}
	}
	else if (CommandName == TEXT("send file"))
	{
		TSharedPtr<FJsonValue> DestinationField = JsonData->TryGetField(TEXT("destination"));
		TSharedPtr<FJsonValue> FileContentField = JsonData->TryGetField(TEXT("content"));
		
		if (DestinationField.IsValid() && FileContentField.IsValid())
		{
			OutTask = MakeUnique<FSwitchboardReceiveFileFromClientTask>(MessageID, InEndpoint, DestinationField->AsString(), FileContentField->AsString());
			return true;
		}
	}
	else if (CommandName == TEXT("receive file"))
	{
		TSharedPtr<FJsonValue> SourceField = JsonData->TryGetField(TEXT("source"));

		if (SourceField.IsValid())
		{
			OutTask = MakeUnique<FSwitchboardSendFileToClientTask>(MessageID, InEndpoint, SourceField->AsString());
			return true;
		}
	}
	else if (CommandName == TEXT("disconnect"))
	{
		OutTask = MakeUnique<FSwitchboardDisconnectTask>(MessageID, InEndpoint);
		return true;
	}
	else if (CommandName == TEXT("keep alive"))
	{
		OutTask = MakeUnique<FSwitchboardKeepAliveTask>(MessageID, InEndpoint);
		return true;
	}
	else if (CommandName == TEXT("get sync status"))
	{
		TSharedPtr<FJsonValue> UUIDField = JsonData->TryGetField(TEXT("uuid"));

		FGuid ProgramID;

		if (FGuid::Parse(UUIDField->AsString(), ProgramID))
		{
			OutTask = MakeUnique<FSwitchboardGetSyncStatusTask>(MessageID, InEndpoint, ProgramID);
			return true;
		}
	}
	else if (CommandName == TEXT("forcefocus"))
	{
		TSharedPtr<FJsonValue> PidField = JsonData->TryGetField(TEXT("pid"));

		uint32 Pid;

		if (PidField.IsValid() && PidField->TryGetNumber(Pid))
		{
			OutTask = MakeUnique<FSwitchboardForceFocusTask>(MessageID, InEndpoint, Pid);
			return true;
		}
	}
	else if (CommandName == TEXT("fixExeFlags"))
	{
		TSharedPtr<FJsonValue> UUIDField = JsonData->TryGetField(TEXT("uuid"));

		FGuid ProgramID;

		if (FGuid::Parse(UUIDField->AsString(), ProgramID))
		{
			OutTask = MakeUnique<FSwitchboardFixExeFlagsTask>(MessageID, InEndpoint, ProgramID);
			return true;
		}
	}

	return false;
}
