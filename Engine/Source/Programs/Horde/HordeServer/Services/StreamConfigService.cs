using HordeServer.Api;
using HordeServer.Controllers;
using HordeServer.Models;
using HordeServer.Notifications;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Polls Perforce for stream config changes
	/// </summary>
	public class StreamConfigService : ElectedBackgroundService
	{
		/// <summary>
		/// Stream service
		/// </summary>
		StreamService StreamService;
		
		/// <summary>
		/// Template service
		/// </summary>
		TemplateService TemplateService;

		/// <summary>
		/// Instance of the perforce service
		/// </summary>
		IPerforceService PerforceService;

		/// <summary>
		/// Instance of the notification service
		/// </summary>
		INotificationService NotificationService;

		/// <summary>
		/// Logging device
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="PerforceService"></param>
		/// <param name="StreamService"></param>
		/// <param name="TemplateService"></param>
		/// <param name="NotificationService"></param>
		/// <param name="Logger"></param>
		public StreamConfigService(DatabaseService DatabaseService, IPerforceService PerforceService, StreamService StreamService, TemplateService TemplateService, INotificationService NotificationService, ILogger<StreamConfigService> Logger)
			: base(DatabaseService, new ObjectId("5ff60549e7632b15e64ac2f7"), Logger)
		{
			this.PerforceService = PerforceService;
			this.StreamService = StreamService;
			this.TemplateService = TemplateService;
			this.NotificationService = NotificationService;
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		protected override async Task<DateTime> TickLeaderAsync(CancellationToken StoppingToken)
		{
			DateTime NextTickTime = DateTime.UtcNow + TimeSpan.FromMinutes(1.0);
			Logger.LogDebug("Updating stream configs from Perforce");

			List<IStream> Streams = await StreamService.GetStreamsAsync();

			List<IStream> ConfigStreams = Streams.Where(x => x.ConfigPath != null).ToList();
			if (ConfigStreams.Count > 0)
			{
				List<FileSummary> Files = await PerforceService.FindFilesAsync(ConfigStreams.Select(x => x.ConfigPath!));
				foreach(FileSummary Error in Files.Where(File => File.Error != null))
				{
					NotificationService.NotifyUpdateStreamFailure(Error);
				}
				Dictionary<string, FileSummary> DepotPathToFileSummary = Files.ToDictionary(x => x.DepotPath, x => x);

				for (int Idx = 0; Idx < ConfigStreams.Count; Idx++)
				{
					IStream Stream = ConfigStreams[Idx];

					FileSummary? Summary;
					if(!DepotPathToFileSummary.TryGetValue(Stream.ConfigPath!, out Summary))
					{
						Logger.LogError("Config file for {StreamName} was not found at {ConfigPath}", Stream.Name, Stream.ConfigPath);
					}
					else if(Summary.Change != Stream.ConfigChange)
					{
						Logger.LogInformation("Updating config for {Stream} ({StreamId}) to {Path}@{Change}", Stream.Name, Stream.Id, Summary.DepotPath, Summary.Change);						
						try
						{
							Logger.LogInformation("Fetching {DepotPath}@{Change}", Summary.DepotPath, Summary.Change);
							byte[] Data = await PerforceService.PrintAsync($"{Summary.DepotPath}@{Summary.Change}");

							Logger.LogInformation("Config file is {NumBytes}", Data.Length);

							JsonSerializerOptions Options = new JsonSerializerOptions();
							Startup.ConfigureJsonSerializer(Options);
							SetStreamRequest Request = JsonSerializer.Deserialize<SetStreamRequest>(Data, Options);

							while (Stream.ConfigChange == null || Stream.ConfigChange < Summary.Change)
							{
								IStream? NewStream = await StreamsController.SetStreamAsync(Stream.ProjectId, Stream.Id, Stream, Summary.Change, Request, StreamService, TemplateService);
								if(NewStream != null)
								{
									break;
								}

								NewStream = await StreamService.GetStreamAsync(Stream.Id);
								if(NewStream == null)
								{
									break;
								}

								Stream = NewStream;
							}

							Logger.LogInformation("Update complete");
						}
						catch (Exception Ex)
						{
							Logger.LogError(Ex, "Error while updating {Stream} ({StreamId})", Stream.Name, Stream.Id);
							List<ChangeDetails> Details = await PerforceService.GetChangeDetailsAsync("", new List<int>() { Summary.Change }, null);
							string? Author = null;
							string? Description = null;
							if(Details.Count == 1)
							{
								Author = Details[0].Author;
								Description = Details[0].Description;
							}
							NotificationService.NotifyUpdateStreamFailure(Stream, Ex.Message, Summary.Change, Author, Description);
						}
					}
				}
			}
			return NextTickTime;
		}
	}
}
