// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Windows.Forms;
using System.IO;
using System.IO.Compression;

namespace UnrealGameSync
{
	public partial class DiagnosticsWindow : Form
	{
		DirectoryReference AppDataFolder;
		DirectoryReference WorkspaceDataFolder;
		List<FileReference> ExtraFiles;

		public DiagnosticsWindow(DirectoryReference InAppDataFolder, DirectoryReference InWorkspaceDataFolder, string InDiagnosticsText, IEnumerable<FileReference> InExtraFiles)
		{
			InitializeComponent();

			AppDataFolder = InAppDataFolder;
			WorkspaceDataFolder = InWorkspaceDataFolder;

			DiagnosticsTextBox.Text = InDiagnosticsText.Replace("\n", "\r\n");
			ExtraFiles = InExtraFiles.ToList();
		}

		private void ViewApplicationDataButton_Click(object sender, EventArgs e)
		{
			Process.Start("explorer.exe", AppDataFolder.FullName);
		}

		private void ViewWorkspaceDataButton_Click(object sender, EventArgs e)
		{
			Process.Start("explorer.exe", WorkspaceDataFolder.FullName);
		}

		private void SaveButton_Click(object sender, EventArgs e)
		{
			SaveFileDialog Dialog = new SaveFileDialog();
			Dialog.Filter = "Zip Files (*.zip)|*.zip|AllFiles (*.*)|*.*";
			Dialog.InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.Desktop);
			Dialog.FileName = Path.Combine(Dialog.InitialDirectory, "UGS-Diagnostics.zip");
			if(Dialog.ShowDialog() == DialogResult.OK)
			{
				FileReference DiagnosticsFileName = FileReference.Combine(AppDataFolder, "Diagnostics.txt");
				try
				{
					FileReference.WriteAllLines(DiagnosticsFileName, DiagnosticsTextBox.Lines);
				}
				catch(Exception Ex)
				{
					MessageBox.Show(String.Format("Couldn't write to '{0}'\n\n{1}", DiagnosticsFileName, Ex.ToString()));
					return;
				}

				string ZipFileName = Dialog.FileName;
				try
				{
					using (ZipArchive Zip = new ZipArchive(File.OpenWrite(ZipFileName), ZipArchiveMode.Create))
					{
						AddFilesToZip(Zip, AppDataFolder, "App/");
						AddFilesToZip(Zip, WorkspaceDataFolder, "Workspace/");

						foreach (FileReference ExtraFile in ExtraFiles)
						{
							if(FileReference.Exists(ExtraFile))
							{
								using (FileStream InputStream = FileReference.Open(ExtraFile, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
								{
									ZipArchiveEntry Entry = Zip.CreateEntry(ExtraFile.FullName.Replace(":", "").Replace('\\', '/'));
									using (Stream OutputStream = Entry.Open())
									{
										InputStream.CopyTo(OutputStream);
									}
								}
							}
						}
					}
				}
				catch(Exception Ex)
				{
					MessageBox.Show(String.Format("Couldn't save '{0}'\n\n{1}", ZipFileName, Ex.ToString()));
					return;
				}
			}
		}

		private static void AddFilesToZip(ZipArchive Zip, DirectoryReference DataFolder, string RelativeDir)
		{
			foreach (FileReference FileName in DirectoryReference.EnumerateFiles(DataFolder))
			{
				if (!FileName.HasExtension(".exe") && !FileName.HasExtension(".dll"))
				{
					using (FileStream InputStream = FileReference.Open(FileName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
					{
						ZipArchiveEntry Entry = Zip.CreateEntry(RelativeDir + FileName.MakeRelativeTo(DataFolder).Replace('\\', '/'));
						using (Stream OutputStream = Entry.Open())
						{
							InputStream.CopyTo(OutputStream);
						}
					}
				}
			}
		}
	}
}
