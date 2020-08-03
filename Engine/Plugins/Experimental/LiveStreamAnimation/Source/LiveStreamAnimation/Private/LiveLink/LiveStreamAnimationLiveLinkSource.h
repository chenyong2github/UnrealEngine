// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "CoreMinimal.h"
#include "LiveStreamAnimationFwd.h"
#include "ILiveLinkSource.h"
#include "LiveLinkTypes.h"
#include "LiveStreamAnimationHandle.h"
#include "UObject/GCObject.h"

class ULiveStreamAnimationLiveLinkFrameTranslator;

namespace LiveStreamAnimation
{
	class FLiveStreamAnimationLiveLinkSource : public ILiveLinkSource, public FGCObject
	{
	public:

		FLiveStreamAnimationLiveLinkSource(ULiveStreamAnimationLiveLinkFrameTranslator* InFrameTranslator);

		//~ Begin ILiveLinkSource Interface
		virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
		virtual void Update() override;
		virtual bool CanBeDisplayedInUI() const override;
		virtual bool IsSourceStillValid() const override;
		virtual bool RequestSourceShutdown() override;
		virtual FText GetSourceType() const override;
		virtual FText GetSourceMachineName() const override;
		virtual FText GetSourceStatus() const override;
		//~ End ILiveLinkSource Interface

		//~ Begin GCObject Interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override
		{
			return "FLiveStreamAnimationLiveLinkSource";
		}
		//~ End GCObject Interface

		bool HandlePacket(class FLiveLinkPacket&& InPacket);
		void SetFrameTranslator(ULiveStreamAnimationLiveLinkFrameTranslator* NewFrameTranslator);

	private:

		void Reset();

		bool HandleAddOrUpdateSubjectPacket(class FLiveLinkAddOrUpdateSubjectPacket&& InPacket);
		bool HandleRemoveSubjectPacket(class FLiveLinkRemoveSubjectPacket&& InPacket);
		bool HandleAnimationFramePacket(class FLiveLinkAnimationFramePacket&& InPacket);

		ILiveLinkClient* LiveLinkClient;
		FGuid SourceGuid;
		bool bIsConnectedToMesh;

		TMap<FLiveStreamAnimationHandle, FLiveLinkSubjectKey> MappedSubjects;
		ULiveStreamAnimationLiveLinkFrameTranslator* FrameTranslator;
	};
}