// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"

class FPixelStreamingLiveLinkSource : public ILiveLinkSource
{
public:
	FPixelStreamingLiveLinkSource();
	// ILiveLinkSource implementation
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual void Update() override;
	virtual bool CanBeDisplayedInUI() const override;
	virtual bool IsSourceStillValid() const override;
	virtual bool RequestSourceShutdown() override;
	virtual FText GetSourceType() const override;
	virtual FText GetSourceMachineName() const override;
	virtual FText GetSourceStatus() const override;
	// Registers a new subject with the Transform Role to the Live Link Client
	// If called with a subject name that already exists in this source then this will reset any buffered data for that subject
	void CreateSubject(FName SubjectName) const;
	void RemoveSubject(FName SubjectName) const;
	void PushTransformForSubject(FName SubjectName, FTransform Transform) const;

private:
	// Cached information for communicating with the live link client
	ILiveLinkClient* LiveLinkClient;
	FGuid SourceGuid;
};
