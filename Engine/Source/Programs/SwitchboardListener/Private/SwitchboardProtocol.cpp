// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardProtocol.h"
#include "SwitchboardTasks.h"

#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"


FString CreateMessage(const FString& InStateDescription, bool bInState, const TMap<FString, FString>& InAdditionalFields)
{
	FString Message;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Message);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue(InStateDescription, bInState);
	for (const auto& Value : InAdditionalFields)
	{
		JsonWriter->WriteValue(Value.Key, Value.Value);
	}
	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();
	return Message;
}

FString CreateCommandAcceptedMessage(const FGuid& InMessageID)
{
	return CreateMessage(TEXT("command accepted"), true, { { TEXT("id"), InMessageID.ToString() } });
}
FString CreateCommandDeclinedMessage(const FGuid& InMessageID, const FString& InErrorMessage)
{
	return CreateMessage(TEXT("command accepted"), false, { { TEXT("id"), InMessageID.ToString() }, {TEXT("error"), InErrorMessage} });
}

FString CreateProgramStartedMessage(const FString& InProgramID, const FString& InMessageID)
{
	return CreateMessage(TEXT("program started"), true, { { TEXT("program id"), InProgramID }, { TEXT("message id"), InMessageID } });
}
FString CreateProgramStartFailedMessage(const FString& InErrorMessage, const FString& InMessageID)
{
	return CreateMessage(TEXT("program started"), false, { { TEXT("error"), InErrorMessage }, { TEXT("message id"), InMessageID } });
}

FString CreateProgramKilledMessage(const FString& InProgramID)
{
	return CreateMessage(TEXT("program killed"), true, { { TEXT("program id"), InProgramID } });
}

FString CreateProgramKillFailedMessage(const FString& InProgramID, const FString& InErrorMessage)
{
	return CreateMessage(TEXT("program killed"), false, { { TEXT("program id"), InProgramID }, { TEXT("error"), InErrorMessage } });
}

FString CreateProgramEndedMessage(const FString& InProgramID, int InReturnCode, const FString& InProgramOutput)
{
	FString Message;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Message);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue(TEXT("program ended"), true);
	JsonWriter->WriteValue(TEXT("program id"), InProgramID);
	JsonWriter->WriteValue(TEXT("returncode"), InReturnCode);
	JsonWriter->WriteValue(TEXT("output"), InProgramOutput);
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


bool CreateTaskFromCommand(const FString& InCommand, const FIPv4Endpoint& InEndpoint, TUniquePtr<FSwitchboardTask>& OutTask)
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

	const FString CommandName = CommandField->AsString().ToLower();
	if (CommandName == TEXT("start"))
	{
		TSharedPtr<FJsonValue> ExeField = JsonData->TryGetField(TEXT("exe"));
		TSharedPtr<FJsonValue> ArgsField = JsonData->TryGetField(TEXT("args"));

		OutTask = MakeUnique<FSwitchboardStartTask>(MessageID, InEndpoint, ExeField->AsString(), ArgsField->AsString());
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
	else if (CommandName == TEXT("killall"))
	{
		OutTask = MakeUnique<FSwitchboardKillAllTask>(MessageID, InEndpoint);
		return true;
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

	return false;
}
