// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeUtilities.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

#include "InterchangeFactoryBaseNode.generated.h"

class UObject;
struct FFrame;


namespace UE
{
	namespace Interchange
	{
		struct INTERCHANGECORE_API FFactoryBaseNodeStaticData : public FBaseNodeStaticData
		{
			static const FString& FactoryDependenciesBaseKey();
		};

	} // namespace Interchange
} // namespace UE


/**
 * This struct is used to store and retrieve key value attributes. The attributes are store in a generic FAttributeStorage which serialize the value in a TArray64<uint8>
 * See UE::Interchange::EAttributeTypes to know the supported template types
 * This is an abstract class. This is the base class of the interchange node graph format, all class in this format should derive from this class
 */
UCLASS(BlueprintType, Experimental)
class INTERCHANGECORE_API UInterchangeFactoryBaseNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeFactoryBaseNode();

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;
	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	/**
	 * Return the UClass of the object we represent so we can find factory/writer
	 */
	virtual class UClass* GetObjectClass() const;

	/**
	 * Return the custom sub-path under PackageBasePath, where the assets will be created.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool GetCustomSubPath(FString& AttributeValue) const;

	/**
	 * Set the custom sub-path under PackageBasePath where the assets will be created.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetCustomSubPath(const FString& AttributeValue);

	/**
	 * This function allow to retrieve the number of factory dependencies for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	int32 GetFactoryDependenciesCount() const;

	/**
	 * This function allow to retrieve the dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void GetFactoryDependencies(TArray<FString>& OutDependencies ) const;

	/**
	 * This function allow to retrieve one dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void GetFactoryDependency(const int32 Index, FString& OutDependency) const;

	/**
	 * Add one dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool AddFactoryDependencyUid(const FString& DependencyUid);

	/**
	 * Remove one dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool RemoveFactoryDependencyUid(const FString& DependencyUid);


	UPROPERTY()
	mutable FSoftObjectPath ReferenceObject;

	static FString BuildFactoryNodeUid(const FString& TranslatedNodeUid);

protected:
	/**
	 * Those dependencies are use by the interchange parsing task to make sure the asset are created in the correct order.
	 * Example: Mesh factory node will have dependencies on material factory node
	 *          Material factory node will have dependencies on texture factory node
	 */
	UE::Interchange::TArrayAttributeHelper<FString> FactoryDependencies;


private:

	const UE::Interchange::FAttributeKey Macro_CustomSubPathKey = UE::Interchange::FAttributeKey(TEXT("SubPath"));

};