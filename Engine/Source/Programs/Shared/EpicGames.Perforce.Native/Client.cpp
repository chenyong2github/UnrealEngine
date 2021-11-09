// Copyright Epic Games, Inc. All Rights Reserved.

#include "clientapi.h"
#include "strtable.h"
#include <assert.h>

#ifdef _MSC_VER
	#define NATIVE_API __declspec(dllexport)
	#define strcasecmp _stricmp
#else
	#define NATIVE_API
#endif

struct FSettings
{
	const char* ServerAndPort;
	const char* User;
	const char* Password;
	const char* Client;
	const char* AppName;
	const char* AppVersion;
};

struct FReadBuffer
{
	unsigned char* Data;
	unsigned int Length;
	unsigned int Count;
	unsigned int MaxLength;
	unsigned int MaxCount;
};

struct FWriteBuffer
{
	unsigned char* Data;
	unsigned int MaxLength;
	unsigned int MaxCount;
};

typedef void (FOnBufferReadyFn)(const FReadBuffer* ReadBuffer, FWriteBuffer* WriteBuffer);

class FClientUser : public ClientUser
{
private:
	unsigned char* Data;
	unsigned int Count;
	unsigned int Length;
	unsigned int MaxCount;
	unsigned int MaxLength;
	FOnBufferReadyFn* OnBufferReady;

public:
	const char* Func = nullptr;
	const char* PromptResponse = nullptr;

	FClientUser(FWriteBuffer* WriteBuffer, FOnBufferReadyFn* OnBufferReady)
	{
		SetWriteBuffer(WriteBuffer);
		this->OnBufferReady = OnBufferReady;
	}

	virtual void Prompt(Error* err, StrBuf& rsp, int noEcho, Error* e) override
	{
		rsp.Set(PromptResponse);
	}

	virtual void Prompt(Error* err, StrBuf& rsp, int noEcho, int noOutput, Error* e) override
	{
		rsp.Set(PromptResponse);
	}

	virtual void Prompt(const StrPtr& msg, StrBuf& rsp, int noEcho, Error* e) override
	{
		rsp.Set(PromptResponse);
	}

	virtual void Prompt(const StrPtr& msg, StrBuf& rsp, int noEcho, int noOutput, Error* e) override
	{
		rsp.Set(PromptResponse);
	}

	void SetWriteBuffer(FWriteBuffer* WriteBuffer)
	{
		this->Data = WriteBuffer->Data;
		this->Length = 0;
		this->Count = 0;
		this->MaxLength = WriteBuffer->MaxLength;
		this->MaxCount = (size_t)WriteBuffer->MaxCount;
	}

	void Flush()
	{
		FReadBuffer ReadBuffer;
		ReadBuffer.Data = Data;
		ReadBuffer.Length = Length;
		ReadBuffer.Count = Count;
		ReadBuffer.MaxLength = MaxLength;
		ReadBuffer.MaxCount = MaxCount;

		FWriteBuffer WriteBuffer;
		memset(&WriteBuffer, 0, sizeof(WriteBuffer));

		(*OnBufferReady)(&ReadBuffer, &WriteBuffer);

		SetWriteBuffer(&WriteBuffer);
	}

	virtual void HandleError(Error* err) override
	{
		while (!TryOutputError(err))
		{
			Flush();
		}
	}

	virtual void Message(Error* Err) override
	{
		while (!TryOutputError(Err))
		{
			Flush();
		}
	}

	bool TryOutputError(Error* Err)
	{
		static const char CodeKey[] = "code";
		static const char SeverityKey[] = "severity";
		static const char GenericKey[] = "generic";
		static const char DataKey[] = "data";

		StrBuf Message;
		Err->Fmt(&Message, 0);
		unsigned int MessageLen = Message.Length();

		unsigned int RecordLen;
		if (Err->GetSeverity() == E_INFO)
		{
			static const char InfoCode[] = "info";

			RecordLen = 1 + MeasureStringField(CodeKey, InfoCode) + MeasureStringField(DataKey, MessageLen) + 1;
			if (Length + RecordLen > MaxLength)
			{
				return false;
			}

			unsigned char* Pos = Data + Length;
			*(Pos++) = '{';
			Pos = WriteStringField(Pos, CodeKey, InfoCode);
			Pos = WriteStringField(Pos, DataKey, Message.Text(), MessageLen);
			*(Pos++) = '0';
			assert(Pos == Data + Length + RecordLen);
		}
		else
		{
			static const char ErrorCode[] = "error";

			RecordLen = 1 + MeasureStringField(CodeKey, ErrorCode) + MeasureIntField(SeverityKey) + MeasureIntField(GenericKey) + MeasureStringField(DataKey, MessageLen) + 1;
			if (Length + RecordLen > MaxLength)
			{
				return false;
			}

			unsigned char* Pos = Data + Length;
			*(Pos++) = '{';
			Pos = WriteStringField(Pos, CodeKey, ErrorCode);
			Pos = WriteIntField(Pos, SeverityKey, Err->GetSeverity());
			Pos = WriteIntField(Pos, GenericKey, Err->GetGeneric());
			Pos = WriteStringField(Pos, DataKey, Message.Text(), MessageLen);
			*(Pos++) = '0';
			assert(Pos == Data + Length + RecordLen);
		}

		Length += RecordLen;
		return true;
	}

	virtual void OutputError(const char* errBuf) override
	{
		assert(false);
	}

	virtual void OutputInfo(char Level, const char* Data) override
	{
		while (!TryOutputInfo(Level, Data))
		{
			Flush();
		}
	}

	bool TryOutputInfo(char Level, const char* Info)
	{
		static const char CodeKey[] = "code";
		static const char LevelKey[] = "level";
		static const char DataKey[] = "data";

		static const char InfoCode[] = "info";

		unsigned int InfoLen = (unsigned int)strlen(Info);
		unsigned int RecordLen = 1 + MeasureStringField(CodeKey, InfoCode) + MeasureIntField(LevelKey) + MeasureStringField(DataKey, InfoLen) + 1;
		if (Length + RecordLen > MaxLength)
		{
			return false;
		}

		unsigned char* Pos = Data + Length;
		*(Pos++) = '{';
		Pos = WriteStringField(Pos, CodeKey, InfoCode);
		Pos = WriteIntField(Pos, LevelKey, Level);
		Pos = WriteStringField(Pos, DataKey, Info, InfoLen);
		*(Pos++) = '0';
		assert(Pos == Data + Length + RecordLen);

		Length += RecordLen;
		return true;
	}

	virtual void OutputBinary(const char* data, int length) override
	{
		assert(false);
	}

	virtual void OutputText(const char* Data, int Length) override
	{
		assert(false);
	}

	virtual void OutputStat(StrDict* VarList) override
	{
		while (!TryWriteRecord(VarList))
		{
			Flush();
		}
		if (++Count > MaxCount)
		{
			Flush();
		}
	}

	template<int KeyLen>
	static int MeasureStringField(const char(&Key)[KeyLen], int ValueLen)
	{
		return 10 + (KeyLen - 1) + ValueLen;
	}

	template<int KeyLen, int ValueLen>
	static int MeasureStringField(const char(&Key)[KeyLen], const char(&Value)[ValueLen])
	{
		return MeasureStringField(Key, ValueLen - 1);
	}

	template<int KeyLen>
	static int MeasureIntField(const char(&Key)[KeyLen])
	{
		return 10 + (KeyLen - 1);
	}

	template<int KeyLen>
	static unsigned char* WriteStringField(unsigned char* Data, const char(&Key)[KeyLen], const char* Value, int ValueLen)
	{
		Data = WriteString(Data, Key);
		return WriteString(Data, Value, ValueLen);
	}

	template<int KeyLen, int ValueLen>
	static unsigned char* WriteStringField(unsigned char* Data, const char(&Key)[KeyLen], const char(&Value)[ValueLen])
	{
		Data = WriteString(Data, Key);
		return WriteString(Data, Value);
	}

	template<int KeyLen>
	static unsigned char* WriteIntField(unsigned char* Data, const char(&Key)[KeyLen], int Value)
	{
		Data = WriteString(Data, Key);
		return WriteInt(Data, Value);
	}

	static unsigned char* WriteInt(unsigned char* Data, int Value)
	{
		*(Data++) = 'i';
		return WriteIntValue(Data, Value);
	}

	static unsigned char* WriteIntValue(unsigned char* Data, int Value)
	{
		*(Data++) = Value & 0xff;
		*(Data++) = (Value >> 8) & 0xff;
		*(Data++) = (Value >> 16) & 0xff;
		*(Data++) = (Value >> 24) & 0xff;
		return Data;
	}

	static unsigned char* WriteString(unsigned char* Data, const char* Text, int Length)
	{
		*(Data++) = 's';
		Data = WriteIntValue(Data, Length);
		memcpy(Data, Text, Length);
		return Data + Length;
	}

	template<int Length>
	static unsigned char* WriteString(unsigned char* Data, const char(&Text)[Length])
	{
		return WriteString(Data, Text, Length - 1);
	}

	bool TryWriteRecord(StrDict* VarList)
	{
		return TryWriteRecord("stat", 4, VarList);
	}

	bool TryWriteRecord(const char* Code, unsigned int CodeLen, StrDict* VarList)
	{
		unsigned int Pos = Length;

		// Record prefix: '{' (open record), string, 4 bytes, 'code', string ...
		unsigned char Prefix[] = { '{', 's', 4, 0, 0, 0, 'c', 'o', 'd', 'e', 's' };
		unsigned int PrefixLen = sizeof(Prefix) / sizeof(Prefix[0]);

		if (Pos + PrefixLen + (unsigned int)sizeof(unsigned int) + CodeLen > MaxLength - 1)
		{
			return false;
		}

		memcpy(Data + Pos, Prefix, PrefixLen);
		Pos += PrefixLen;

		memcpy(Data + Pos, &CodeLen, sizeof(CodeLen));
		Pos += sizeof(unsigned int);

		memcpy(Data + Pos, Code, CodeLen);
		Pos += CodeLen;

		for (int Idx = 0;; Idx++)
		{
			StrRef Field;
			StrRef Value;
			if (VarList->GetVar(Idx, Field, Value) == 0)
			{
				break;
			}

			unsigned int FieldLength = Field.Length();
			unsigned int ValueLength = Value.Length();

			if (Pos + 1 + 4 + FieldLength + 1 + 4 + ValueLength > MaxLength - 1)
			{
				return false;
			}

			Data[Pos] = 's';
			Pos++;

			memcpy(Data + Pos, &FieldLength, sizeof(FieldLength));
			Pos += sizeof(FieldLength);

			memcpy(Data + Pos, Field.Text(), FieldLength);
			Pos += FieldLength;

			Data[Pos] = 's';
			Pos++;

			memcpy(Data + Pos, &ValueLength, sizeof(ValueLength));
			Pos += sizeof(ValueLength);

			memcpy(Data + Pos, Value.Text(), ValueLength);
			Pos += ValueLength;
		}

		Data[Pos++] = '0';

		Length = Pos;
		return true;
	}
};

class FClient
{
public:
	ClientApi ClientApi;
	FClientUser User;

	FClient(FWriteBuffer* WriteBuffer, FOnBufferReadyFn* OnBufferReady)
		: User(WriteBuffer, OnBufferReady)
	{
	}
};

extern "C" NATIVE_API FClient* Client_Create(const FSettings* Settings, FWriteBuffer* WriteBuffer, FOnBufferReadyFn* OnBufferReady)
{
	FClient* Client = new FClient(WriteBuffer, OnBufferReady);

	if (Settings != nullptr)
	{
		if (Settings->ServerAndPort != nullptr)
		{
			Client->ClientApi.SetPort(Settings->ServerAndPort);
		}
		if (Settings->User != nullptr)
		{
			Client->ClientApi.SetUser(Settings->User);
		}
		if (Settings->Password != nullptr)
		{
			Client->ClientApi.SetPassword(Settings->Password);
		}
		if (Settings->Client != nullptr)
		{
			Client->ClientApi.SetClient(Settings->Client);
		}
		if (Settings->AppName != nullptr)
		{
			Client->ClientApi.SetProg(Settings->AppName);
		}
		if (Settings->AppVersion != nullptr)
		{
			Client->ClientApi.SetVersion(Settings->AppVersion);
		}
	}
	Client->ClientApi.SetProtocol("tag", "");

	Error Err;
	Client->ClientApi.Init(&Err);
	Client->User.HandleError(&Err);
	Client->User.Flush();

	return Client;
}

extern "C" NATIVE_API void Client_Login(FClient * Client, const char* Password)
{
	Client->User.PromptResponse = Password;
	Client->ClientApi.SetArgv(0, nullptr);
	Client->ClientApi.Run("login", &Client->User);
	Client->User.Flush();
	Client->User.PromptResponse = nullptr;
}

extern "C" NATIVE_API void Client_Command(FClient* Client, const char* Func, int ArgCount, const char** Args)
{
	Client->User.Func = Func;
	Client->ClientApi.SetArgv(ArgCount, (char* const*)Args);
	Client->ClientApi.Run(Func, &Client->User);
	Client->User.Flush();
	Client->User.Func = nullptr;
}

extern "C" NATIVE_API void Client_Destroy(FClient* Client)
{
	Error Err;
	Client->ClientApi.Final(&Err);
	Client->User.HandleError(&Err);
	delete Client;
}
