// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Mvc.Formatters;
using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Net.Http.Headers;
using System.Threading.Tasks;
using System.IO;
using EpicGames.Serialization;
using EpicGames.Serialization.Converters;
using System.Reflection;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Converter to allow reading compact binary objects as request bodies
	/// </summary>
	public class CbInputFormatter : InputFormatter
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public CbInputFormatter()
		{
			SupportedMediaTypes.Add(MediaTypeHeaderValue.Parse("application/x-ue-cb"));
		}

		/// <inheritdoc/>
		protected override bool CanReadType(Type Type)
		{
			return true;
		}

		/// <inheritdoc/>
		public override Task<InputFormatterResult> ReadRequestBodyAsync(InputFormatterContext Context)
		{
			// Buffer the data into an array
			byte[] Data;
			using (MemoryStream Stream = new MemoryStream())
			{
				Context.HttpContext.Request.Body.CopyToAsync(Stream);
				Data = Stream.ToArray();
			}

			// Serialize the object
			return InputFormatterResult.SuccessAsync(CbSerializer.Deserialize(new CbField(Data), Context.ModelType));
		}
	}

	/// <summary>
	/// Converter to allow writing compact binary objects as responses
	/// </summary>
	public class CbOutputFormatter : OutputFormatter
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public CbOutputFormatter()
		{
			SupportedMediaTypes.Add(MediaTypeHeaderValue.Parse("application/x-ue-cb"));
		}

		/// <inheritdoc/>
		protected override bool CanWriteType(Type Type)
		{
			return true;
		}

		/// <inheritdoc/>
		public override async Task WriteResponseBodyAsync(OutputFormatterWriteContext Context)
		{
			ReadOnlyMemory<byte> Data = CbSerializer.Serialize(Context.ObjectType, Context.Object).GetView();
			await Context.HttpContext.Response.BodyWriter.WriteAsync(Data);
		}
	}
}
