// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Logging/TokenizedMessage.h"

class FField;
class UEdGraphNode;
class UEdGraphPin;

class ICompilerResultsLog
{
public:
	/**
	 * Write a note in to the compiler log.
	 * Note: @@ will be replaced by node or pin links for nodes/pins passed via varargs
	 */
	template<typename... ArgTypes>
	TSharedRef<FTokenizedMessage> Note(const TCHAR* Format, ArgTypes... Args)
	{
		TSharedRef<FTokenizedMessage> Line = FTokenizedMessage::Create(EMessageSeverity::Info);
		InternalLogMessage(NAME_None, Format, Line, Args...);
		return Line;
	}

	/**
	 * Write a warning in to the compiler log.
	 * Note: @@ will be replaced by node or pin links for nodes/pins passed via varargs
	 */
	template<typename... ArgTypes>
	TSharedRef<FTokenizedMessage> Warning(const TCHAR* Format, ArgTypes... Args)
	{
		IncrementWarningCount();
		TSharedRef<FTokenizedMessage> Line = FTokenizedMessage::Create(EMessageSeverity::Warning);
		InternalLogMessage(NAME_None, Format, Line, Args...);
		return Line;
	}

	/**
	 * Write a warning in to the compiler log.
	 * Note: @@ will be replaced by node or pin links for nodes/pins passed via varargs
	 */
	template<typename... ArgTypes>
	void Warning(FName ID, const TCHAR* Format, ArgTypes... Args)
	{
		if (!IsMessageEnabled(ID))
		{
			return;
		}

		IncrementWarningCount();
		TSharedRef<FTokenizedMessage> Line = FTokenizedMessage::Create(EMessageSeverity::Warning);
		InternalLogMessage(ID, Format, Line, Args...);
		return;
	}

	/**
	 * Write an error in to the compiler log.
	 * Note: @@ will be replaced by node or pin links for nodes/pins passed via varargs
	 */
	template<typename... ArgTypes>
	TSharedRef<FTokenizedMessage> Error(const TCHAR* Format, ArgTypes... Args)
	{
		IncrementErrorCount();
		TSharedRef<FTokenizedMessage> Line = FTokenizedMessage::Create(EMessageSeverity::Error);
		InternalLogMessage(NAME_None, Format, Line, Args...);
		return Line;
	}

	virtual void SetSilentMode(bool bValue) = 0;

protected:

	void Tokenize(const TCHAR* Text, FTokenizedMessage& OutMessage, TArray<UEdGraphNode*>& OutSourceNode)
	{
		OutMessage.AddToken(FTextToken::Create(FText::FromString(Text)));
	}

	template<typename T, typename... ArgTypes>
	void Tokenize(const TCHAR* Format, FTokenizedMessage& OutMessage, TArray<UEdGraphNode*>& OutSourceNode, T First, ArgTypes... Rest)
	{
		// read to next "@@":
		if (const TCHAR* DelimiterStr = FCString::Strstr(Format, TEXT("@@")))
		{
			int32 TokenLength = UE_PTRDIFF_TO_INT32(DelimiterStr - Format);
			OutMessage.AddToken(FTextToken::Create(FText::FromString(FString(TokenLength, Format))));
			//FEdGraphToken::Create(First, this, OutMessage, OutSourceNode);
			FEdGraphToken_Create(First, OutMessage, OutSourceNode);

			const TCHAR* NextChunk = DelimiterStr + FCString::Strlen(TEXT("@@"));
			if (*NextChunk)
			{
				Tokenize(NextChunk, OutMessage, OutSourceNode, Rest...);
			}
		}
		else
		{
			Tokenize(Format, OutMessage, OutSourceNode);
		}
	}

	template<typename... ArgTypes>
	void InternalLogMessage(FName MessageID, const TCHAR* Format, const TSharedRef<FTokenizedMessage>& Message, ArgTypes... Args)
	{
		// Convention for SourceNode established by the original version of the compiler results log
		// was to annotate the error on the first node we can find. I am preserving that behavior
		// for this type safe, variadic version:
		TArray<UEdGraphNode*> SourceNodes;
		Tokenize(Format, *Message, SourceNodes, Args...);
		InternalLogMessage(MessageID, Message, SourceNodes);
	}

	/** Returns true if the user has requested this compiler message be suppressed */
	virtual bool IsMessageEnabled(FName ID) = 0;

	virtual void InternalLogMessage(FName MessageID, const TSharedRef<FTokenizedMessage>& Message, const TArray<UEdGraphNode*>& SourceNodes) = 0;

	virtual void FEdGraphToken_Create(const UObject* InObject, FTokenizedMessage& OutMessage, TArray<UEdGraphNode*>& OutSourceNodes) = 0;
	virtual void FEdGraphToken_Create(const UEdGraphPin* InPin, FTokenizedMessage& OutMessage, TArray<UEdGraphNode*>& OutSourceNodes) = 0;
	virtual void FEdGraphToken_Create(const TCHAR* String, FTokenizedMessage& OutMessage, TArray<UEdGraphNode*>& OutSourceNodes) = 0;
	virtual void FEdGraphToken_Create(const FField* InField, FTokenizedMessage& OutMessage, TArray<UEdGraphNode*>& OutSourceNodes) = 0;

	virtual void IncrementErrorCount() = 0;
	virtual void IncrementWarningCount() = 0;
};
