// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"

#include "UObject/Class.h"

#if WITH_EDITOR
#include "Serialization/BulkDataRegistry.h"
#endif // WITH_EDITOR

#if WITH_EDITOR
namespace UE::PoseSearch
{
	const UE::DerivedData::FValueId FPoseSearchDatabaseAsyncCacheTask::Id = UE::DerivedData::FValueId::FromName("Data");
	const UE::DerivedData::FCacheBucket FPoseSearchDatabaseAsyncCacheTask::Bucket("PoseSearchDatabase");
}
#endif // WITH_EDITOR

#if WITH_EDITOR

void FPoseSearchDatabaseDerivedData::Cache(UPoseSearchDatabase& Database, bool bForceRebuild)
{
	CancelCache();
	if (Database.IsValidForIndexing())
	{
		CreateDatabaseBuildTask(Database, bForceRebuild);
	}
	else
	{
		SearchIndex.Reset();
		SearchIndex.Schema = Database.Schema;
		DerivedDataKey = { UE::DerivedData::FCacheBucket(), FIoHash::Zero };
		PendingDerivedDataKey = FIoHash::Zero;
	}
}

void FPoseSearchDatabaseDerivedData::CancelCache()
{
	if (AsyncTask)
	{
		AsyncTask->Cancel();
	}

	FinishCache();
}

void FPoseSearchDatabaseDerivedData::FinishCache()
{
	if (AsyncTask)
	{
		AsyncTask->Wait();
		delete AsyncTask;
		AsyncTask = nullptr;
	}
}

void FPoseSearchDatabaseDerivedData::CreateDatabaseBuildTask(UPoseSearchDatabase& Database, bool bForceRebuild)
{
	AsyncTask = new UE::PoseSearch::FPoseSearchDatabaseAsyncCacheTask(Database, *this, bForceRebuild);
}

#endif // WITH_EDITOR

namespace UE::PoseSearch
{

#if WITH_EDITOR

	FPoseSearchDatabaseAsyncCacheTask::FPoseSearchDatabaseAsyncCacheTask(
		UPoseSearchDatabase& InDatabase,
		FPoseSearchDatabaseDerivedData& InDerivedData,
		bool bForceRebuild)
		: Owner(UE::DerivedData::EPriority::Normal)
		, DerivedData(InDerivedData)
		, Database(InDatabase)
	{
		using namespace UE::DerivedData;

		FIoHash DerivedDataKey = CreateKey(Database);
		DerivedData.PendingDerivedDataKey = DerivedDataKey;

		InDatabase.NotifyDerivedDataBuildStarted();

		if (bForceRebuild)
		{
			// when the build is forced, the derived data key is zeroed so the comparison with the pending key fails, 
			// informing other systems that data is being rebuilt
			DerivedData.DerivedDataKey.Hash = FIoHash::Zero;
			BuildAndWrite({ Bucket, DerivedDataKey });
		}
		else
		{
			BeginCache();
		}
	}

	bool FPoseSearchDatabaseAsyncCacheTask::Cancel()
	{
		Owner.Cancel();
		return true;
	}

	void FPoseSearchDatabaseAsyncCacheTask::Wait()
	{
		Owner.Wait();
	}

	bool FPoseSearchDatabaseAsyncCacheTask::Poll() const
	{
		return Owner.Poll();
	}


	void FPoseSearchDatabaseAsyncCacheTask::BeginCache()
	{
		using namespace UE::DerivedData;

		TArray<FCacheGetRequest> CacheRequests;
		ECachePolicy CachePolicy = ECachePolicy::Default;
		const FCacheKey CacheKey{ Bucket, DerivedData.PendingDerivedDataKey };
		CacheRequests.Add({ {Database.GetPathName()}, CacheKey, CachePolicy });
		GetCache().Get(CacheRequests, Owner, [this](FCacheGetResponse&& Response)
		{
			OnGetComplete(MoveTemp(Response));
		});
	}

	void FPoseSearchDatabaseAsyncCacheTask::OnGetComplete(UE::DerivedData::FCacheGetResponse&& Response)
	{
		using namespace UE::DerivedData;

		if (Response.Status == EStatus::Ok)
		{
			BuildIndexFromCacheRecord(MoveTemp(Response.Record));
			DerivedData.DerivedDataKey = Response.Record.GetKey();
		}
		else if (Response.Status == EStatus::Error)
		{
			BuildAndWrite(Response.Record.GetKey());
		}
	}

	void FPoseSearchDatabaseAsyncCacheTask::BuildAndWrite(const UE::DerivedData::FCacheKey& NewKey)
	{
		GetRequestOwner().LaunchTask(TEXT("PoseSearchDatabaseBuild"), [this, NewKey]
		{
			if (!GetRequestOwner().IsCanceled())
			{
				DerivedData.SearchIndex.Reset();
				DerivedData.SearchIndex.Schema = Database.Schema;
				const bool bIndexReady = BuildIndex(&Database, DerivedData.SearchIndex);

				WriteIndexToCache(NewKey);
			}
		});
	}

	void FPoseSearchDatabaseAsyncCacheTask::WriteIndexToCache(const UE::DerivedData::FCacheKey& NewKey)
	{
		using namespace UE::DerivedData;

		TArray<uint8> RawBytes;
		FMemoryWriter Writer(RawBytes);
		Writer << DerivedData.SearchIndex;
		FSharedBuffer RawData = MakeSharedBufferFromArray(MoveTemp(RawBytes));

		FCacheRecordBuilder Builder(NewKey);
		Builder.AddValue(Id, RawData);

		Owner.KeepAlive();
		GetCache().Put({ { { Database.GetPathName() }, Builder.Build() } }, Owner);
		DerivedData.DerivedDataKey = NewKey;
	}

	void FPoseSearchDatabaseAsyncCacheTask::BuildIndexFromCacheRecord(UE::DerivedData::FCacheRecord&& CacheRecord)
	{
		using namespace UE::DerivedData;

		DerivedData.SearchIndex.Reset();
		DerivedData.SearchIndex.Schema = Database.Schema;

		FSharedBuffer RawData = CacheRecord.GetValue(Id).GetData().Decompress();
		FMemoryReaderView Reader(RawData);
		Reader << DerivedData.SearchIndex;
	}

	FIoHash FPoseSearchDatabaseAsyncCacheTask::CreateKey(const UPoseSearchDatabase& Database)
	{
		FBlake3 Writer;

		const FGuid VersionGuid =  FDevSystemGuids::GetSystemGuid(FDevSystemGuids::Get().POSESEARCHDB_DERIVEDDATA_VER);
		Writer.Update(MakeMemoryView(VersionGuid.ToString()));

		AddPoseSearchSchemaToWriter(Database.Schema, Writer);

		for (int32 i = 0; i < Database.Sequences.Num(); ++i)
		{
			AddDbSequenceToWriter(Database.Sequences[i], Writer);
		}

		for (int32 i = 0; i < Database.BlendSpaces.Num(); ++i)
		{
			AddDbBlendSpaceToWriter(Database.BlendSpaces[i], Writer);
		}

		Writer.Update(&Database.NumberOfPrincipalComponents, sizeof(Database.NumberOfPrincipalComponents));
		Writer.Update(&Database.KDTreeMaxLeafSize, sizeof(Database.KDTreeMaxLeafSize));
		Writer.Update(&Database.KDTreeQueryNumNeighbors, sizeof(Database.KDTreeQueryNumNeighbors));
		Writer.Update(&Database.PoseSearchMode, sizeof(Database.PoseSearchMode));

		return Writer.Finalize();
	}

	void FPoseSearchDatabaseAsyncCacheTask::AddPoseSearchSchemaToWriter(
		const UPoseSearchSchema* Schema, 
		FBlake3& InOutWriter)
	{
		if (IsValid(Schema))
		{
			for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : Schema->Channels)
			{
				if (Channel)
				{
					const FBlake3Hash& ChannelClassHash = Channel->GetClass()->GetSchemaHash(false);
					InOutWriter.Update(MakeMemoryView(ChannelClassHash.GetBytes()));

					Channel->GenerateDDCKey(InOutWriter);
				}
			}
			InOutWriter.Update(&Schema->DataPreprocessor, sizeof(Schema->DataPreprocessor));
			InOutWriter.Update(&Schema->EffectiveDataPreprocessor, sizeof(Schema->EffectiveDataPreprocessor));
			InOutWriter.Update(&Schema->SamplingInterval, sizeof(Schema->SamplingInterval));
			InOutWriter.Update(MakeMemoryView(Schema->BoneIndicesWithParents));
		}
	}

	void FPoseSearchDatabaseAsyncCacheTask::AddDbSequenceToWriter(
		const FPoseSearchDatabaseSequence& DbSequence, 
		FBlake3& InOutWriter)
	{
		// Main Sequence
		AddRawSequenceToWriter(DbSequence.Sequence, InOutWriter);
		InOutWriter.Update(&DbSequence.SamplingRange, sizeof(DbSequence.SamplingRange));
		if (DbSequence.Sequence)
		{
			InOutWriter.Update(&DbSequence.Sequence->bLoop, sizeof(DbSequence.Sequence->bLoop));
		}
		InOutWriter.Update(&DbSequence.MirrorOption, sizeof(DbSequence.MirrorOption));

		// Lead in sequence
		AddRawSequenceToWriter(DbSequence.LeadInSequence, InOutWriter);
		if (DbSequence.LeadInSequence)
		{
			InOutWriter.Update(&DbSequence.LeadInSequence->bLoop, sizeof(DbSequence.LeadInSequence->bLoop));
		}

		// Follow up sequence
		AddRawSequenceToWriter(DbSequence.FollowUpSequence, InOutWriter);
		if (DbSequence.FollowUpSequence)
		{
			InOutWriter.Update(&DbSequence.FollowUpSequence->bLoop, sizeof(DbSequence.FollowUpSequence->bLoop));
		}

		// Tags
		InOutWriter.Update(&DbSequence.GroupTags, sizeof(DbSequence.GroupTags));

		// Notifies
		AddPoseSearchNotifiesToWriter(DbSequence.Sequence, InOutWriter);
	}

	void FPoseSearchDatabaseAsyncCacheTask::AddRawSequenceToWriter(
		const UAnimSequence* Sequence, 
		FBlake3& InOutWriter)
	{
		if (Sequence)
		{
			InOutWriter.Update(MakeMemoryView(Sequence->GetName()));
			InOutWriter.Update(MakeMemoryView(Sequence->GetRawDataGuid().ToString()));
		}
	}

	void FPoseSearchDatabaseAsyncCacheTask::AddPoseSearchNotifiesToWriter(
		const UAnimSequence* Sequence, 
		FBlake3& InOutWriter)
	{
		if (!Sequence)
		{
			return;
		}

		FAnimNotifyContext NotifyContext;
		Sequence->GetAnimNotifies(0.0f, Sequence->GetPlayLength(), NotifyContext);

		for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
		{
			const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
			if (!NotifyEvent || !NotifyEvent->NotifyStateClass)
			{
				continue;
			}

			if (NotifyEvent->NotifyStateClass->IsA<UAnimNotifyState_PoseSearchBase>())
			{
				const float StartTime = NotifyEvent->GetTriggerTime();
				const float EndTime = NotifyEvent->GetEndTriggerTime();
				InOutWriter.Update(&StartTime, sizeof(float));
				InOutWriter.Update(&EndTime, sizeof(float));

				if (const auto ModifyCostNotifyState =
					Cast<const UAnimNotifyState_PoseSearchModifyCost>(NotifyEvent->NotifyStateClass))
				{
					InOutWriter.Update(&ModifyCostNotifyState->CostAddend, sizeof(float));
				}
			}
		}
	}

	void FPoseSearchDatabaseAsyncCacheTask::AddDbBlendSpaceToWriter(
		const FPoseSearchDatabaseBlendSpace& DbBlendSpace, 
		FBlake3& InOutWriter)
	{
		if (IsValid(DbBlendSpace.BlendSpace))
		{
			const TArray<FBlendSample>& BlendSpaceSamples = DbBlendSpace.BlendSpace->GetBlendSamples();
			for (const FBlendSample& Sample : BlendSpaceSamples)
			{
				AddRawSequenceToWriter(Sample.Animation, InOutWriter);
				InOutWriter.Update(&Sample.SampleValue, sizeof(Sample.SampleValue));
				InOutWriter.Update(&Sample.RateScale, sizeof(Sample.RateScale));
			}

			InOutWriter.Update(&DbBlendSpace.BlendSpace->bLoop, sizeof(DbBlendSpace.BlendSpace->bLoop));
			InOutWriter.Update(&DbBlendSpace.MirrorOption, sizeof(DbBlendSpace.MirrorOption));
			InOutWriter.Update(&DbBlendSpace.bUseGridForSampling, sizeof(DbBlendSpace.bUseGridForSampling));
			InOutWriter.Update(&DbBlendSpace.NumberOfHorizontalSamples, sizeof(DbBlendSpace.NumberOfHorizontalSamples));
			InOutWriter.Update(&DbBlendSpace.NumberOfVerticalSamples, sizeof(DbBlendSpace.NumberOfVerticalSamples));
			InOutWriter.Update(&DbBlendSpace.GroupTags, sizeof(DbBlendSpace.GroupTags));
		}
	}

#endif // WITH_EDITOR

	FArchive& operator<<(FArchive& Ar, FPoseSearchIndexPreprocessInfo& Info)
	{
		int32 NumTransformationMatrix = 0;

		if (Ar.IsSaving())
		{
			NumTransformationMatrix = Info.TransformationMatrix.Num();
		}

		Ar << Info.NumDimensions;
		Ar << NumTransformationMatrix;

		if (Ar.IsLoading())
		{
			Info.TransformationMatrix.SetNumUninitialized(NumTransformationMatrix);
			Info.InverseTransformationMatrix.SetNumUninitialized(NumTransformationMatrix);
			Info.SampleMean.SetNumUninitialized(Info.NumDimensions);
		}

		if (Info.TransformationMatrix.Num() > 0)
		{
			Ar.Serialize(
				&Info.TransformationMatrix[0],
				Info.TransformationMatrix.Num() * Info.TransformationMatrix.GetTypeSize());
		}

		if (Info.InverseTransformationMatrix.Num() > 0)
		{
			Ar.Serialize(
				&Info.InverseTransformationMatrix[0],
				Info.InverseTransformationMatrix.Num() * Info.InverseTransformationMatrix.GetTypeSize());
		}

		if (Info.SampleMean.Num() > 0)
		{
			Ar.Serialize(&Info.SampleMean[0], Info.SampleMean.Num() * Info.SampleMean.GetTypeSize());
		}

		return Ar;
	}

	FArchive& operator<<(FArchive& Ar, FPoseSearchIndex& Index)
	{
		int32 NumValues = 0;
		int32 NumPCAValues = 0;
		int32 NumAssets = 0;
		int32 NumGroups = 0;

		if (Ar.IsSaving())
		{
			NumValues = Index.Values.Num();
			NumPCAValues = Index.PCAValues.Num();
			NumAssets = Index.Assets.Num();
			NumGroups = Index.Groups.Num();
		}

		Ar << Index.NumPoses;
		Ar << NumValues;
		Ar << NumPCAValues;
		Ar << NumAssets;
		Ar << NumGroups;

		if (Ar.IsLoading())
		{
			Index.Values.SetNumUninitialized(NumValues);
			Index.PCAValues.SetNumUninitialized(NumPCAValues);
			Index.PoseMetadata.SetNumUninitialized(Index.NumPoses);
			Index.Assets.SetNumUninitialized(NumAssets);
			Index.Groups.SetNum(NumGroups);
		}

		if (Index.Values.Num() > 0)
		{
			Ar.Serialize(&Index.Values[0], Index.Values.Num() * Index.Values.GetTypeSize());
		}

		if (Index.PCAValues.Num() > 0)
		{
			Ar.Serialize(&Index.PCAValues[0], Index.PCAValues.Num() * Index.PCAValues.GetTypeSize());
		}

		if (Index.PoseMetadata.Num() > 0)
		{
			Ar.Serialize(&Index.PoseMetadata[0], Index.PoseMetadata.Num() * Index.PoseMetadata.GetTypeSize());
		}

		if (Index.Assets.Num() > 0)
		{
			Ar.Serialize(&Index.Assets[0], Index.Assets.Num() * Index.Assets.GetTypeSize());
		}

		Ar << Index.PreprocessInfo;

		for (int i = 0; i < Index.Groups.Num(); ++i)
		{
			FGroupSearchIndex& GroupSearchIndex = Index.Groups[i];
			Ar << GroupSearchIndex.StartPoseIndex;
			Ar << GroupSearchIndex.EndPoseIndex;
			Ar << GroupSearchIndex.GroupIndex;
			Ar << GroupSearchIndex.Weights;
			Ar << GroupSearchIndex.Mean;
			Ar << GroupSearchIndex.PCAProjectionMatrix;

			check(GroupSearchIndex.PCAProjectionMatrix.Num() > 0 && GroupSearchIndex.Mean.Num() > 0);
			check(GroupSearchIndex.PCAProjectionMatrix.Num() % GroupSearchIndex.Mean.Num() == 0);
			const int NumberOfPrincipalComponents = GroupSearchIndex.PCAProjectionMatrix.Num() / GroupSearchIndex.Mean.Num();
			float* GroupPCAValues = Index.PCAValues.GetData() + GroupSearchIndex.StartPoseIndex * NumberOfPrincipalComponents;
			Serialize(Ar, GroupSearchIndex.KDTree, GroupPCAValues);
		}

		return Ar;
	}

}