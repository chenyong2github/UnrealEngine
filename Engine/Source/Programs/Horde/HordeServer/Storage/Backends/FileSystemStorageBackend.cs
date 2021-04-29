// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Impl
{
	/// <summary>
	/// IStorageProvider implementation which uses the file system
	/// </summary>
	public sealed class FileSystemStorageBackend : IStorageBackend
	{
		/// <summary>
		/// Base directory for log files
		/// </summary>
		private readonly DirectoryReference BaseDir;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Settings">Current Horde Settings</param>
		public FileSystemStorageBackend(IOptionsMonitor<ServerSettings> Settings)
		{
			ServerSettings CurrentSettings = Settings.CurrentValue;
			BaseDir = new DirectoryReference(CurrentSettings.LocalStorageDir);
			DirectoryReference.CreateDirectory(BaseDir);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
		}

		/// <summary>
		/// Tests if a given path exists
		/// </summary>
		/// <param name="Path">Path to check</param>
		/// <returns>True if the path exists</returns>
		public bool Exists(string Path)
		{
			FileReference Location = FileReference.Combine(BaseDir, Path);
			return FileReference.Exists(Location);
		}

		/// <summary>
		/// Touches the given path
		/// </summary>
		/// <param name="Path"></param>
		/// <returns></returns>
		public Task<bool> TouchAsync(string Path)
		{
			FileReference Location = FileReference.Combine(BaseDir, Path);
			if (FileReference.Exists(Location))
			{
				try
				{
					FileReference.SetLastWriteTimeUtc(Location, DateTime.UtcNow);
					return Task.FromResult(true);
				}
				catch(FileNotFoundException)
				{
					// Does not exist
				}
			}
			return Task.FromResult(false);
		}

		/// <inheritdoc/>
		public async Task<ReadOnlyMemory<byte>?> ReadAsync(string Path)
		{
			FileReference Location = FileReference.Combine(BaseDir, Path);
			if (!FileReference.Exists(Location))
			{
				return null;
			}

			try
			{
				return await FileReference.ReadAllBytesAsync(Location);
			}
			catch (DirectoryNotFoundException)
			{
				return null;
			}
			catch(FileNotFoundException)
			{
				return null;
			}
		}

		/// <inheritdoc/>
		public async Task WriteAsync(string Path, ReadOnlyMemory<byte> Data)
		{
			FileReference Location = FileReference.Combine(BaseDir, Path);
			DirectoryReference.CreateDirectory(Location.Directory);

			FileReference TempLocation = new FileReference(Location.FullName + ".tmp");
			using (FileStream Stream = FileReference.Open(TempLocation, FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				await Stream.WriteAsync(Data);
			}

			try
			{
				FileReference.Move(TempLocation, Location, true);
			}
			catch(IOException)
			{
				// Already exists
			}
		}

		/// <inheritdoc/>
		public Task DeleteAsync(string Path)
		{
			FileReference Location = FileReference.Combine(BaseDir, Path);
			FileReference.Delete(Location);
			return Task.CompletedTask;
		}
	}
}
