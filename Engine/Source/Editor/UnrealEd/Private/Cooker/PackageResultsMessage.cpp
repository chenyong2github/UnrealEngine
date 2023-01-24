// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageResultsMessage.h"

#include "CookPackageData.h"

namespace UE::Cook
{

TArray<UE::CompactBinaryTCP::FMarshalledMessage> FPackageRemoteResult::FPlatformResult::ReleaseMessages()
{
	TArray<UE::CompactBinaryTCP::FMarshalledMessage> Result = MoveTemp(Messages);
	Messages.Empty();
	return Result;
}

TArray<UE::CompactBinaryTCP::FMarshalledMessage> FPackageRemoteResult::ReleaseMessages()
{
	TArray<UE::CompactBinaryTCP::FMarshalledMessage> Result = MoveTemp(Messages);
	Messages.Empty();
	return Result;
}

void FPackageRemoteResult::AddPackageMessage(const FGuid& MessageType, FCbObject&& Object)
{
	Messages.Add(UE::CompactBinaryTCP::FMarshalledMessage{ MessageType, MoveTemp(Object) });
}

void FPackageRemoteResult::AddPlatformMessage(const ITargetPlatform* TargetPlatform, const FGuid& MessageType, FCbObject&& Object)
{
	check(TargetPlatform != nullptr);
	int32 PlatformIndex = Platforms.IndexOfByPredicate([TargetPlatform](const FPlatformResult& Result)
		{ return Result.Platform == TargetPlatform; }
	);
	FPlatformResult* Result;
	if (PlatformIndex != INDEX_NONE)
	{
		Result = &Platforms[PlatformIndex];
	}
	else
	{
		Result = &Platforms.Emplace_GetRef();
		Result->Platform = TargetPlatform;
	}

	Result->Messages.Add(UE::CompactBinaryTCP::FMarshalledMessage{ MessageType, MoveTemp(Object)});
}

void FPackageRemoteResult::SetPlatforms(TConstArrayView<ITargetPlatform*> OrderedSessionPlatforms)
{
	Platforms.Reserve(OrderedSessionPlatforms.Num());
	for (ITargetPlatform* TargetPlatform : OrderedSessionPlatforms)
	{
		FPackageRemoteResult::FPlatformResult& PlatformResult = Platforms.Emplace_GetRef();
		PlatformResult.Platform = TargetPlatform;
	}
}

void FPackageResultsMessage::Write(FCbWriter& Writer) const
{
	Writer.BeginArray("R");
	for (const FPackageRemoteResult& Result : Results)
	{
		Writer.BeginObject();
		{
			Writer << "N" << Result.PackageName;
			Writer << "R" << (uint8) Result.SuppressCookReason;
			Writer << "E" << Result.bReferencedOnlyByEditorOnlyData;
			WriteMessagesArray(Writer, Result.Messages);
			Writer.BeginArray("P");
			for (const FPackageRemoteResult::FPlatformResult& PlatformResult : Result.Platforms)
			{
				Writer.BeginObject();
				Writer << "S" << PlatformResult.bSuccessful;
				Writer << "G" << PlatformResult.PackageGuid;
				Writer << "D" << PlatformResult.TargetDomainDependencies;
				WriteMessagesArray(Writer, PlatformResult.Messages);
				Writer.EndObject();
			}
			Writer.EndArray();
		}
		Writer.EndObject();
	}
	Writer.EndArray();
}

void FPackageResultsMessage::WriteMessagesArray(FCbWriter& Writer,
	TConstArrayView<UE::CompactBinaryTCP::FMarshalledMessage> InMessages)
{
	if (!InMessages.IsEmpty())
	{
		// We write a nonhomogenous array of length 2N. 2N+0 is the Message type, 2N+1 is the message object.
		Writer.BeginArray("M");
		for (const UE::CompactBinaryTCP::FMarshalledMessage& Message : InMessages)
		{
			Writer << Message.MessageType;
			Writer << Message.Object;
		}
		Writer.EndArray();
	}
}

bool FPackageResultsMessage::TryRead(FCbObjectView Object)
{
	Results.Reset();
	for (FCbFieldView ResultField : Object["R"])
	{
		FCbObjectView ResultObject = ResultField.AsObjectView();
		FPackageRemoteResult& Result = Results.Emplace_GetRef();
		LoadFromCompactBinary(ResultObject["N"], Result.PackageName);
		if (Result.PackageName.IsNone())
		{
			return false;
		}
		int32 LocalSuppressCookReason = ResultObject["R"].AsUInt8(MAX_uint8);
		if (LocalSuppressCookReason == MAX_uint8)
		{
			return false;
		}
		Result.SuppressCookReason = static_cast<ESuppressCookReason>(LocalSuppressCookReason);
		Result.bReferencedOnlyByEditorOnlyData = ResultObject["E"].AsBool();
		if (!TryReadMessagesArray(ResultObject, Result.Messages))
		{
			return false;
		}
		Result.Platforms.Reset();
		for (FCbFieldView PlatformField : ResultObject["P"])
		{
			FPackageRemoteResult::FPlatformResult& PlatformResult = Result.Platforms.Emplace_GetRef();
			FCbObjectView PlatformObject = PlatformField.AsObjectView();

			PlatformResult.bSuccessful = PlatformObject["S"].AsBool();
			PlatformResult.PackageGuid = PlatformObject["G"].AsUuid();
			PlatformResult.TargetDomainDependencies = FCbObject::Clone(PlatformObject["D"].AsObjectView());

			if (!TryReadMessagesArray(PlatformObject, PlatformResult.Messages))
			{
				return false;
			}
		}
	}
	return true;
}

bool FPackageResultsMessage::TryReadMessagesArray(FCbObjectView ObjectWithMessageField,
	TArray<UE::CompactBinaryTCP::FMarshalledMessage>& InMessages)
{
	// We read a nonhomogenous array of length 2N. 2N+0 is the Message type, 2N+1 is the message object.
	FCbArrayView MessagesArray = ObjectWithMessageField["M"].AsArrayView();
	InMessages.Reset(MessagesArray.Num() / 2);
	FCbFieldViewIterator MessageField = MessagesArray.CreateViewIterator();
	while (MessageField)
	{
		UE::CompactBinaryTCP::FMarshalledMessage& Message = InMessages.Emplace_GetRef();
		Message.MessageType = MessageField->AsUuid();
		if (MessageField.HasError())
		{
			return false;
		}
		++MessageField;
		Message.Object = FCbObject::Clone(MessageField->AsObjectView());
		if (MessageField.HasError())
		{
			return false;
		}
		++MessageField;
	}
	return true;
}

FGuid FPackageResultsMessage::MessageType(TEXT("4631C6C0F6DC4CEFB2B09D3FB0B524DB"));

}