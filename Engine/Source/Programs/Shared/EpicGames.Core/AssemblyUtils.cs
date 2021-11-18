// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;

namespace EpicGames.Core
{
	public static class AssemblyUtils
	{
		/// <summary>
		/// Gets the original location (path and filename) of an assembly.
		/// This method is using Assembly.CodeBase property to properly resolve original
		/// assembly path in case shadow copying is enabled.
		/// </summary>
		/// <returns>Absolute path and filename to the assembly.</returns>
		public static string GetOriginalLocation(this Assembly ThisAssembly)
		{
			return new Uri(ThisAssembly.Location).LocalPath;
		}

		/// <summary>
		/// Version info of the executable which runs this code.
		/// </summary>
		public static FileVersionInfo ExecutableVersion
		{
			get
			{
				return FileVersionInfo.GetVersionInfo(Assembly.GetEntryAssembly()!.GetOriginalLocation());
			}
		}

		/// <summary>
		/// Installs an assembly resolver. Mostly used to get shared assemblies that we don't want copied around to various output locations as happens when "Copy Local" is set to true
		/// for an assembly reference (which is the default).
		/// </summary>
		public static void InstallAssemblyResolver(string PathToBinariesDotNET)
		{
			AppDomain.CurrentDomain.AssemblyResolve += (sender, args) =>
			{
				// Name is fully qualified assembly definition - e.g. "p4dn, Version=1.0.0.0, Culture=neutral, PublicKeyToken=ff968dc1933aba6f"
				string AssemblyName = args.Name!.Split(',')[0];

				return (
					from KnownAssemblyName in new[] { "SwarmAgent.exe", "../ThirdParty/Ionic/Ionic.Zip.Reduced.dll", "../ThirdParty/Newtonsoft/NewtonSoft.Json.dll" }
					where AssemblyName.Equals(Path.GetFileNameWithoutExtension(KnownAssemblyName), StringComparison.InvariantCultureIgnoreCase)
					let ResolvedAssemblyFilename = Path.Combine(PathToBinariesDotNET, KnownAssemblyName)
					// check if the file exists first. If we just try to load it, we correctly throw an exception, but it's a generic
					// FileNotFoundException, which is not informative. Better to return null.
					select File.Exists(ResolvedAssemblyFilename) ? Assembly.LoadFile(ResolvedAssemblyFilename) : null
					).FirstOrDefault();
			};
		}

		/// <summary>
		/// Installs an assembly resolver, which will load *any* assembly which exists recursively within the supplied folder.
		/// </summary>
		/// <param name="RootDirectory">The directory to enumerate.</param>
		public static void InstallRecursiveAssemblyResolver(string RootDirectory)
		{
			RefreshAssemblyCache(RootDirectory);

			AppDomain.CurrentDomain.AssemblyResolve += (sender, args) =>
			{
				// Name is fully qualified assembly definition - e.g. "p4dn, Version=1.0.0.0, Culture=neutral, PublicKeyToken=ff968dc1933aba6f"
				string AssemblyName = args.Name!.Split(',')[0];
				if (AssemblyLocationCache.TryGetValue(AssemblyName, out string? AssemblyLocation))
				{
					// We have this assembly in our folder.					
					if (File.Exists(AssemblyLocation))
					{
						// The assembly still exists, so load it.
						return Assembly.LoadFile(AssemblyLocation);
					}
					else
					{
						// The assembly no longer exists on disk, so remove it from our cache.
						AssemblyLocationCache.Remove(AssemblyName);
					}
				}

				// The assembly wasn't found, though may have been compiled or copied as a dependency
				RefreshAssemblyCache(RootDirectory, string.Format("{0}.dll", AssemblyName));
				if (AssemblyLocationCache.TryGetValue(AssemblyName, out AssemblyLocation))
				{
					return Assembly.LoadFile(AssemblyLocation);
				}

				return null;
			};
		}

		private static void RefreshAssemblyCache(string RootDirectory, string Pattern = "*.dll")
		{
			// Initialize our cache of assemblies by enumerating all files in the given folder.
			foreach (string DiscoveredAssembly in Directory.EnumerateFiles(RootDirectory, Pattern, SearchOption.AllDirectories))
			{
				AddFileToAssemblyCache(DiscoveredAssembly);
			}

		}

		public static void AddFileToAssemblyCache(string AssemblyPath)
		{
			string AssemblyName = Path.GetFileNameWithoutExtension(AssemblyPath);
			DateTime AssemblyLastWriteTime = File.GetLastWriteTimeUtc(AssemblyPath);
			if (AssemblyLocationCache.ContainsKey(AssemblyName))
			{
				// We already have this assembly in our cache. Only replace it if the discovered file is newer (to avoid stale assemblies breaking stuff).
				if (AssemblyLastWriteTime > AssemblyWriteTimes[AssemblyName])
				{
					AssemblyLocationCache[AssemblyName] = AssemblyPath;
					AssemblyWriteTimes[AssemblyName] = AssemblyLastWriteTime;
				}
			}
			else
			{
				// This is the first copy of this assembly ... add it to our cache.
				AssemblyLocationCache.Add(AssemblyName, AssemblyPath);
				AssemblyWriteTimes.Add(AssemblyName, AssemblyLastWriteTime);
			}
		}

		// Map of assembly name to path on disk
		private static readonly Dictionary<string, string> AssemblyLocationCache = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		// Track last modified date of each assembly, so we can ensure we always reference the latest one in the case of stale assemblies on disk.
		private static readonly Dictionary<string, DateTime> AssemblyWriteTimes = new Dictionary<string, DateTime>(StringComparer.OrdinalIgnoreCase);

	}
}
