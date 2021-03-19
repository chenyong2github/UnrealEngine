// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkSubject.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "LiveLinkFrameTranslator.h"

#include "LiveLinkVirtualSubject.generated.h"


class ILiveLinkClient;



// A Virtual subject is made up of one or more real subjects from a source
UCLASS(Abstract)
class LIVELINKINTERFACE_API ULiveLinkVirtualSubject : public UObject, public ILiveLinkSubject
{
	GENERATED_BODY()

	//~ Begin ILiveLinkSubject Interface
public:
	virtual void Initialize(FLiveLinkSubjectKey SubjectKey, TSubclassOf<ULiveLinkRole> Role, ILiveLinkClient* LiveLinkClient) override;
	virtual void Update() override;
	virtual void ClearFrames() override;
	virtual FLiveLinkSubjectKey GetSubjectKey() const override { return SubjectKey; }
	virtual TSubclassOf<ULiveLinkRole> GetRole() const override { return Role; }
	virtual bool HasValidFrameSnapshot() const override;
	virtual FLiveLinkStaticDataStruct& GetStaticData() override { return FrameSnapshot.StaticData; }
	virtual const FLiveLinkStaticDataStruct& GetStaticData() const override { return FrameSnapshot.StaticData; }
	virtual const TArray<ULiveLinkFrameTranslator::FWorkerSharedPtr> GetFrameTranslators() const override { return CurrentFrameTranslators; }
	virtual TArray<FLiveLinkTime> GetFrameTimes() const override;
	virtual bool IsRebroadcasted() const override { return bRebroadcastSubject; }
	virtual bool HasStaticDataBeenRebroadcasted() const override { return bHasStaticDataBeenRebroadcast; }
	virtual void SetStaticDataAsRebroadcasted(const bool bInSent) override { bHasStaticDataBeenRebroadcast = bInSent; }
protected:
	virtual const FLiveLinkSubjectFrameData& GetFrameSnapshot() const override { return FrameSnapshot; }
	//~ End ILiveLinkSubject Interface

public:
	ILiveLinkClient* GetClient() const { return LiveLinkClient; }

	/** Returns the live subjects associated with this virtual one */
	const TArray<FLiveLinkSubjectName>& GetSubjects() const { return Subjects; }

	/** Returns the translators assigned to this virtual subject */
	const TArray<ULiveLinkFrameTranslator*>& GetTranslators() const { return FrameTranslators; }

	/** Returns the current frame data of this virtual subject */
	const FLiveLinkFrameDataStruct& GetFrameData() const { return FrameSnapshot.FrameData; }

	/** Returns true whether this virtual subject depends on the Subject named SubjectName */
	virtual bool DependsOnSubject(FName SubjectName) const;
	
protected:
	void UpdateTranslatorsForThisFrame();

protected:
	/** The role the subject was build with. */
	UPROPERTY()
	TSubclassOf<ULiveLinkRole> Role;

	/** Names of the real subjects to combine into a virtual subject */
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	TArray<FLiveLinkSubjectName> Subjects;

	/** List of available translator the subject can use. */
	UPROPERTY(EditAnywhere, Instanced, Category = "LiveLink", meta=(DisplayName="Translators"))
	TArray<ULiveLinkFrameTranslator*> FrameTranslators;

	/** If enabled, rebroadcast this subject */
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	bool bRebroadcastSubject = false;

	/** LiveLinkClient to get access to subjects */
	ILiveLinkClient* LiveLinkClient;

	/** Last evaluated frame for this subject. */
	FLiveLinkSubjectFrameData FrameSnapshot;

	/** Name of the subject */
	FLiveLinkSubjectKey SubjectKey;
	
	/** If true, static data has been sent for this rebroadcast */
	bool bHasStaticDataBeenRebroadcast = false;

private:
	TArray<ULiveLinkFrameTranslator::FWorkerSharedPtr> CurrentFrameTranslators;
};
