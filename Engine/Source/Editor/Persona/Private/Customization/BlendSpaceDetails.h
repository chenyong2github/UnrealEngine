// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
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
	IDetailLayoutBuilder* Builder;
	UBlendSpace* BlendSpaceBase;
	TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase> BlendSpaceNode;
};
