// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Texture/TextureTranslatorUtilities.h"

#include "CoreMinimal.h"
#include "InterchangeSourceData.h"
#include "InterchangeTextureNode.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

bool UE::Interchange::FTextureTranslatorUtilities::Generic2DTextureTranslate(const UInterchangeSourceData* SourceData, UInterchangeBaseNodeContainer& BaseNodeContainer)
{
	FString Filename = SourceData->GetFilename();
	if (!FPaths::FileExists(Filename))
	{
		return false;
	}

	FName DisplayLabel = *FPaths::GetBaseFilename(Filename);
	FName NodeUID(*Filename);
	UInterchangeTextureNode* TextureNode = NewObject<UInterchangeTextureNode>(&BaseNodeContainer, NAME_None);
	if (!ensure(TextureNode))
	{
		return false;
	}
	//Creating a UTexture2D
	TextureNode->InitializeTextureNode(NodeUID, DisplayLabel, UTexture2D::StaticClass()->GetName());
	TextureNode->SetPayLoadKey(Filename);

	BaseNodeContainer.AddNode(TextureNode);

	return true;
}
