// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using System.IO;
using System.Linq;	
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	public sealed class UserErrorException : Exception
	{
		public int Code { get; }

		public UserErrorException(string Message, int Code = 1) : base(Message)
		{
			this.Code = Code;
		}
	}

	public class PerforceChangeDetails
	{
		/// <summary>
		/// Set of extensions to treat as code
		/// </summary>
		static readonly HashSet<string> CodeExtensions = new HashSet<string>
		{
			".c",
			".cc",
			".cpp",
			".inl",
			".m",
			".mm",
			".rc",
			".cs",
			".csproj",
			".h",
			".hpp",
			".inl",
			".usf",
			".ush",
			".uproject",
			".uplugin",
			".sln"
		};

		public string Description;
		public bool bContainsCode;
		public bool bContainsContent;

		public PerforceChangeDetails(DescribeRecord DescribeRecord)
		{
			Description = DescribeRecord.Description;

			// Check whether the files are code or content
			foreach (DescribeFileRecord File in DescribeRecord.Files)
			{
				if (CodeExtensions.Any(Extension => File.DepotFile.EndsWith(Extension, StringComparison.OrdinalIgnoreCase)))
				{
					bContainsCode = true;
				}
				else
				{
					bContainsContent = true;
				}

				if (bContainsCode && bContainsContent)
				{
					break;
				}
			}
		}
	}

	public static class Utility
	{
		static JsonSerializerOptions GetDefaultJsonSerializerOptions()
		{
			JsonSerializerOptions Options = new JsonSerializerOptions();
			Options.AllowTrailingCommas = true;
			Options.ReadCommentHandling = JsonCommentHandling.Skip;
			Options.PropertyNameCaseInsensitive = true;
			Options.Converters.Add(new JsonStringEnumConverter());
			return Options;
		}

		public static JsonSerializerOptions DefaultJsonSerializerOptions { get; } = GetDefaultJsonSerializerOptions();

		public static bool TryLoadJson<T>(FileReference File, [NotNullWhen(true)] out T? Object) where T : class
		{
			if (!FileReference.Exists(File))
			{
				Object = null;
				return false;
			}

			try
			{
				Object = LoadJson<T>(File);
				return true;
			}
			catch
			{
				Object = null;
				return false;
			}
		}

		public static T LoadJson<T>(FileReference File)
		{
			byte[] Data = FileReference.ReadAllBytes(File);
			return JsonSerializer.Deserialize<T>(Data, DefaultJsonSerializerOptions)!;
		}

		public static void SaveJson<T>(FileReference File, T Object)
		{
			JsonSerializerOptions Options = new JsonSerializerOptions { DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull, WriteIndented = true };
			Options.Converters.Add(new JsonStringEnumConverter());

			using (Stream Stream = FileReference.Open(File, FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				using (Utf8JsonWriter Writer = new Utf8JsonWriter(Stream, new JsonWriterOptions { Indented = true }))
				{
					JsonSerializer.Serialize(Writer, Object, Options);
				}
			}
		}

		public static string GetPathWithCorrectCase(FileInfo Info)
		{
			DirectoryInfo ParentInfo = Info.Directory!;
			if(Info.Exists)
			{
				return Path.Combine(GetPathWithCorrectCase(ParentInfo), ParentInfo.GetFiles(Info.Name)[0].Name); 
			}
			else
			{
				return Path.Combine(GetPathWithCorrectCase(ParentInfo), Info.Name);
			}
		}

		public static string GetPathWithCorrectCase(DirectoryInfo Info)
		{
			DirectoryInfo ParentInfo = Info.Parent!;
			if(ParentInfo == null)
			{
				return Info.FullName.ToUpperInvariant();
			}
			else if(Info.Exists)
			{
				return Path.Combine(GetPathWithCorrectCase(ParentInfo), ParentInfo.GetDirectories(Info.Name)[0].Name);
			}
			else
			{
				return Path.Combine(GetPathWithCorrectCase(ParentInfo), Info.Name);
			}
		}

		public static void ForceDeleteFile(string FileName)
		{
			if(File.Exists(FileName))
			{
				File.SetAttributes(FileName, File.GetAttributes(FileName) & ~FileAttributes.ReadOnly);
				File.Delete(FileName);
			}
		}

		public static bool SpawnProcess(string FileName, string CommandLine)
		{
			using(Process ChildProcess = new Process())
			{
				ChildProcess.StartInfo.FileName = FileName;
				ChildProcess.StartInfo.Arguments = String.IsNullOrEmpty(CommandLine) ? "" : CommandLine;
				ChildProcess.StartInfo.UseShellExecute = false;
				return ChildProcess.Start();
			}
		}

		public static bool SpawnHiddenProcess(string FileName, string CommandLine)
		{
			using(Process ChildProcess = new Process())
			{
				ChildProcess.StartInfo.FileName = FileName;
				ChildProcess.StartInfo.Arguments = String.IsNullOrEmpty(CommandLine) ? "" : CommandLine;
				ChildProcess.StartInfo.UseShellExecute = false;
				ChildProcess.StartInfo.RedirectStandardOutput = true;
				ChildProcess.StartInfo.RedirectStandardError = true;
				ChildProcess.StartInfo.CreateNoWindow = true;
				try
				{
					return ChildProcess.Start();
				}
				catch
				{
					return false;
				}
			}
		}

		public static async Task<int> ExecuteProcessAsync(string FileName, string? WorkingDir, string CommandLine, Action<string> OutputLine, CancellationToken CancellationToken)
		{
			using (ManagedProcess NewProcess = new ManagedProcess(null, FileName, CommandLine, WorkingDir, null, null, ProcessPriorityClass.Normal))
			{
				for (; ; )
				{
					string? Line = await NewProcess.ReadLineAsync(CancellationToken);
					if (Line == null)
					{
						NewProcess.WaitForExit();
						return NewProcess.ExitCode;
					}
					OutputLine(Line);
				}
			}
		}

		public static bool SafeIsFileUnderDirectory(string FileName, string DirectoryName)
		{
			try
			{
				string FullDirectoryName = Path.GetFullPath(DirectoryName).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
				string FullFileName = Path.GetFullPath(FileName);
				return FullFileName.StartsWith(FullDirectoryName, StringComparison.InvariantCultureIgnoreCase);
			}
			catch(Exception)
			{
				return false;
			}
		}

		/// <summary>
		/// Expands variables in $(VarName) format in the given string. Variables are retrieved from the given dictionary, or through the environment of the current process.
		/// Any unknown variables are ignored.
		/// </summary>
		/// <param name="InputString">String to search for variable names</param>
		/// <param name="AdditionalVariables">Lookup of variable names to values</param>
		/// <returns>String with all variables replaced</returns>
		public static string ExpandVariables(string InputString, Dictionary<string, string>? AdditionalVariables = null)
		{
			string Result = InputString;
			for (int Idx = Result.IndexOf("$("); Idx != -1; Idx = Result.IndexOf("$(", Idx))
			{
				// Find the end of the variable name
				int EndIdx = Result.IndexOf(')', Idx + 2);
				if (EndIdx == -1)
				{
					break;
				}

				// Extract the variable name from the string
				string Name = Result.Substring(Idx + 2, EndIdx - (Idx + 2));

				// Strip the format from the name
				string? Format = null;
				int FormatIdx = Name.IndexOf(':');
				if(FormatIdx != -1)
				{ 
					Format = Name.Substring(FormatIdx + 1);
					Name = Name.Substring(0, FormatIdx);
				}

				// Find the value for it, either from the dictionary or the environment block
				string? Value;
				if (AdditionalVariables == null || !AdditionalVariables.TryGetValue(Name, out Value))
				{
					Value = Environment.GetEnvironmentVariable(Name);
					if (Value == null)
					{
						Idx = EndIdx + 1;
						continue;
					}
				}

				// Encode the variable if necessary
				if(Format != null)
				{
					if(String.Equals(Format, "URI", StringComparison.InvariantCultureIgnoreCase))
					{
						Value = Uri.EscapeDataString(Value);
					}
				}

				// Replace the variable, or skip past it
				Result = Result.Substring(0, Idx) + Value + Result.Substring(EndIdx + 1);
			}
			return Result;
		}

		class ProjectJson
		{
			public bool Enterprise { get; set; }
		}

		/// <summary>
		/// Determines if a project is an enterprise project
		/// </summary>
		/// <param name="FileName">Path to the project file</param>
		/// <returns>True if the given filename is an enterprise project</returns>
		public static bool IsEnterpriseProjectFromText(string Text)
		{
			try
			{
				JsonSerializerOptions Options = new JsonSerializerOptions();
				Options.PropertyNameCaseInsensitive = true;
				Options.Converters.Add(new JsonStringEnumConverter());

				ProjectJson Project = JsonSerializer.Deserialize<ProjectJson>(Text, Options)!;

				return Project.Enterprise;
			}
			catch
			{
				return false;
			}
		}

		/******/

		private static void AddLocalConfigPaths_WithSubFolders(DirectoryInfo BaseDir, string FileName, List<FileInfo> Files)
		{
			if(BaseDir.Exists)
			{
				FileInfo BaseFileInfo = new FileInfo(Path.Combine(BaseDir.FullName, FileName));
				if(BaseFileInfo.Exists)
				{
					Files.Add(BaseFileInfo);
				}

				foreach (DirectoryInfo SubDirInfo in BaseDir.EnumerateDirectories())
				{
					FileInfo SubFile = new FileInfo(Path.Combine(SubDirInfo.FullName, FileName));
					if (SubFile.Exists)
					{
						Files.Add(SubFile);
					}
				}
			}
		}

		private static void AddLocalConfigPaths_WithExtensionDirs(DirectoryInfo BaseDir, string RelativePath, string FileName, List<FileInfo> Files)
		{
			if (BaseDir.Exists)
			{
				AddLocalConfigPaths_WithSubFolders(new DirectoryInfo(Path.Combine(BaseDir.FullName, RelativePath)), FileName, Files);

				DirectoryInfo PlatformExtensionsDir = new DirectoryInfo(Path.Combine(BaseDir.FullName, "Platforms"));
				if (PlatformExtensionsDir.Exists)
				{
					foreach (DirectoryInfo PlatformExtensionDir in PlatformExtensionsDir.EnumerateDirectories())
					{
						AddLocalConfigPaths_WithSubFolders(new DirectoryInfo(Path.Combine(PlatformExtensionDir.FullName, RelativePath)), FileName, Files);
					}
				}

				DirectoryInfo RestrictedBaseDir = new DirectoryInfo(Path.Combine(BaseDir.FullName, "Restricted"));
				if (RestrictedBaseDir.Exists)
				{
					foreach (DirectoryInfo RestrictedDir in RestrictedBaseDir.EnumerateDirectories())
					{
						AddLocalConfigPaths_WithSubFolders(new DirectoryInfo(Path.Combine(RestrictedDir.FullName, RelativePath)), FileName, Files);
					}
				}
			}
		}

		public static List<FileInfo> GetLocalConfigPaths(DirectoryInfo EngineDir, FileInfo ProjectFile)
		{
			List<FileInfo> SearchPaths = new List<FileInfo>();
			AddLocalConfigPaths_WithExtensionDirs(EngineDir, "Programs/UnrealGameSync", "UnrealGameSync.ini", SearchPaths);

			if (ProjectFile.Name.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
			{
				AddLocalConfigPaths_WithExtensionDirs(ProjectFile.Directory!, "Build", "UnrealGameSync.ini", SearchPaths);
			}
			else
			{
				AddLocalConfigPaths_WithExtensionDirs(EngineDir, "Programs/UnrealGameSync", "DefaultEngine.ini", SearchPaths);
			}
			return SearchPaths;
		}

		/******/

		private static void AddDepotConfigPaths_PlatformFolders(string BasePath, string FileName, List<string> SearchPaths)
		{
			SearchPaths.Add(String.Format("{0}/{1}", BasePath, FileName));
			SearchPaths.Add(String.Format("{0}/*/{1}", BasePath, FileName));
		}

		private static void AddDepotConfigPaths_PlatformExtensions(string BasePath, string RelativePath, string FileName, List<string> SearchPaths)
		{
			AddDepotConfigPaths_PlatformFolders(BasePath + RelativePath, FileName, SearchPaths);
			AddDepotConfigPaths_PlatformFolders(BasePath + "/Platforms/*" + RelativePath, FileName, SearchPaths);
			AddDepotConfigPaths_PlatformFolders(BasePath + "/Restricted/*" + RelativePath, FileName, SearchPaths);
		}

		public static List<string> GetDepotConfigPaths(string EnginePath, string ProjectPath)
		{
			List<string> SearchPaths = new List<string>();
			AddDepotConfigPaths_PlatformExtensions(EnginePath, "/Programs/UnrealGameSync", "UnrealGameSync.ini", SearchPaths);

			if (ProjectPath.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
			{
				AddDepotConfigPaths_PlatformExtensions(ProjectPath.Substring(0, ProjectPath.LastIndexOf('/')), "/Build", "UnrealGameSync.ini", SearchPaths);
			}
			else
			{
				AddDepotConfigPaths_PlatformExtensions(EnginePath, "/Programs/UnrealGameSync", "DefaultEngine.ini", SearchPaths);
			}
			return SearchPaths;
		}

		/******/

		public static async Task<string[]?> TryPrintFileUsingCacheAsync(IPerforceConnection Perforce, string DepotPath, DirectoryReference CacheFolder, string? Digest, ILogger Logger, CancellationToken CancellationToken)
		{
			if(Digest == null)
			{
				PerforceResponse<PrintRecord<string[]>> Response = await Perforce.TryPrintLinesAsync(DepotPath, CancellationToken);
				if (Response.Succeeded)
				{
					return Response.Data.Contents;
				}
				else
				{
					return null;
				}
			}

			FileReference CacheFile = FileReference.Combine(CacheFolder, Digest);
			if(FileReference.Exists(CacheFile))
			{
				Logger.LogDebug("Reading cached copy of {DepotFile} from {LocalFile}", DepotPath, CacheFile);
				string[] Lines = FileReference.ReadAllLines(CacheFile);
				try
				{
					FileReference.SetLastWriteTimeUtc(CacheFile, DateTime.UtcNow);
				}
				catch(Exception Ex)
				{
					Logger.LogWarning(Ex, "Exception touching cache file {LocalFile}", CacheFile);
				}
				return Lines;
			}
			else
			{
				DirectoryReference.CreateDirectory(CacheFolder);

				FileReference TempFile = new FileReference(String.Format("{0}.{1}.temp", CacheFile.FullName, Guid.NewGuid()));
				PerforceResponse<PrintRecord> Response = await Perforce.TryPrintAsync(TempFile.FullName, DepotPath, CancellationToken);
				if (!Response.Succeeded)
				{
					return null;
				}
				else
				{
					string[] Lines = await FileReference.ReadAllLinesAsync(TempFile);
					try
					{
						FileReference.SetAttributes(TempFile, FileAttributes.Normal);
						FileReference.SetLastWriteTimeUtc(TempFile, DateTime.UtcNow);
						FileReference.Move(TempFile, CacheFile);
					}
					catch
					{
						try
						{
							FileReference.Delete(TempFile);
						}
						catch
						{
						}
					}
					return Lines;
				}
			}
		}

		public static void ClearPrintCache(DirectoryReference CacheFolder)
		{
			DirectoryInfo CacheDir = CacheFolder.ToDirectoryInfo();
			if(CacheDir.Exists)
			{
				DateTime DeleteTime = DateTime.UtcNow - TimeSpan.FromDays(5.0);
				foreach(FileInfo CacheFile in CacheDir.EnumerateFiles())
				{
					if(CacheFile.LastWriteTimeUtc < DeleteTime || CacheFile.Name.EndsWith(".temp", StringComparison.OrdinalIgnoreCase))
					{
						try
						{
							CacheFile.Attributes = FileAttributes.Normal;
							CacheFile.Delete();
						}
						catch
						{
						}
					}
				}
			}
		}

		public static Color Blend(Color First, Color Second, float T)
		{
			return Color.FromArgb((int)(First.R + (Second.R - First.R) * T), (int)(First.G + (Second.G - First.G) * T), (int)(First.B + (Second.B - First.B) * T));
		}

		public static PerforceSettings OverridePerforceSettings(IPerforceSettings DefaultConnection, string? ServerAndPort, string? UserName)
		{
			PerforceSettings NewSettings = new PerforceSettings(DefaultConnection);
			if(!String.IsNullOrWhiteSpace(ServerAndPort))
			{
				NewSettings.ServerAndPort = ServerAndPort;
			}
			if (!String.IsNullOrWhiteSpace(UserName))
			{
				NewSettings.UserName = UserName;
			}
			return NewSettings;
		}

		public static string FormatRecentDateTime(DateTime Date)
		{
			DateTime Now = DateTime.Now;

			DateTime Midnight = new DateTime(Now.Year, Now.Month, Now.Day);
			DateTime MidnightTonight = Midnight + TimeSpan.FromDays(1.0);

			if(Date > MidnightTonight)
			{
				return String.Format("{0} at {1}", Date.ToLongDateString(), Date.ToShortTimeString());
			}
			else if(Date >= Midnight)
			{
				return String.Format("today at {0}", Date.ToShortTimeString());
			}
			else if(Date >= Midnight - TimeSpan.FromDays(1.0))
			{
				return String.Format("yesterday at {0}", Date.ToShortTimeString());
			}
			else if(Date >= Midnight - TimeSpan.FromDays(5.0))
			{
				return String.Format("{0:dddd} at {1}", Date, Date.ToShortTimeString());
			}
			else
			{
				return String.Format("{0} at {1}", Date.ToLongDateString(), Date.ToShortTimeString());
			}
		}

		public static string FormatDurationMinutes(TimeSpan Duration)
		{
			return FormatDurationMinutes((int)(Duration.TotalMinutes + 1));
		}

		public static string FormatDurationMinutes(int TotalMinutes)
		{
			if(TotalMinutes > 24 * 60)
			{
				return String.Format("{0}d {1}h", TotalMinutes / (24 * 60), (TotalMinutes / 60) % 24);
			}
			else if(TotalMinutes > 60)
			{
				return String.Format("{0}h {1}m", TotalMinutes / 60, TotalMinutes % 60);
			}
			else
			{
				return String.Format("{0}m", TotalMinutes);
			}
		}

		public static string FormatUserName(string UserName)
		{
			StringBuilder NormalUserName = new StringBuilder();
			for(int Idx = 0; Idx < UserName.Length; Idx++)
			{
				if(Idx == 0 || UserName[Idx - 1] == '.')
				{
					NormalUserName.Append(Char.ToUpper(UserName[Idx]));
				}
				else if(UserName[Idx] == '.')
				{
					NormalUserName.Append(' ');
				}
				else
				{
					NormalUserName.Append(Char.ToLower(UserName[Idx]));
				}
			}
			return NormalUserName.ToString();
		}

		public static void OpenUrl(string Url)
		{
			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = Url;
			StartInfo.UseShellExecute = true;
			using Process? _ = Process.Start(StartInfo);
		}
	}
}
