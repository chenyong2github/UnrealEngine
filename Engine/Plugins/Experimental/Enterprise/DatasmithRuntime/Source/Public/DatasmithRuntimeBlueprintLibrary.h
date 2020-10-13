// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/StrongObjectPtr.h"

#include "DatasmithRuntimeBlueprintLibrary.generated.h"

class ADatasmithRuntimeActor;
class UWorld;

namespace DatasmithRuntime
{
	class FDirectLinkProxyImpl;
}

USTRUCT(BlueprintType)
struct DATASMITHRUNTIME_API FDatasmithRuntimeSourceInfo
{
	GENERATED_USTRUCT_BODY()

	FDatasmithRuntimeSourceInfo()
		: Hash(0xffffffff)
	{
	}

	FDatasmithRuntimeSourceInfo(const FString& InName, uint32 InHash)
		: Name(InName)
		, Hash(InHash)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DatasmithRuntime")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DatasmithRuntime")
	int32 Hash;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDatasmithRuntimeChangeEvent);

// Class to interface with the DirectLink end point
UCLASS(BlueprintType)
class DATASMITHRUNTIME_API UDirectLinkProxy : public UObject
{
	GENERATED_BODY()

public:
	UDirectLinkProxy();
	~UDirectLinkProxy();

	UFUNCTION(BlueprintCallable, Category = "DirectLink")
	FString GetEndPointName();

	UFUNCTION(BlueprintCallable, Category = "DirectLink")
	TArray<FDatasmithRuntimeSourceInfo> GetListOfSources();

	UFUNCTION(BlueprintCallable, Category = "DirectLink")
	FString GetDestinationName(ADatasmithRuntimeActor* DatasmithRuntimeActor);

	UFUNCTION(BlueprintCallable, Category = "DirectLink")
	bool IsConnected(ADatasmithRuntimeActor* DatasmithRuntimeActor);

	UFUNCTION(BlueprintCallable, Category = "DirectLink")
	FString GetSourcename(ADatasmithRuntimeActor* DatasmithRuntimeActor);

	UFUNCTION(BlueprintCallable, Category = "DirectLink")
	void ConnectToSource(ADatasmithRuntimeActor* DatasmithRuntimeActor, int32 SourceIndex);

	// Dynamic delegate used to trigger an event in the Game when there is
	// a change in the DirectLink network
	UPROPERTY(BlueprintAssignable)
	FDatasmithRuntimeChangeEvent OnDirectLinkChange;

	friend class UDatasmithRuntimeLibrary;
};

UCLASS(meta = (ScriptName = "DatasmithRuntimeLibrary"))
class DATASMITHRUNTIME_API UDatasmithRuntimeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a DatasmithRuntime actor
	 * @param TargetWorld	The world in which to create the DatasmithRuntime actor.
	 * @param RootTM		The transform to apply to the DatasmithRuntime actor on creation.
	 * @return	A pointer to the DatasmithRuntime actor if the createion is successful; nullptr, otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	static bool LoadDatasmithScene(ADatasmithRuntimeActor* DatasmithRuntimeActor, const FString& FilePath);

	/*
	 * Opens a file dialog for the specified data. Leave FileTypes empty to be able to select any files.
	 * Filetypes must be in the format of: <File type Description>|*.<actual extension>
	 * You can combine multiple extensions by placing ";" between them
	 * For example: Text Files|*.txt|Excel files|*.csv|Image Files|*.png;*.jpg;*.bmp will display 3 lines for 3 different type of files.
	*/
	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	static void LoadDatasmithSceneFromExplorer(ADatasmithRuntimeActor* DatasmithRuntimeActor, const FString& DefaultPath, const FString& FileTypes);

	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	static void ResetActor(ADatasmithRuntimeActor* DatasmithRuntimeActor);

	/** Returns an interface to the DirectLink end point */
	UFUNCTION(BlueprintCallable, Category = "DatasmithRuntime")
	static UDirectLinkProxy* GetDirectLinkProxy();
};
