// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeSkeletalMeshLodDataNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct FSkeletalMeshNodeLodDataStaticData : public FBaseNodeStaticData
		{
			static const FString& GetBlendShapesNameBaseKey()
			{
				static FString BlendShapesName_BaseKey = TEXT("BlendShape_Names");
				return BlendShapesName_BaseKey;
			}

			static const FString& GetTranslatorMeshKeysBaseKey()
			{
				static FString TranslatorMeshKeys_BaseKey = TEXT("TranslatorMesh_Keys");
				return TranslatorMeshKeys_BaseKey;
			}
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeSkeletalMeshLodDataNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSkeletalMeshLodDataNode()
		:UInterchangeBaseNode()
	{
		BlendShapesNames.Initialize(Attributes, UE::Interchange::FSkeletalMeshNodeLodDataStaticData::GetBlendShapesNameBaseKey());
		TranslatorMeshKeys.Initialize(Attributes, UE::Interchange::FSkeletalMeshNodeLodDataStaticData::GetTranslatorMeshKeysBaseKey());
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("SkeletalMeshLodDataNode");
		return TypeName;
	}

	virtual FGuid GetHash() const override
	{
		return Attributes->GetStorageHash();
	}


public:
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool GetCustomSkeletonID(FString& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(SkeletonID, FString);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool SetCustomSkeletonID(const FString& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SkeletonID, FString)
	}

	/* Translator mesh keys*/

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	int32 GetTranslatorMeshKeyCount() const
	{
		return TranslatorMeshKeys.GetCount();
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	void GetTranslatorMeshKeys(TArray<FString>& OutTranslatorMeshKeys) const
	{
		TranslatorMeshKeys.GetNames(OutTranslatorMeshKeys);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool AddTranslatorMeshKey(const FString& TranslatorMeshKey)
	{
		return TranslatorMeshKeys.AddName(TranslatorMeshKey);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool RemoveTranslatorMeshKey(const FString& TranslatorMeshKey)
	{
		return TranslatorMeshKeys.RemoveName(TranslatorMeshKey);
	}

	/* Blend shapes*/

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	int32 GetBlendShapeCount() const
	{
		return BlendShapesNames.GetCount();
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	void GetBlendShapes(TArray<FString>& OutBlendShapeNames) const
	{
		BlendShapesNames.GetNames(OutBlendShapeNames);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool AddBlendShape(const FString& BlendShapeName)
	{
		return BlendShapesNames.AddName(BlendShapeName);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool RemoveBlendShape(const FString& BlendShapeName)
	{
		return BlendShapesNames.RemoveName(BlendShapeName);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool RemoveAllBlendShape()
	{
		return BlendShapesNames.RemoveAllNames();
	}

private:

	bool IsEditorOnlyDataDefined()
	{
#if WITH_EDITORONLY_DATA
		return true;
#else
		return false;
#endif
	}

	//SkeletalMesh
	const UE::Interchange::FAttributeKey Macro_CustomSkeletonIDKey = UE::Interchange::FAttributeKey(TEXT("SkeletonID"));

	UE::Interchange::FNameAttributeArrayHelper BlendShapesNames;
	UE::Interchange::FNameAttributeArrayHelper TranslatorMeshKeys;
protected:
};
