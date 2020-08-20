// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#include "Input/Reply.h"

struct FLiveLinkXRSettings;

DECLARE_DELEGATE_OneParam(FOnLiveLinkXRSourceSettingAccepted, FLiveLinkXRSettings);

class SLiveLinkXRSourceFactory : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SLiveLinkXRSourceFactory)
	{}
		SLATE_EVENT(FOnLiveLinkXRSourceSettingAccepted, OnSourceSettingAccepted)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);


private:

	typedef SLiveLinkXRSourceFactory ThisClass;

	FReply OnSettingAccepted();
	FOnLiveLinkXRSourceSettingAccepted OnSourceSettingAccepted;
};