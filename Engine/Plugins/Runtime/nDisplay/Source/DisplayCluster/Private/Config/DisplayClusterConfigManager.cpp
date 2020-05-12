// Copyright Epic Games, Inc. All Rights Reserved.

#include "Config/DisplayClusterConfigManager.h"

#include "Cluster/IPDisplayClusterClusterManager.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Config/Parser/DisplayClusterConfigParserText.h"
#include "Config/Parser/DisplayClusterConfigParserXml.h"
#include "Config/Parser/DisplayClusterConfigParserDebugAuto.h"

#include "Misc/Paths.h"

#include "Misc/DisplayClusterBuildConfig.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"

#include "HAL/FileManager.h"


FDisplayClusterConfigManager::FDisplayClusterConfigManager()
{
}

FDisplayClusterConfigManager::~FDisplayClusterConfigManager()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterConfigManager::Init(EDisplayClusterOperationMode OperationMode)
{
	return true;
}

void FDisplayClusterConfigManager::Release()
{
}

bool FDisplayClusterConfigManager::StartSession(const FString& InConfigPath, const FString& InNodeId)
{
	ConfigPath = InConfigPath;
	ClusterNodeId = InNodeId;

	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Starting session with config: %s"), *ConfigPath);

	// Load data
	return LoadConfig(ConfigPath);
}

void FDisplayClusterConfigManager::EndSession()
{
	ConfigPath.Empty();
	ClusterNodeId.Empty();

	ResetConfigData();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterConfigManager
//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster nodes
TArray<FDisplayClusterConfigClusterNode> FDisplayClusterConfigManager::GetClusterNodes() const
{
	return CfgClusterNodes;
}

int32 FDisplayClusterConfigManager::GetClusterNodesAmount() const
{
	return CfgClusterNodes.Num();
}

bool FDisplayClusterConfigManager::GetClusterNode(const FString& ClusterNodeID, FDisplayClusterConfigClusterNode& CfgClusterNode) const
{
	return GetItem(CfgClusterNodes, ClusterNodeID, CfgClusterNode, FString("GetClusterNode"));
}

bool FDisplayClusterConfigManager::GetMasterClusterNode(FDisplayClusterConfigClusterNode& CfgMasterNode) const
{
	const FDisplayClusterConfigClusterNode* const FoundItem = CfgClusterNodes.FindByPredicate([](const FDisplayClusterConfigClusterNode& Item)
	{
		return Item.IsMaster == true;
	});

	if (!FoundItem)
	{
		UE_LOG(LogDisplayClusterConfig, Error, TEXT("Master node configuration not found"));
		return false;
	}

	CfgMasterNode = *FoundItem;
	return true;
}

// Windows
int32 FDisplayClusterConfigManager::GetWindowsAmount() const
{
	return CfgWindows.Num();
}

TArray<FDisplayClusterConfigWindow> FDisplayClusterConfigManager::GetWindows() const
{
	return CfgWindows;
}

bool FDisplayClusterConfigManager::GetWindow(const FString& ID, FDisplayClusterConfigWindow& Window) const
{
	return GetItem(CfgWindows, ID, Window, FString("GetWindow"));
}

bool FDisplayClusterConfigManager::GetMasterWindow(FDisplayClusterConfigWindow& Window) const
{
	if (!GDisplayCluster)
	{
		return false;
	}

	IPDisplayClusterConfigManager* ConfigMgr = GDisplayCluster->GetPrivateConfigMgr();
	if (!ConfigMgr)
	{
		return false;
	}

	FDisplayClusterConfigClusterNode MasterClusterNode;
	if (!ConfigMgr->GetMasterClusterNode(MasterClusterNode))
	{
		return false;
	}

	return ConfigMgr->GetWindow(MasterClusterNode.Id, Window);
}

// Screens
TArray<FDisplayClusterConfigScreen> FDisplayClusterConfigManager::GetScreens() const
{
	return CfgScreens;
}

int32 FDisplayClusterConfigManager::GetScreensAmount() const
{
	return CfgScreens.Num();
}

bool FDisplayClusterConfigManager::GetScreen(const FString& ScreenID, FDisplayClusterConfigScreen& CfgScreen) const
{
	return GetItem(CfgScreens, ScreenID, CfgScreen, FString("GetScreen"));
}


// Cameras
TArray<FDisplayClusterConfigCamera> FDisplayClusterConfigManager::GetCameras() const
{
	return CfgCameras;
}

int32 FDisplayClusterConfigManager::GetCamerasAmount() const
{
	return CfgCameras.Num();
}

bool FDisplayClusterConfigManager::GetCamera(const FString& CameraID, FDisplayClusterConfigCamera& CfgCamera) const
{
	return GetItem(CfgCameras, CameraID, CfgCamera, FString("GetCamera"));
}


// Viewports
TArray<FDisplayClusterConfigViewport> FDisplayClusterConfigManager::GetViewports() const
{
	return CfgViewports;
}

int32 FDisplayClusterConfigManager::GetViewportsAmount() const
{
	return CfgViewports.Num();
}

bool FDisplayClusterConfigManager::GetViewport(const FString& ViewportID, FDisplayClusterConfigViewport& CfgViewport) const
{
	return GetItem(CfgViewports, ViewportID, CfgViewport, FString("GetViewport"));
}


// Postprocess
TArray<FDisplayClusterConfigPostprocess> FDisplayClusterConfigManager::GetPostprocess() const
{
	return CfgPostprocess;
}

int32 FDisplayClusterConfigManager::GetPostprocessAmount() const
{
	return CfgPostprocess.Num();
}

bool FDisplayClusterConfigManager::GetPostprocess(const FString& PostprocessID, FDisplayClusterConfigPostprocess& OutCfgPostprocess) const
{
	return GetItem(CfgPostprocess, PostprocessID, OutCfgPostprocess, FString("GetPostprocess"));
}


// Scene nodes
TArray<FDisplayClusterConfigSceneNode> FDisplayClusterConfigManager::GetSceneNodes() const
{
	return CfgSceneNodes;
}

int32 FDisplayClusterConfigManager::GetSceneNodesAmount() const
{
	return CfgSceneNodes.Num();
}

bool FDisplayClusterConfigManager::GetSceneNode(const FString& SceneNodeID, FDisplayClusterConfigSceneNode& CfgSceneNode) const
{
	return GetItem(CfgSceneNodes, SceneNodeID, CfgSceneNode, FString("GetSceneNode"));
}


// Input devices
TArray<FDisplayClusterConfigInput> FDisplayClusterConfigManager::GetInputDevices() const
{
	return CfgInputDevices;
}

int32 FDisplayClusterConfigManager::GetInputDevicesAmount() const
{
	return CfgInputDevices.Num();
}

bool FDisplayClusterConfigManager::GetInputDevice(const FString& InputDeviceID, FDisplayClusterConfigInput& CfgInput) const
{
	return GetItem(CfgInputDevices, InputDeviceID, CfgInput, FString("GetInputDevice"));
}

TArray<FDisplayClusterConfigInputSetup> FDisplayClusterConfigManager::GetInputSetupRecords() const
{
	return CfgInputSetupRecords;
}

bool FDisplayClusterConfigManager::GetInputSetupRecord(const FString& InputSetupID, FDisplayClusterConfigInputSetup& CfgInputSetup) const
{
	return GetItem(CfgInputSetupRecords, InputSetupID, CfgInputSetup, FString("GetInputSetupRecord"));
}

TArray<FDisplayClusterConfigProjection> FDisplayClusterConfigManager::GetProjections() const
{
	return CfgProjections;
}

bool FDisplayClusterConfigManager::GetProjection(const FString& ProjectionID, FDisplayClusterConfigProjection& CfgProjection) const
{
	return GetItem(CfgProjections, ProjectionID, CfgProjection, FString("GetProjection"));
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterConfigParserListener
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfigManager::AddInfo(const FDisplayClusterConfigInfo& InCfgInfo)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found info node: %s"), *InCfgInfo.ToString());
	CfgInfo = InCfgInfo;
}

void FDisplayClusterConfigManager::AddClusterNode(const FDisplayClusterConfigClusterNode& InCfgCNode)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found cluster node: %s"), *InCfgCNode.ToString());
	CfgClusterNodes.Add(InCfgCNode);
}

void FDisplayClusterConfigManager::AddWindow(const FDisplayClusterConfigWindow& InCfgWindow)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found window: %s"), *InCfgWindow.ToString());
	CfgWindows.Add(InCfgWindow);
}

void FDisplayClusterConfigManager::AddScreen(const FDisplayClusterConfigScreen& InCfgScreen)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found screen: %s"), *InCfgScreen.ToString());
	CfgScreens.Add(InCfgScreen);
}

void FDisplayClusterConfigManager::AddViewport(const FDisplayClusterConfigViewport& InCfgViewport)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found viewport: %s"), *InCfgViewport.ToString());
	CfgViewports.Add(InCfgViewport);
}

void FDisplayClusterConfigManager::AddPostprocess(const FDisplayClusterConfigPostprocess& InCfgPostprocess)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found postprocess: %s"), *InCfgPostprocess.ToString());
	CfgPostprocess.Add(InCfgPostprocess);
}

void FDisplayClusterConfigManager::AddCamera(const FDisplayClusterConfigCamera& InCfgCamera)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found camera: %s"), *InCfgCamera.ToString());
	CfgCameras.Add(InCfgCamera);
}

void FDisplayClusterConfigManager::AddSceneNode(const FDisplayClusterConfigSceneNode& InCfgSNode)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found scene node: %s"), *InCfgSNode.ToString());
	CfgSceneNodes.Add(InCfgSNode);
}

void FDisplayClusterConfigManager::AddInput(const FDisplayClusterConfigInput& InCfgInput)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found input device: %s"), *InCfgInput.ToString());
	CfgInputDevices.Add(InCfgInput);
}

void FDisplayClusterConfigManager::AddInputSetup(const FDisplayClusterConfigInputSetup& InCfgInputSetup)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found input setup record: %s"), *InCfgInputSetup.ToString());
	CfgInputSetupRecords.Add(InCfgInputSetup);
}

void FDisplayClusterConfigManager::AddGeneral(const FDisplayClusterConfigGeneral& InCfgGeneral)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found general: %s"), *InCfgGeneral.ToString());
	CfgGeneral = InCfgGeneral;
}

void FDisplayClusterConfigManager::AddRender(const FDisplayClusterConfigRender& InCfgRender)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found render: %s"), *InCfgRender.ToString());
	CfgRender = InCfgRender;
}

void FDisplayClusterConfigManager::AddNvidia(const FDisplayClusterConfigNvidia& InCfgNvidia)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found NVIDIA: %s"), *InCfgNvidia.ToString());
	CfgNvidia = InCfgNvidia;
}

void FDisplayClusterConfigManager::AddStereo(const FDisplayClusterConfigStereo& InCfgStereo)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found stereo: %s"), *InCfgStereo.ToString());
	CfgStereo = InCfgStereo;
}

void FDisplayClusterConfigManager::AddNetwork(const FDisplayClusterConfigNetwork& InCfgNetwork)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found network: %s"), *InCfgNetwork.ToString());
	CfgNetwork = InCfgNetwork;
}

void FDisplayClusterConfigManager::AddDebug(const FDisplayClusterConfigDebug& InCfgDebug)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found debug: %s"), *InCfgDebug.ToString());
	CfgDebug = InCfgDebug;
}

void FDisplayClusterConfigManager::AddCustom(const FDisplayClusterConfigCustom& InCfgCustom)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found custom: %s"), *InCfgCustom.ToString());
	CfgCustom = InCfgCustom;
}

void FDisplayClusterConfigManager::AddProjection(const FDisplayClusterConfigProjection& InCfgProjection)
{
	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Found projection: %s"), *InCfgProjection.ToString());
	CfgProjections.Add(InCfgProjection);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterConfigManager
//////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterConfigManager::EConfigFileType FDisplayClusterConfigManager::GetConfigFileType(const FString& InConfigPath) const
{
#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
	if (InConfigPath == DisplayClusterStrings::misc::DbgStubConfig)
	{
		UE_LOG(LogDisplayClusterConfig, Log, TEXT("Debug auto config requested"));
		return EConfigFileType::DebugAuto;
	}
#endif

	const FString ext = FPaths::GetExtension(InConfigPath).ToLower();
	if (ext == FString(DisplayClusterStrings::cfg::file::FileExtXml).ToLower())
	{
		UE_LOG(LogDisplayClusterConfig, Log, TEXT("XML config: %s"), *InConfigPath);
		return EConfigFileType::Xml;
	}
	else if (
		ext == FString(DisplayClusterStrings::cfg::file::FileExtCfg1).ToLower() ||
		ext == FString(DisplayClusterStrings::cfg::file::FileExtCfg2).ToLower() ||
		ext == FString(DisplayClusterStrings::cfg::file::FileExtCfg3).ToLower() ||
		ext == FString(DisplayClusterStrings::cfg::file::FileExtTxt).ToLower())
	{
		UE_LOG(LogDisplayClusterConfig, Log, TEXT("TXT config: %s"), *InConfigPath);
		return EConfigFileType::Text;
	}

	UE_LOG(LogDisplayClusterConfig, Warning, TEXT("Unknown file extension: %s"), *ext);
	return EConfigFileType::Unknown;
}

bool FDisplayClusterConfigManager::LoadConfig(const FString& InConfigPath)
{
	FString ConfigFile = InConfigPath;
	ConfigFile.TrimStartAndEndInline();

	if (FPaths::IsRelative(ConfigFile))
	{
		const FString ProjectDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectDir());
		ConfigFile = FPaths::ConvertRelativePathToFull(ProjectDir, ConfigFile);
	}

	// Actually the data is reset on EndFrame. This one is a safety call.
	ResetConfigData();

#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
	if (ConfigFile.Compare(FString(DisplayClusterStrings::misc::DbgStubConfig), ESearchCase::IgnoreCase) != 0 &&
		FPaths::FileExists(ConfigFile) == false)
	{
		UE_LOG(LogDisplayClusterConfig, Error, TEXT("File not found: %s"), *ConfigFile);
		return false;
	}
#else
	if (FPaths::FileExists(ConfigFile) == false)
	{
		UE_LOG(LogDisplayClusterConfig, Error, TEXT("File not found: %s"), *ConfigFile);
		return false;
	}
#endif

	// Instantiate appropriate parser
	TUniquePtr<FDisplayClusterConfigParser> Parser;
	switch (GetConfigFileType(ConfigFile))
	{
	case EConfigFileType::Text:
		Parser.Reset(new FDisplayClusterConfigParserText(this));
		break;

	case EConfigFileType::Xml:
		Parser.Reset(new FDisplayClusterConfigParserXml(this));
		break;

#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
	case EConfigFileType::DebugAuto:
		bIsDebugAuto = true;
		Parser.Reset(new FDisplayClusterConfigParserDebugAuto(this));
		break;
#endif

	default:
		UE_LOG(LogDisplayClusterConfig, Error, TEXT("Unknown config type"));
		return false;
	}

	return Parser->ParseFile(ConfigFile);
}

void FDisplayClusterConfigManager::ResetConfigData()
{
	CfgClusterNodes.Reset();
	CfgWindows.Reset();
	CfgScreens.Reset();
	CfgViewports.Reset();
	CfgPostprocess.Reset();
	CfgCameras.Reset();
	CfgSceneNodes.Reset();
	CfgInputDevices.Reset();
	CfgInputSetupRecords.Reset();
	CfgProjections.Reset();

	CfgInfo    = FDisplayClusterConfigInfo();
	CfgGeneral = FDisplayClusterConfigGeneral();
	CfgStereo  = FDisplayClusterConfigStereo();
	CfgRender  = FDisplayClusterConfigRender();
	CfgNvidia  = FDisplayClusterConfigNvidia();
	CfgNetwork = FDisplayClusterConfigNetwork();
	CfgDebug   = FDisplayClusterConfigDebug();
	CfgCustom  = FDisplayClusterConfigCustom();
}

template <typename DataType>
bool FDisplayClusterConfigManager::GetItem(const TArray<DataType>& Container, const FString& ID, DataType& Item, const FString& LogHeader) const
{
	auto pFound = Container.FindByPredicate([ID](const DataType& _item)
	{
		return _item.Id == ID;
	});

	if (!pFound)
	{
		UE_LOG(LogDisplayClusterConfig, Warning, TEXT("%s: ID not found <%s>"), *LogHeader, *ID);
		return false;
	}

	Item = *pFound;
	return true;
}

FString FDisplayClusterConfigManager::GetFullPathToFile(const FString& FileName) const
{
	if (!FPaths::FileExists(FileName))
	{
		TArray<FString> OrderedBaseDirs;

		//Add ordered search base dirs
		OrderedBaseDirs.Add(FPaths::GetPath(ConfigPath));
		OrderedBaseDirs.Add(FPaths::RootDir());

		// Process base dirs in order:
		for (auto It : OrderedBaseDirs)
		{
			FString FullPath = FPaths::ConvertRelativePathToFull(It, FileName);
			if (FPaths::FileExists(FullPath))
			{
				return FullPath;
			}
		}

		//@Handle error, file not found
		UE_LOG(LogDisplayClusterConfig, Warning, TEXT("File '%s' not found. In case of relative path do not forget to put './' at the beginning"), *FileName);
	}

	return FileName;
}

FString FDisplayClusterConfigManager::GetFullPathToNewFile(const FString& FileName) const
{
	TArray<FString> OrderedBaseDirs;

	//Add ordered search base dirs
	OrderedBaseDirs.Add(FPaths::GetPath(ConfigPath));
	OrderedBaseDirs.Add(FPaths::RootDir());

	// Process base dirs in order:
	for (auto It : OrderedBaseDirs)
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(It, FileName);
		
		if (FPaths::DirectoryExists(FPaths::GetPath(FullPath)))
		{
			return FullPath;
		}
	}

	return FileName;
}
