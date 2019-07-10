// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#define USE_NEW_PROCESS_JOBS

using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace UnrealGameSync
{
	static class Utility
	{
		public static string GetPathWithCorrectCase(FileInfo Info)
		{
			DirectoryInfo ParentInfo = Info.Directory;
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
			DirectoryInfo ParentInfo = Info.Parent;
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

		public static void WriteException(this TextWriter Writer, Exception Ex, string Format, params object[] Args)
		{
			Writer.WriteLine(Format, Args);
			foreach(string Line in Ex.ToString().Split('\n'))
			{
				Writer.WriteLine(">>>  " + Line);
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
				return ChildProcess.Start();
			}
		}

		public static int ExecuteProcess(string FileName, string WorkingDir, string CommandLine, string Input, TextWriter Log)
		{
			return ExecuteProcess(FileName, WorkingDir, CommandLine, Input, Line => Log.WriteLine(Line));
		}

		public static int ExecuteProcess(string FileName, string WorkingDir, string CommandLine, string Input, out List<string> OutputLines)
		{
			using(ChildProcess NewProcess = new ChildProcess(FileName, WorkingDir, CommandLine, Input))
			{
				OutputLines = NewProcess.ReadAllLines();
				return NewProcess.ExitCode;
			}
		}

		public static int ExecuteProcess(string FileName, string WorkingDir, string CommandLine, string Input, Action<string> OutputLine)
		{
#if USE_NEW_PROCESS_JOBS
			using(ChildProcess NewProcess = new ChildProcess(FileName, WorkingDir, CommandLine, Input))
			{
				string Line;
				while(NewProcess.TryReadLine(out Line))
				{
					OutputLine(Line);
				}
				return NewProcess.ExitCode;
			}
#else
			using(Process ChildProcess = new Process())
			{
				try
				{
					object LockObject = new object();

					DataReceivedEventHandler OutputHandler = (x, y) => { if(y.Data != null){ lock(LockObject){ OutputLine(y.Data.TrimEnd()); } } };

					ChildProcess.StartInfo.FileName = FileName;
					ChildProcess.StartInfo.Arguments = String.IsNullOrEmpty(CommandLine) ? "" : CommandLine;
					ChildProcess.StartInfo.UseShellExecute = false;
					ChildProcess.StartInfo.RedirectStandardOutput = true;
					ChildProcess.StartInfo.RedirectStandardError = true;
					ChildProcess.OutputDataReceived += OutputHandler;
					ChildProcess.ErrorDataReceived += OutputHandler;
					ChildProcess.StartInfo.RedirectStandardInput = Input != null;
					ChildProcess.StartInfo.CreateNoWindow = true;
					ChildProcess.StartInfo.StandardOutputEncoding = new System.Text.UTF8Encoding(false, false);
					ChildProcess.Start();
					ChildProcess.BeginOutputReadLine();
					ChildProcess.BeginErrorReadLine();

					if(!String.IsNullOrEmpty(Input))
					{
						ChildProcess.StandardInput.WriteLine(Input);
						ChildProcess.StandardInput.Close();
					}

					// Busy wait for the process to exit so we can get a ThreadAbortException if the thread is terminated. It won't wait until we enter managed code
					// again before it throws otherwise.
					for(;;)
					{
						if(ChildProcess.WaitForExit(20))
						{
							ChildProcess.WaitForExit();
							break;
						}
					}

					return ChildProcess.ExitCode;
				}
				finally
				{
					CloseHandle(JobObject);
				}
			}
#endif
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
		/// <param name="Variables">Lookup of variable names to values</param>
		/// <returns>String with all variables replaced</returns>
		public static string ExpandVariables(string InputString, Dictionary<string, string> AdditionalVariables = null)
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
				string Format = null;
				int FormatIdx = Name.IndexOf(':');
				if(FormatIdx != -1)
				{ 
					Format = Name.Substring(FormatIdx + 1);
					Name = Name.Substring(0, FormatIdx);
				}

				// Find the value for it, either from the dictionary or the environment block
				string Value;
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

		/// <summary>
		/// Determines if a project is an enterprise project
		/// </summary>
		/// <param name="FileName">Path to the project file</param>
		/// <returns>True if the given filename is an enterprise project</returns>
		public static bool IsEnterpriseProjectFromText(string Text)
		{
			try
			{
				JavaScriptSerializer Serializer = new JavaScriptSerializer();
				Dictionary<string, object> RawObject = Serializer.Deserialize<Dictionary<string, object>>(Text);

				object Enterprise;
				if(RawObject.TryGetValue("Enterprise", out Enterprise) && Enterprise is bool)
				{
					return (bool)Enterprise;
				}

				return false;
			}
			catch
			{
				return false;
			}
		}

		public static List<string> GetConfigFileLocations(string BaseWorkspacePath, string ProjectPath, char Separator)
		{
			List<string> ProjectConfigFileNames = new List<string>();
			ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}UnrealGameSync.ini", BaseWorkspacePath, Separator));
			ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}PS4{1}UnrealGameSync.ini", BaseWorkspacePath, Separator));
			ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}XboxOne{1}UnrealGameSync.ini", BaseWorkspacePath, Separator));
			ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}Switch{1}UnrealGameSync.ini", BaseWorkspacePath, Separator));
			ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}NotForLicensees{1}UnrealGameSync.ini", BaseWorkspacePath, Separator));
			if(ProjectPath.EndsWith(".uproject", StringComparison.InvariantCultureIgnoreCase))
			{
				ProjectConfigFileNames.Add(String.Format("{0}{1}Build{1}UnrealGameSync.ini", ProjectPath.Substring(0, ProjectPath.LastIndexOf(Separator)), Separator));
			    ProjectConfigFileNames.Add(String.Format("{0}{1}Build{1}PS4{1}UnrealGameSync.ini", ProjectPath.Substring(0, ProjectPath.LastIndexOf(Separator)), Separator));
			    ProjectConfigFileNames.Add(String.Format("{0}{1}Build{1}XboxOne{1}UnrealGameSync.ini", ProjectPath.Substring(0, ProjectPath.LastIndexOf(Separator)), Separator));
			    ProjectConfigFileNames.Add(String.Format("{0}{1}Build{1}Switch{1}UnrealGameSync.ini", ProjectPath.Substring(0, ProjectPath.LastIndexOf(Separator)), Separator));
				ProjectConfigFileNames.Add(String.Format("{0}{1}Build{1}NotForLicensees{1}UnrealGameSync.ini", ProjectPath.Substring(0, ProjectPath.LastIndexOf(Separator)), Separator));
			}
			else
			{
				ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}DefaultProject.ini", BaseWorkspacePath, Separator));
			    ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}PS4{1}DefaultProject.ini", BaseWorkspacePath, Separator));
			    ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}XboxOne{1}DefaultProject.ini", BaseWorkspacePath, Separator));
			    ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}Switch{1}DefaultProject.ini", BaseWorkspacePath, Separator));
				ProjectConfigFileNames.Add(String.Format("{0}{1}Engine{1}Programs{1}UnrealGameSync{1}NotForLicensees{1}DefaultProject.ini", BaseWorkspacePath, Separator));
			}
			return ProjectConfigFileNames;
		}

		public static void ReadGlobalPerforceSettings(ref string ServerAndPort, ref string UserName, ref string DepotPath)
		{
			using (RegistryKey Key = Registry.CurrentUser.OpenSubKey("SOFTWARE\\Epic Games\\UnrealGameSync", false))
			{
				if(Key != null)
				{
					ServerAndPort = Key.GetValue("ServerAndPort", ServerAndPort) as string;
					UserName = Key.GetValue("UserName", UserName) as string;
					DepotPath = Key.GetValue("DepotPath", DepotPath) as string;
				}
			}
		}

		public static void DeleteRegistryKey(RegistryKey RootKey, string KeyName, string ValueName)
		{
			using (RegistryKey Key = RootKey.OpenSubKey(KeyName, true))
			{
				if (Key != null)
				{
					DeleteRegistryKey(Key, ValueName);
				}
			}
		}

		public static void DeleteRegistryKey(RegistryKey Key, string Name)
		{
			string[] ValueNames = Key.GetValueNames();
			if (ValueNames.Any(x => String.Compare(x, Name, StringComparison.OrdinalIgnoreCase) == 0))
			{
				try
				{ 
					Key.DeleteValue(Name);
				}
				catch
				{ 
				}
			}
		}

		public static void SaveGlobalPerforceSettings(string ServerAndPort, string UserName, string DepotPath)
		{
			try
			{
				using (RegistryKey Key = Registry.CurrentUser.CreateSubKey("SOFTWARE\\Epic Games\\UnrealGameSync"))
				{
					// Delete this legacy setting
					DeleteRegistryKey(Key, "Server");

					if(String.IsNullOrEmpty(ServerAndPort))
					{
						try { Key.DeleteValue("ServerAndPort"); } catch(Exception) { }
					}
					else
					{
						Key.SetValue("ServerAndPort", ServerAndPort);
					}

					if(String.IsNullOrEmpty(UserName))
					{
						try { Key.DeleteValue("UserName"); } catch(Exception) { }
					}
					else
					{
						Key.SetValue("UserName", UserName);
					}

					if(String.IsNullOrEmpty(DepotPath) || (DeploymentSettings.DefaultDepotPath != null && String.Equals(DepotPath, DeploymentSettings.DefaultDepotPath, StringComparison.InvariantCultureIgnoreCase)))
					{
						DeleteRegistryKey(Key, "DepotPath");
					}
					else
					{
						Key.SetValue("DepotPath", DepotPath);
					}
				}
			}
			catch(Exception Ex)
			{
				MessageBox.Show("Unable to save settings.\n\n" + Ex.ToString());
			}
		}

		public static bool TryPrintFileUsingCache(PerforceConnection Perforce, string DepotPath, string CacheFolder, string Digest, out List<string> Lines, TextWriter Log)
		{
			if(Digest == null)
			{
				return Perforce.Print(DepotPath, out Lines, Log);
			}

			string CacheFile = Path.Combine(CacheFolder, Digest);
			if(File.Exists(CacheFile))
			{
				Log.WriteLine("Reading cached copy of {0} from {1}", DepotPath, CacheFile);
				Lines = new List<string>(File.ReadAllLines(CacheFile));
				try
				{
					File.SetLastWriteTimeUtc(CacheFile, DateTime.UtcNow);
				}
				catch(Exception Ex)
				{
					Log.WriteLine("Exception touching cache file {0}: {1}", CacheFile, Ex.ToString());
				}
				return true;
			}
			else
			{
				string TempFile = String.Format("{0}.{1}.temp", CacheFile, Guid.NewGuid());
				if(!Perforce.PrintToFile(DepotPath, TempFile, Log))
				{
					Lines = null;
					return false;
				}
				else
				{
					Lines = new List<string>(File.ReadAllLines(TempFile));
					try
					{
						File.SetAttributes(TempFile, FileAttributes.Normal);
						File.SetLastWriteTimeUtc(TempFile, DateTime.UtcNow);
						File.Move(TempFile, CacheFile);
					}
					catch
					{
						try
						{
							File.Delete(TempFile);
						}
						catch
						{
						}
					}
					return true;
				}
			}
		}

		public static void ClearPrintCache(string CacheFolder)
		{
			DirectoryInfo CacheDir = new DirectoryInfo(CacheFolder);
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

		public static PerforceConnection OverridePerforceSettings(PerforceConnection DefaultConnection, string ServerAndPort, string UserName)
		{
			string ResolvedServerAndPort = String.IsNullOrWhiteSpace(ServerAndPort) ? DefaultConnection.ServerAndPort : ServerAndPort;
			string ResolvedUserName = String.IsNullOrWhiteSpace(UserName) ? DefaultConnection.UserName : UserName;
			return new PerforceConnection(ResolvedUserName, null, ResolvedServerAndPort);
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
	}
}
