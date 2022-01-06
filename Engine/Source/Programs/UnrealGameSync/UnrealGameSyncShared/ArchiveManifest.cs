// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	[DebuggerDisplay("{FileName}")]
	public class ArchiveManifestFile
	{
		public string FileName;
		public long Length;
		public DateTime LastWriteTimeUtc;

		public ArchiveManifestFile(BinaryReader Reader)
		{
			FileName = Reader.ReadString();
			Length = Reader.ReadInt64();
			LastWriteTimeUtc = new DateTime(Reader.ReadInt64());
		}

		public ArchiveManifestFile(string InFileName, long InLength, DateTime InLastWriteTimeUtc)
		{
			FileName = InFileName;
			Length = InLength;
			LastWriteTimeUtc = InLastWriteTimeUtc;
		}

		public void Write(BinaryWriter Writer)
		{
			Writer.Write(FileName);
			Writer.Write(Length);
			Writer.Write(LastWriteTimeUtc.Ticks);
		}
	}

	public class ArchiveManifest
	{
		const int Signature = ((int)'U' << 24) | ((int)'A' << 16) | ((int)'M' << 8) | 1;

		public List<ArchiveManifestFile> Files = new List<ArchiveManifestFile>();

		public ArchiveManifest()
		{
		}

		public ArchiveManifest(FileStream InputStream)
		{
			BinaryReader Reader = new BinaryReader(InputStream);
			if(Reader.ReadInt32() != Signature)
			{
				throw new Exception("Archive manifest signature does not match");
			}

			int NumFiles = Reader.ReadInt32();
			for(int Idx = 0; Idx < NumFiles; Idx++)
			{
				Files.Add(new ArchiveManifestFile(Reader));
			}
		}

		public void Write(FileStream OutputStream)
		{
			BinaryWriter Writer = new BinaryWriter(OutputStream);
			Writer.Write(Signature);
			Writer.Write(Files.Count);
			foreach(ArchiveManifestFile File in Files)
			{
				File.Write(Writer);
			}
		}
	}

	public static class ArchiveUtils
	{
		public static void ExtractFiles(FileReference ArchiveFileName, DirectoryReference BaseDirectoryName, FileReference? ManifestFileName, ProgressValue Progress, ILogger Logger)
		{
			DateTime TimeStamp = DateTime.UtcNow;
			using (ZipArchive Zip = new ZipArchive(File.OpenRead(ArchiveFileName.FullName)))
			{
				if (ManifestFileName != null)
				{
					FileReference.Delete(ManifestFileName);

					// Create the manifest
					ArchiveManifest Manifest = new ArchiveManifest();
					foreach (ZipArchiveEntry Entry in Zip.Entries)
					{
						if (!Entry.FullName.EndsWith("/") && !Entry.FullName.EndsWith("\\"))
						{
							Manifest.Files.Add(new ArchiveManifestFile(Entry.FullName, Entry.Length, TimeStamp));
						}
					}

					// Write it out to a temporary file, then move it into place
					FileReference TempManifestFileName = ManifestFileName + ".tmp";
					using (FileStream OutputStream = FileReference.Open(TempManifestFileName, FileMode.Create, FileAccess.Write))
					{
						Manifest.Write(OutputStream);
					}
					FileReference.Move(TempManifestFileName, ManifestFileName);
				}

				// Extract all the files
				int EntryIdx = 0;
				foreach(ZipArchiveEntry Entry in Zip.Entries)
				{
					if(!Entry.FullName.EndsWith("/") && !Entry.FullName.EndsWith("\\"))
					{
						FileReference FileName = FileReference.Combine(BaseDirectoryName, Entry.FullName);
						DirectoryReference.CreateDirectory(FileName.Directory);
						Logger.LogInformation("Writing {0}", FileName);

						Entry.ExtractToFile(FileName.FullName, true);
						FileReference.SetLastWriteTimeUtc(FileName, TimeStamp);
					}
					Progress.Set((float)++EntryIdx / (float)Zip.Entries.Count);
				}
			}
		}

		public static void RemoveExtractedFiles(DirectoryReference BaseDirectoryName, FileReference ManifestFileName, ProgressValue Progress, ILogger LogWriter)
		{
			// Read the manifest in
			ArchiveManifest Manifest;
			using(FileStream InputStream = FileReference.Open(ManifestFileName, FileMode.Open, FileAccess.Read))
			{
				Manifest = new ArchiveManifest(InputStream);
			}

			// Remove all the files that haven't been modified match
			for(int Idx = 0; Idx < Manifest.Files.Count; Idx++)
			{
				FileInfo File = FileReference.Combine(BaseDirectoryName, Manifest.Files[Idx].FileName).ToFileInfo();
				if(File.Exists)
				{
					if(File.Length != Manifest.Files[Idx].Length)
					{
						LogWriter.LogInformation("Skipping {FileName} due to modified length", File.FullName);
					}
					else if(Math.Abs((File.LastWriteTimeUtc - Manifest.Files[Idx].LastWriteTimeUtc).TotalSeconds) > 2.0)
					{
						LogWriter.LogInformation("Skipping {FileName} due to modified timestamp", File.FullName);
					}
					else
					{
						LogWriter.LogInformation("Removing {FileName}", File.FullName);
						File.Delete();
					}
				}
				Progress.Set((float)(Idx + 1) / (float)Manifest.Files.Count);
			}
		}
	}
}
