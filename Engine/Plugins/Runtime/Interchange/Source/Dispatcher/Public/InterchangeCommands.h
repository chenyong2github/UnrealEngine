// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeDispatcherTask.h"
#include "Serialization/Archive.h"

class FArchive;

namespace UE
{
	namespace Interchange
	{

		class INTERCHANGEDISPATCHER_API DispatcherCommandVersion
		{
		public:
			//Major version should be updated when there is an existing API that has been change.
			static int32 GetMajor() { return 1; }
			//Minor version must be updated when there is an addition to the API.
			static int32 GetMinor() { return 0; }
			//Patch version should be update if there is some bug fixes in the private code.
			static int32 GetPatch() { return 0; }

			/** Return the version in string format "Major.Minor.Patch" */
			static FString ToString()
			{
				static FString Version = FString::FromInt(GetMajor()) + TEXT(".") + FString::FromInt(GetMinor()) + TEXT(".") + FString::FromInt(GetPatch());
				return Version;
			}

			static bool FromString(const FString& VersionStr, int32& OutMajor, int32& OutMinor, int32& OutPatch)
			{
				TArray<FString> Tokens;
				VersionStr.ParseIntoArray(Tokens, TEXT("."));
				if (Tokens.Num() != 3)
				{
					return false;
				}
				OutMajor = FCString::Atoi(*(Tokens[0]));
				OutMinor = FCString::Atoi(*(Tokens[1]));
				OutPatch = FCString::Atoi(*(Tokens[2]));
				return true;
			}

			/**
			 * We consider having the same major and minor version will make the API fully compatible.
			 * Patch is only a hint in case you need a particular fixes
			 * @param Major - The Major version you want to test against the compile Major version
			 * @param Minor - The Minor version you want to test against the compile Minor version
			 * @param Patch - In case we need it later, not use currently
			 *
			 * @return true if Major and Minor version equal the compile one, InPatch is only there in case we need it in a future release
			 */
			static bool IsAPICompatible(int32 Major, int32 Minor, int32 Patch)
			{
				return (GetMajor() == Major && GetMinor() == Minor);
			}
		};

		enum class ECommandId : uint8
		{
			Invalid,
			Ping,
			BackPing,
			RunTask,
			NotifyEndTask,
			Terminate,
			Last
		};

		class ICommand
		{
		public:
			virtual ~ICommand() = default;

			virtual ECommandId GetType() const = 0;

			friend void operator<<(FArchive& Ar, ICommand& C) { C.SerializeImpl(Ar); }

		protected:
			virtual void SerializeImpl(FArchive&) {}
		};



		// Create a new command from its type
		TSharedPtr<ICommand> CreateCommand(ECommandId CommandType);

		// Converts a command into a byte buffer
		void SerializeCommand(ICommand& Command, TArray<uint8>& OutBuffer);

		// Converts byte buffer back into a Command
		// returns nullptr in case of error
		TSharedPtr<ICommand> DeserializeCommand(const TArray<uint8>& InBuffer);



		class INTERCHANGEDISPATCHER_API FTerminateCommand : public ICommand
		{
		public:
			virtual ECommandId GetType() const override { return ECommandId::Terminate; }
		};


		class INTERCHANGEDISPATCHER_API FPingCommand : public ICommand
		{
		public:
			virtual ECommandId GetType() const override { return ECommandId::Ping; }
		};


		class INTERCHANGEDISPATCHER_API FBackPingCommand : public ICommand
		{
		public:
			virtual ECommandId GetType() const override { return ECommandId::BackPing; }
		};

		class INTERCHANGEDISPATCHER_API FRunTaskCommand : public ICommand
		{
		public:
			FRunTaskCommand() = default;
			FRunTaskCommand(const FTask& Task) : JsonDescription(Task.JsonDescription), JobIndex(Task.Index) {}
			virtual ECommandId GetType() const override { return ECommandId::RunTask; }

		protected:
			virtual void SerializeImpl(FArchive&) override;

		public:
			FString JsonDescription;
			int32 JobIndex = -1;
		};

		class INTERCHANGEDISPATCHER_API FCompletedTaskCommand : public ICommand
		{
		public:
			virtual ECommandId GetType() const override { return ECommandId::NotifyEndTask; }

		protected:
			virtual void SerializeImpl(FArchive&) override;

		public:
			ETaskState ProcessResult = ETaskState::Unknown;
			FString JSonResult;
			TArray<FString> JSonMessages;
		};

	} //ns Interchange
}//ns UE
