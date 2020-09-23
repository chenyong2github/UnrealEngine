// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
namespace UE
{
	namespace Interchange
	{
		enum class ETaskState
		{
			Unknown,
			Running,
			UnTreated,
			ProcessOk,
			ProcessFailed,
		};

		struct FTask
		{
			FTask() = default;

			FTask(const FString& InJsonDescription)
			{
				JsonDescription = InJsonDescription;
				State = ETaskState::UnTreated;
			}

			FString JsonDescription;
			int32 Index = -1;
			ETaskState State = ETaskState::Unknown;
			FString JsonResult;
			TArray<FString> JsonMessages;
		};

		/**
		 * Json cmd helper to be able to read and write a FTask::JsonDescription
		 */
		class INTERCHANGEDISPATCHER_API IJsonCmdBase
		{
		public:
			virtual ~IJsonCmdBase() = default;

			virtual FString GetAction() const = 0;
			virtual FString GetTranslatorID() const = 0;
			virtual FString ToJson() const = 0;

			/**
			 * Return false if the JsonString do not match the command, true otherwise.
			 */
			virtual bool FromJson(const FString& JsonString) = 0;

			static FString GetCommandIDKey()
			{
				static const FString Key = TEXT("CmdID");
				return Key;
			}
			static FString GetTranslatorIDKey()
			{
				static const FString Key = TEXT("TranslatorID");
				return Key;
			}
			static FString GetCommandDataKey()
			{
				static const FString Key = TEXT("CmdData");
				return Key;
			}

		protected:
			//Use this member to know if the data is initialize before using it
			bool bIsDataInitialize = false;
		};

		class INTERCHANGEDISPATCHER_API FJsonLoadSourceCmd : public IJsonCmdBase
		{
		public:
			FJsonLoadSourceCmd()
			{
				bIsDataInitialize = false;
			}

			FJsonLoadSourceCmd(const FString& InTranslatorID, const FString& InSourceFilename)
				: TranslatorID(InTranslatorID)
				, SourceFilename(InSourceFilename)
			{
				bIsDataInitialize = true;
			}

			virtual FString GetAction() const override
			{
				static const FString LoadString = TEXT("Load");
				return LoadString;
			}

			virtual FString GetTranslatorID() const override
			{
				//Code should not do query data if the data was not set before
				ensure(bIsDataInitialize);
				return TranslatorID;
			}

			virtual FString ToJson() const;
			virtual bool FromJson(const FString& JsonString);

			FString GetSourceFilename() const
			{
				//Code should not do query data if the data was not set before
				ensure(bIsDataInitialize);
				return SourceFilename;
			}

			static FString GetSourceFilenameKey()
			{
				static const FString Key = TEXT("SourceFile");
				return Key;
			}

			/**
			 * Use this class helper to create the cmd result json string and to read it
			 */
			class INTERCHANGEDISPATCHER_API JsonResultParser
			{
			public:
				FString GetResultFilename() const
				{
					return ResultFilename;
				}
				void SetResultFilename(const FString& InResultFilename)
				{
					ResultFilename = InResultFilename;
				}
				FString ToJson() const;
				bool FromJson(const FString& JsonString);

				static FString GetResultFilenameKey()
				{
					const FString Key = TEXT("ResultFile");
					return Key;
				}
			private:
				FString ResultFilename = FString();
			};

		private:
			FString TranslatorID = FString();
			FString SourceFilename = FString();
		};

	} //ns Interchange
}//ns UE
