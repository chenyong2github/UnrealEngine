// Copyright Epic Games, Inc. All Rights Reserved.
#include "FSwarmCommentsAPI.h"

#include "HttpModule.h"
#include "ISourceControlModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

THIRD_PARTY_INCLUDES_START
// used to retrieve the swarm url from p4
#include <p4/clientapi.h>
THIRD_PARTY_INCLUDES_END

FSwarmCommentsAPI::FSwarmCommentsAPI()
	: AuthTicket(RetrieveAuthorizationTicket())
	, SwarmURL(RetrieveSwarmURL())
{
}

FString FSwarmCommentsAPI::GetUsername() const
{
	return AuthTicket.Username;
}

void FSwarmCommentsAPI::GetComments(const FReviewTopic& Topic, const OnGetCommentsComplete& OnComplete) const
{
    const TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	HttpRequest->SetHeader(TEXT("Authorization"), AuthTicket);
	
    HttpRequest->SetURL(FString::Format(TEXT("{0}?topic={1}&max={2}"),
    	{
    		CommentsURL(),
    		Topic.ToString(), // filter to comments in a specific review
    		5000 // limit to 5k comments. This should obviously be more than enough and loads at a reasonable rate
    	}));
    HttpRequest->SetVerb(TEXT("GET"));
	
	HttpRequest->OnProcessRequestComplete().BindLambda([OnComplete, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!bConnectedSuccessfully)
		{
			OnComplete.ExecuteIfBound({}, TEXT("Connection Failed"));
			return;
		}
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		TSharedPtr<FJsonObject> JsonObject;
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
		{
			if (JsonObject->HasField("error"))
			{
				OnComplete.ExecuteIfBound({}, JsonObject->GetStringField("error"));
				return;
			}
			if (JsonObject->HasField("comments"))
			{
				TArray<FReviewComment> Comments;
				const TArray<TSharedPtr<FJsonValue>> CommentsJson = JsonObject->GetArrayField("comments");
 				for (const TSharedPtr<FJsonValue>& CommentJson : CommentsJson)
				{
 					FReviewComment Comment = FReviewComment::FromJson(CommentJson->AsObject());
 					TakeMetadataFromBody(Comment);
					Comments.Add(Comment);
				}
				OnComplete.ExecuteIfBound(MoveTemp(Comments), {});
				return;
			}
		}
		OnComplete.ExecuteIfBound({}, TEXT("Malformed Response"));
	});
		
	
	HttpRequest->ProcessRequest();
}

void FSwarmCommentsAPI::PostComment(FReviewComment& Comment, const OnPostCommentComplete& OnComplete) const
{
	const TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	HttpRequest->SetHeader(TEXT("Authorization"), AuthTicket);
	HttpRequest->SetURL(CommentsURL());
	HttpRequest->SetVerb(TEXT("POST"));

	Comment.User = AuthTicket.Username;
	FReviewComment CleanedComment = Comment;
	PutMetadataInBody(CleanedComment);
	HttpRequest->SetContentAsString(CleanedComment.ToJson());
	
	HttpRequest->OnProcessRequestComplete().BindLambda([OnComplete, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!bConnectedSuccessfully)
		{
			OnComplete.ExecuteIfBound({}, TEXT("Connection Failed"));
			return;
		}
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		TSharedPtr<FJsonObject> JsonObject;
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
		{
			if (JsonObject->HasField("error"))
			{
				OnComplete.ExecuteIfBound({}, JsonObject->GetStringField("error"));
				return;
			}
			if (JsonObject->HasField("comment"))
			{
				FReviewComment Comment = FReviewComment::FromJson(JsonObject->GetObjectField("comment"));
				TakeMetadataFromBody(Comment);
				OnComplete.ExecuteIfBound(Comment, {});
				return;
			}
		}
		OnComplete.ExecuteIfBound({}, TEXT("Malformed Response"));
	});
	
	HttpRequest->ProcessRequest();
}

void FSwarmCommentsAPI::EditComment(const FReviewComment& Comment, const OnEditCommentComplete& OnComplete) const
{
	const TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	HttpRequest->SetHeader(TEXT("Authorization"), AuthTicket);
	const FString CommentID = FString::FromInt(Comment.CommentID.GetValue());
	HttpRequest->SetURL(CommentsURL() / CommentID);
	HttpRequest->SetVerb(TEXT("PATCH"));
	
	FReviewComment CleanedComment = Comment;
	PutMetadataInBody(CleanedComment);
	HttpRequest->SetContentAsString(CleanedComment.ToJson());
	HttpRequest->OnProcessRequestComplete().BindLambda([OnComplete, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!bConnectedSuccessfully)
		{
			OnComplete.ExecuteIfBound({}, TEXT("Connection Failed"));
			return;
		}
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		TSharedPtr<FJsonObject> JsonObject;
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
		{
			if (JsonObject->HasField("error"))
			{
				OnComplete.ExecuteIfBound({}, JsonObject->GetStringField("error"));
				return;
			}
			if (JsonObject->HasField("comment"))
			{
				OnComplete.ExecuteIfBound(FReviewComment::FromJson(JsonObject->GetObjectField("comment")), {});
				return;
			}
		}
		OnComplete.ExecuteIfBound({}, TEXT("Malformed Response"));
	});
	
	HttpRequest->ProcessRequest();
}

void FSwarmCommentsAPI::GetReviewTopicForCL(const FString& ChangelistNum,
	const OnGetReviewTopicForCLComplete& OnComplete) const
{
	const TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	HttpRequest->SetHeader(TEXT("Authorization"), AuthTicket);
	
	HttpRequest->SetURL(FString::Format(TEXT("{0}?change={1}&max={2}"),
		{
			ReviewsURL(),
			ChangelistNum, // get the review for a specific CL
			1 // we only want a single review.
		}));
	HttpRequest->SetVerb(TEXT("GET"));
	
	HttpRequest->OnProcessRequestComplete().BindLambda([OnComplete, ChangelistNum, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!bConnectedSuccessfully)
		{
			OnComplete.ExecuteIfBound({}, TEXT("Connection Failed"));
			return;
		}
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		TSharedPtr<FJsonObject> JsonObject;
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
		{
			if (JsonObject->HasField("error"))
			{
				OnComplete.ExecuteIfBound({}, JsonObject->GetStringField("error"));
				return;
			}
			if (JsonObject->HasField("reviews"))
			{
				const TArray<TSharedPtr<FJsonValue>> ReviewsJson = JsonObject->GetArrayField("reviews");
				if (ReviewsJson.IsEmpty())
				{
					CreateReviewTopicForCL(ChangelistNum, OnComplete);
					return;
				}
				
				const TSharedPtr<FJsonObject> Review = ReviewsJson[0]->AsObject();
				
				OnComplete.ExecuteIfBound(FReviewTopic{
					FString::FromInt(Review->GetIntegerField(TEXT("id"))),
					EReviewTopicType::Review
				}, {});
				return;
			}
		}
		
		OnComplete.ExecuteIfBound({}, TEXT("Malformed Response"));
	});
	
	HttpRequest->ProcessRequest();
}

void FSwarmCommentsAPI::CreateReviewTopicForCL(const FString& ChangelistNum,
	const OnGetReviewTopicForCLComplete& OnComplete) const
{
	
	const TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	HttpRequest->SetHeader(TEXT("Authorization"), AuthTicket);
	
	HttpRequest->SetURL(ReviewsURL());
	HttpRequest->SetContentAsString(FString::Format(TEXT("{\"change\":{0}}"), {ChangelistNum}));
	HttpRequest->SetVerb(TEXT("POST"));
	
	HttpRequest->OnProcessRequestComplete().BindLambda([OnComplete, ChangelistNum, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
	{
		if (!bConnectedSuccessfully)
		{
			OnComplete.ExecuteIfBound({}, TEXT("Connection Failed"));
			return;
		}
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		TSharedPtr<FJsonObject> JsonObject;
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
		{
			if (JsonObject->HasField("error"))
			{
				OnComplete.ExecuteIfBound({}, JsonObject->GetStringField("error"));
				return;
			}
			if (JsonObject->HasField("review"))
			{
				const TSharedPtr<FJsonObject> ReviewJson = JsonObject->GetObjectField("review");
				
				OnComplete.ExecuteIfBound(FReviewTopic{
					FString::FromInt(ReviewJson->GetIntegerField(TEXT("id"))),
					EReviewTopicType::Review
				}, {});
				return;
			}
		}
		
		OnComplete.ExecuteIfBound({}, TEXT("Malformed Response"));
	});
	
	HttpRequest->ProcessRequest();
}

FString FSwarmCommentsAPI::CommentsURL() const
{
	return SwarmURL.IsEmpty() ? FString{} : SwarmURL / TEXT("api/v9/comments");
}

FString FSwarmCommentsAPI::ReviewsURL() const
{
	return SwarmURL.IsEmpty() ? FString{} : SwarmURL / TEXT("api/v9/reviews");
}


static TMap<FString, FString> ParseReviewCommentMetadata(const FString& Comment)
{
	TMap<FString, FString> Result;
	FString Body, Metadata;
	if (!Comment.Split(TEXT("[metadata]"), &Body, &Metadata))
	{
		return {{TEXT("Body"), Comment.TrimEnd()}};
	}
	
	Result.Add(TEXT("Body"), Body.TrimEnd());
	
	TArray<FString> MetaDataLines;
	Metadata.ParseIntoArrayLines(MetaDataLines);
	for (const FString& Line : MetaDataLines)
	{
		const int32 KeyEnd = Line.Find(TEXT("="), ESearchCase::CaseSensitive, ESearchDir::FromStart);
		if (KeyEnd == INDEX_NONE)
		{
			continue;
		}
		FString Key = Line.Left(KeyEnd);
		
		const int32 ValBegin = KeyEnd + 1; // skip '=' char
		const int32 ValLen = Line.Len() - ValBegin; // extend to end of line
		const FString Val = Line.Mid(ValBegin, ValLen);
		Result.Add(Key, Val);
	}
	return Result;
}

static TMap<FString, FString> ParseReviewCommentMetadata(const FReviewComment& Comment)
{
	return ParseReviewCommentMetadata(Comment.Body.Get({}));
}

static void SetReviewCommentMetadata(FReviewComment& Comment, TMap<FString, FString> Metadata)
{
	FString CommentText = Metadata["Body"];
	if (Metadata.Num() == 1)
	{
		Comment.Body = CommentText;
		return;
	}
	
	CommentText += TEXT("\n\n[metadata]\n");
	for (auto&[Key,Value] : Metadata)
	{
		if (Key != "Body")
		{
			CommentText += FString::Format(TEXT("{0}={1}\n"), {Key, Value});
		}
	}
	Comment.Body = CommentText;
}

void FSwarmCommentsAPI::PutMetadataInBody(FReviewComment& Comment)
{
	TMap<FString, FString> Metadata = ParseReviewCommentMetadata(Comment);
	if (Comment.Context.File.IsSet())
	{
		Metadata.FindOrAdd(TEXT("File"), Comment.Context.File.GetValue());
		Comment.Context.File.Reset();
	}
	if (Comment.Context.ReplyTo.IsSet())
	{
		Metadata.FindOrAdd(TEXT("ReplyTo"), FString::FromInt(Comment.Context.ReplyTo.GetValue()));
		Comment.Context.ReplyTo.Reset();
	}
	if (Comment.Context.Category.IsSet())
	{
		Metadata.FindOrAdd(TEXT("Category"), Comment.Context.Category.GetValue());
		Comment.Context.Category.Reset();
	}
	if (Comment.Likes.IsSet())
	{
		FString LikesString;
		TSet<FString> &Likes = Comment.Likes.GetValue();
		if (!Likes.IsEmpty())
		{
			int32 Index = 0;
			for (const FString& User : Likes)
			{
				LikesString += User;
				if (Index != Likes.Num() - 1)
				{
					LikesString += TEXT(",");
				}
				++Index;
			}
			Metadata.FindOrAdd(TEXT("Likes"), LikesString);
		}
		else
		{
			// if Likes is set but it's empty, explicitly remove it from the metadata
			Metadata.Remove(TEXT("Likes"));
		}
	}
	SetReviewCommentMetadata(Comment, Metadata);
}

void FSwarmCommentsAPI::TakeMetadataFromBody(FReviewComment& Comment)
{
	// because swarm has very limited support for certain properties, they may be stored inside the body
	// as a workaround. parse them out and put them in their proper place.
	TMap<FString, FString> Metadata = ParseReviewCommentMetadata(Comment);
	if (const FString* File = Metadata.Find(TEXT("File")))
	{
		Comment.Context.File = *File;
	}
	if (const FString* ReplyTo = Metadata.Find(TEXT("ReplyTo")))
	{
		Comment.Context.ReplyTo = FCString::Atoi(**ReplyTo);
	}
	if (const FString* Category = Metadata.Find(TEXT("Category")))
	{
		Comment.Context.Category = *Category;
	}
	if (const FString* Likes = Metadata.Find(TEXT("Likes")))
	{
		Comment.Likes = TSet<FString>();
		TArray<FString> LikesArray;
		Likes->ParseIntoArray(LikesArray, TEXT(","));
		for (const FString& Like : LikesArray)
		{
			Comment.Likes.GetValue().Add(Like);
		}
	}
	Comment.Body = Metadata[TEXT("Body")];
}


static FString GetEnvironmentVariable(const char* Key)
{
	#pragma warning(suppress : 4996) // because I'm immediately copying the result of getenv() into managed memory, it's safe.
	if (const char* PathCStr = std::getenv(Key))
	{
		FString Result = FString(PathCStr);
		FPaths::NormalizeFilename(Result);
		return MoveTemp(Result);
	}
	return {};
}

// retrieves the default directory that p4tickets file is stored in
static FString GetP4TicketsPath()
{
	// if the P4TICKETS environment var is set, use that path
	const FString P4TicketsVar = GetEnvironmentVariable("P4TICKETS");
	if (!P4TicketsVar.IsEmpty())
	{
		return P4TicketsVar;
	}

	// if P4TICKETS wasn't set, default to "%USERPROFILE%\p4tickets.txt" on Windows, or "$HOME/.p4tickets" otherwise
#if PLATFORM_WINDOWS
	return GetEnvironmentVariable("USERPROFILE") / TEXT("p4tickets.txt");
#else
	return GetEnvironmentVariable("HOME") / TEXT(".p4tickets");
#endif
}

FString FSwarmCommentsAPI::RetrieveAuthorizationTicket()
{
	TArray<FString> TicketStrings;
	FFileHelper::LoadFileToStringArray(TicketStrings, *GetP4TicketsPath());
	if (TicketStrings.IsEmpty())
	{
		return {};
	}

	// TODO: @jordan.hoffmann: make a setting to let users choose which user they want to use
	const FString& TicketString = TicketStrings[0];
	
	// find beginning of ticket
	int32 ChopIndex = TicketString.Find(TEXT("p4d1="), ESearchCase::CaseSensitive, ESearchDir::FromStart);
	if (ChopIndex == INDEX_NONE)
	{
		return {};
	}
	
	// skip 'p4d1='
	ChopIndex += 5;
	
	return TicketString.RightChop(ChopIndex);
}


FString FSwarmCommentsAPI::RetrieveSwarmURL()
{
	// Initialize P4Client
	Error P4Error;
	ClientApi P4Client;
	P4Client.Init(&P4Error);
	if (P4Error.Test())
	{
		StrBuf ErrorMsg;
		P4Error.Fmt(&ErrorMsg);
		UE_LOG(LogSourceControl, Error, TEXT("P4ERROR: Invalid connection to server."));
		UE_LOG(LogSourceControl, Error, TEXT("%s"), ANSI_TO_TCHAR(ErrorMsg.Text()));
	}

	// Create a ClientUser that can capture the output from the command
	struct MyClientUser : ClientUser
	{
		virtual void OutputInfo(char level, const char* data) override
		{
			const FString Info(data);
			const int32 Found = Info.Find(TEXT(" = "));
			if (Found != INDEX_NONE)
			{
				Result = Info.RightChop(Found + 3);
			}
		}
		
		virtual void HandleError(Error* P4Error) override
		{
			StrBuf ErrorMsg;
			P4Error->Fmt(&ErrorMsg);
			UE_LOG(LogSourceControl, Error, TEXT("P4ERROR: %s"), (ANSI_TO_TCHAR(ErrorMsg.Text())));
		}

		FString Result;
	} P4User;

	// Run property -l -n P4.Swarm.URL
	const char* ArgV[] = { "-l", "-n", "P4.Swarm.URL" };
	P4Client.SetArgv(3, const_cast<char* const*>(ArgV));
	P4Client.Run("property", &P4User);

	// Cleanup P4Client
	P4Client.Final(&P4Error);
	if (P4Error.Test())
	{
		StrBuf ErrorMsg;
		P4Error.Fmt(&ErrorMsg);
		UE_LOG(LogSourceControl, Error, TEXT("P4ERROR: Failed to disconnect from Server."));
		UE_LOG(LogSourceControl, Error, TEXT("%s"), (ANSI_TO_TCHAR(ErrorMsg.Text())));
	}

	return P4User.Result;
}

FSwarmCommentsAPI::FAuthTicket::FAuthTicket(const FString& InUsername, const FString& InPassword)
	: Username(InUsername), Password(InPassword)
{}

FSwarmCommentsAPI::FAuthTicket::FAuthTicket(const FString& TicketString)
{
	int32 ChopIndex;
	check(TicketString.FindChar(':', ChopIndex));
	Username = TicketString.Left(ChopIndex);
	Password = TicketString.RightChop(ChopIndex + 1);
}

FSwarmCommentsAPI::FAuthTicket::operator FString() const
{
	return TEXT("Basic ") + FBase64::Encode(Username + TEXT(":") + Password);
}
