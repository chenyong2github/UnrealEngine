// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BackChannel/Protocol/OSC/BackChannelOSCPacket.h"
#include "BackChannel/IBackChannelPacket.h"


/**
 *	Representation of an OSC message. Data can be read/written using the explicit
 *	Read/Write functions, or the Serialize function / << operator where the behaviour
 * is overloaded based on whether the message was created for reading or writing.
 *
 *	Any failed Reads() will result in the default value of the type (e.g. 0, 0.0, false, "")
 *	being returned.
 */
class BACKCHANNEL_API FBackChannelOSCMessage : public FBackChannelOSCPacket, public IBackChannelPacket
{
public:

	FBackChannelOSCMessage(OSCPacketMode InMode);

	FBackChannelOSCMessage(const TCHAR* Address);

	virtual ~FBackChannelOSCMessage();

	/* Move constructor */
	FBackChannelOSCMessage(FBackChannelOSCMessage&& RHS);

	/* Move operator */
	FBackChannelOSCMessage& operator=(FBackChannelOSCMessage&& RHS);

	/* Return our type */
	virtual OSCPacketType GetType() const override { return OSCPacketType::Message; }

	/* Return our size (plus any necessary padding) */
	virtual int32 GetSize() const override;

	/* Returns a buffer with the contents of this message. Data is padded per OSC requirements */
	virtual TArray<uint8> WriteToBuffer() const override;
	
	/* Writes this message into the provided buffer at an offset of Buffer.Num() */
	virtual void WriteToBuffer(TArray<uint8>& Buffer) const override;

	/* Helper to check our read/write status */
	bool IsWriting() const { return Mode == OSCPacketMode::Write; }

	/* Helper to check our read/write status */
	bool IsReading() const { return Mode == OSCPacketMode::Read; }

	/* Returns the address of this packet */
	FString GetPath() const
	{
		return Address;
	}

	/* Return our argument tags */
	const FString& GetTags() const
	{
		return TagString;
	}
	
	/* Returns the number of arguments in this message */
	int32 GetArgumentCount() const
	{
		return TagString.Len();
	}
	
	/* Returns the type of our next argument */
	TCHAR GetNextArgumentType() const
	{
		return TagString[TagIndex];
	}

	/* Return the size (plus padding) of all our arguments) */
	const int32 GetArgumentSize() const
	{
		return Buffer.Num();
	}

	/* Set our destination address */
	int SetPath(const TCHAR* Address);

	/* Reset us for reading. The next argument read will be our first argument */
	void	ResetRead();

	virtual FBackChannelPacketType GetProtocolID() const { return FBackChannelPacketType('B','O','S','C'); }

	virtual FString GetProtocolName() const { return TEXT("BackChannelOSC"); }

	virtual bool IsWritable() const { return IsWriting(); }

	virtual bool IsReadable() const { return IsReading(); }

	//! Int32 read/write

	/* Write an int32 into our arguments */
	virtual int Write(const TCHAR* InName, const int32 Value) override
	{
		check(IsWriting());
		int32 SwappedValue = !IsLegacyConnection() ? ByteSwap(Value) : Value;
		WriteTagAndData(TEXT('i'), &SwappedValue, sizeof(SwappedValue));
		return 0;
	}

	/* Write a float to our arguments */
	virtual int Write(const TCHAR* InName, const float Value) override
	{
		check(IsWriting());
		float SwappedValue = !IsLegacyConnection() ? ByteSwap(Value) : Value;
		return WriteTagAndData(TEXT('f'), &SwappedValue, sizeof(SwappedValue));
	}

	/* Write a bool to our arguments */
	virtual int Write(const TCHAR* InName, const bool Value) override
	{
		return Write(InName, int32(Value));
	}

	/* Write a string to our arguments */
	virtual int Write(const TCHAR* InName, const TCHAR* Value) override
	{
		return WriteTagAndData(TEXT('s'), TCHAR_TO_ANSI(Value), FCString::Strlen(Value) + 1);
	}

	/* Write a string to our arguments */
	virtual int Write(const TCHAR* InName, const FString& Value) override
	{
		return Write(InName, *Value);
	}

	/* Write a blob of data to our arguments */
	virtual int Write(const TCHAR* InName, const void* InBlob, int32 BlobSize) override
	{
		check(IsWriting());
		return WriteTagAndData(TEXT('b'), InBlob, BlobSize);
	}

	/* Write a TArray into the message */
	virtual int Write(const TCHAR* InName, const TArrayView<const uint8> Value) override
	{
		FString SizeParam = FString(InName) + TEXT("_Size");
		Write(*SizeParam, Value.Num());
		return Write(InName, Value.GetData(), Value.Num());
	}

	/* Read an int32 from our arguments */
	virtual int Read(const TCHAR* InName, int32& Value) override
	{
		check(IsReading());
		int Err = ReadTagAndData(TEXT('i'), &Value, sizeof(Value));

		if (!Err && !IsLegacyConnection())
		{
			Value = ByteSwap(Value);
		}

		return Err;
	}	

	/* Read a float from our arguments */
	virtual int Read(const TCHAR* InName, float& OutValue) override
	{
		check(IsReading());
		int Err = ReadTagAndData(TEXT('f'), &OutValue, sizeof(OutValue));

		if (!Err && !IsLegacyConnection())
		{
			OutValue = ByteSwap(OutValue);
		}

		return Err;
	}

	/* Read a bool from our arguments */
	virtual int Read(const TCHAR* InName, bool& Value) override
	{
		int32 Tmp(0);
		int result = Read(InName, Tmp);
		if (result == 0)
		{
			Value = Tmp == 0 ? false : true;
		}
		return result;
	}

	/* Read a string from our arguments.  */
	virtual int Read(const TCHAR* InName, FString& OutValue) override;

	//! Raw data blobs

	/* Read a blob of data from our arguments */
	virtual int Read(const TCHAR* InName, void* InBlob, int32 BlobSize) override
	{
		check(IsReading());
		return ReadTagAndData(TEXT('b'), InBlob, BlobSize);
	}		

	/* Read data from the message into a TArray. It must have been serialized by the Read form for TArray(!) */
	virtual int Read(const TCHAR* InName, TArray<uint8>& Data) override
	{
		FString SizeParam = FString(InName) + TEXT("_Size");
		int ArraySize(0);
		Read(*SizeParam, ArraySize);
		Data.Empty(ArraySize);
		Data.AddUninitialized(ArraySize);

		return Read(InName, Data.GetData(), ArraySize * sizeof(uint8));
	}

#if 0
	/*
	*	Write a TArray64 of type T to our arguments. This is a helper that writes an int32
	*	for the size, then a blob of sizeof(t) * NumItems
	*/
	template<typename T>
	void Write(const TCHAR* Name, const TArray64<T>& Value)
	{
		ensureMsgf(Value.Num() == (int32)Value.Num(), TEXT("Tried to write array with %" INT64_FMT " elements, which would overflow because the element count is 32-bit"), Value.Num());
		Write((int32)Value.Num());
		Write(Value.GetData(), Value.Num() * sizeof(T));
	}
#endif

	/*
	 *	Read a TArray of type T from our arguments. This is a helper that reads an int
	*	for the size, then allocated and reads a blob of sizeof(t) * NumItems
	 */
	template<typename T>
	int Read(const TCHAR* InName, TArray<T>& Value)
	{
		FString SizeParam = FString(InName) + TEXT("_Size");

		int32 ArraySize(0);
		Read(*SizeParam, ArraySize);
		Value.Empty();
		Value.AddUninitialized(ArraySize);
		return Read(InName, Value.GetData(), Value.Num() * sizeof(T));
	}
	

	/* Serialize helper that will read/write based on the open mode of this message */
	template<typename T>
	void Serialize(const TCHAR* Name, T& Value)
	{
		if (IsWriting())
		{
			Write(Name, Value);
		}
		else
		{
			Read(Name, Value);
		}
	}

	/* Serialize helper that will read/write based on the open mode of this message */
	void Serialize(const TCHAR* Name, void* InBlob, int32 BlobSize)
	{
		if (IsReading())
		{
			Read(Name, InBlob, BlobSize);
		}
		else
		{
			Write(Name, InBlob, BlobSize);
		}
	}

	static int32 RoundedArgumentSize(int32 ArgSize)
	{
		return ((ArgSize + 3) / 4) * 4;
	}

	static TSharedPtr<FBackChannelOSCMessage> CreateFromBuffer(const void* Data, int32 DataLength);

	static void SetLegacyMode(const bool bEnable) { bIsLegacyConnection  = bEnable; }
	static bool IsLegacyConnection() { return bIsLegacyConnection; }

protected:

	int Serialize(const TCHAR Code, void* InData, int32 InSize);

	int ReadTagAndData(const TCHAR Code, void* InData, int32 InSize);
	int ReadData(void* InData, int32 InSize);

	int WriteTagAndData(const TCHAR Code, const void* InData, int32 InSize);
	int WriteData(const void* InData, int32 InSize);

protected:

	OSCPacketMode		Mode;
	FString				Address;
	FString				TagString;
	int					TagIndex;
	int					BufferIndex;
	TArray<uint8>		Buffer;

	static bool bIsLegacyConnection;
};

/*
BACKCHANNEL_API FBackChannelOSCMessage& operator << (FBackChannelOSCMessage& Msg, int32& Value);

BACKCHANNEL_API FBackChannelOSCMessage& operator << (FBackChannelOSCMessage& Msg, float& Value);

BACKCHANNEL_API FBackChannelOSCMessage& operator << (FBackChannelOSCMessage& Msg, bool& Value);

BACKCHANNEL_API FBackChannelOSCMessage& operator << (FBackChannelOSCMessage& Msg, TCHAR& Value);

BACKCHANNEL_API FBackChannelOSCMessage& operator << (FBackChannelOSCMessage& Msg, FString& Value);

template <typename T>
FBackChannelOSCMessage& operator << (FBackChannelOSCMessage& Msg, TArray<T>& Value)
{
	if (Msg.IsWriting())
	{
		Msg.Write(nullptr, Value);
	}
	else
	{
		Msg.Read(nullptr, Value);
	}

	return Msg;
}


template <typename T>
FBackChannelOSCMessage& SerializeOut(FBackChannelOSCMessage& Msg, const T& Value)
{
	T Tmp = Value;
	Msg << Tmp;
	return Msg;
}
*/