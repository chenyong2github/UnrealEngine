// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "PropertyHandle.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Engine/EngineTypes.h"

#include "TargetPlatformAudioCustomization.h"

/**
* Detail customization for PS4 target settings panel
*/
class FLuminTargetSettingsDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	// These are hard-wired defaults for the engine tree default icons.
	const FString DefaultIconModelPath;
	const FString DefaultIconPortalPath;
	const FString GameLuminPath;
	const FString GameProjectSetupPath;
	IDetailLayoutBuilder* SavedLayoutBuilder;

	FLuminTargetSettingsDetails();

	void BuildCertificatePicker(IDetailCategoryBuilder& Category, bool bDisableUntilConfigured);
	FReply OnPickCertificate(const FString& CertificatePath);
	FReply OnClearCertificate();

	void BuildIconPickers(IDetailCategoryBuilder& Category);
	FReply OnPickDefaultIconModel(const FString& IconModelFileName);
	FReply OnClearDefaultIconModel();
	FReply OnPickDefaultIconPortal(const FString& DirPath);
	FReply OnClearDefaultIconPortal();

	void BuildLocalizedAppNameSection();

	// Setup files.

	TAttribute<bool> SetupForPlatformAttribute;

	TSharedPtr<IPropertyHandle> IconModelPathProp;
	TSharedPtr<IPropertyHandle> IconPortalPathProp;
	TSharedPtr<IPropertyHandle> CertificateProp;

	TAttribute<FString> IconModelPathAttribute;
	TAttribute<FString> IconPortalPathAttribute;
	TAttribute<FString> CertificateAttribute;

	FString IconModelPathGetter();
	FString IconPortalPathGetter();
	FString CertificateGetter();

	void BuildAppTileSection(IDetailLayoutBuilder& DetailBuilder);
	void CopySetupFilesIntoProject();
	FReply OpenBuildFolder();
	bool CopyDir(FString SourceDir, FString TargetDir);

	// Audio section.

	FAudioPluginWidgetManager AudioPluginManager;

	void BuildAudioSection(IDetailLayoutBuilder& DetailBuilder);
};