// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import * as fs from 'fs'
import * as os from 'os'
import * as util from 'util'

import {Arg, readProcessArgs} from '../common/args'
import {Perforce, ClientSpec, Workspace} from '../common/perforce'
import * as p4util from '../common/p4util'
import {Branch} from './branch-interfaces'
import {Engine} from './engine'
import {PAUSE_TYPE_MANUAL_LOCK} from './branchbot'
import {Mailer} from '../common/mailer'
import {AutoBranchUpdater} from './autobranchupdater'
import {Analytics} from '../common/analytics'
import {_setTimeout} from '../common/helper'
import {IPC, Message} from './ipc'
import {CertFiles} from '../common/webserver'

/*************************
 * RoboMerge main process
 *************************/

// I seem to have broken this
const DEBUG_SKIP_BRANCH_SETUP = false;

const COMMAND_LINE_ARGS: {[param: string]: (Arg<any>)} = {
	botname: {
		match: /^-botname=([a-zA-Z0-9_,]+)$/,
		parse: (str: string) => str.split(','),
		env: 'BOTNAME',
		dflt: ["TEST"]
	},
	runbots: {
		match: /^-runbots=(.+)$/,
		parse: (str: string) => {
			if (str === "yes")
				return true;
			if (str === "no")
				return false;
			throw new Error(`Invalid -runbots=${str}`);
		},
		env: 'ROBO_RUNBOTS',
		dflt: true
	},

	hostInfo: {
		match: /^-hostInfo=(.+)$/,
		env: 'ROBO_HOST_INFO',
		dflt: os.hostname()
	},

	branchSpecsRootPath: {
		match: /^-bs_root=(.*)$/,
		env: 'ROBO_BRANCHSPECS_ROOT_PATH',
		dflt: '//GamePlugins/Main/Programs/RoboMerge/data'
	},

	branchSpecsWorkspace: {
		match: /^-bs_workspace=(.+)$/,
		env: 'ROBO_BRANCHSPECS_WORKSPACE',
		dflt: 'robomerge-branchspec-' + os.hostname()
	},

	branchSpecsDirectory: {
		match: /^-bs_directory=(.+)$/,
		env: 'ROBO_BRANCHSPECS_DIRECTORY',
		dflt: './data'
	},

	noIPC: {
		match: /^(-noIPC)$/,
		parse: _str => true,
		env: 'ROBO_NO_IPC',
		dflt: false
	},

	// only applicable if noIPC is true
	noTLS: {
		match: /^(-noTLS)$/,
		parse: _str => true,
		env: 'ROBO_NO_TLS',
		dflt: false
	},
};

const args = readProcessArgs(COMMAND_LINE_ARGS);
if (!args) {
	process.exit(1)
}

Mailer.emailTemplateFilepath = `${args.branchSpecsDirectory}/email-template.html`;
Engine.dataDirectory = args.branchSpecsDirectory;

function _readVersion() {
	try {
		const versionStr = require('fs').readFileSync('./version.json');
		return JSON.parse(versionStr);
	}
	catch (err) {
		return '<unknown>';
	}
}

const VERSION = _readVersion();


export class RoboMerge {
	readonly engines = new Map<string, Engine>()
	readonly p4 = new Perforce('v' + VERSION.build.toString())
	readonly analytics = new Analytics(args.hostInfo!)
	mailer: Mailer

	readonly VERSION_STRING = `build ${VERSION.build} (cl ${VERSION.cl})`;

	getAllBranches() : Branch[] {
		const branches: Branch[] = [];
		for (const engine of robo.engines.values()) {
			branches.push(...engine.branchMap.branches);
		}
		return branches;
	}

	getBranchMap(name: string) {
		const engine = this.engines.get(name.toUpperCase())
		return engine ? engine.branchMap : null
	}

	dumpSettingsForBot(name: string) {
		const engine = this.engines.get(name.toUpperCase())
		return engine && engine.settings.object
	}

	// findConflict(botname: string, conflictId: string) {
	// 	const engine = this.engines.get(botname.toUpperCase())
	// 	return engine ? engine.findConflict(conflictId) : null
	// }

	stop() {
		this.analytics.stop()
	}
}

const robo = new RoboMerge;
const ipc = new IPC(robo);

async function _getExistingWorkspaces() {
	const existingWorkspacesObjects = await robo.p4.find_workspaces();
	return new Set<string>(existingWorkspacesObjects.map((ws: ClientSpec) => ws.client));
}

async function _initWorkspacesForEngine(engine: Engine, existingWorkspaces: Set<string>) {
	const workspacesToReset: string[] = []

	for (const branch of engine.branchMap.branches) {
		if (branch.workspace !== null) {
			util.log("Using manually configured workspace "+branch.workspace+" for branch"+branch.name);
			continue;
		}

		// try to load from settings
		const settings = engine.settings.getContext(branch.upperName);
		branch.workspace = settings.getString("workspace");

		// name the workspace
		if (!branch.workspace) {
			let workspaceName = "ROBOMERGE_"+branch.parent.botname+"_"+branch.name;
			if (robo.p4.username !== 'robomerge')
				workspaceName = robo.p4.username!.toUpperCase() + '_' + workspaceName;
			branch.workspace = workspaceName.replace(/[\/\.-\s]/g, "_").replace(/_+/g,"_");
		}

		const ws = <string>branch.workspace;

		// ensure root directory exists (we set the root diretory to be the cwd)
		const fs = require('fs');

		const path = Perforce.getRootDirectoryForBranch(ws);
		if (!fs.existsSync(path)) {
			util.log(`Making directory ${path}`);
			fs.mkdirSync(path);
		}

		// see if we already have this workspace
		if (existingWorkspaces.has(ws)) {
			workspacesToReset.push(ws);
		}
		else {
			const params: any = {};
			if (branch.stream) {
				params['Stream'] = branch.stream;
			}
			else {
				params['View'] = [
					`${branch.rootPath} //${ws}/...`
				];
			}

			await robo.p4.newEngineWorkspace(ws, params);

			// save to settings
			settings.set("workspace", branch.workspace);

			// if we're on linux, remove the directory whenever we create the workspace for the first time
			if (process.platform === "linux") {
				let dir = "/src/"+branch.workspace;
				util.log(`Cleaning ${dir}...`);

				// delete the directory contents (but not the directory)
				require('child_process').execSync(`rm -rf ${dir}/*`);
			}
			else {
				await robo.p4.clean(branch.workspace);
			}
		}
	}

	if (workspacesToReset.length > 0) {
		util.log('The following workspaces already exist and will be reset: ' + workspacesToReset.join(', '))
		await Promise.all(workspacesToReset.map(ws => robo.p4.sync(ws, `//${ws}/...#0`)))
	}
}

async function _initBranchWorkspacesForAllBots() {
	const existingWorkspaces = await _getExistingWorkspaces();
	for (const engine of robo.engines.values()) {
		await _initWorkspacesForEngine(engine, existingWorkspaces);
	}
}

let BRANCH_BOT_DEPS: any = null;

// should be called after data directory has been synced, to get latest email template
function _initMailer() {
	robo.mailer = new Mailer(robo.analytics);

	BRANCH_BOT_DEPS = {
		p4: robo.p4,
		mailer: robo.mailer
	};

}

function _checkForAutoPauseBots(branches: Branch[]) {
	if (!args.runbots) {
		let paused = 0
		for (const branch of branches) {
			if (branch.bot) {
				branch.bot.pause({
					type: PAUSE_TYPE_MANUAL_LOCK,
					message: 'Paused due to command line arguments or environment variables',
					owner: 'robomerge'
				})
				++paused
			}
		}

		if (paused !== 0) {
			util.log(`Auto-pause: ${paused} branch bot${paused > 1 ? 's' : ''} paused`)
		}
	}
}

async function _onBranchSpecReloaded(engine: Engine) {
	await _initWorkspacesForEngine(engine, await _getExistingWorkspaces())

	engine.initBots()

	util.log(`Restarting monitoring ${engine.branchMap.botname} branches after reloading branch definitions`)
	engine.runbots()

	// no need to check for paused bots any more - pause state persists
}

async function init() {
	if (!args.branchSpecsRootPath) {
		util.log('WARNING: auto brancher updater not configured!')
	}
	else {
	const autoUpdaterConfig = {
		rootPath: args.branchSpecsRootPath,
		workspace: {directory: args.branchSpecsDirectory, name: args.branchSpecsWorkspace}
	}

	// Ensure we have a workspace for branch specs
	let workspace : Array<Object> = await robo.p4._execP4Ztag(null, ['clients', '-E', args.branchSpecsWorkspace]);
	if (workspace.length == 0) {
		util.log("Cannot find branch spec workspace " + args.branchSpecsWorkspace + 
					", creating a new one.");
		await robo.p4.newBranchSpecWorkspace(args.branchSpecsWorkspace, args.branchSpecsRootPath, fs.realpathSync(args.branchSpecsDirectory));
	}

	// make sure we've got the latest branch specs
	util.log('Syncing latest branch specs')
	await AutoBranchUpdater.init({p4: robo.p4}, autoUpdaterConfig)
	}

	_initMailer()
	_initEngines()
	if (!DEBUG_SKIP_BRANCH_SETUP) {
		await _initBranchWorkspacesForAllBots()
	}

	for (const engine of robo.engines.values()) {
		engine.initBots()
	}
}

function startBots() {

	// start them up
	util.log("Starting branch bots...");
	for (let engine of robo.engines.values()) {
		if (AutoBranchUpdater.config) {
			engine.autoUpdater = new AutoBranchUpdater(engine)
		}
		engine.runbots()
	}

	if (!args.runbots) {
		_checkForAutoPauseBots(robo.getAllBranches())
	}
}

import {RoboServer} from './roboserver';

if (!args.noIPC) {
	process.on('message', async (msg: Message) => {
		let result: any | null = null
		try {
			result = await ipc.handle(msg)
		}
		catch (err) {
			util.log('IPC error! ' + err.toString())
			result = {statusCode: 500, msg: 'Internal server error'}
		}
		process.send!({cbid: msg.cbid, args: result})
	});
}
else {
	const logEntries: string[] = [];
	const originalStdOutWrite = process.stdout.write;
	(<any>process.stdout).write = (str: string, encoding: any, fd: any) => {
		originalStdOutWrite.call(process.stdout, str, encoding, fd);
		logEntries.push(str);
	}


	const sendMessage = (name: string, args?: any[]) => ipc.handle({name:name, args:args});

	const ws = new RoboServer(sendMessage, () => logEntries.join('\n'),
		() => util.log('Received: getLastCrash'),
		() => util.log('Received: stopBot'),
		() => util.log('Received: startBot'));

		let certFiles: any | null = args.noTLS ? null : {
		key: fs.readFileSync('/Users/James.Hopkin/git/server.key'),
		cert: fs.readFileSync('/Users/James.Hopkin/git/server.crt')
	}

	const protocol = args.noTLS ? 'http' : 'https'
	const port = args.noTLS ? 8080 : 4433
	ws.open(port, protocol, certFiles as CertFiles).then(() =>
		util.log(`Running in-process web server (${protocol}) on port ${port}`));
}

// bind to shutdown
let is_shutting_down = false;
function shutdown(exitCode: number) {
	if (is_shutting_down) return;
	is_shutting_down = true;
	util.log("Shutting down...");

	// record the exit code
	let finalExitCode = exitCode || 0;

	robo.stop()

	// figure out how many stop callbacks to wait for
	let callCount = 1;
	let callback = () => {
		if (--callCount === 0) {
			util.log("... shutdown complete.");

			// force exit so we don't wait for anything else (like the webserver)
			process.exit(finalExitCode);
		}
		else if (callCount < 0) {
			throw new Error("shutdown weirdness");
		}
	}

	// stop all the branch bots
	for (let engine of robo.engines.values()) {
		++callCount;
		engine.stop(callback);
	}

	// make sure this gets called at least once (matches starting callCount at 1)
	callback();
}

process.once('SIGINT', () => { console.log("Caught SIGINT"); shutdown(2); });
process.once('SIGTERM', () => { console.log("Caught SIGTERM"); shutdown(0); });

async function cleanWorkspaces(branches: Branch[]) {
	if (branches.length === 0) {
		util.log('NO BRANCHES!');
		return;
	}

	const workspaceNames = new Set<string>(branches.map(branch => 
		(branch.workspace as Workspace).name || (branch.workspace as string)))
	return p4util.cleanWorkspaces(robo.p4, workspaceNames);
}

function _initEngines() {
	for (let botname of args.botname)
	{
		util.log(`Initializing bot ${botname}`)
		const engine = new Engine(botname, BRANCH_BOT_DEPS, robo.analytics)
		robo.engines.set(engine.branchMap.botname, engine)

		engine.reloadAsyncListeners.add(_onBranchSpecReloaded)
	}
}

async function main() {
	while (true) {
		try {
			await robo.p4.start();
			break;
		}
		catch (err) {
			util.log(`ERROR: P4 is not configured yet: ${err}`);

			const timeout = 15.0;
			util.log(`Will check again in ${timeout} sec...`);
			await _setTimeout(timeout*1000);
		}
	}
	// log the user name
	util.log(`P4 appears to be configured. User=${robo.p4.username}`);

	await init();
	if (!DEBUG_SKIP_BRANCH_SETUP) {
		await cleanWorkspaces(robo.getAllBranches());
	}

	startBots();
}

main();
