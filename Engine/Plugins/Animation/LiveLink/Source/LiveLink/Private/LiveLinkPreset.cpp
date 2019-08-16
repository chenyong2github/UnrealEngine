// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkPreset.h"
#include "Features/IModularFeatures.h"
#include "LiveLinkClient.h"

bool ULiveLinkPreset::ApplyToClient() const
{
	bool bResult = false;
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		FLiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<FLiveLinkClient>(ILiveLinkClient::ModularFeatureName);

		LiveLinkClient.RemoveAllSources();
		LiveLinkClient.Tick();

		bResult = true;
		for (const FLiveLinkSourcePreset& SourcePreset : Sources)
		{
			bResult |= LiveLinkClient.CreateSource(SourcePreset);
		}

		for (const FLiveLinkSubjectPreset& SubjectPreset : Subjects)
		{
			bResult |= LiveLinkClient.CreateSubject(SubjectPreset);
		}
	}

	return bResult;
}

void ULiveLinkPreset::BuildFromClient()
{
	Sources.Reset();
	Subjects.Reset();

	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		FLiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<FLiveLinkClient>(ILiveLinkClient::ModularFeatureName);

		for (FGuid SourceGuid : LiveLinkClient.GetSources())
		{
			Sources.Add(LiveLinkClient.GetSourcePreset(SourceGuid, this));
		}

		for (FLiveLinkSubjectKey SubjectKey : LiveLinkClient.GetSubjects(true, true))
		{
			Subjects.Add(LiveLinkClient.GetSubjectPreset(SubjectKey, this));
		}
	}
}
