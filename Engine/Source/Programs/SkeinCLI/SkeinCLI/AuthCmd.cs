// Copyright Epic Games, Inc. All Rights Reserved.

using McMaster.Extensions.CommandLineUtils;
using Serilog;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace SkeinCLI
{
    [Command(Name = "auth", Description = "Executes authorization related commands")]
    [Subcommand(typeof(AuthLoginCmd),
        typeof(AuthLogoutCmd))]
    class AuthCmd : SkeinCmdBase
    {
        private SkeinCmdBase Parent { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("auth");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
            //'skein auth' is an incomplete command, so we show help here even if --help is not specified
            app.ShowHelp();
            return base.OnExecute(app);
        }
    }

    [Command(Name = "login", Description = "Skein login")]
    class AuthLoginCmd : SkeinCmdBase
    {
        private AuthCmd Parent { get; set; }

		public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("login");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
			if (AuthUtils.IsLoggedIn())
			{
				Log.Logger.ForContext<AuthLoginCmd>().Information("Login succeeded (token still valid).");
				return Task.FromResult(0);
			}

			if (AuthUtils.Refresh())
			{
				Log.Logger.ForContext<AuthLoginCmd>().Information("Login succeeded (token refreshed).");
				return Task.FromResult(0);
			}

			if (AuthUtils.Login())
			{
				Log.Logger.ForContext<AuthLoginCmd>().Information("Login succeeded.");
				return Task.FromResult(0);
			}
			
			Log.Logger.ForContext<AuthLoginCmd>().Error("Login failed.");
			return Task.FromResult(1);
		}
	}

    [Command(Name = "logout", Description = "Skein logout")]
    class AuthLogoutCmd : SkeinCmdBase
    {
        private AuthCmd Parent { get; set; }

        public override List<string> CreateArgs()
        {
            var args = Parent.CreateArgs();
            args.Add("logout");

            return args;
        }

        protected override Task<int> OnExecute(CommandLineApplication app)
        {
			if (AuthUtils.Logout())
			{
				Log.Logger.ForContext<AuthLoginCmd>().Information("Logout succeeded.");
				return Task.FromResult(0);
			}
			
			Log.Logger.ForContext<AuthLoginCmd>().Error("Logout failed.");
			return Task.FromResult(1);
		}
    }
}