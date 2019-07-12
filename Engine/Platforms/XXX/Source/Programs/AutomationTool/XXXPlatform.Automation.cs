// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;
using System.Diagnostics;
using System.Net.NetworkInformation;
using System.Threading;
using AutomationTool;
using UnrealBuildTool;
using Tools.DotNETCommon;

public class XXXPlatform : Platform
{
	public XXXPlatform() : base(UnrealTargetPlatform.XXX)
	{
	}

	protected override string GetPlatformExeExtension()
	{
		return ".xexe";
	}
	private string GetBatchPath(ProjectParams Params, DeploymentContext SC)
	{
		string BinariesLocation = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(Params.RawProjectPath.FullName)), "Binaries/XXX");
		// override if prebuilt packaged build
		if (Params.Prebuilt)
		{
			BinariesLocation = Path.Combine(Params.BaseStageDirectory, "XXX");
		}
		// override if creating a release version
		if (Params.HasCreateReleaseVersion && SC != null)
		{
			BinariesLocation = Params.GetCreateReleaseVersionPath(SC, Params.Client);
		}

		return Path.Combine(BinariesLocation, Params.ShortProjectName + ".bat");
	}

	public override string GetPlatformPakCommandLine(ProjectParams Params, DeploymentContext SC)
	{
		string PakParams = "";

		return PakParams;
	}


	public override void Package(ProjectParams Params, DeploymentContext SC, int WorkingCL)
	{
		if (SC.StageTargetConfigurations.Count != 1)
		{
			throw new AutomationException(ExitCode.Error_OnlyOneTargetConfigurationSupported, "XXX is currently only able to package one target configuration at a time, but StageTargetConfigurations contained {0} configurations", SC.StageTargetConfigurations.Count);
		}

		string BatchPath = GetBatchPath(Params, SC);
		File.WriteAllText(BatchPath, string.Format("notepad {0}", Params.GetProjectExeForPlatform(UnrealTargetPlatform.XXX).ToString()));

		PrintRunTime();
	}


	public override void GetFilesToArchive(ProjectParams Params, DeploymentContext SC)
	{
		if (SC.StageTargetConfigurations.Count != 1)
		{
			throw new AutomationException(ExitCode.Error_OnlyOneTargetConfigurationSupported, "Android is currently only able to package one target configuration at a time, but StageTargetConfigurations contained {0} configurations", SC.StageTargetConfigurations.Count);
		}


	}
	

	public override bool RetrieveDeployedManifests(ProjectParams Params, DeploymentContext SC, string DeviceName, out List<string> UFSManifests, out List<string> NonUFSManifests)
	{
		UFSManifests = null;
		NonUFSManifests = null;

		return false;
	}

    public override void Deploy(ProjectParams Params, DeploymentContext SC)
    {

    }


	public override IProcessResult RunClient(ERunOptions ClientRunFlags, string ClientApp, string ClientCmdLine, ProjectParams Params)
	{
		string BatchPath = GetBatchPath(Params, null);

		return Run(BatchPath, ClientCmdLine, null, ERunOptions.Default);
	}

	public override void GetFilesToDeployOrStage(ProjectParams Params, DeploymentContext SC)
	{
	}

    /// <summary>
    /// Gets cook platform name for this platform.
    /// </summary>
    /// <returns>Cook platform string.</returns>
    public override string GetCookPlatform(bool bDedicatedServer, bool bIsClientOnly)
	{
		return bIsClientOnly ? "XXXClient" : "XXX";
	}

	public override bool DeployLowerCaseFilenames()
	{
		return false;
	}

	public override string LocalPathToTargetPath(string LocalPath, string LocalRoot)
	{
		return LocalPath.Replace("\\", "/").Replace(LocalRoot, "../../..");
	}

	public override bool IsSupported { get { return true; } }

	public override PakType RequiresPak(ProjectParams Params)
	{
		return PakType.DontCare;
	}
    public override bool SupportsMultiDeviceDeploy
    {
        get
        {
            return true;
        }
    }

    /*
        public override bool RequiresPackageToDeploy
        {
            get { return true; }
        }
    */

	public override List<string> GetDebugFileExtensions()
	{
		return new List<string> { };
	}

	public override void StripSymbols(FileReference SourceFile, FileReference TargetFile)
	{
	}
}
