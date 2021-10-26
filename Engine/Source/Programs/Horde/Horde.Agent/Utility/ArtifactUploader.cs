// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Grpc.Core;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Runtime.Serialization;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace HordeAgent.Utility
{
	/// <summary>
	/// Helper class for uploading artifacts to the server
	/// </summary>
	static class ArtifactUploader
	{
        /// <summary>
        /// Dictionary of horde common mime types
        /// </summary>
        private static Dictionary<string, string> HordeMimeTypes = new Dictionary<string, string>()
        {
            { ".bin", "application/octet-stream" },
            { ".json", "application/json" },
            { ".pm", "text/plain" },
            { ".pl", "text/plain" },
            { ".sh", "application/x-sh" },
            { ".txt", "text/plain" },
            { ".log", "text/plain" },
            { ".xml", "text/xml" }
        };

        /// <summary>
        /// Gets a mime type from a file extension
        /// </summary>
        /// <param name="File">The file object to parse</param>
        /// <returns>the mimetype if present in the dictionary, binary otherwise</returns>
        private static string GetMimeType(FileReference File)
        {
            string? ContentType;
            if (!HordeMimeTypes.TryGetValue(File.GetExtension(), out ContentType))
            {
                ContentType = "application/octet-stream";
            }
            return ContentType;
        }
        
        /// <summary>
        /// Uploads an artifact (with retries)
        /// </summary>
        /// <param name="RpcConnection">The grpc client</param>
        /// <param name="JobId">Job id</param>
        /// <param name="BatchId">Job batch id</param>
        /// <param name="StepId">Job step id</param>
        /// <param name="ArtifactName">Name of the artifact</param>
        /// <param name="ArtifactFile">File to upload</param>
        /// <param name="Logger">Logger interfact</param>
        /// <param name="CancellationToken">Cancellation token</param>
        /// <returns></returns>
        public static async Task<string?> UploadAsync(IRpcConnection RpcConnection, string JobId, string BatchId, string StepId, string ArtifactName, FileReference ArtifactFile, ILogger Logger, CancellationToken CancellationToken)
        {
			try
			{
				string ArtifactId = await RpcConnection.InvokeAsync(RpcClient => DoUploadAsync(RpcClient, JobId, BatchId, StepId, ArtifactName, ArtifactFile, Logger, CancellationToken), new RpcContext(), CancellationToken);
				return ArtifactId;
			}
			catch (Exception Ex)
			{
				Logger.LogInformation(KnownLogEvents.Systemic_Horde_ArtifactUpload, Ex, "Exception while attempting to upload artifact: {Message}", Ex.Message);
				return null;
			}
        }

        /// <summary>
		/// Uploads an artifact
		/// </summary>
		/// <param name="Client">The grpc client</param>
		/// <param name="JobId">Job id</param>
		/// <param name="BatchId">Job batch id</param>
		/// <param name="StepId">Job step id</param>
		/// <param name="ArtifactName">Name of the artifact</param>
		/// <param name="ArtifactFile">File to upload</param>
		/// <param name="Logger">Logger interfact</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns></returns>
		private static async Task<string> DoUploadAsync(HordeRpc.HordeRpcClient Client, string JobId, string BatchId, string StepId, string ArtifactName, FileReference ArtifactFile, ILogger Logger, CancellationToken CancellationToken)
        {
			Logger.LogInformation("Uploading artifact {ArtifactName} from {ArtifactFile}", ArtifactName, ArtifactFile);
            using (FileStream ArtifactStream = FileReference.Open(ArtifactFile, FileMode.Open, FileAccess.Read, FileShare.Read))
			{
				using (AsyncClientStreamingCall<UploadArtifactRequest, UploadArtifactResponse> Cursor = Client.UploadArtifact(null, null, CancellationToken))
                {
					// Upload the metadata in the initial request
					UploadArtifactMetadata Metadata = new UploadArtifactMetadata();
					Metadata.JobId = JobId;
					Metadata.BatchId = BatchId;
					Metadata.StepId = StepId;
					Metadata.Name = ArtifactName;
					Metadata.MimeType = GetMimeType(ArtifactFile);
					Metadata.Length = ArtifactStream.Length;

					UploadArtifactRequest InitialRequest = new UploadArtifactRequest();
					InitialRequest.Metadata = Metadata;

					await Cursor.RequestStream.WriteAsync(InitialRequest);

					// Upload the data in chunks
					byte[] Buffer = new byte[4096];
					for (int Offset = 0; Offset < Metadata.Length; )
                    {
                        int BytesRead = await ArtifactStream.ReadAsync(Buffer, 0, Buffer.Length);
						if(BytesRead == 0)
						{
							throw new InvalidDataException($"Unable to read data from {ArtifactFile} beyond offset {Offset}; expected length to be {Metadata.Length}");
						}

						UploadArtifactRequest Request = new UploadArtifactRequest();
						Request.Data = Google.Protobuf.ByteString.CopyFrom(Buffer, 0, BytesRead);
						await Cursor.RequestStream.WriteAsync(Request);

                        Offset += BytesRead;
                    }

					// Close the stream
                    await Cursor.RequestStream.CompleteAsync();

					// Read the response
					UploadArtifactResponse Response = await Cursor.ResponseAsync;
					return Response.Id;
                }
            }
        }
	}
}
