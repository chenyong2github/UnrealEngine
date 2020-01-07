// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkSourceCollection.h"

#include "ILiveLinkSource.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkSubjectSettings.h"


// Completely empty "source" that virtual subjects can hang off
struct FLiveLinkVirtualSubjectSource : public ILiveLinkSource
{
	virtual bool CanBeDisplayedInUI() const override { return false; }
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override {}
	virtual bool IsSourceStillValid() const override { return true; }
	virtual bool RequestSourceShutdown() override { return true; }

	virtual FText GetSourceType() const override { return NSLOCTEXT("TempLocTextLiveLink", "LiveLinkVirtualSubjectName", "Virtual Subjects"); }
	virtual FText GetSourceMachineName() const override { return FText(); }
	virtual FText GetSourceStatus() const override { return FText(); }
};

bool FLiveLinkCollectionSourceItem::IsVirtualSource() const
{
	return Guid == FLiveLinkSourceCollection::VirtualSubjectGuid;
}

FLiveLinkCollectionSubjectItem::FLiveLinkCollectionSubjectItem(FLiveLinkSubjectKey InKey, TUniquePtr<FLiveLinkSubject> InLiveSubject, ULiveLinkSubjectSettings* InSettings, bool bInEnabled)
	: Key(InKey)
	, bEnabled(bInEnabled)
	, bPendingKill(false)
	, Setting(InSettings)
	, LiveSubject(MoveTemp(InLiveSubject))
	, VirtualSubject(nullptr)
{
}

FLiveLinkCollectionSubjectItem::FLiveLinkCollectionSubjectItem(FLiveLinkSubjectName InSubjectName, ULiveLinkVirtualSubject* InVirtualSubject, bool bInEnabled)
	: bEnabled(bInEnabled)
	, bPendingKill(false)
	, Setting(nullptr)
	, VirtualSubject(InVirtualSubject)
{
	Key.Source = FLiveLinkSourceCollection::VirtualSubjectGuid;
	Key.SubjectName = InSubjectName;
}


const FGuid FLiveLinkSourceCollection::VirtualSubjectGuid{ 0x4ed2dc4e, 0xcc5911ce, 0x4af0635d, 0xa8b24a5a };


FLiveLinkSourceCollection::FLiveLinkSourceCollection()
{
	FLiveLinkCollectionSourceItem Data;
	Data.Source = MakeShared<FLiveLinkVirtualSubjectSource>();
	Data.Guid = VirtualSubjectGuid;
	Data.Setting = nullptr;

	Sources.Add(Data);
}

void FLiveLinkSourceCollection::AddReferencedObjects(FReferenceCollector & Collector)
{
	for (FLiveLinkCollectionSourceItem& Item : Sources)
	{
		Collector.AddReferencedObject(Item.Setting);
	}

	for (FLiveLinkCollectionSubjectItem& Item : Subjects)
	{
		Collector.AddReferencedObject(Item.VirtualSubject);
		Collector.AddReferencedObject(Item.Setting);
	}
}

void FLiveLinkSourceCollection::AddSource(FLiveLinkCollectionSourceItem InSource)
{
	Sources.Add(InSource);
	OnLiveLinkSourceAdded().Broadcast(InSource.Guid);
	OnLiveLinkSourcesChanged().Broadcast();
}


void FLiveLinkSourceCollection::RemoveSource(FGuid InSourceGuid)
{
	if (InSourceGuid != FLiveLinkSourceCollection::VirtualSubjectGuid)
	{
		int32 SourceIndex = Sources.IndexOfByPredicate([InSourceGuid](const FLiveLinkCollectionSourceItem& Other) { return Other.Guid == InSourceGuid; });
		if (SourceIndex != INDEX_NONE)
		{
			bool bRemovedSubject = false;
			FGuid SourceGuid = Sources[SourceIndex].Guid;
			for (int32 SubjectIndex = Subjects.Num() - 1; SubjectIndex >= 0; --SubjectIndex)
			{
				if (Subjects[SubjectIndex].Key.Source == SourceGuid)
				{
					bRemovedSubject = true;
					FLiveLinkSubjectKey Key = Subjects[SubjectIndex].Key;
					Subjects.RemoveAtSwap(SubjectIndex);
					OnLiveLinkSubjectRemoved().Broadcast(Key);
				}
			}

			if (bRemovedSubject)
			{
				OnLiveLinkSubjectsChanged().Broadcast();
			}

			Sources.RemoveAtSwap(SourceIndex);
			OnLiveLinkSourceRemoved().Broadcast(InSourceGuid);
			OnLiveLinkSourcesChanged().Broadcast();
		}
	}
}


void FLiveLinkSourceCollection::RemoveAllSources()
{
	const bool bHasSubjects = Subjects.Num() > 0;
	for (int32 Index = Subjects.Num() - 1; Index >= 0; --Index)
	{
		FLiveLinkSubjectKey Key = Subjects[Index].Key;
		Subjects.RemoveAtSwap(Index);
		OnLiveLinkSubjectRemoved().Broadcast(Key);
	}
	if (bHasSubjects)
	{
		OnLiveLinkSubjectsChanged().Broadcast();
	}

	bool bHasRemovedSource = false;
	for (int32 Index = Sources.Num() - 1; Index >= 0; --Index)
	{
		if (Sources[Index].Guid != FLiveLinkSourceCollection::VirtualSubjectGuid)
		{
			bHasRemovedSource = true;
			FGuid Key = Sources[Index].Guid;
			Sources.RemoveAtSwap(Index);
			OnLiveLinkSourceRemoved().Broadcast(Key);
		}
	}
	if (bHasRemovedSource)
	{
		OnLiveLinkSourcesChanged().Broadcast();
	}
}


FLiveLinkCollectionSourceItem* FLiveLinkSourceCollection::FindSource(TSharedPtr<ILiveLinkSource> InSource)
{
	return Sources.FindByPredicate([InSource](const FLiveLinkCollectionSourceItem& Other) { return Other.Source == InSource; });
}


const FLiveLinkCollectionSourceItem* FLiveLinkSourceCollection::FindSource(TSharedPtr<ILiveLinkSource> InSource) const
{
	return const_cast<FLiveLinkSourceCollection*>(this)->FindSource(InSource);
}


FLiveLinkCollectionSourceItem* FLiveLinkSourceCollection::FindSource(FGuid InSourceGuid)
{
	return Sources.FindByPredicate([InSourceGuid](const FLiveLinkCollectionSourceItem& Other) { return Other.Guid == InSourceGuid; });
}


const FLiveLinkCollectionSourceItem* FLiveLinkSourceCollection::FindSource(FGuid InSourceGuid) const
{
	return const_cast<FLiveLinkSourceCollection*>(this)->FindSource(InSourceGuid);
}


void FLiveLinkSourceCollection::AddSubject(FLiveLinkCollectionSubjectItem InSubject)
{
	Subjects.Add(MoveTemp(InSubject));
	OnLiveLinkSubjectAdded().Broadcast(InSubject.Key);
	OnLiveLinkSubjectsChanged().Broadcast();
}


void FLiveLinkSourceCollection::RemoveSubject(FLiveLinkSubjectKey InSubjectKey)
{
	int32 IndexOf = Subjects.IndexOfByPredicate([InSubjectKey](const FLiveLinkCollectionSubjectItem& Other) { return Other.Key == InSubjectKey; });
	if (IndexOf != INDEX_NONE)
	{
		Subjects.RemoveAtSwap(IndexOf);
		OnLiveLinkSubjectRemoved().Broadcast(InSubjectKey);
		OnLiveLinkSubjectsChanged().Broadcast();
	}
}


FLiveLinkCollectionSubjectItem* FLiveLinkSourceCollection::FindSubject(FLiveLinkSubjectKey InSubjectKey)
{
	return Subjects.FindByPredicate([InSubjectKey](const FLiveLinkCollectionSubjectItem& Other) { return Other.Key == InSubjectKey; });
}


const FLiveLinkCollectionSubjectItem* FLiveLinkSourceCollection::FindSubject(FLiveLinkSubjectKey InSubjectKey) const
{
	return const_cast<FLiveLinkSourceCollection*>(this)->FindSubject(InSubjectKey);
}


const FLiveLinkCollectionSubjectItem* FLiveLinkSourceCollection::FindEnabledSubject(FLiveLinkSubjectName InSubjectName) const
{
	return Subjects.FindByPredicate([InSubjectName](const FLiveLinkCollectionSubjectItem& Other) { return Other.Key.SubjectName == InSubjectName && Other.bEnabled; });
}


bool FLiveLinkSourceCollection::IsSubjectEnabled(FLiveLinkSubjectKey InSubjectKey) const
{
	if (const FLiveLinkCollectionSubjectItem* Item = FindSubject(InSubjectKey))
	{
		return Item->bEnabled;
	}
	return false;
}


void FLiveLinkSourceCollection::SetSubjectEnabled(FLiveLinkSubjectKey InSubjectKey, bool bEnabled)
{
	if (bEnabled)
	{
		// clear all bEnabled only if found
		if (FLiveLinkCollectionSubjectItem* NewEnabledItem = FindSubject(InSubjectKey))
		{
			NewEnabledItem->bEnabled = true;

			for (FLiveLinkCollectionSubjectItem& SubjectItem : Subjects)
			{
				if (SubjectItem.bEnabled && SubjectItem.Key.SubjectName == InSubjectKey.SubjectName && !(SubjectItem.Key == InSubjectKey))
				{
					SubjectItem.bEnabled = false;
				}
			}
		}
	}
	else
	{
		for (FLiveLinkCollectionSubjectItem& SubjectItem : Subjects)
		{
			if (SubjectItem.Key.SubjectName == InSubjectKey.SubjectName)
			{
				SubjectItem.bEnabled = false;
			}
		}
	}
}

void FLiveLinkSourceCollection::RemovePendingKill()
{
	// Remove Sources that are pending kill
	for (int32 SourceIndex = Sources.Num() - 1; SourceIndex >= 0; --SourceIndex)
	{
		FLiveLinkCollectionSourceItem& SourceItem = Sources[SourceIndex];
		if (SourceItem.bPendingKill)
		{
			if (SourceItem.IsVirtualSource())
			{
				// Keep the source but mark the subject as pending kill
				for (FLiveLinkCollectionSubjectItem& SubjectItem : Subjects)
				{
					if (SubjectItem.Key.Source == FLiveLinkSourceCollection::VirtualSubjectGuid)
					{
						SubjectItem.bPendingKill = true;
					}
				}
				SourceItem.bPendingKill = false;
			}
			else if (SourceItem.Source->RequestSourceShutdown())
			{
				RemoveSource(SourceItem.Guid);
			}
		}
	}

	// Remove Subjects that are pending kill
	for (int32 SubjectIndex = Subjects.Num() - 1; SubjectIndex >= 0; --SubjectIndex)
	{
		const FLiveLinkCollectionSubjectItem& SubjectItem = Subjects[SubjectIndex];
		if (SubjectItem.bPendingKill)
		{
			RemoveSubject(SubjectItem.Key);
		}
	}
}

bool FLiveLinkSourceCollection::RequestShutdown()
{
	bool bHadSubject = Subjects.Num() > 0;
	Subjects.Reset();

	for (int32 SourceIndex = Sources.Num() - 1; SourceIndex >= 0; --SourceIndex)
	{
		FLiveLinkCollectionSourceItem& SourceItem = Sources[SourceIndex];
		if (SourceItem.Source->RequestSourceShutdown())
		{
			Sources.RemoveAtSwap(SourceIndex);
		}
	}

	// No callback when we shutdown

	return Sources.Num() == 0;
}
