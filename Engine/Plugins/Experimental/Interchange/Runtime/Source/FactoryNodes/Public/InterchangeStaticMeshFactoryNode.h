// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeMeshFactoryNode.h"
#include "Nodes/InterchangeFactoryBaseNode.h"

#if WITH_ENGINE
#include "Engine/StaticMesh.h"
#endif

#include "InterchangeStaticMeshFactoryNode.generated.h"


namespace UE
{
	namespace Interchange
	{
		struct INTERCHANGEFACTORYNODES_API FStaticMeshNodeStaticData : public FBaseNodeStaticData
		{
			static const FAttributeKey& GetSocketUidsBaseKey();
		};
	} // namespace Interchange
} // namespace UE


UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeStaticMeshFactoryNode : public UInterchangeMeshFactoryNode
{
	GENERATED_BODY()

public:
	UInterchangeStaticMeshFactoryNode();

	/**
	 * Initialize node data
	 * @param UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the StaticMesh factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	void InitializeStaticMeshNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass);

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	virtual class UClass* GetObjectClass() const override;

public:
	/** Return The number of socket UIDs this static mesh has.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	int32 GetSocketUidCount() const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	void GetSocketUids(TArray<FString>& OutSocketUids) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool AddSocketUid(const FString& SocketUid);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool AddSocketUids(const TArray<FString>& InSocketUids);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool RemoveSocketUd(const FString& SocketUid);

private:

	virtual void FillAssetClassFromAttribute() override;
	virtual bool SetNodeClassFromClassAttribute() override;

	UE::Interchange::TArrayAttributeHelper<FString> SocketUids;

protected:
#if WITH_ENGINE
	TSubclassOf<UStaticMesh> AssetClass = nullptr;
#endif
};
