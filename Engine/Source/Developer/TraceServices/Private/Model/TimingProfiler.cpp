// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/TimingProfiler.h"
#include "Model/TimingProfilerPrivate.h"
#include "AnalysisServicePrivate.h"
#include "Common/StringStore.h"
#include "Common/TimelineStatistics.h"
#include "Templates/TypeHash.h"

namespace Trace
{

struct FTimingProfilerCallstackKey
{
	bool operator==(const FTimingProfilerCallstackKey& Other) const
	{
		return Other.TimerStack == TimerStack;
	}

	friend uint32 GetTypeHash(const FTimingProfilerCallstackKey& Key)
	{
		return Key.Hash;
	}

	TArray<uint32> TimerStack;
	uint32 Hash;
};

class FTimingProfilerButterfly
	: public ITimingProfilerButterfly
{
public:
	FTimingProfilerButterfly();
	virtual ~FTimingProfilerButterfly() = default;
	virtual const FTimingProfilerButterflyNode& GenerateCallersTree(uint32 TimerId) override;
	virtual const FTimingProfilerButterflyNode& GenerateCalleesTree(uint32 TimerId) override;

private:
	void CreateCallersTreeRecursive(const FTimingProfilerButterflyNode* TimerNode, const FTimingProfilerButterflyNode* RootNode, FTimingProfilerButterflyNode* OutputParent);
	void CreateCalleesTreeRecursive(const FTimingProfilerButterflyNode* TimerNode, FTimingProfilerButterflyNode* OutputParent);

	FSlabAllocator Allocator;
	TPagedArray<FTimingProfilerButterflyNode> Nodes;
	TArray<TArray<FTimingProfilerButterflyNode*>> TimerCallstacksMap;
	TMap<uint32, FTimingProfilerButterflyNode*> CachedCallerTrees;
	TMap<uint32, FTimingProfilerButterflyNode*> CachedCalleeTrees;

	friend class FTimingProfilerProvider;
};

FTimingProfilerProvider::FTimingProfilerProvider(IAnalysisSession& InSession)
	: Session(InSession)
{
	Timelines.Add(MakeShared<TimelineInternal>(Session.GetLinearAllocator()));

	AggregatedStatsTableLayout.
		AddColumn<const TCHAR*>([](const FTimingProfilerAggregatedStats& Row)
			{
				return Row.Timer->Name;
			},
			TEXT("Name")).
		AddColumn(&FTimingProfilerAggregatedStats::InstanceCount, TEXT("Count")).
		AddColumn(&FTimingProfilerAggregatedStats::TotalInclusiveTime, TEXT("Incl")).
		AddColumn(&FTimingProfilerAggregatedStats::MinInclusiveTime, TEXT("I.Min")).
		AddColumn(&FTimingProfilerAggregatedStats::MaxInclusiveTime, TEXT("I.Max")).
		AddColumn(&FTimingProfilerAggregatedStats::AverageInclusiveTime, TEXT("I.Avg")).
		AddColumn(&FTimingProfilerAggregatedStats::MedianInclusiveTime, TEXT("I.Med")).
		AddColumn(&FTimingProfilerAggregatedStats::TotalExclusiveTime, TEXT("Excl")).
		AddColumn(&FTimingProfilerAggregatedStats::MinExclusiveTime, TEXT("E.Min")).
		AddColumn(&FTimingProfilerAggregatedStats::MaxExclusiveTime, TEXT("E.Max")).
		AddColumn(&FTimingProfilerAggregatedStats::AverageExclusiveTime, TEXT("E.Avg")).
		AddColumn(&FTimingProfilerAggregatedStats::MedianExclusiveTime, TEXT("E.Med"));
}

FTimingProfilerProvider::~FTimingProfilerProvider()
{
}

uint32 FTimingProfilerProvider::AddCpuTimer(const TCHAR* Name)
{
	Session.WriteAccessCheck();

	FTimingProfilerTimer& Timer = AddTimerInternal(Name, false);
	return Timer.Id;
}

void FTimingProfilerProvider::SetTimerName(uint32 TimerId, const TCHAR* Name)
{
	Session.WriteAccessCheck();
	
	FTimingProfilerTimer& Timer = Timers[TimerId];
	Timer.Name = Session.StoreString(Name);
	uint32 NameHash = 0;
	for (const TCHAR* c = Name; *c; ++c)
	{
		NameHash = (NameHash + *c) * 0x2c2c57ed;
	}
	Timer.NameHash = NameHash;
}

uint32 FTimingProfilerProvider::AddGpuTimer(const TCHAR* Name)
{
	Session.WriteAccessCheck();

	FTimingProfilerTimer& Timer = AddTimerInternal(Name, true);
	return Timer.Id;
}

FTimingProfilerTimer& FTimingProfilerProvider::AddTimerInternal(const TCHAR* Name, bool IsGpuTimer)
{
	FTimingProfilerTimer& Timer = Timers.AddDefaulted_GetRef();
	Timer.Id = Timers.Num() - 1;
	Timer.Name = Session.StoreString(Name);
	uint32 NameHash = 0;
	for (const TCHAR* c = Name; *c; ++c)
	{
		NameHash = (NameHash + *c) * 0x2c2c57ed;
	}
	Timer.NameHash = NameHash;
	Timer.IsGpuTimer = IsGpuTimer;
	return Timer;
}

FTimingProfilerProvider::TimelineInternal& FTimingProfilerProvider::EditCpuThreadTimeline(uint32 ThreadId)
{
	Session.WriteAccessCheck();

	if (!CpuThreadTimelineIndexMap.Contains(ThreadId))
	{
		TSharedRef<TimelineInternal> Timeline = MakeShared<TimelineInternal>(Session.GetLinearAllocator());
		uint32 TimelineIndex = Timelines.Num();
		CpuThreadTimelineIndexMap.Add(ThreadId, TimelineIndex);
		Timelines.Add(Timeline);
		return Timeline.Get();
	}
	else
	{
		uint32 TimelineIndex = CpuThreadTimelineIndexMap[ThreadId];
		return Timelines[TimelineIndex].Get();
	}
}

FTimingProfilerProvider::TimelineInternal& FTimingProfilerProvider::EditGpuTimeline()
{
	Session.WriteAccessCheck();

	return Timelines[GpuTimelineIndex].Get();
}

bool FTimingProfilerProvider::GetCpuThreadTimelineIndex(uint32 ThreadId, uint32& OutTimelineIndex) const
{
	Session.ReadAccessCheck();

	if (CpuThreadTimelineIndexMap.Contains(ThreadId))
	{
		OutTimelineIndex = CpuThreadTimelineIndexMap[ThreadId];
		return true;
	}
	return false;
}

bool FTimingProfilerProvider::GetGpuTimelineIndex(uint32& OutTimelineIndex) const
{
	Session.ReadAccessCheck();

	OutTimelineIndex = GpuTimelineIndex;
	return true;
}

bool FTimingProfilerProvider::ReadTimeline(uint32 Index, TFunctionRef<void(const Timeline &)> Callback) const
{
	Session.ReadAccessCheck();

	if (Index < uint32(Timelines.Num()))
	{
		Callback(*Timelines[Index]);
		return true;
	}
	else
	{
		return false;
	}
}

void FTimingProfilerProvider::EnumerateTimelines(TFunctionRef<void(const Timeline&)> Callback) const
{
	Session.ReadAccessCheck();

	for (const auto& Timeline : Timelines)
	{
		Callback(*Timeline);
	}
}

void FTimingProfilerProvider::ReadTimers(TFunctionRef<void(const FTimingProfilerTimer*, uint64)> Callback) const
{
	Session.ReadAccessCheck();

	Callback(Timers.GetData(), Timers.Num());
}

ITable<FTimingProfilerAggregatedStats>* FTimingProfilerProvider::CreateAggregation(double IntervalStart, double IntervalEnd, TFunctionRef<bool(uint32)> CpuThreadFilter, bool IncludeGpu) const
{
	Session.ReadAccessCheck();

	TArray<const TimelineInternal*> IncludedTimelines;
	if (IncludeGpu)
	{
		IncludedTimelines.Add(&Timelines[GpuTimelineIndex].Get());
	}
	for (const auto& KV : CpuThreadTimelineIndexMap)
	{
		if (CpuThreadFilter(KV.Key))
		{
			IncludedTimelines.Add(&Timelines[KV.Value].Get());
		}
	}

	auto BucketMappingFunc = [this](const TimelineInternal::EventType& Event) -> const FTimingProfilerTimer*
	{
		return &Timers[Event.TimerIndex];
	};

	TMap<const FTimingProfilerTimer*, FAggregatedTimingStats> Aggregation;
	FTimelineStatistics::CreateAggregation(IncludedTimelines, BucketMappingFunc, IntervalStart, IntervalEnd, Aggregation);
	TTable<FTimingProfilerAggregatedStats>* Table = new TTable<FTimingProfilerAggregatedStats>(AggregatedStatsTableLayout);
	for (const auto& KV : Aggregation)
	{
		FTimingProfilerAggregatedStats& Row = Table->AddRow();
		Row.Timer = KV.Key;
		const FAggregatedTimingStats& Stats = KV.Value;
		Row.InstanceCount = Stats.InstanceCount;
		Row.TotalInclusiveTime = Stats.TotalInclusiveTime;
		Row.MinInclusiveTime = Stats.MinInclusiveTime;
		Row.MaxInclusiveTime = Stats.MaxInclusiveTime;
		Row.AverageInclusiveTime = Stats.AverageInclusiveTime;
		Row.MedianInclusiveTime = Stats.MedianInclusiveTime;
		Row.TotalExclusiveTime = Stats.TotalExclusiveTime;
		Row.MinExclusiveTime = Stats.MinExclusiveTime;
		Row.MaxExclusiveTime = Stats.MaxExclusiveTime;
		Row.AverageExclusiveTime = Stats.AverageExclusiveTime;
		Row.MedianExclusiveTime = Stats.MedianExclusiveTime;
	}
	return Table;
}

FTimingProfilerButterfly::FTimingProfilerButterfly()
	: Allocator(2 << 20)
	, Nodes(Allocator, 1024)
{

}

void FTimingProfilerButterfly::CreateCallersTreeRecursive(const FTimingProfilerButterflyNode* TimerNode, const FTimingProfilerButterflyNode* RootNode, FTimingProfilerButterflyNode* OutputParent)
{
	if (!TimerNode)
	{
		return;
	}
	FTimingProfilerButterflyNode* AggregatedChildNode = nullptr;
	for (FTimingProfilerButterflyNode* Candidate : OutputParent->Children)
	{
		if (Candidate->Timer == TimerNode->Timer)
		{
			AggregatedChildNode = Candidate;
			break;
		}
	}
	if (!AggregatedChildNode)
	{
		AggregatedChildNode = &Nodes.PushBack();
		AggregatedChildNode->Timer = TimerNode->Timer;
		OutputParent->Children.Add(AggregatedChildNode);
		AggregatedChildNode->Parent = OutputParent;
	}

	AggregatedChildNode->InclusiveTime += RootNode->InclusiveTime;
	AggregatedChildNode->ExclusiveTime += RootNode->ExclusiveTime;
	AggregatedChildNode->Count += RootNode->Count;

	CreateCallersTreeRecursive(TimerNode->Parent, RootNode, AggregatedChildNode);
}

const FTimingProfilerButterflyNode& FTimingProfilerButterfly::GenerateCallersTree(uint32 TimerId)
{
	FTimingProfilerButterflyNode** Cached = CachedCallerTrees.Find(TimerId);
	if (Cached)
	{
		return **Cached;
	}

	FTimingProfilerButterflyNode* Root = &Nodes.PushBack();
	for (FTimingProfilerButterflyNode* TimerNode : TimerCallstacksMap[TimerId])
	{
		Root->Timer = TimerNode->Timer;
		Root->InclusiveTime += TimerNode->InclusiveTime;
		Root->ExclusiveTime += TimerNode->ExclusiveTime;
		Root->Count += TimerNode->Count;

		CreateCallersTreeRecursive(TimerNode->Parent, TimerNode, Root);
	}
	CachedCallerTrees.Add(TimerId, Root);
	return *Root;
}

void FTimingProfilerButterfly::CreateCalleesTreeRecursive(const FTimingProfilerButterflyNode* TimerNode, FTimingProfilerButterflyNode* OutputParent)
{
	for (const FTimingProfilerButterflyNode* ChildNode : TimerNode->Children)
	{
		FTimingProfilerButterflyNode* AggregatedChildNode = nullptr;
		for (FTimingProfilerButterflyNode* Candidate : OutputParent->Children)
		{
			if (Candidate->Timer == ChildNode->Timer)
			{
				AggregatedChildNode = Candidate;
				break;
			}
		}
		if (!AggregatedChildNode)
		{
			AggregatedChildNode = &Nodes.PushBack();
			AggregatedChildNode->Timer = ChildNode->Timer;
			OutputParent->Children.Add(AggregatedChildNode);
			AggregatedChildNode->Parent = OutputParent;
		}
		AggregatedChildNode->InclusiveTime += ChildNode->InclusiveTime;
		AggregatedChildNode->ExclusiveTime += ChildNode->ExclusiveTime;
		AggregatedChildNode->Count += ChildNode->Count;

		CreateCalleesTreeRecursive(ChildNode, AggregatedChildNode);
	}
}

const FTimingProfilerButterflyNode& FTimingProfilerButterfly::GenerateCalleesTree(uint32 TimerId)
{
	FTimingProfilerButterflyNode** Cached = CachedCalleeTrees.Find(TimerId);
	if (Cached)
	{
		return **Cached;
	}

	FTimingProfilerButterflyNode* Root = &Nodes.PushBack();
	for (FTimingProfilerButterflyNode* TimerNode : TimerCallstacksMap[TimerId])
	{
		Root->Timer = TimerNode->Timer;
		Root->InclusiveTime += TimerNode->InclusiveTime;
		Root->ExclusiveTime += TimerNode->ExclusiveTime;
		Root->Count += TimerNode->Count;

		CreateCalleesTreeRecursive(TimerNode, Root);
	}
	CachedCalleeTrees.Add(TimerId, Root);
	return *Root;
}

ITimingProfilerButterfly* FTimingProfilerProvider::CreateButterfly(double IntervalStart, double IntervalEnd, TFunctionRef<bool(uint32)> CpuThreadFilter, bool IncludeGpu) const
{
	FTimingProfilerButterfly* Butterfly = new FTimingProfilerButterfly();
	Butterfly->TimerCallstacksMap.AddDefaulted(Timers.Num());

	TArray<const TimelineInternal*> IncludedTimelines;
	if (IncludeGpu)
	{
		IncludedTimelines.Add(&Timelines[GpuTimelineIndex].Get());
	}
	for (const auto& KV : CpuThreadTimelineIndexMap)
	{
		if (CpuThreadFilter(KV.Key))
		{
			IncludedTimelines.Add(&Timelines[KV.Value].Get());
		}
	}

	FTimingProfilerCallstackKey CurrentCallstackKey;
	CurrentCallstackKey.TimerStack.Reserve(1024);

	struct FLocalStackEntry
	{
		FTimingProfilerButterflyNode* Node;
		double StartTime;
		double ExclusiveTime;
		uint32 CurrentCallstackHash;
	};

	TArray<FLocalStackEntry> CurrentCallstack;
	CurrentCallstack.Reserve(1024);

	TMap<FTimingProfilerCallstackKey, FTimingProfilerButterflyNode*> CallstackNodeMap;

	double LastTime = 0.0;
	for (const TimelineInternal* Timeline : IncludedTimelines)
	{
		Timeline->EnumerateEvents(IntervalStart, IntervalEnd, [this, IntervalStart, IntervalEnd, &CurrentCallstackKey, &CurrentCallstack, &CallstackNodeMap, &LastTime, Butterfly](bool IsEnter, double Time, const FTimingProfilerEvent& Event)
		{
			Time = FMath::Clamp(Time, IntervalStart, IntervalEnd);
			FTimingProfilerButterflyNode* ParentNode = nullptr;
			uint32 ParentCallstackHash = 17;
			if (CurrentCallstack.Num())
			{
				FLocalStackEntry& Stackentry = CurrentCallstack.Top();
				ParentNode = Stackentry.Node;
				ParentCallstackHash = Stackentry.CurrentCallstackHash;
				Stackentry.ExclusiveTime += Time - LastTime;
			}
			LastTime = Time;
			if (IsEnter)
			{
				FLocalStackEntry& StackEntry = CurrentCallstack.AddDefaulted_GetRef();
				StackEntry.StartTime = Time;
				StackEntry.ExclusiveTime = 0.0;
				StackEntry.CurrentCallstackHash = ParentCallstackHash * 17 + Event.TimerIndex;

				CurrentCallstackKey.TimerStack.Push(Event.TimerIndex);
				CurrentCallstackKey.Hash = StackEntry.CurrentCallstackHash;

				FTimingProfilerButterflyNode** FindIt = CallstackNodeMap.Find(CurrentCallstackKey);
				if (FindIt)
				{
					StackEntry.Node = *FindIt;
				}
				else
				{
					StackEntry.Node = &Butterfly->Nodes.PushBack();
					CallstackNodeMap.Add(CurrentCallstackKey, StackEntry.Node);
					Butterfly->TimerCallstacksMap[Event.TimerIndex].Add(StackEntry.Node);
					StackEntry.Node->InclusiveTime = 0.0;
					StackEntry.Node->ExclusiveTime = 0.0;
					StackEntry.Node->Count = 0;
					StackEntry.Node->Timer = &this->Timers[Event.TimerIndex];
					StackEntry.Node->Parent = ParentNode;
					if (ParentNode)
					{
						ParentNode->Children.Add(StackEntry.Node);
					}
				}
			}
			else
			{
				FLocalStackEntry& StackEntry = CurrentCallstack.Top();
				double InclusiveTime = Time - StackEntry.StartTime;
				check(InclusiveTime >= 0.0);
				check(StackEntry.ExclusiveTime >= 0.0 && StackEntry.ExclusiveTime <= InclusiveTime);
				
				StackEntry.Node->InclusiveTime += InclusiveTime;
				StackEntry.Node->ExclusiveTime += StackEntry.ExclusiveTime;
				++StackEntry.Node->Count;

				CurrentCallstack.Pop(false);
				CurrentCallstackKey.TimerStack.Pop(false);
			}
		});
	}
	return Butterfly;
}

}
