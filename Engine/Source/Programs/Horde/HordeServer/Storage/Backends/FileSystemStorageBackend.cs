// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Storage.Backends
{
	/// <summary>
	/// Options for the filesystem backend
	/// </summary>
	public interface IFileSystemStorageOptions
	{
		/// <summary>
		/// Base directory for storing files
		/// </summary>
		public string? BaseDir { get; }
	}

	/// <summary>
	/// Storage backend using the filesystem
	/// </summary>
	public class FileSystemStorageBackend : IStorageBackend
	{
		/// <summary>
		/// Base directory for log files
		/// </summary>
		private readonly DirectoryReference BaseDir;

		/// <summary>
		/// Unique identifier for this instance
		/// </summary>
		private string InstanceId;

		/// <summary>
		/// Unique id for each write
		/// </summary>
		private int UniqueId;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Options">Current Horde Settings</param>
		public FileSystemStorageBackend(IFileSystemStorageOptions Options)
		{
			this.BaseDir = DirectoryReference.Combine(Program.DataDir, Options.BaseDir ?? "Storage");
			this.InstanceId = Guid.NewGuid().ToString("N");
			DirectoryReference.CreateDirectory(BaseDir);
		}

		/// <inheritdoc/>
		public Task<Stream?> ReadAsync(string Path)
		{
			FileReference Location = FileReference.Combine(BaseDir, Path);
			if (!FileReference.Exists(Location))
			{
				return Task.FromResult<Stream?>(null);
			}

			try
			{
				return Task.FromResult<Stream?>(FileReference.Open(Location, FileMode.Open, FileAccess.Read, FileShare.Read));
			}
			catch (DirectoryNotFoundException)
			{
				return Task.FromResult<Stream?>(null);
			}
			catch (FileNotFoundException)
			{
				return Task.FromResult<Stream?>(null);
			}
		}

		/// <inheritdoc/>
		public async Task WriteAsync(string Path, Stream Stream)
		{
			FileReference FinalLocation = FileReference.Combine(BaseDir, Path);
			if (!FileReference.Exists(FinalLocation))
			{
				// Write to a temp file first
				int NewUniqueId = Interlocked.Increment(ref UniqueId);

				DirectoryReference.CreateDirectory(FinalLocation.Directory);
				FileReference TempLocation = new FileReference($"{FinalLocation}.{InstanceId}.{UniqueId:x8}");

				using (Stream OutputStream = FileReference.Open(TempLocation, FileMode.Create, FileAccess.Write, FileShare.Read))
				{
					await Stream.CopyToAsync(OutputStream);
				}

				// Move the temp file into place
				try
				{
					FileReference.Move(TempLocation, FinalLocation, true);
				}
				catch (IOException) // Already exists
				{
					if (FileReference.Exists(FinalLocation))
					{
						FileReference.Delete(TempLocation);
					}
					else
					{
						throw;
					}
				}
			}
		}

		/// <inheritdoc/>
		public Task<bool> ExistsAsync(string Path)
		{
			FileReference Location = FileReference.Combine(BaseDir, Path);
			return Task.FromResult(FileReference.Exists(Location));
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
