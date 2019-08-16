// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ILiveLinkSubject.h"

#include "ILiveLinkClient.h"
#include "LiveLinkFrameTranslator.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiveLinkSubject, Warning, All);


bool ILiveLinkSubject::EvaluateFrame(TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	TSubclassOf<ULiveLinkRole> Role = GetRole();
	if (Role == nullptr)
	{
		UE_LOG(LogLiveLinkSubject, Warning, TEXT("Can't evaluate frame for subject %s. No role has been set yet."), *GetSubjectKey().SubjectName.ToString());
		return false;
	}

	if (InDesiredRole == nullptr)
	{
		UE_LOG(LogLiveLinkSubject, Warning, TEXT("Can't evaluate frame for subject '%s'. Invalid role was received for evaluation."), *GetSubjectKey().SubjectName.ToString());
		return false;
	}

	if (!HasValidFrameSnapshot())
	{
		UE_LOG(LogLiveLinkSubject, Verbose, TEXT("Can't evaluate frame for subject '%s'. No data was available."), *GetSubjectKey().SubjectName.ToString());
		return false;
	}

	if (Role == InDesiredRole || Role->IsChildOf(InDesiredRole))
	{
		//Copy the current snapshot over
		OutFrame.StaticData.InitializeWith(GetFrameSnapshot().StaticData);
		OutFrame.FrameData.InitializeWith(GetFrameSnapshot().FrameData);
		return true;
	}

	const bool bSuccess = Translate(this, InDesiredRole, GetFrameSnapshot().StaticData, GetFrameSnapshot().FrameData, OutFrame);
	if (!bSuccess)
	{
		UE_LOG(LogLiveLinkSubject, Verbose, TEXT("Can't evaluate frame for subject '%s' for incompatible role '%s. Subject has the role '%s' and no translators could work."), *GetSubjectKey().SubjectName.ToString(), *InDesiredRole->GetName(), *Role->GetName());
	}

	return bSuccess;
}


bool ILiveLinkSubject::SupportsRole(TSubclassOf<ULiveLinkRole> InDesiredRole) const
{
	if (GetRole() == InDesiredRole || GetRole()->IsChildOf(InDesiredRole))
	{
		return true;
	}

	for (ULiveLinkFrameTranslator::FWorkerSharedPtr Translator : GetFrameTranslators())
	{
		check(Translator.IsValid());
		if (Translator->CanTranslate(InDesiredRole))
		{
			return true;
		}
	}

	return false;
}


bool ILiveLinkSubject::HasValidFrameSnapshot() const
{
	const FLiveLinkSubjectFrameData& FrameSnapshot = GetFrameSnapshot();
	return FrameSnapshot.StaticData.IsValid() && FrameSnapshot.FrameData.IsValid();
}


bool ILiveLinkSubject::Translate(const ILiveLinkSubject* InLinkSubject, TSubclassOf<ULiveLinkRole> InDesiredRole, const FLiveLinkStaticDataStruct& InStaticData, const FLiveLinkFrameDataStruct& InFrameData, FLiveLinkSubjectFrameData& OutFrame)
{
	// Find one that matches exactly
	bool bFound = false;
	for (ULiveLinkFrameTranslator::FWorkerSharedPtr Translator : InLinkSubject->GetFrameTranslators())
	{
		check(Translator.IsValid());
		if (Translator->GetToRole() == InDesiredRole)
		{
			Translator->Translate(InStaticData, InFrameData, OutFrame);
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		for (ULiveLinkFrameTranslator::FWorkerSharedPtr Translator : InLinkSubject->GetFrameTranslators())
		{
			if (Translator->CanTranslate(InDesiredRole))
			{
				Translator->Translate(InStaticData, InFrameData, OutFrame);
				bFound = true;
				break;
			}
		}
	}

	return bFound;
}
