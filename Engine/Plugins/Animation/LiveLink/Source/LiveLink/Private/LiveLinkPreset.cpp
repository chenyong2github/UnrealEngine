// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkPreset.h"
#include "Features/IModularFeatures.h"
#include "HAL/IConsoleManager.h"
#include "LiveLinkClient.h"
#include "LiveLinkLog.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	static FAutoConsoleCommand LiveLinkPresetApplyCmd(
		TEXT("LiveLink.Preset.Apply"),
		TEXT("Apply a LiveLinkPreset. Use: LiveLink.Preset.Apply Preset=/Game/Folder/MyLiveLinkPreset.MyLiveLinkPreset"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				for (const FString& Element : Args)
				{
					const TCHAR* PresetStr = TEXT("Preset=");
					int32 FoundElement = Element.Find(PresetStr);
					if (FoundElement != INDEX_NONE)
					{
						UObject* Object = StaticLoadObject(ULiveLinkPreset::StaticClass(), nullptr, *Element + FoundElement + FCString::Strlen(PresetStr));
						if (Object)
						{
							CastChecked<ULiveLinkPreset>(Object)->ApplyToClient();
						}
					}
				}
			})
		);

	static FAutoConsoleCommand LiveLinkPresetAddCmd(
		TEXT("LiveLink.Preset.Add"),
		TEXT("Add a LiveLinkPreset. Use: LiveLink.Preset.Add Preset=/Game/Folder/MyLiveLinkPreset.MyLiveLinkPreset"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				for (const FString& Element : Args)
				{
					const TCHAR* PresetStr = TEXT("Preset=");
					int32 FoundElement = Element.Find(PresetStr);
					if (FoundElement != INDEX_NONE)
					{
						UObject* Object = StaticLoadObject(ULiveLinkPreset::StaticClass(), nullptr, *Element + FoundElement + FCString::Strlen(PresetStr));
						if (Object)
						{
							CastChecked<ULiveLinkPreset>(Object)->AddToClient();
						}
					}
				}
			})
		);
}

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
			bResult &= LiveLinkClient.CreateSource(SourcePreset);
		}

		for (const FLiveLinkSubjectPreset& SubjectPreset : Subjects)
		{
			bResult &= LiveLinkClient.CreateSubject(SubjectPreset);
		}
	}

	if (bResult)
	{
		FLiveLinkLog::Info(TEXT("Applied '%s'"), *GetFullName());
	}
	else
	{
		FLiveLinkLog::Error(TEXT("Could not apply '%s'"), *GetFullName());
	}

	return bResult;
}

bool ULiveLinkPreset::AddToClient(const bool bRecreatePresets) const
{
	bool bResult = false;
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		FLiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<FLiveLinkClient>(ILiveLinkClient::ModularFeatureName);

		TArray<FGuid> AllSources;
		AllSources.Append(LiveLinkClient.GetSources());
		AllSources.Append(LiveLinkClient.GetVirtualSources());
		TArray<FLiveLinkSubjectKey> AllSubjects = LiveLinkClient.GetSubjects(true, true);

		TSet<FGuid> FoundSources;
		TSet<FLiveLinkSubjectKey> FoundSubjects;

		for (const FLiveLinkSourcePreset& SourcePreset : Sources)
		{
			if (AllSources.Contains(SourcePreset.Guid))
			{
				if (bRecreatePresets)
				{
					LiveLinkClient.RemoveSource(SourcePreset.Guid);
				}
				else
				{
					FoundSources.Add(SourcePreset.Guid);
				}
			}
		}

		for (const FLiveLinkSubjectPreset& SubjectPreset : Subjects)
		{
			if (AllSubjects.Contains(SubjectPreset.Key))
			{
				if (bRecreatePresets)
				{
					LiveLinkClient.RemoveSubject_AnyThread(SubjectPreset.Key);
				}
				else
				{
					FoundSubjects.Add(SubjectPreset.Key);
				}
			}
		}

		LiveLinkClient.Tick();

		bResult = true;
		for (const FLiveLinkSourcePreset& SourcePreset : Sources)
		{
			bResult &= (FoundSources.Contains(SourcePreset.Guid) || LiveLinkClient.CreateSource(SourcePreset));
		}

		for (const FLiveLinkSubjectPreset& SubjectPreset : Subjects)
		{
			bResult &= (FoundSubjects.Contains(SubjectPreset.Key) || LiveLinkClient.CreateSubject(SubjectPreset));
		}
	}

	if (bResult)
	{
		FLiveLinkLog::Info(TEXT("Added '%s'"), *GetFullName());
	}
	else
	{
		FLiveLinkLog::Error(TEXT("Could not add '%s'"), *GetFullName());
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

		for (FGuid SourceGuid : LiveLinkClient.GetVirtualSources())
		{
			FLiveLinkSourcePreset NewPreset = LiveLinkClient.GetSourcePreset(SourceGuid, this);
			if (NewPreset.Guid.IsValid())
			{
				Sources.Add(MoveTemp(NewPreset));
			}
		}

		for (FLiveLinkSubjectKey SubjectKey : LiveLinkClient.GetSubjects(true, true))
		{
			Subjects.Add(LiveLinkClient.GetSubjectPreset(SubjectKey, this));
		}
	}
}
