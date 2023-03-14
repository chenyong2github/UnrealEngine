// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace UnrealBuildTool
{
	partial class MicrosoftPlatformSDK : UEBuildPlatformSDK
	{
		/// <summary>
		/// The default set of components that should be suggested to be installed for Visual Studio 2019 or 2022.
		/// This or the 2019\2022 specific components should be updated if the preferred visual cpp version changes
		/// </summary>
		static readonly string[] VisualStudioSuggestedComponents = new string[]
		{
			"Microsoft.VisualStudio.Workload.CoreEditor",
			"Microsoft.VisualStudio.Workload.NativeDesktop",
			"Microsoft.VisualStudio.Workload.NativeGame",
			"Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
			"Microsoft.VisualStudio.Component.Windows10SDK.22000",
		};

		/// <summary>
		/// Additional set of components that should be suggested to be installed for Visual Studio 2019 or 2022
		/// to support the HoloLens platform.
		/// </summary>
		static readonly string[] VisualStudioSuggestedHololensComponents = new string[]
		{
			"Microsoft.VisualStudio.Workload.Universal",
			"Microsoft.VisualStudio.Component.VC.Tools.ARM64",
		};

		/// <summary>
		/// Additional set of components that should be suggested to be installed for Visual Studio 2019 or 2022
		/// to support the Linux platform.
		/// </summary>
		static readonly string[] VisualStudioSuggestedLinuxComponents = new string[]
		{
			"Microsoft.VisualStudio.Workload.NativeCrossPlat",
		};

		/// <summary>
		/// Additional set of components that should be suggested to be installed for Visual Studio 2019.
		/// </summary>
		static readonly string[] VisualStudio2019SuggestedComponents = new string[]
		{
		};

		/// <summary>
		/// Additional set of components that should be suggested to be installed for Visual Studio 2022.
		/// </summary>
		static readonly string[] VisualStudio2022SuggestedComponents = new string[]
		{
			"Microsoft.VisualStudio.Workload.ManagedDesktop",
			"Microsoft.VisualStudio.Component.VC.14.35.17.5.x86.x64",
			"Microsoft.Net.Component.4.6.2.TargetingPack",
		};

		/// <summary>
		/// Additional set of components that should be suggested to be installed for Visual Studio 2022
		/// to support the HoloLens platform.
		/// </summary>
		static readonly string[] VisualStudio2022SuggestedHololensComponents = new string[]
		{
			"Microsoft.VisualStudio.Component.VC.14.35.17.5.ARM64",
		};

		/// <summary>
		/// Returns the list of suggested of components that should be suggested to be installed for Visual Studio.
		/// Used to generate a .vsconfig file which will prompt Visual Studio to ask the user to install these components.
		/// </summary>
		public static IEnumerable<string> GetVisualStudioSuggestedComponents(VCProjectFileFormat Format)
		{
			bool LinuxValid = InstalledPlatformInfo.IsValidPlatform(UnrealTargetPlatform.Linux) && UEBuildPlatform.IsPlatformAvailable(UnrealTargetPlatform.Linux);
			bool HololensValid = InstalledPlatformInfo.IsValidPlatform(UnrealTargetPlatform.HoloLens) && UEBuildPlatform.IsPlatformAvailable(UnrealTargetPlatform.HoloLens);

			SortedSet<string> Components = new SortedSet<string>();
			Components.UnionWith(VisualStudioSuggestedComponents);

			switch (Format)
			{
				case VCProjectFileFormat.VisualStudio2019:
					Components.UnionWith(VisualStudio2019SuggestedComponents);
					break;
				case VCProjectFileFormat.VisualStudio2022:
					Components.UnionWith(VisualStudio2022SuggestedComponents);
					if (HololensValid)
					{
						Components.UnionWith(VisualStudio2022SuggestedHololensComponents);
					}
					break;
				default:
					throw new BuildException("Unsupported Visual Studio version");
			}

			if (LinuxValid)
			{
				Components.UnionWith(VisualStudioSuggestedLinuxComponents);
			}

			if (HololensValid)
			{
				Components.UnionWith(VisualStudioSuggestedHololensComponents);
			}

			return Components;
		}
	}
}
