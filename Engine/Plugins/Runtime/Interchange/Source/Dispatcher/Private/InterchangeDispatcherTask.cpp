// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeDispatcherTask.h"

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace UE
{
	namespace Interchange
	{
		FString FJsonLoadSourceCmd::ToJson() const
		{
			//Code should not do a ToJson if the data was not set before
			ensure(bIsDataInitialize);

			TSharedPtr<FJsonObject> CmdObject = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> ActionDataObject = MakeShared<FJsonObject>();
			//CmdObject
			CmdObject->SetStringField(GetCommandIDKey(), GetAction());
			CmdObject->SetStringField(GetTranslatorIDKey(), GetTranslatorID());
			ActionDataObject->SetStringField(GetSourceFilenameKey(), GetSourceFilename());
			CmdObject->SetObjectField(GetCommandDataKey(), ActionDataObject);

			FString LoadSourceCmd;
			TSharedRef< TJsonWriter< TCHAR, TPrettyJsonPrintPolicy<TCHAR> > > JsonWriter = TJsonWriterFactory< TCHAR, TPrettyJsonPrintPolicy<TCHAR> >::Create(&LoadSourceCmd);
			if (!FJsonSerializer::Serialize(CmdObject.ToSharedRef(), JsonWriter))
			{
				//Error creating the json cmd string 
				return FString();
			}
			return LoadSourceCmd;
		}

		bool FJsonLoadSourceCmd::FromJson(const FString& JsonString)
		{
			TSharedRef<TJsonReader<TCHAR>> Reader = FJsonStringReader::Create(JsonString);

			TSharedPtr<FJsonObject> CmdObject;
			if (!FJsonSerializer::Deserialize(Reader, CmdObject) || !CmdObject.IsValid())
			{
				//Cannot read the json file
				return false;
			}
			FString JsonActionValue;
			if (!CmdObject->TryGetStringField(GetCommandIDKey(), JsonActionValue))
			{
				//The json cmd id key is missing
				return false;
			}

			if (JsonActionValue != GetAction())
			{
				//This json do not represent a load command
				return false;
			}

			//Read the json
			if (!CmdObject->TryGetStringField(GetTranslatorIDKey(), TranslatorID))
			{
				//Missing Load command translator ID
				return false;
			}
			const TSharedPtr<FJsonObject>* ActionDataObject = nullptr;
			if (!CmdObject->TryGetObjectField(GetCommandDataKey(), ActionDataObject))
			{
				//Missing Load Action data object
				return false;
			}
			if (!((*ActionDataObject)->TryGetStringField(GetSourceFilenameKey(), SourceFilename)))
			{
				return false;
			}

			//Since we filled the data from the json file, set the data has been initialize.
			bIsDataInitialize = true;
			return true;
		}

		FString FJsonLoadSourceCmd::JsonResultParser::ToJson() const
		{
			TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
			//CmdObject
			ResultObject->SetStringField(GetResultFilenameKey(), GetResultFilename());

			FString JsonResult;
			TSharedRef< TJsonWriter< TCHAR, TPrettyJsonPrintPolicy<TCHAR> > > JsonWriter = TJsonWriterFactory< TCHAR, TPrettyJsonPrintPolicy<TCHAR> >::Create(&JsonResult);
			if (!FJsonSerializer::Serialize(ResultObject.ToSharedRef(), JsonWriter))
			{
				//Error creating the json cmd string 
				return FString();
			}
			return JsonResult;
		}

		bool FJsonLoadSourceCmd::JsonResultParser::FromJson(const FString& JsonString)
		{
			TSharedRef<TJsonReader<TCHAR>> Reader = FJsonStringReader::Create(JsonString);

			TSharedPtr<FJsonObject> ResultObject;
			if (!FJsonSerializer::Deserialize(Reader, ResultObject) || !ResultObject.IsValid())
			{
				//Cannot read the json file
				return false;
			}
			return ResultObject->TryGetStringField(GetResultFilenameKey(), ResultFilename);
		}

	} //ns Interchange
}//ns UE
