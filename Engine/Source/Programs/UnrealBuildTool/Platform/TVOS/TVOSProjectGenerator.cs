// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using EpicGames.Core;

namespace UnrealBuildTool
{
	class TVOSProjectGenerator : IOSProjectGenerator
    {
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Arguments">Command line arguments passed to the project generator</param>
		public TVOSProjectGenerator(CommandLineArguments Arguments)
			: base(Arguments)
		{
		}

		/// <summary>
		/// Enumerate all the platforms that this generator supports
		/// </summary>
		public override IEnumerable<UnrealTargetPlatform> GetPlatforms()
		{
			yield return UnrealTargetPlatform.TVOS;
		}
    }
}
