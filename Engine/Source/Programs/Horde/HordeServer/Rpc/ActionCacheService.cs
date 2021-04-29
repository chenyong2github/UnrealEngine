// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using EpicGames.Core;
using Google.Protobuf;
using Grpc.Core;
using HordeServer.Storage;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using HordeServer.Storage.Primitives;
using HordeServer.Utility;

namespace HordeServer.Rpc
{
	/// <inheritdoc cref="ActionCache"/>
	public class ActionCacheService : ActionCache.ActionCacheBase
	{
		/// <summary>
		/// Storage service interface
		/// </summary>
		IStorageService StorageService;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StorageService">The storage service instance</param>
		public ActionCacheService(IStorageService StorageService)
		{
			this.StorageService = StorageService;
		}

		/// <inheritdoc/>
		public override async Task<ActionResult> UpdateActionResult(UpdateActionResultRequest Request, ServerCallContext Context)
		{
			int Length = Request.ActionResult.CalculateSize();

			byte[] Data = new byte[Length];
			using (CodedOutputStream Stream = new CodedOutputStream(Data))
			{
				Request.ActionResult.WriteTo(Stream);
			}

			BlobHash ActionHash = BlobHash.Parse(Request.ActionDigest.Hash);
			BlobHash ResultHash = await StorageService.PutBlobAsync(Data);
			await StorageService.SetRefAsync(ActionHash, ResultHash);

			return Request.ActionResult;
		}

		/// <inheritdoc/>
		public override async Task<ActionResult> GetActionResult(GetActionResultRequest Request, ServerCallContext Context)
		{
			ActionResult? Result = await TryGetResult(Request.ActionDigest);
			if (Result == null)
			{
				throw new RpcException(new Status(StatusCode.NotFound, $"Cache does not contain {Request.ActionDigest.Hash}"));
			}
			return Result;
		}

		/// <summary>
		/// Attempts to get the cached result of an action
		/// </summary>
		/// <param name="ActionDigest"></param>
		/// <returns></returns>
		public async Task<ActionResult?> TryGetResult(Digest ActionDigest)
		{
			BlobHash ActionHash = ActionDigest.ToHashValue();

			BlobHash? ResultHash = await StorageService.GetRefAsync(ActionHash, TimeSpan.FromDays(1.0));
			if (ResultHash == null)
			{
				return null;
			}

			ReadOnlyMemory<byte>? Data = await StorageService.TryGetBlobAsync(ResultHash.Value);
			if (Data == null)
			{
				return null;
			}

			return ActionResult.Parser.ParseFrom(Data.Value);
		}
	}
}
