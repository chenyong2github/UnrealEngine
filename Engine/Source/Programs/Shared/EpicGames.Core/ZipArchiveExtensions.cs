// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Runtime.InteropServices;
using System.IO.Compression;
using System.Reflection;

namespace EpicGames.Core
{
	/// <summary>
	/// Additional functionality around <see cref="System.IO.Compression.ZipArchive"/> to support non-Windows filesystems
	/// </summary>
	public static class ZipArchiveExtensions
	{
		/// <summary>
		/// Create a zip archive entry, preserving platform mode bits
		/// </summary>
		/// <param name="Destination"></param>
		/// <param name="SourceFileName"></param>
		/// <param name="EntryName"></param>
		/// <param name="CompressionLevel"></param>
		/// <returns></returns>
		public static ZipArchiveEntry CreateEntryFromFile_CrossPlatform(this ZipArchive Destination, string SourceFileName, string EntryName, CompressionLevel CompressionLevel)
		{
			ZipArchiveEntry Entry = ZipFileExtensions.CreateEntryFromFile(Destination, SourceFileName, EntryName, CompressionLevel);
			int Result = -1;

			if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				Result = FileUtils.GetFileMode_Linux(SourceFileName);
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				Result = FileUtils.GetFileMode_Mac(SourceFileName);
			}

			if(Result >= 0)
			{
				Entry.ExternalAttributes = (int)Result << 16;
			}

			return Entry;
		}

		/// <summary>
		/// Internal field storing information about the platform that created a ZipArchiveEntry. Cannot interpret how to treat the attribute bits without reading this.
		/// </summary>
		static FieldInfo VersionMadeByPlatformField = typeof(ZipArchiveEntry).GetField("_versionMadeByPlatform", BindingFlags.NonPublic | BindingFlags.Instance)!;

		/// <summary>
		/// Extract a zip archive entry, preserving platform mode bits
		/// </summary>
		/// <param name="Entry"></param>
		/// <param name="TargetFileName"></param>
		/// <param name="Overwrite"></param>
		public static void ExtractToFile_CrossPlatform(this ZipArchiveEntry Entry, string TargetFileName, bool Overwrite)
		{
			ZipFileExtensions.ExtractToFile(Entry, TargetFileName, Overwrite);

			if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				int MadeByPlatform = Convert.ToInt32(VersionMadeByPlatformField.GetValue(Entry)!);
				if (MadeByPlatform == 3 || MadeByPlatform == 19) // Unix or OSX
				{
					FileUtils.SetFileMode_Linux(TargetFileName, (ushort)(Entry.ExternalAttributes >> 16));
				}
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				int MadeByPlatform = Convert.ToInt32(VersionMadeByPlatformField.GetValue(Entry)!);
				if (MadeByPlatform == 3 || MadeByPlatform == 19) // Unix or OSX
				{
					FileUtils.SetFileMode_Mac(TargetFileName, (ushort)(Entry.ExternalAttributes >> 16));
				}
			}
		}
	}
}
