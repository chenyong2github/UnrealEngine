// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.IdentityModel.Tokens;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Channels;
using System.Threading.Tasks;

using AgentSoftwareChannelName = HordeServer.Utilities.StringId<HordeServer.Services.AgentSoftwareChannels>;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System.Runtime.InteropServices;
using Microsoft.Extensions.Options;
using System.IO.Compression;

namespace HordeServer.Services
{
	/// <summary>
	/// Information about a channel
	/// </summary>
	public class AgentSoftwareChannel : IAgentSoftwareChannel
	{
		/// <summary>
		/// The channel id
		/// </summary>
		public AgentSoftwareChannelName Name { get; set; }

		/// <summary>
		/// Name of the user that made the last modification
		/// </summary>
		public string? ModifiedBy { get; set; }

		/// <summary>
		/// Last modification time
		/// </summary>
		public DateTime ModifiedTime { get; set; }

		/// <summary>
		/// The software revision number
		/// </summary>
		public string Version { get; set; } = String.Empty;

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		[BsonConstructor]
		private AgentSoftwareChannel()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the channel</param>
		public AgentSoftwareChannel(AgentSoftwareChannelName Name)
		{
			this.Name = Name;
		}
	}

	/// <summary>
	/// Singleton document used to track different versions of the agent software
	/// </summary>
	[SingletonDocument("5f455039d97900f2b6c735a9")]
	public class AgentSoftwareChannels : SingletonBase
	{
		/// <summary>
		/// The next version number
		/// </summary>
		public int NextVersion { get; set; } = 1;

		/// <summary>
		/// List of channels
		/// </summary>
		public List<AgentSoftwareChannel> Channels { get; set; } = new List<AgentSoftwareChannel>();

		/// <summary>
		/// Finds an existing channel by the given name, or adds a new one
		/// </summary>
		/// <param name="Name"></param>
		/// <returns></returns>
		public AgentSoftwareChannel FindOrAddChannel(AgentSoftwareChannelName Name)
		{
			AgentSoftwareChannel? Channel = Channels.FirstOrDefault(x => x.Name == Name);
			if (Channel == null)
			{
				Channel = new AgentSoftwareChannel(Name);
				Channels.Add(Channel);
			}
			return Channel;
		}
	}

	/// <summary>
	/// Wrapper for a collection of software revisions. 
	/// </summary>
	public class AgentSoftwareService
	{
		/// <summary>
		/// Name of the default channel
		/// </summary>
		public static AgentSoftwareChannelName DefaultChannelName { get; } = new AgentSoftwareChannelName("default");

		/// <summary>
		/// Collection of software documents
		/// </summary>
		IAgentSoftwareCollection Collection;

		/// <summary>
		/// Channels singleton
		/// </summary>
		ISingletonDocument<AgentSoftwareChannels> Singleton;

		/// <summary>
		/// Cached copy of the channels singleton
		/// </summary>
		LazyCachedValue<Task<AgentSoftwareChannels>> ChannelsDocument;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Collection">The software collection</param>
		/// <param name="Singleton">The channels singleton</param>
		public AgentSoftwareService(IAgentSoftwareCollection Collection, ISingletonDocument<AgentSoftwareChannels> Singleton)
		{
			this.Collection = Collection;
			this.Singleton = Singleton;

			ChannelsDocument = new LazyCachedValue<Task<AgentSoftwareChannels>>(() => Singleton.GetAsync(), TimeSpan.FromSeconds(20.0));
		}

		/// <summary>
		/// Finds all software archives matching a set of criteria
		/// </summary>
		public async Task<List<IAgentSoftwareChannel>> FindChannelsAsync()
		{
			AgentSoftwareChannels Current = await ChannelsDocument.GetLatest();
			return Current.Channels.ConvertAll<IAgentSoftwareChannel>(x => x);
		}

		/// <summary>
		/// Gets a single software archive
		/// </summary>
		/// <param name="Name">Unique id of the channel to find software for</param>
		public async Task<IAgentSoftwareChannel?> GetChannelAsync(AgentSoftwareChannelName Name)
		{
			AgentSoftwareChannels Current = await ChannelsDocument.GetLatest();
			return Current.Channels.FirstOrDefault(x => x.Name == Name);
		}

		/// <summary>
		/// Gets a cached channel name
		/// </summary>
		/// <param name="Name">The cached channel name</param>
		/// <returns></returns>
		public async Task<IAgentSoftwareChannel?> GetCachedChannelAsync(AgentSoftwareChannelName Name)
		{
			AgentSoftwareChannels Document = await ChannelsDocument.GetCached();
			return Document.Channels.FirstOrDefault(x => x.Name == Name);
		}

		/// <summary>
		/// Removes a channel
		/// </summary>
		/// <param name="Name">The channel id</param>
		/// <returns>Async task</returns>
		public async Task DeleteChannelAsync(AgentSoftwareChannelName Name)
		{
			for (; ; )
			{
				AgentSoftwareChannels Current = await Singleton.GetAsync();

				int ChannelIdx = Current.Channels.FindIndex(x => x.Name == Name);
				if (ChannelIdx == -1)
				{
					break;
				}

				string Version = Current.Channels[ChannelIdx].Version;
				Current.Channels.RemoveAt(ChannelIdx);

				if (await Singleton.TryUpdateAsync(Current))
				{
					if (!Current.Channels.Any(x => x.Version == Version))
					{
						await Collection.RemoveAsync(Version);
					}
					break;
				}
			}
		}

		/// <summary>
		/// Reads the version number from an archive
		/// </summary>
		/// <param name="Data">The archive data</param>
		/// <returns></returns>
		static string ReadVersion(byte[] Data)
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
							InputEntry.ExtractToFile(TempFile);
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
		/// Updates a new software revision
		/// </summary>
		/// <param name="Name">Name of the channel</param>
		/// <param name="Author">Name of the user uploading this file</param>
		/// <param name="Data">The input data stream. This should be a zip archive containing the HordeAgent executable.</param>
		/// <returns>Unique id for the file</returns>
		public async Task<string> SetArchiveAsync(AgentSoftwareChannelName Name, string? Author, byte[] Data)
		{
			// Upload the software
			string Version = ReadVersion(Data);
			await Collection.AddAsync(Version, Data);

			// Update the channel
			for(; ;)
			{
				AgentSoftwareChannels Instance = await Singleton.GetAsync();

				AgentSoftwareChannel Channel = Instance.FindOrAddChannel(Name);
				Channel.ModifiedBy = Author;
				Channel.ModifiedTime = DateTime.UtcNow;
				Channel.Version = Version;

				if (await Singleton.TryUpdateAsync(Instance))
				{
					break;
				}
			}
			return Version;
		}

		/// <summary>
		/// Gets the zip file for a given channel
		/// </summary>
		/// <param name="Name">The channel name</param>
		/// <returns>Data for the given archive</returns>
		public async Task<byte[]?> GetArchiveAsync(AgentSoftwareChannelName Name)
		{
			AgentSoftwareChannels Instance = await Singleton.GetAsync();

			AgentSoftwareChannel? Channel = Instance.Channels.FirstOrDefault(x => x.Name == Name);
			if (Channel == null)
			{
				return null;
			}
			else
			{
				return await Collection.GetAsync(Channel.Version);
			}
		}

		/// <summary>
		/// Gets the zip file with a given version number
		/// </summary>
		/// <param name="Version">The version</param>
		/// <returns>Data for the given archive</returns>
		public Task<byte[]?> GetArchiveAsync(string Version)
		{
			return Collection.GetAsync(Version);
		}
	}
}
