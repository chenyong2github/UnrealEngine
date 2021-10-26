// Copyright Epic Games, Inc. All Rights Reserved.

using Grpc.Net.Client;
using HordeAgent.Utility;
using HordeCommon.Rpc;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace HordeAgent.Commands.Utilities
{
	/// <summary>
	/// Publishes the contents of this application directory to the server
	/// </summary>
	[Command("Publish", "Publishes this application to the server")]
	class PublishCommand : Command
	{
		/// <summary>
		/// The server to upload the software to
		/// </summary>
		[CommandLine("-Server=", Required = true)]
		public string Server = null!;

		/// <summary>
		/// Access token used to authenticate with the server
		/// </summary>
		[CommandLine("-Token=", Required = true)]
		public string Token = null!;

		/// <summary>
		/// The input directory
		/// </summary>
		[CommandLine("-InputDir=", Required = true)]
		public DirectoryReference InputDir = null!;

		/// <summary>
		/// The channel to post to
		/// </summary>
		[CommandLine("-Channel=", Required = true)]
		public string Channel = null!;

		/// <summary>
		/// Main entry point for this command
		/// </summary>
		/// <param name="Logger"></param>
		/// <returns>Async task</returns>
		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			// Create the zip archive
			Logger.LogInformation("Creating archive...");
			byte[] ArchiveData;
			using (MemoryStream OutputStream = new MemoryStream())
			{
				using (ZipArchive OutputArchive = new ZipArchive(OutputStream, ZipArchiveMode.Create, true))
				{
					foreach (FileReference InputFile in DirectoryReference.EnumerateFiles(InputDir, "*", SearchOption.AllDirectories))
					{
						string RelativePath = InputFile.MakeRelativeTo(InputDir).Replace(Path.DirectorySeparatorChar, '/');
						if (!RelativePath.Equals("Log.json", StringComparison.OrdinalIgnoreCase) && !RelativePath.Equals("Log.txt", StringComparison.OrdinalIgnoreCase))
						{
							Logger.LogInformation("  Adding {RelativePath}", RelativePath);

							ZipArchiveEntry OutputEntry = OutputArchive.CreateEntry(RelativePath, CompressionLevel.Optimal);

							using Stream InputEntryStream = FileReference.Open(InputFile, FileMode.Open, FileAccess.Read, FileShare.Read);
							using Stream OutputEntryStream = OutputEntry.Open();

							await InputEntryStream.CopyToAsync(OutputEntryStream);
						}
					}
				}
				ArchiveData = OutputStream.ToArray();
			}
			Logger.LogInformation("Complete ({Size:n0} bytes)", ArchiveData.Length);

			// Read the application settings
			IConfigurationRoot Config = new ConfigurationBuilder()
				.SetBasePath(Program.AppDir.FullName)
				.AddJsonFile("appsettings.json", true)
				.AddJsonFile("appsettings.Production.json", true)
				.Build();

			AgentSettings Settings = new AgentSettings();
			Config.GetSection("Horde").Bind(Settings);

			// Get the server we're using
			ServerProfile ServerProfile = Settings.GetServerProfile(Server);

			// Create the http message handler
			HttpClientHandler Handler = new HttpClientHandler();
			Handler.ServerCertificateCustomValidationCallback += (Sender, Cert, Chain, Errors) => CertificateHelper.CertificateValidationCallBack(Logger, Sender, Cert, Chain, Errors, ServerProfile);

			// Create a new http client for uploading
			HttpClient HttpClient = new HttpClient(Handler);
			HttpClient.BaseAddress = new Uri(ServerProfile.Url);
			HttpClient.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", Token);

			GrpcChannelOptions ChannelOptions = new GrpcChannelOptions
			{
				HttpClient = HttpClient,
				MaxReceiveMessageSize = 100 * 1024 * 1024, // 100 MB
				MaxSendMessageSize = 100 * 1024 * 1024 // 100 MB
			};
			using (GrpcChannel RpcChannel = GrpcChannel.ForAddress(ServerProfile.Url, ChannelOptions))
			{
				HordeRpc.HordeRpcClient RpcClient = new HordeRpc.HordeRpcClient(RpcChannel);

				UploadSoftwareRequest UploadRequest = new UploadSoftwareRequest();
				UploadRequest.Channel = Channel;
				UploadRequest.Data = Google.Protobuf.ByteString.CopyFrom(ArchiveData);

				UploadSoftwareResponse UploadResponse = await RpcClient.UploadSoftwareAsync(UploadRequest);
				Logger.LogInformation("Created software (version={SoftwareId} channel={Default})", UploadResponse.Version, UploadRequest.Channel);
			}

			return 0;
		}
	}
}
