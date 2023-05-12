// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
    XcodeProjectSettings.h: Declares the UXcodeProjectSettings class.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IDetailCustomization.h"
#include "XcodeProjectSettings.generated.h"

class IPropertyHandle;
class FReply;

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
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (EditCondition="bUseModernXcode", DisplayName = "Use Modern Code Signing"))
    bool bUseModernCodeSigning;
    
    /**
     * Team ID used for native Xcode code signing
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (EditCondition="bUseModernXcode && bUseModernCodeSigning", DisplayName = "Modern Code Sign Team"))
    FString ModernSigningTeam;
    
    /**
     * Bundle ID prefix used for native Xcode code signing
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (EditCondition="bUseModernXcode && bUseModernCodeSigning", DisplayName = "Bundle ID Prefix"))
    FString ModernSigningPrefix;
    
    /**
     * Bundle ID used for nativr Xcode code signing
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (EditCondition="bUseModernXcode && bUseModernCodeSigning", DisplayName = "Bundle ID"))
    FString ModernBundleIdentifier;
    
    /**
     * The App Category that will be used for Mac App Store submission
     */
    UPROPERTY(EditAnywhere, config, Category=Xcode, meta = (EditCondition="bUseModernXcode", DisplayName = "App Category"))
    FString AppCategory;
    
    /**
     * The template info.plist used for Mac game targets
     */
    UPROPERTY(EditAnywhere, config, Category="Info.plist", meta = (EditCondition="bUseModernXcode", DisplayName = "Mac Target Info.plist"))
    FFilePath TemplateMacPlist;
    
    /**
     * The template info.plist used for iOS game targets
     */
    UPROPERTY(EditAnywhere, config, Category="Info.plist", meta = (EditCondition="bUseModernXcode", DisplayName = "iOS Target Info.plist"))
    FFilePath TemplateIOSPlist;
    
    /**
     * The premade entitlement file used for development Mac builds
     */
    UPROPERTY(EditAnywhere, config, Category="Entitlement", meta = (EditCondition="bUseModernXcode", DisplayName = "Development Mac Entitlement"))
    FFilePath PremadeMacEntitlements;
    
    /**
     * The premade entitlement file used for shipping Mac builds
     */
    UPROPERTY(EditAnywhere, config, Category="Entitlement", meta = (EditCondition="bUseModernXcode", DisplayName = "Shipping Mac Entitlement"))
    FFilePath ShippingSpecificMacEntitlements;
};

class FXcodeProjectSettingsDetailsCustomization : public IDetailCustomization
{
public:
    /** Makes a new instance of this detail layout class for a specific detail view requesting it */
    static TSharedRef<IDetailCustomization> MakeInstance();

    // IDetailCustomization interface
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
    // End of IDetailCustomization interface
    
    FReply OnRestorePlistClicked();
    FReply OnRestoreEntitlementClicked();
    
    TSharedPtr<IPropertyHandle> TemplateMacPlist;
    TSharedPtr<IPropertyHandle> TemplateIOSPlist;
    TSharedPtr<IPropertyHandle> PremadeMacEntitlements;
    TSharedPtr<IPropertyHandle> ShippingEntitlements;
};
