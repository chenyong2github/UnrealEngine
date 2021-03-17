// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.Windows.Forms;
using System.IO;
using System.IO.Compression;

namespace UnrealGameSync
{
	public partial class DiagnosticsWindow : Form
	{
		string DataFolder;

		public DiagnosticsWindow(string InDataFolder, string InDiagnosticsText)
		{
			InitializeComponent();
			DataFolder = InDataFolder;
			DiagnosticsTextBox.Text = InDiagnosticsText.Replace("\n", "\r\n");
		}

		private void ViewLogsButton_Click(object sender, EventArgs e)
		{
			Process.Start("explorer.exe", DataFolder);
		}

		private void SaveButton_Click(object sender, EventArgs e)
		{
			SaveFileDialog Dialog = new SaveFileDialog();
			Dialog.Filter = "Zip Files (*.zip)|*.zip|AllFiles (*.*)|*.*";
			Dialog.InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.Desktop);
			Dialog.FileName = Path.Combine(Dialog.InitialDirectory, "UGS-Diagnostics.zip");
			if(Dialog.ShowDialog() == DialogResult.OK)
			{
				string DiagnosticsFileName = Path.Combine(DataFolder, "Diagnostics.txt");
				try
				{
					File.WriteAllLines(DiagnosticsFileName, DiagnosticsTextBox.Lines);
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
						foreach (string FileName in Directory.EnumerateFiles(DataFolder))
						{
							if (!FileName.EndsWith(".exe", StringComparison.InvariantCultureIgnoreCase) && !FileName.EndsWith(".dll", StringComparison.InvariantCultureIgnoreCase))
							{
								using (FileStream InputStream = File.Open(FileName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
								{
									ZipArchiveEntry Entry = Zip.CreateEntry(Path.GetFileName(FileName));
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
	}
}
