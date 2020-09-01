// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Windows.Forms;
using Microsoft.VisualStudio.Setup.Configuration;
using EnvDTE;
using System.Diagnostics;

namespace UnrealGameSync
{

	static class VisualStudioAutomation
	{
		public static bool OpenFile(string FileName, out string ErrorMessage, int Line = -1)
		{
			ErrorMessage = null;

			// first try to open via DTE
			DTE DTE = VisualStudioAccessor.GetDTE();
			if (DTE != null)
			{
				DTE.ItemOperations.OpenFile(FileName);
				DTE.MainWindow.Activate();

				if (Line != -1)
				{
					(DTE.ActiveDocument.Selection as TextSelection).GotoLine(Line);
				}

				return true;
			}

			// unable to get DTE connection, so launch nesw VS instance
			string Arguments = string.Format("\"{0}\"", FileName);
			if (Line != -1)
			{
				Arguments += string.Format(" /command \"edit.goto {0}\"", Line);
			}

			// Launch new visual studio instance
			if (!VisualStudioAccessor.LaunchVisualStudio(Arguments, out ErrorMessage))
			{
				return false;
			}

			return true;
		}
	}

	/// <summary>
	/// Visual Studio automation accessor
	/// </summary>
	static class VisualStudioAccessor
	{
		public static bool LaunchVisualStudio(string Arguments, out string ErrorMessage)
		{
			ErrorMessage = "";
			VisualStudioInstallation Install = VisualStudioInstallations.GetPreferredInstallation();

			if (Install == null)
			{
				ErrorMessage = string.Format("Unable to get Visual Studio installation");
				return false;
			}
			try
			{

				System.Diagnostics.Process VSProcess = new System.Diagnostics.Process { StartInfo = new ProcessStartInfo(Install.DevEnvPath, Arguments) };
				VSProcess.Start();
				VSProcess.WaitForInputIdle();
			}
			catch (Exception Ex)
			{
				ErrorMessage = Ex.Message;
				return false;
			}

			return true;

		}

		[STAThread]
		public static DTE GetDTE()
		{
			IRunningObjectTable Table;
			if (Succeeded(GetRunningObjectTable(0, out Table)) && Table != null)
			{
				IEnumMoniker MonikersTable;
				Table.EnumRunning(out MonikersTable);

				if (MonikersTable == null)
				{
					return null;
				}

				MonikersTable.Reset();

				// Look for all visual studio instances in the ROT
				IMoniker[] Monikers = new IMoniker[] { null };
				while (MonikersTable.Next(1, Monikers, IntPtr.Zero) == 0)
				{
					IBindCtx BindContext;
					string OutDisplayName;
					IMoniker CurrentMoniker = Monikers[0];

					if (!Succeeded(CreateBindCtx(0, out BindContext)))
					{
						continue;
					}

					try
					{
						CurrentMoniker.GetDisplayName(BindContext, null, out OutDisplayName);
						if (string.IsNullOrEmpty(OutDisplayName) || !IsVisualStudioDTEMoniker(OutDisplayName))
						{
							continue;
						}
					}
					catch (UnauthorizedAccessException)
					{
						// Some ROT objects require elevated permissions
						continue;
					}

					object ComObject;
					if (!Succeeded(Table.GetObject(CurrentMoniker, out ComObject)))
					{
						continue;
					}

					return ComObject as DTE;
				}
			}

			return null;

		}

		static bool IsVisualStudioDTEMoniker(string InName)
		{
			VisualStudioInstallation[] Installs = VisualStudioInstallations.Installs;

			for (int Idx = 0; Idx < Installs.Length; Idx++)
			{
				if (InName.StartsWith(Installs[Idx].ROTMoniker))
				{
					return true;
				}
			}

			return false;
		}

		static bool Succeeded(int Result)
		{
			return Result >= 0;
		}


		[DllImport("ole32.dll")]
		public static extern int CreateBindCtx(int Reserved, out IBindCtx BindCtx);

		[DllImport("ole32.dll")]
		public static extern int GetRunningObjectTable(int Reserved, out IRunningObjectTable ROT);

	}

	class VisualStudioInstallation
	{
		/// <summary>
		/// Base directory for the installation
		/// </summary>
		public string BaseDir;


		/// <summary>
		/// Path of the devenv executable
		/// </summary>
		public string DevEnvPath;

		/// <summary>
		/// Visual Studio major version number
		/// </summary>
		public int MajorVersion;

		/// <summary>
		///  Running Object Table moniker for this installation
		/// </summary>
		public string ROTMoniker;

	}

	/// <summary>
	///  
	/// </summary>
	static class VisualStudioInstallations
	{

		public static VisualStudioInstallation GetPreferredInstallation(int MajorVersion = 0)
		{

			if (CachedInstalls.Count == 0)
			{
				return null;
			}

			if (MajorVersion == 0)
			{
				return CachedInstalls.First();
			}

			VisualStudioInstallation Installation = CachedInstalls.FirstOrDefault(Install => { return Install.MajorVersion == MajorVersion; });

			return Installation;

		}

		public static VisualStudioInstallation[] Installs
		{
			get { return CachedInstalls.ToArray(); }
		}

		public static void Refresh()
		{
			GetVisualStudioInstallations();
		}

		static List<VisualStudioInstallation> GetVisualStudioInstallations()
		{
			CachedInstalls.Clear();

			try
			{
				SetupConfiguration Setup = new SetupConfiguration();
				IEnumSetupInstances Enumerator = Setup.EnumAllInstances();

				ISetupInstance[] Instances = new ISetupInstance[1];
				for (; ; )
				{
					int NumFetched;
					Enumerator.Next(1, Instances, out NumFetched);

					if (NumFetched == 0)
					{
						break;
					}

					ISetupInstance2 Instance = (ISetupInstance2)Instances[0];
					if ((Instance.GetState() & InstanceState.Local) == InstanceState.Local)
					{
						string VersionString = Instance.GetInstallationVersion();
						string[] Components = VersionString.Split('.');

						if (Components.Length == 0)
						{
							continue;
						}

						int MajorVersion;
						string InstallationPath = Instance.GetInstallationPath();
						string DevEnvPath = Path.Combine(InstallationPath, "Common7\\IDE\\devenv.exe");

						if (!int.TryParse(Components[0], out MajorVersion) || (MajorVersion != 15 && MajorVersion != 16))
						{
							continue;
						}


						if (!File.Exists(DevEnvPath))
						{
							continue;
						}

						VisualStudioInstallation Installation = new VisualStudioInstallation() { BaseDir = InstallationPath, DevEnvPath = DevEnvPath, MajorVersion = MajorVersion, ROTMoniker = string.Format("!VisualStudio.DTE.{0}.0", MajorVersion) };

						CachedInstalls.Add(Installation);
					}
				}
			}
			catch (Exception Ex)
			{
				MessageBox.Show(string.Format("Exception while finding Visual Studio installations {0}", Ex.Message));
			}

			// prefer newer versions
			CachedInstalls.Sort((A, B) => { return -A.MajorVersion.CompareTo(B.MajorVersion); });

			return CachedInstalls;
		}

		static VisualStudioInstallations()
		{
			GetVisualStudioInstallations();
		}

		static readonly List<VisualStudioInstallation> CachedInstalls = new List<VisualStudioInstallation>();

	}

}