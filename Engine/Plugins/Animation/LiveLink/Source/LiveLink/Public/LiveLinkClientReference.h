// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ILiveLinkClient;
class IModularFeature;

// References the live link client modular feature and handles add/remove
struct LIVELINK_API FLiveLinkClientReference
{
public:

	FLiveLinkClientReference();
	FLiveLinkClientReference(const FLiveLinkClientReference& Other);
	FLiveLinkClientReference& operator=(const FLiveLinkClientReference& Other);
	FLiveLinkClientReference(const FLiveLinkClientReference&& Other) = delete;
	~FLiveLinkClientReference();

	ILiveLinkClient* GetClient() const { return LiveLinkClient; }

private:
	void InitClient();

	// Handlers for modular features coming and going
	void OnLiveLinkClientRegistered(const FName& Type, class IModularFeature* ModularFeature);
	void OnLiveLinkClientUnregistered(const FName& Type, class IModularFeature* ModularFeature);

	ILiveLinkClient* LiveLinkClient;
};