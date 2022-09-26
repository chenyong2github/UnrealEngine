// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Amazon.EC2.Model;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Horde.Build.Acls;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace Horde.Build.Storage
{
	/// <summary>
	/// Response from uploading a bundle
	/// </summary>
	public class WriteBundleResponse
	{
		/// <summary>
		/// Locator for the uploaded bundle
		/// </summary>
		public BlobLocator Locator { get; set; }
	}

	/// <summary>
	/// Request object for writing a ref
	/// </summary>
	public class WriteRefRequest
	{
		/// <summary>
		/// Locator for the target blob
		/// </summary>
		public BlobLocator Locator { get; set; }

		/// <summary>
		/// Export index for the ref
		/// </summary>
		public int ExportIdx { get; set; }
	}

	/// <summary>
	/// Response object for reading a ref
	/// </summary>
	public class ReadRefResponse
	{
		/// <summary>
		/// Locator for the target blob
		/// </summary>
		public BlobLocator Blob { get; set; }

		/// <summary>
		/// Export index for the ref
		/// </summary>
		public int ExportIdx { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public ReadRefResponse(NodeLocator target)
		{
			Blob = target.Blob;
			ExportIdx = target.ExportIdx;
		}
	}

	/// <summary>
	/// Controller for the /api/v1/storage endpoint
	/// </summary>
	[Authorize]
	[ApiController]
	[Route("[controller]")]
	public class StorageController : HordeControllerBase
	{
		readonly StorageService _storageService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="storageService"></param>
		public StorageController(StorageService storageService)
		{
			_storageService = storageService;
		}

		/// <summary>
		/// Uploads data to the storage service. 
		/// </summary>
		/// <param name="namespaceId">Namespace to fetch from</param>
		/// <param name="file">Data to be uploaded</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		[HttpPost]
		[Route("/api/v1/storage/{namespaceId}/bundles")]
		public async Task<ActionResult<WriteBundleResponse>> WriteBundleAsync(NamespaceId namespaceId, IFormFile file, CancellationToken cancellationToken)
		{
			if (!await _storageService.AuthorizeAsync(namespaceId, User, AclAction.WriteBlobs, null, cancellationToken))
			{
				return Forbid(AclAction.WriteBlobs);
			}

			IStorageClient client = await _storageService.GetClientAsync(namespaceId, cancellationToken);
			using (Stream stream = file.OpenReadStream())
			{
				BlobLocator locator = await client.WriteBlobAsync(stream, cancellationToken: cancellationToken);
				return new WriteBundleResponse { Locator = locator };
			}
		}

		/// <summary>
		/// Retrieves data from the storage service. 
		/// </summary>
		/// <param name="namespaceId">Namespace to fetch from</param>
		/// <param name="locator">Bundle to retrieve</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		[HttpGet]
		[Route("/api/v1/storage/{namespaceId}/bundles/{locator}")]
		public async Task<ActionResult> ReadBundleAsync(NamespaceId namespaceId, BlobLocator locator, CancellationToken cancellationToken)
		{
			if (!await _storageService.AuthorizeAsync(namespaceId, User, AclAction.ReadBlobs, null, cancellationToken))
			{
				return Forbid(AclAction.WriteBlobs);
			}

			IStorageClient client = await _storageService.GetClientAsync(namespaceId, cancellationToken);
			Stream stream = await client.ReadBlobAsync(locator, cancellationToken);

#pragma warning disable CA2000 // Dispose objects before losing scope
			return File(stream, "application/octet-stream");
#pragma warning restore CA2000 // Dispose objects before losing scope
		}

		/// <summary>
		/// Writes a ref to the storage service.
		/// </summary>
		/// <param name="namespaceId">Namespace to write to</param>
		/// <param name="refName">Name of the ref</param>
		/// <param name="request">Request for the ref to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		[HttpPut]
		[Route("/api/v1/storage/{namespaceId}/refs/{refName}")]
		public async Task<ActionResult> WriteRefAsync(NamespaceId namespaceId, RefName refName, [FromBody] WriteRefRequest request, CancellationToken cancellationToken)
		{
			if (!await _storageService.AuthorizeAsync(namespaceId, User, AclAction.WriteRefs, null, cancellationToken))
			{
				return Forbid(AclAction.WriteBlobs);
			}

			IStorageClient client = await _storageService.GetClientAsync(namespaceId, cancellationToken);
			NodeLocator target = new NodeLocator(request.Locator, request.ExportIdx);
			await client.WriteRefTargetAsync(refName, target, cancellationToken);

			return Ok();
		}

		/// <summary>
		/// Uploads data to the storage service. 
		/// </summary>
		/// <param name="namespaceId"></param>
		/// <param name="refName"></param>
		/// <param name="cancellationToken"></param>
		[HttpGet]
		[Route("/api/v1/storage/{namespaceId}/refs/{refName}")]
		public async Task<ActionResult<ReadRefResponse>> ReadRefAsync(NamespaceId namespaceId, RefName refName, CancellationToken cancellationToken)
		{
			if (!await _storageService.AuthorizeAsync(namespaceId, User, AclAction.ReadRefs, null, cancellationToken))
			{
				return Forbid(AclAction.ReadRefs);
			}

			IStorageClient client = await _storageService.GetClientAsync(namespaceId, cancellationToken);

			NodeLocator target = await client.TryReadRefTargetAsync(refName, cancellationToken: cancellationToken);
			if (!target.IsValid())
			{
				return NotFound();
			}

			return new ReadRefResponse(target);
		}
	}
}
