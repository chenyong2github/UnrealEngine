// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
    XcodeProjectSettings.h: Declares the UXcodeProjectSettings class.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "XcodeProjectSettings.generated.h"

/**
 * Implements the settings for Xcode projects
 */
UCLASS(config=Engine, defaultconfig)
class MACTARGETPLATFORM_API UXcodeProjectSettings
    : public UObject
{
public:
    
    GENERATED_UCLASS_BODY()
    
    /**
     * Enable modernized Xcode, when building from Xcode, use native Xcode for bundle generation and archiving instead of UBT
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "Modernized Xcode"))
    bool bUseModernXcode;
    
    /**
     * Enable native Xcode code signing
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "Use Modern Code Signing"))
    bool bUseModernCodeSigning;
    
    /**
     * Team ID used for native Xcode code signing
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "Modern Code Sign Team"))
    FString ModernSigningTeam;
    
    /**
     * Bundle ID prefix used for native Xcode code signing
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "ModernSigningPrefix"))
    FString ModernSigningPrefix;
    
    /**
     * Bundle ID used for nativr Xcode code signing
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "ModernBundleIdentifier"))
    FString ModernBundleIdentifier;
    
    /**
     * The App Category that will be used for Mac App Store submission
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "AppCategory"))
    FString AppCategory;
    
    /**
     * The template info.plist used for Mac game targets
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "TemplateMacPlist"))
    FFilePath TemplateMacPlist;
    
    /**
     * The template info.plist used for Mac editor targets
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "TemplateMacEditorPlist"))
    FFilePath TemplateMacEditorPlist;
    
    /**
     * The template info.plist used for iOS game targets
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "TemplateIOSPlist"))
    FFilePath TemplateIOSPlist;
    
    /**
     * The premade entitlement file used for development Mac builds
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "PremadeMacEntitlements"))
    FFilePath PremadeMacEntitlements;
    
    /**
     * The premade entitlement file used for shipping Mac builds
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (DisplayName = "ShippingSpecificMacEntitlements"))
    FFilePath ShippingSpecificMacEntitlements;
};
