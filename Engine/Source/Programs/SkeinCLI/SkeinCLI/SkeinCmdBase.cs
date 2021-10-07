// Copyright Epic Games, Inc. All Rights Reserved.

using McMaster.Extensions.CommandLineUtils;
using Newtonsoft.Json;
using Serilog;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace SkeinCLI
{
    [HelpOption("--help")]
    abstract class SkeinCmdBase
    {
		public enum ReturnCodes
		{
			AlreadyExecuting = -1,
			Success = 0,
			GenericFailure = 1
		}

		public class SkeinOutput
		{
			[JsonProperty("code")]
			public int Code { get; set; }

			[JsonProperty("ok")]
			public bool OK { get; set; }

			[JsonProperty("message")]
			public string Message{ get; set; }

			[JsonProperty("data")]
			public object Data { get; set; }
		}

		public abstract List<string> CreateArgs();
        
        [Option("-q|--quiet", CommandOptionType.NoValue, Description = "Execute in quiet mode")]
        public bool Quiet { get; set; }

		[Option("-j|--json", CommandOptionType.NoValue, Description = "Output result in JSON format")]
		public bool JsonOutput { get; set; }

		[Option("-d|--debug", CommandOptionType.NoValue, Description = "Run in Debug (verbose) mode")]
		public bool Debug { get; set; }

		protected virtual Task<int> OnExecute(CommandLineApplication app)
        {
            var args = CreateArgs();
            if (Quiet)
            {
                args.Add("-q");
			}

			if (JsonOutput)
			{
				args.Add("-j");
			}

			if (Debug)
			{
				args.Add("-d");
			}

            Log.Logger.ForContext<SkeinCmdBase>().Debug("Executing: skein " + ArgumentEscaper.EscapeAndConcatenate(args));
            return Task.FromResult((int)ReturnCodes.Success);
        }

        protected void OnException(Exception ex)
        {
            Log.Logger.ForContext<SkeinCmdBase>().Error(ex, "Exception");
        }

		protected void OutputError(int errorCode, string errorMessage, object data = null)
		{
			if (JsonOutput)
			{
				SkeinOutput output = new SkeinOutput() { Code = errorCode, OK = false, Message = errorMessage, Data = data };
				string json = JsonConvert.SerializeObject(output);
				Console.WriteLine(json);
			}
			else
			{
				Console.WriteLine($"Error {errorCode}: {errorMessage}");
				if (data != null)
				{
					Console.WriteLine($"Data: {JsonConvert.SerializeObject(data)}"); 
				}			
			}
		}

		protected void Output(string message, object data = null)
		{
			if (JsonOutput)
			{
				SkeinOutput output = new SkeinOutput() { Code = 200, OK = true, Message = message, Data = data };
				string json = JsonConvert.SerializeObject(output);
				Console.WriteLine(json);
			}
			else
			{
				Console.WriteLine($"OK: {message}");
				if (data != null)
				{
					Console.WriteLine($"Data: {JsonConvert.SerializeObject(data)}");
				}
			}
		}
	}
}