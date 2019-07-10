// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkSourceFactory.h"


TSharedPtr<SWidget> ULiveLinkSourceFactory::BuildCreationPanel(FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const
{
	return TSharedPtr<SWidget>();
}

TSharedPtr<SWidget> ULiveLinkSourceFactory::CreateSourceCreationPanel()
{
	return TSharedPtr<SWidget>();
}

TSharedPtr<ILiveLinkSource> ULiveLinkSourceFactory::OnSourceCreationPanelClosed(bool bMakeSource)
{
	return TSharedPtr<ILiveLinkSource>();
}
