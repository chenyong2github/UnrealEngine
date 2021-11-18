// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MetasoundFrontendDocument.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundSettings.generated.h"


// Forward Declarations
struct FMetasoundFrontendClassName;


UENUM()
enum class EMetaSoundMessageLevel : uint8
{
	Error,
	Warning,
	Info
};

USTRUCT()
struct METASOUNDENGINE_API FDefaultMetaSoundAssetAutoUpdateSettings
{
	GENERATED_BODY()

	/** MetaSound to prevent from AutoUpdate. */
	UPROPERTY(EditAnywhere, Category = "AutoUpdate", meta = (AllowedClasses = "MetaSound, MetaSoundSource"))
	FSoftObjectPath MetaSound;
};

UCLASS(config = MetaSound, defaultconfig, meta = (DisplayName = "MetaSounds"))
class METASOUNDENGINE_API UMetaSoundSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** If true, AutoUpdate is enabled, increasing load times.  If false, skips AutoUpdate on load, but can result in MetaSounds failing to load, 
	  * register, and execute if interface differences are present. */
	UPROPERTY(EditAnywhere, config, Category = AutoUpdate)
	bool bAutoUpdateEnabled = true;

	/** List of native MetaSound classes whose node references should not be AutoUpdated. */
	UPROPERTY(EditAnywhere, config, Category = AutoUpdate, meta = (DisplayName = "DenyList", EditCondition = "bAutoUpdateEnabled"))
	TArray<FMetasoundFrontendClassName> AutoUpdateBlacklist;

	/** List of MetaSound assets whose node references should not be AutoUpdated. */
	UPROPERTY(EditAnywhere, config, Category = AutoUpdate, meta = (DisplayName = "Asset DenyList", EditCondition = "bAutoUpdateEnabled"))
	TArray<FDefaultMetaSoundAssetAutoUpdateSettings> AutoUpdateAssetBlacklist;

	/** Directories to scan & automatically register MetaSound post initial asset scan on engine start-up.
	  * May speed up subsequent calls to playback MetaSounds post asset scan but increases application load time.
	  * See 'MetaSoundAssetSubsystem::RegisterAssetClassesInDirectories' to dynamically register or 
	  * 'MetaSoundAssetSubsystem::UnregisterAssetClassesInDirectories' to unregister asset classes.
	  */
	UPROPERTY(EditAnywhere, config, Category = Registration, meta = (RelativePath, LongPackageName))
	TArray<FDirectoryPath> DirectoriesToRegister;

	UPROPERTY(Transient)
	int32 DenyListCacheChangeID = 0;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		DenyListCacheChangeID++;
	}
#endif // WITH_EDITOR
};
