// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

template <class CharType, class PrintPolicy>
class TGLTFJsonWriterImpl final : public IGLTFJsonWriter
{
public:

	TGLTFJsonWriterImpl(FArchive* const Archive)
		: JsonWriter(TJsonWriterFactory<CharType, PrintPolicy>::Create(Archive))
	{
	}

	virtual void Close() override
	{
		JsonWriter->Close();
	}

	virtual void Write(bool Boolean) override
	{
		if (CurrentIdentifier.IsEmpty())
		{
			JsonWriter->WriteValue(Boolean);
		}
		else
		{
			JsonWriter->WriteValue(CurrentIdentifier, Boolean);
			CurrentIdentifier.Empty();
		}
	}

	virtual void Write(int32 Number) override
	{
		if (CurrentIdentifier.IsEmpty())
		{
			JsonWriter->WriteValue(Number);
		}
		else
		{
			JsonWriter->WriteValue(CurrentIdentifier, Number);
			CurrentIdentifier.Empty();
		}
	}

	virtual void Write(int64 Number) override
	{
		if (CurrentIdentifier.IsEmpty())
		{
			JsonWriter->WriteValue(Number);
		}
		else
		{
			JsonWriter->WriteValue(CurrentIdentifier, Number);
			CurrentIdentifier.Empty();
		}
	}

	virtual void Write(float Number) override
	{
		// NOTE: Specifying 9 significant digits, this ensures no precision is lost
		FString ExactString = FString::Printf(TEXT("%.9g"), Number);

		if (CurrentIdentifier.IsEmpty())
		{
			JsonWriter->WriteRawJSONValue(ExactString);
		}
		else
		{
			JsonWriter->WriteRawJSONValue(CurrentIdentifier, ExactString);
			CurrentIdentifier.Empty();
		}
	}

	virtual void Write(const FString& String) override
	{
		if (CurrentIdentifier.IsEmpty())
		{
			JsonWriter->WriteValue(String);
		}
		else
		{
			JsonWriter->WriteValue(CurrentIdentifier, String);
			CurrentIdentifier.Empty();
		}
	}

	virtual void Write(TYPE_OF_NULLPTR) override
	{
		if (CurrentIdentifier.IsEmpty())
		{
			JsonWriter->WriteValue(nullptr);
		}
		else
		{
			JsonWriter->WriteValue(CurrentIdentifier, nullptr);
			CurrentIdentifier.Empty();
		}
	}

	virtual void SetIdentifier(const FString& Identifier) override
	{
		this->CurrentIdentifier = Identifier;
	}

	virtual void StartObject() override
	{
		if (CurrentIdentifier.IsEmpty())
		{
			JsonWriter->WriteObjectStart();
		}
		else
		{
			JsonWriter->WriteObjectStart(CurrentIdentifier);
			CurrentIdentifier.Empty();
		}
	}

	virtual void EndObject() override
	{
		JsonWriter->WriteObjectEnd();
	}

	virtual void StartArray() override
	{
		if (CurrentIdentifier.IsEmpty())
		{
			JsonWriter->WriteArrayStart();
		}
		else
		{
			JsonWriter->WriteArrayStart(CurrentIdentifier);
			CurrentIdentifier.Empty();
		}
	}

	virtual void EndArray() override
	{
		JsonWriter->WriteArrayEnd();
	}

private:

	FString CurrentIdentifier;
	TSharedRef<TJsonWriter<CharType, PrintPolicy>> JsonWriter;
};

TSharedRef<IGLTFJsonWriter> IGLTFJsonWriter::Create(FArchive* const Archive, bool bPrettyJson)
{
	return MakeShareable(bPrettyJson
		? static_cast<IGLTFJsonWriter*>(new TGLTFJsonWriterImpl<UTF8CHAR, TPrettyJsonPrintPolicy<UTF8CHAR>>(Archive))
		: static_cast<IGLTFJsonWriter*>(new TGLTFJsonWriterImpl<UTF8CHAR, TCondensedJsonPrintPolicy<UTF8CHAR>>(Archive))
	);
}
