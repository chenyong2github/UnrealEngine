// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "FbxHelper.h"
#include "FbxInclude.h"

/** Forward declarations */
struct FMeshDescription;
class UInterchangeBaseNodeContainer;
class UInterchangeSceneNode;

namespace UE::Interchange::Private
{
	class FAnimationPayloadContextTransform : public FPayloadContextBase
	{
	public:
		virtual ~FAnimationPayloadContextTransform() {}
		virtual FString GetPayloadType() const override { return TEXT("TransformAnimation-PayloadContext"); }
		virtual bool FetchPayloadToFile(FFbxParser& Parser, const FString& PayloadFilepath) override;
		virtual bool FetchAnimationBakeTransformPayloadToFile(FFbxParser& Parser, const double BakeFrequency, const double RangeStartTime, const double RangeEndTime, const FString& PayloadFilepath) override;
		FbxNode* Node = nullptr;
		FbxScene* SDKScene = nullptr;
	};

	class FFbxAnimation
	{
	public:
		/** This function add the payload key if the scene node transform is animated. */
		static void AddNodeTransformAnimation(FbxScene* SDKScene, FbxNode* JointNode, UInterchangeBaseNodeContainer& NodeContainer, UInterchangeSceneNode* UnrealNode, TMap<FString, TSharedPtr<FPayloadContextBase>>& PayloadContexts);
	};
}//ns UE::Interchange::Private
