// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class IDetailLayoutBuilder;
class UBlendSpace;
class UAnimGraphNode_BlendSpaceGraphBase;

class FBlendSpaceDetails : public IDetailCustomization
{
public:
	FBlendSpaceDetails();
	~FBlendSpaceDetails();

	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable( new FBlendSpaceDetails() );
	}

	// IDetailCustomization interface
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
private:
	FReply HandleClearSamples();
	FReply HandleAnalyzeSamples();
	void HandleAnalysisFunctionChanged(int32 AxisIndex, TSharedPtr<FString> NewItem);

	IDetailLayoutBuilder* Builder;
	UBlendSpace* BlendSpace;
	TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase> BlendSpaceNode;
	TArray<TSharedPtr<FString>> AnalysisFunctionNames[3];
};
