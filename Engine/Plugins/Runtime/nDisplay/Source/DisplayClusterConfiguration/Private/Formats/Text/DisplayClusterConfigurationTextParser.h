// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Formats/IDisplayClusterConfigurationDataParser.h"
#include "Formats/Text/DisplayClusterConfigurationTextTypes.h"
#include "DisplayClusterConfigurationLog.h"

class UDisplayClusterConfigurationData;


/**
 * Config parser for text based config files
 */
class FDisplayClusterConfigurationTextParser
	: public IDisplayClusterConfigurationDataParser
{
public:
	FDisplayClusterConfigurationTextParser()  = default;
	~FDisplayClusterConfigurationTextParser() = default;

public:
	// Load data from a specified file
	virtual UDisplayClusterConfigurationData* LoadData(const FString& FilePath, UObject* Owner = nullptr) override;

	// Save data to a specified file
	virtual bool SaveData(const UDisplayClusterConfigurationData* ConfigData, const FString& FilePath) override;

	// Convert configuration to string
	virtual bool AsString(const UDisplayClusterConfigurationData* ConfigData, FString& OutString) override;

protected:
	// Fill generic data container with parsed information
	UDisplayClusterConfigurationData* ConvertDataToInternalTypes();

protected:
	// Entry point for file parsing
	bool ParseTextFile(const FString& FilePath);
	// Entry point for line parsing
	void ParseTextLine(const FString& line);

protected:
	void AddInfo(const FDisplayClusterConfigurationTextInfo& InCfgInfo);
	void AddClusterNode(const FDisplayClusterConfigurationTextClusterNode& InCfgCNode);
	void AddWindow(const FDisplayClusterConfigurationTextWindow& InCfgWindow);
	void AddScreen(const FDisplayClusterConfigurationTextScreen& InCfgScreen);
	void AddViewport(const FDisplayClusterConfigurationTextViewport& InCfgViewport);
	void AddProjection(const FDisplayClusterConfigurationTextProjection& InCfgProjection);
	void AddPostprocess(const FDisplayClusterConfigurationTextPostprocess& InCfgPostprocess);
	void AddCamera(const FDisplayClusterConfigurationTextCamera& InCfgCamera);
	void AddSceneNode(const FDisplayClusterConfigurationTextSceneNode& InCfgSNode);
	void AddGeneral(const FDisplayClusterConfigurationTextGeneral& InCfgGeneral);
	void AddNvidia(const FDisplayClusterConfigurationTextNvidia& InCfgNvidia);
	void AddNetwork(const FDisplayClusterConfigurationTextNetwork& InCfgNetwork);
	void AddDebug(const FDisplayClusterConfigurationTextDebug& InCfgDebug);
	void AddCustom(const FDisplayClusterConfigurationTextCustom& InCfgCustom);

protected:
	TArray<FDisplayClusterConfigurationTextSceneNode>   CfgSceneNodes;
	TArray<FDisplayClusterConfigurationTextScreen>      CfgScreens;
	TArray<FDisplayClusterConfigurationTextCamera>      CfgCameras;
	TArray<FDisplayClusterConfigurationTextClusterNode> CfgClusterNodes;
	TArray<FDisplayClusterConfigurationTextWindow>      CfgWindows;
	TArray<FDisplayClusterConfigurationTextViewport>    CfgViewports;
	TArray<FDisplayClusterConfigurationTextProjection>  CfgProjections;
	TArray<FDisplayClusterConfigurationTextPostprocess> CfgPostprocess;

	FDisplayClusterConfigurationTextInfo    CfgInfo;
	FDisplayClusterConfigurationTextGeneral CfgGeneral;
	FDisplayClusterConfigurationTextNvidia  CfgNvidia;
	FDisplayClusterConfigurationTextNetwork CfgNetwork;
	FDisplayClusterConfigurationTextDebug   CfgDebug;
	FDisplayClusterConfigurationTextCustom  CfgCustom;

protected:
	// Data type parsing
	template <typename T>
	inline T impl_parse(const FString& Line)
	{
		static_assert(std::is_base_of<FDisplayClusterConfigurationTextBase, T>::value, "Only text based config types allowed");
		T Temp;
		bool bResult = static_cast<FDisplayClusterConfigurationTextBase&>(Temp).DeserializeFromString(Line);
		UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("Deserialization: %s"), bResult ? TEXT("ok") : TEXT("failed"));
		return Temp;
	}

	UObject* ConfigDataOwner = nullptr;
	FString  ConfigFile;
};
