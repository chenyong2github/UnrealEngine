// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text.RegularExpressions;
using System.Text.Json;
using System.Diagnostics;

#nullable disable

namespace HordeServer.Utilities
{
	static class AgentUtilities
	{
		/// <summary>
		/// Reads the version number from an archive
		/// </summary>
		/// <param name="Data">The archive data</param>
		/// <returns></returns>
		public static string ReadVersion(byte[] Data)
		{
			MemoryStream InputStream = new MemoryStream(Data);
			using (ZipArchive InputArchive = new ZipArchive(InputStream, ZipArchiveMode.Read, true))
			{
				foreach (ZipArchiveEntry InputEntry in InputArchive.Entries)
				{
					if (InputEntry.FullName.Equals("HordeAgent.dll", StringComparison.OrdinalIgnoreCase))
					{
						string TempFile = Path.GetTempFileName();
						try
						{
							InputEntry.ExtractToFile(TempFile, true);
							return FileVersionInfo.GetVersionInfo(TempFile).ProductVersion;
						}
						finally
						{
							File.Delete(TempFile);
						}
					}
				}
			}
			throw new Exception("Unable to find HordeAgent.dll in archive");
		}


		/// <summary>
		/// Updates the agent app settings within the archive data
		/// </summary>
		/// <param name="Data">Data for the zip archive</param>
		/// <param name="Settings">The settings to update</param>
		/// <returns>New agent app data</returns>
		public static byte[] UpdateAppSettings(byte[] Data, Dictionary<string, object> Settings)
		{
			bool bWrittenClientId = false;

			MemoryStream OutputStream = new MemoryStream();
			using (ZipArchive OutputArchive = new ZipArchive(OutputStream, ZipArchiveMode.Create, true))
			{
				MemoryStream InputStream = new MemoryStream(Data);
				using (ZipArchive InputArchive = new ZipArchive(InputStream, ZipArchiveMode.Read, true))
				{
					foreach (ZipArchiveEntry InputEntry in InputArchive.Entries)
					{
						ZipArchiveEntry OutputEntry = OutputArchive.CreateEntry(InputEntry.FullName);

						using System.IO.Stream InputEntryStream = InputEntry.Open();
						using System.IO.Stream OutputEntryStream = OutputEntry.Open();

						if (InputEntry.FullName.Equals("appsettings.json", StringComparison.OrdinalIgnoreCase))
						{
							using MemoryStream MemoryStream = new MemoryStream();
							InputEntryStream.CopyTo(MemoryStream);

							Dictionary<string, Dictionary<string, object>> Document = JsonSerializer.Deserialize<Dictionary<string, Dictionary<string, object>>>(MemoryStream.ToArray());
							foreach (KeyValuePair<string, object> Pair in Settings) {								
								Document["Horde"][Pair.Key] = Pair.Value;
							}

							using Utf8JsonWriter Writer = new Utf8JsonWriter(OutputEntryStream, new JsonWriterOptions { Indented = true });
							JsonSerializer.Serialize<Dictionary<string, Dictionary<string, object>>>(Writer, Document, new JsonSerializerOptions { WriteIndented = true });

							bWrittenClientId = true;
						}
						else
						{
							InputEntryStream.CopyTo(OutputEntryStream);
						}
					}
				}
			}

			if (!bWrittenClientId)
			{
				throw new InvalidDataException("Missing appsettings.json file from zip archive");
			}

			return OutputStream.ToArray();
		}

	}
}
