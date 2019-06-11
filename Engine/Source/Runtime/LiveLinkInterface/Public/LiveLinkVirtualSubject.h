// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	virtual FLiveLinkSubjectKey GetSubjectKey() const { return SubjectKey; }
	virtual TSubclassOf<ULiveLinkRole> GetRole() const override { return Role; }
	virtual bool HasValidFrameSnapshot() const override;
	virtual FLiveLinkStaticDataStruct& GetStaticData() override { return FrameSnapshot.StaticData; }
	virtual const FLiveLinkStaticDataStruct& GetStaticData() const override { return FrameSnapshot.StaticData; }
	virtual const TArray<ULiveLinkFrameTranslator::FWorkerSharedPtr> GetFrameTranslators() const override { return CurrentFrameTranslators; }
protected:
	virtual const FLiveLinkSubjectFrameData& GetFrameSnapshot() const { return FrameSnapshot; }
	//~ End ILiveLinkSubject Interface

public:
	ILiveLinkClient* GetClient() const { return LiveLinkClient; }
	const TArray<FLiveLinkSubjectName>& GetSubjects() const { return Subjects; }
	virtual bool DependsOnSubject(FName SubjectName) const;

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

	/** LiveLinkClient to get access to subjects */
	ILiveLinkClient* LiveLinkClient;

	/** Last evaluated frame for this subject. */
	FLiveLinkSubjectFrameData FrameSnapshot;

	/** Name of the subject */
	FLiveLinkSubjectKey SubjectKey;

private:
	TArray<ULiveLinkFrameTranslator::FWorkerSharedPtr> CurrentFrameTranslators;
};
