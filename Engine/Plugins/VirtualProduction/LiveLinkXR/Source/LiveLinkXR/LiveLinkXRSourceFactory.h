// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkSourceFactory.h"
#include "LiveLinkXRSourceSettings.h"
#include "LiveLinkXRSourceFactory.generated.h"

class SLiveLinkXRSourceEditor;

UCLASS()
class ULiveLinkXRSourceFactory : public ULiveLinkSourceFactory
{
public:
	GENERATED_BODY()

	virtual FText GetSourceDisplayName() const;
	virtual FText GetSourceTooltip() const;

	virtual EMenuType GetMenuType() const { return EMenuType::SubPanel; }
	virtual TSharedPtr<SWidget> BuildCreationPanel(FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const;
	virtual TSharedPtr<ILiveLinkSource> CreateSource(const FString& ConnectionString) const override;

private:
	void CreateSourceFromSetting(FLiveLinkXRSettings Setting, FOnLiveLinkSourceCreated OnSourceCreated) const;
};