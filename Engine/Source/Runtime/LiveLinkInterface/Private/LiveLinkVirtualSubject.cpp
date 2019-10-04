// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkVirtualSubject.h"

#include "ILiveLinkClient.h"
#include "LiveLinkFrameTranslator.h"


void ULiveLinkVirtualSubject::Initialize(FLiveLinkSubjectKey InSubjectKey, TSubclassOf<ULiveLinkRole> InRole, ILiveLinkClient* InLiveLinkClient)
{
	// The role for Virtual Subject should already be defined in the constructor of the default object.
	//It it used by the FLiveLinkRoleTrait to found the available Virtual Subject
	check(Role == InRole);

	SubjectKey = InSubjectKey;
	LiveLinkClient = InLiveLinkClient;
}

void ULiveLinkVirtualSubject::Update()
{
	// Invalid the snapshot
	FrameSnapshot.StaticData.Reset();
	FrameSnapshot.FrameData.Reset();

	// Create the new translator for this frame
	CurrentFrameTranslators.Reset();
	for (ULiveLinkFrameTranslator* Translator : FrameTranslators)
	{
		if (Translator)
		{
			ULiveLinkFrameTranslator::FWorkerSharedPtr NewTranslator = Translator->FetchWorker();
			if (NewTranslator.IsValid())
			{
				CurrentFrameTranslators.Add(NewTranslator);
			}
		}
	}
}


void ULiveLinkVirtualSubject::ClearFrames()
{
	FrameSnapshot.StaticData.Reset();
}


bool ULiveLinkVirtualSubject::HasValidFrameSnapshot() const
{
	return FrameSnapshot.StaticData.IsValid() && FrameSnapshot.FrameData.IsValid();
}

TArray<FLiveLinkTime> ULiveLinkVirtualSubject::GetFrameTimes() const
{
	if (!HasValidFrameSnapshot())
	{
		return TArray<FLiveLinkTime>();
	}

	TArray<FLiveLinkTime> Result;
	Result.Emplace(FrameSnapshot.FrameData.GetBaseData()->WorldTime.GetOffsettedTime(), FrameSnapshot.FrameData.GetBaseData()->MetaData.SceneTime);
	return Result;
}

bool ULiveLinkVirtualSubject::DependsOnSubject(FName SubjectName) const
{
	return Subjects.Contains(SubjectName);
}
