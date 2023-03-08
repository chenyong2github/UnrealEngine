// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ReviewComments.h"

class FSwarmCommentsAPI : public IReviewCommentAPI
{
public:
	FSwarmCommentsAPI();

	virtual FString GetUsername() const override;

	virtual void GetComments(const FReviewTopic& Topic, const OnGetCommentsComplete& OnComplete) const override;
	// Body must be set
	virtual void PostComment(FReviewComment& Comment, const OnPostCommentComplete& OnComplete) const override;
	// all unset members will be left unchanged.
	virtual void EditComment(const FReviewComment& Comment, const OnEditCommentComplete& OnComplete) const override;
	
	// retrieves the review associated with a CL. If there is none, it creates a new review and returns that.
	virtual void GetReviewTopicForCL(const FString &ChangelistNum, const OnGetReviewTopicForCLComplete& OnComplete) const override;

private:
	void CreateReviewTopicForCL(const FString &ChangelistNum, const OnGetReviewTopicForCLComplete& OnComplete) const;

	FString CommentsURL() const;
	FString ReviewsURL() const;
	
	static void PutMetadataInBody(FReviewComment& Comment);
	static void TakeMetadataFromBody(FReviewComment& Comment);
	
	static FString RetrieveAuthorizationTicket();
	static FString RetrieveSwarmURL();

	// used to authorize http requests to swarm
	struct FAuthTicket
	{
		FAuthTicket() = default;
		FAuthTicket(const FString& Username, const FString& Password);
		FAuthTicket(const FString& TicketString);
		operator FString() const;
		FString Username;
	private:
		FString Password;
	} AuthTicket;

	// base url for all swarm api requests
	FString SwarmURL;
};
