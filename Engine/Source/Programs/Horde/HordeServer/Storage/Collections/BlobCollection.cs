// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Utilities;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Collections
{
	using NamespaceId = StringId<INamespace>;

	class FileSystemBlobCollection : IBlobCollection
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
		/// Constructor
		/// </summary>
		/// <param name="Settings">Current Horde Settings</param>
		public FileSystemBlobCollection(IOptions<ServerSettings> Settings)
		{
			ServerSettings CurrentSettings = Settings.Value;
			BaseDir = CurrentSettings.LocalBlobsDirRef;
			InstanceId = Guid.NewGuid().ToString("N");
			DirectoryReference.CreateDirectory(BaseDir);
		}

		/// <inheritdoc/>
		public async Task AddAsync(NamespaceId NamespaceId, IoHash Hash, Stream Input)
		{
			FileReference Location = GetPath(NamespaceId, Hash);
			DirectoryReference.CreateDirectory(Location.Directory);

			FileReference TempLocation = new FileReference($"{Location}.{InstanceId}");

			Blake3.Hasher Hasher = Blake3.Hasher.New();
			using (FileStream Output = FileReference.Open(TempLocation, FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				byte[] ReadBuffer = new byte[64 * 1024];
				Task<int> ReadTask = Task.Run(() => Input.ReadAsync(ReadBuffer, 0, ReadBuffer.Length));

				byte[] WriteBuffer = new byte[64 * 1024];
				int WriteLength = await ReadTask;

				while (WriteLength > 0)
				{
					(WriteBuffer, ReadBuffer) = (ReadBuffer, WriteBuffer);

					ReadTask = Task.Run(() => Input.ReadAsync(ReadBuffer, 0, ReadBuffer.Length));
					Task WriteTask = Task.Run(() => Output.WriteAsync(WriteBuffer, 0, WriteLength));

					Hasher.Update(WriteBuffer.AsSpan(0, WriteLength));

					await Task.WhenAll(ReadTask, WriteTask);
					WriteLength = await ReadTask;
				}
			}

			IoHash ActualHash = new IoHash(Hasher);
			if (ActualHash != Hash)
			{
				throw new ArgumentException($"Hash for blob is incorrect; should be {ActualHash}, not {Hash}");
			}

			try
			{
				FileReference.Move(TempLocation, Location, true);
			}
			catch (IOException) // Already exists
			{
				if (FileReference.Exists(Location))
				{
					FileReference.Delete(TempLocation);
				}
				else
				{
					throw;
				}
			}
		}

		/// <inheritdoc/>
		public Task<Stream?> GetAsync(NamespaceId NamespaceId, IoHash Hash)
		{
			FileReference Location = GetPath(NamespaceId, Hash);
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
		public Task<List<IoHash>> ExistsAsync(NamespaceId NamespaceId, IEnumerable<IoHash> Hashes)
		{
			return Task.FromResult<List<IoHash>>(Hashes.Where(x => FileReference.Exists(GetPath(NamespaceId, x))).ToList());
		}

		/// <summary>
		/// Gets the path for a blob
		/// </summary>
		/// <param name="NsId">The namespace id</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <returns></returns>
		FileReference GetPath(NamespaceId NsId, IoHash Hash)
		{
			string HashText = Hash.ToString();
			return FileReference.Combine(BaseDir, NsId.ToString(), HashText[0..2], HashText[2..4], $"{HashText}.blob");
		}
	}
}
