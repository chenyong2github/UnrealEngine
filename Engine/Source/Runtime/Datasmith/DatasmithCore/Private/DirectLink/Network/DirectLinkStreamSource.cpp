// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/Network/DirectLinkStreamSource.h"

#include "DirectLink/Misc.h"
#include "DirectLink/Network/DirectLinkStreamSender.h"

namespace DirectLink
{

void FStreamSource::SetRoot(ISceneGraphNode* InRoot)
{
	Root = InRoot;
}


void FStreamSource::Snapshot()
{
	TSharedPtr<FSceneSnapshot> NewSnapshot = SnapshotScene(Root);

	DumpSceneSnapshot(*NewSnapshot.Get(), TEXT("source"));

	{
		FRWScopeLock _(CurrentSnapshotLock, SLT_Write);
		CurrentSnapshot = NewSnapshot;
	}

	{
		FRWScopeLock _(SendersLock, SLT_ReadOnly);
		for (TSharedPtr<FStreamSender>& Sender : Senders)
		{
			Sender->SetSceneSnapshot(NewSnapshot);
		}
	}
}


void FStreamSource::LinkSender(const TSharedPtr<FStreamSender>& Sender)
{
	if (ensure(Sender))
	{
		{
			FRWScopeLock _(SendersLock, SLT_Write);
			Senders.Add(Sender);
		}

		{
			FRWScopeLock _(CurrentSnapshotLock, SLT_ReadOnly);
			Sender->SetSceneSnapshot(CurrentSnapshot);
		}
	}
}


} // namespace DirectLink

