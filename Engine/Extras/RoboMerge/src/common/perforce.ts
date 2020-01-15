// Copyright Epic Games, Inc. All Rights Reserved.
import {execFile, ExecFileOptions} from 'child_process';
import * as util from 'util';
import * as path from 'path';

const p4exe = process.platform === 'win32' ? 'p4.exe' : 'p4';

const ztag_group_rex = /\n\n\.\.\.\s/;
const ztag_field_rex = /(?:\n|^)\.\.\.\s/;
const newline_rex = /\r\n|\n|\r/g;
const integer_rex = /^[1-9][0-9]*\s*$/;

export interface BranchSpec {
	name: string;
	reverse: boolean;
}

export interface IntegrationSource {
	changelist: number;
	path_from: string;
	branchspec: BranchSpec;
}

export interface OpenedFileRecord {
	depotFile: string
	clientFile: string
	rev: string
	haveRev: string
	action: string
	type: string
	user: string

	movedFile?: string
}

interface DescribeEntry {
	depotFile: string
	action: string
	rev: number
}

export interface DescribeResult {
	user: string
	status: string
	description: string
	entries: DescribeEntry[]
}


// parse the perforce tagged output format into an array of objects
// TODO: probably should switch this to scrape Python dictionary format (-G) since ztag is super inconsistent with multiline fields
export function parseZTag(buffer: string, multiLine?: boolean) {
	let output = [];

	// check for error lines ahead of the first ztag field
	let ztag_start = buffer.indexOf('...');
	if (ztag_start > 0)
	{
		// split the start off the buffer, then split it into newlines
		let preamble = buffer.substr(0, ztag_start).trim();
		output.push(preamble.split(newline_rex));
		buffer = buffer.substr(ztag_start);
	}
	else if (ztag_start < 0)
	{
		let preamble = buffer.trim();
		if (preamble.length > 0)
			output.push(preamble.split(newline_rex));
		buffer = "";
	}

	// split into groups
	let groups = buffer.split(ztag_group_rex);
	for (let i=0;i<groups.length;++i)
	{
		// make an object for each group
		let group: any = {};
		let text: string[] = [];

		// split fields
		let pairs = groups[i].split(ztag_field_rex);
		if (pairs[0] === "") {
			pairs.shift();
		}

		let setValue = false;
		for (let j=0;j<pairs.length;++j) {
			// each field is a key-value pair
			let pair = pairs[j].trim();
			if (pair === "")
				continue;

			let key, value;
			let s = pair.indexOf(' ');
			if (s >= 0)
			{
				key = pair.substr(0, s);
				value = pair.substr(s+1);
				if (value.indexOf('\n') >= 0 && !multiLine)
				{
					let lines = value.split('\n');
					value = lines.shift();
					text = text.concat(lines.filter((str) => { return str !== ""; }));
				}

				// if it's an integer, convert
				if (value!.match(integer_rex))
					value = parseInt(value!);
			}
			else
			{
				key = pair;
				value = true;
			}

			// set it on the group
			group[key] = value;
			setValue = true;
		}

		// if we have no values, omit this output
		if (!setValue)
			continue;

		// set to output
		output.push(group);

		// if we have raw text, add it at the end
		if (text.length > 0)
			output.push(text);
	}

	// temporarily log all ztag output
	//console.log("Temp ZTAG Log: ", output);
	return output;
}

function readObjectListFromZtagOutput(obj: any, keysToLookFo: string[]) {
	const result: any = []

	for (let index = 0;; ++index) {
		if (index === 1000) {
			util.log('Parse -ztag WARNING: broke out after 1000 items')
			break
		}

		const item: any = {}
		for (const lookFor of keysToLookFo) {
			const key = lookFor + index
			if (obj[key]) {
				item[lookFor] = obj[key]
			}
		}
		if (Object.keys(item).length === 0)
			break

		result.push(item)
	}
	return result
}

class CommandRecord {
	constructor(public cmd: string, public start: Date = new Date())
	{
	}
}

export interface Change {
	// from Perforce (ztag)
	change: number;
	client: string;
	user: string;
	path?: string;
	desc?: string;
	status?: string;
	shelved?: number;

	// also from Perforce, but maybe not useful
	changeType?: string;
	time?: number;

	// hacked in
	isManual?: boolean;
	ignoreExcludedAuthors?: boolean;
	forceRoboShelf?: boolean;
}


// After TS conversion, tidy up workspace usage
// Workspace and RoboWorkspace are fudged parameter types for specifying a workspace
// ClientSpec is a (partial) Perforce definition of a workspace

export interface Workspace {
	name: string,
	directory: string
}

// temporary fudging of workspace string used by main Robo code
export type RoboWorkspace = Workspace | string | null;

export interface ClientSpec {
	client: string;
	Stream?: string
}

interface ExecOpts {
	stdin?: string;
	quiet?: boolean;
	noCwd?: boolean;
}

interface ExecZtagOpts extends ExecOpts {
	multiline?: boolean;
}

export interface EditOwnerOpts {
	newWorkspace?: string;
	changeSubmitted?: boolean;
}

// interface ConflictedFile {
// 	contentResolveType: string  // e.g. '3waytext' (check what it is for binaries)

// 	startFromRev: number
// 	endFromRev: number

// 	fromFile: string
// 	resolveType: string // e.g. 'content', 'branch'

// 	resolveFlag: string // 'c' ?
// }

export class ResolveResult {
	private result: string
	
	constructor(_resolveOutput: any[] | null, resolveDashNOutput: string) {

		this.result = resolveDashNOutput
	}

	hasConflict() {
		return this.result !== 'success'
	}

	getRawOutput() {
		return this.result
	}

	getConflictsText() {
		// @todo
		return this.getRawOutput()
	}
}

// P4 control object (contains some state like client)
// this class serializes all P4 commands sent to it.
export class Perforce {
	running = new Set<CommandRecord>();
	verbose = false;
	username: string;

	static FORCE = '-f';

	constructor(version: string) {
		this.robomergeVersion = version;
	}

	// check if we are logged in and p4 is set up correctly
	async start() {
		const output = await this._execP4Ztag(null, ["login", "-s"]);
		let resp = output[0];

		if (resp && resp.User) {
			this.username = resp.User;
		}
		return resp;
	}

	// get a list of all pending changes for this user
	get_pending_changes() {
		if (!this.username) {
			throw new Error("username not set");
		}

		return this._execP4Ztag(null, ['changes', '-u', this.username, '-s', 'pending'], {multiline: true});
	}

	// get a list of changes in a path since a specific CL
	// output format is list of changelists
	async latestChange(path: string) {
		const result = await this.changes(path, 0, 1);
		return <Change>(result && result.length > 0 ? result[0] : null);
	}

	/** get a single change in the format of changes() */
	async getChange(path_in: string, changenum: number) {
		const list = await this.changes(`${path_in}@${changenum},${changenum}`, -1, 1)
		if (list.length <= 0) {
			throw new Error(`Could not find changelist ${changenum} in ${path_in}`);
		}
		return <Change>list[0];
	}

	/**
	 * Get a list of changes in a path since a specific CL
	 * @return Promise to list of changelists
	 */
	async changes(path_in: string, since: number, limit?: number): Promise<Change[]> {
		const path = since > 0 ? path_in + '@>' + since : path_in;
		const args = ['changes', '-l', '-ssubmitted',
			...(limit ? [`-m${limit}`] : []),
			path];

		const parsedChanges = await this._execP4Ztag(null, args, {multiline: true, quiet: true});
		for (const change of parsedChanges) {
			if (!change.change || !change.client || !change.user) {
				throw new Error('Unrecognised change format');
			}

			// Perforce sometimes 'helpfully' gives changelist descriptions as numbers
			if (change.desc && typeof(change.desc) === 'number') {
				change.desc = change.desc.toString();
			}
		}

		return parsedChanges;
	}

	// find a workspace for the given user
	// output format is an array of workspace names
	async find_workspaces(user?: string) {
		let parsedClients = await this._execP4Ztag(null, ['clients', '-u', user || this.username], {multiline: true});
		let workspaces = [];
		for (let clientDef of parsedClients)
		{
			if (clientDef.client) {
				workspaces.push(clientDef);
			}
		}
		return workspaces as ClientSpec[];
	}

	async clean(roboWorkspace: RoboWorkspace) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		let result;
		try {
			result = await this._execP4(workspace, ['clean', '-e', '-a']);
		}
		catch([err, output]) {
			// ignore this error
			if (!output.trim().startsWith('No file(s) to reconcile')) {
				throw(err);
			}
			result = output;
		}
		util.log(`p4 clean:\n${result}`);
	}

	// sync the depot path specified
	async sync(roboWorkspace: RoboWorkspace, depotPath: string, opts?: string[]) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		const args = ['sync', ...(opts || []), depotPath];
		try
		{
			await this._execP4(workspace, args);
		}
		catch ([err, output])
		{
			// this is an acceptable non-error case for us
			if (!output.trim().endsWith("up-to-date."))
				throw new Error(err);
		}
	}

	async syncAndReturnChangelistNumber(roboWorkspace: RoboWorkspace, depotPath: string, opts?: string[]) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		const change = await this.latestChange(depotPath);
		if (!change) {
			throw new Error('Unable to find changelist');
		}
		await this.sync(workspace, `${depotPath}@${change.change}`, opts);
		return change.change;
	}

	static getRootDirectoryForBranch(name: string): string {
		return process.platform === "win32" ? `d:/ROBO/${name}` : `/src/${name}`;
	}

	newWorkspace(workspaceName:string, workspaceForm: string, params: any) {
		// drop any custom params
		for (let key in params) {
			let val = params[key];
			if (Array.isArray(val)) {
				workspaceForm += `${key}:\n`;
				for (let item of val) {
					workspaceForm += `\t${item}\n`;
				}
			}
			else {
				workspaceForm += `${key}: ${val}\n`;
			}
		}

		// run the p4 client command
		util.log(`Executing: 'p4 client -i' to create workspace ${workspaceName}`);
		return this._execP4(null, ['client', '-i'], {stdin: workspaceForm, quiet: true});
	}

	// Create a new workspace for Robomerge engine
	newEngineWorkspace(name: string, params: any) {
		let form = `Client: ${name}\n`;

		form += `Owner: ${this.username}\n`;
		form += `Root: d:/ROBO/${name}\n`; // windows path
		form += `AltRoots: /src/${name}\n`; // linux path
		form += "Options: noallwrite clobber nocompress nomodtime\n";
		form += "SubmitOptions: submitunchanged\n";
		form += "LineEnd: local\n";

		return this.newWorkspace(name, form, params);
	}

	// Create a new workspace for Robomerge to read branchspecs from
	newBranchSpecWorkspace(workspaceName: string, bsDepotPath: string, bsDirectory?: string) {
		let form = `Client: ${workspaceName}\n`;

		form += `Owner: ${this.username}\n`;
		form += `Root: d:/ROBO/${workspaceName}\n`; // windows path

		form += `AltRoots: /app/data\n`; // default linux path

		if (bsDirectory != undefined && bsDirectory != "/app/data") {
			form += `\t${bsDirectory}\n`; // specified directory
		}
		
		form += "Options: noallwrite clobber nocompress nomodtime\n";
		form += "SubmitOptions: submitunchanged\n";
		form += "LineEnd: local\n\n";

		// Perforce paths are mighty particular 
		if (bsDepotPath.endsWith("/...")) {
			form += `View: ${bsDepotPath} //${workspaceName}/...`;
		} else if (bsDepotPath.endsWith("/")) {
			form += `View: ${bsDepotPath}... //${workspaceName}/...`;
		} else {
			form += `View: ${bsDepotPath}/... //${workspaceName}/...`;
		}

		return this.newWorkspace(workspaceName, form, undefined);
	}

	// temp
	static coerceWorkspace(workspace: any): Workspace | null {
		if (!workspace)
			return null

		if (typeof(workspace) === "string") {
			workspace = {name: workspace};
		}

		if (!workspace.directory) {
			workspace.directory = Perforce.getRootDirectoryForBranch(workspace.name);
		}
		return <Workspace>workspace
	}

	// create a new CL with a specific description
	// output format is just CL number
	async new_cl(roboWorkspace: RoboWorkspace, description: string, files?: string[]) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		// build the minimal form
		let form = 'Change:\tnew\nStatus:\tnew\nType:\tpublic\n';
		if (workspace)
			form += `Client:\t${workspace.name}\n`;

		if (files) {
			form += 'Files:\n';
			for (const filename of files) {
				form += `\t${filename}\n`;
			}
		}
		form += 'Description:\n\t' + this._sanitizeDescription(description);

		// run the P4 change command
		util.log("Executing: 'p4 change -i' to create a new CL");
		const output = await this._execP4(workspace, ['change', '-i'], {stdin: form, quiet: true});
		// parse the CL out of output
		const match = output.match(/Change (\d+) created./);
		if (!match) {
			throw new Error('Unable to parse new_cl output:\n' + output);
		}

		// return the changelist number
		return parseInt(match[1]);
	}

	// integrate a CL from source to destination, resolve, and place the results in a new CL
	// output format is true if the integration resolved or false if the integration wasn't necessary (still considered a success)
	// failure to resolve is treated as an error condition
	async integrate(roboWorkspace: RoboWorkspace, source: IntegrationSource, dest_changelist: number, path_to: string): Promise<[string, (Change | string)[]]> {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		// build a command
		let cmdList = [
			"integrate",
			"-Ob",
			"-Or",
			"-Rd",
			"-Rb",
			"-c"+dest_changelist
		];

		const range = `@${source.changelist},${source.changelist}`
		if (source.branchspec)
		{
			cmdList.push("-b");
			cmdList.push(source.branchspec.name);
			if (source.branchspec.reverse) {
				cmdList.push("-r");
			}
			cmdList.push(path_to + range);
		}
		else
		{
			cmdList.push(source.path_from + range);
			cmdList.push(path_to);
		}

		// execute the P4 command
		let changes;
		try {
			changes = await this._execP4Ztag(workspace, cmdList);
		}
		catch ([err, output]) {
			if (!output || !output.match) {
				throw err;
			}

			// if this change has already been integrated, this is a special return (still a success)
			if (output.match(/already integrated\.\n/)) {
				return ["already_integrated", []];
			}
			else if (output.match(/No file\(s\) at that changelist number\.\n/)) {
				return ["no_files", []];
			}
			else if (source.branchspec && output.match(/No such file\(s\)\.\n/)) {
				return ["no_files", []];
			}
			else if (output.match(/no target file\(s\) in branch view\n/)) {
				return ["no_files", []];
			}
			else if (output.match(/resolve move to/)) {
				return ["partial_integrate", output.split('\n')];
			}
			// otherwise pass on error
			throw err;
		}

		// annoyingly, p4 outputs failure to integrate locked files here on stdout (not stderr)

		const failures: string[] = [];
		for (const change of changes) {
			if (Array.isArray(change))
			{
				// P4 emitted some error(s) in the stdout
				failures.push(...change);
			}
		}

		// if there were any failures, return that
		if (failures.length > 0) {
			return ["partial_integrate", failures];
		}

		// everything looks good
		return ["integrated", <Change[]>changes];
	}


	// output format a list of conflicting files (p4 output)
	async resolve(roboWorkspace: RoboWorkspace, changelist: number, resolution: string): Promise<ResolveResult> {
		const workspace = Perforce.coerceWorkspace(roboWorkspace)
		let flag = null
		switch (resolution)
		{
		case 'safe': 	flag = '-as'; break
		case 'normal':	flag = '-am'; break
		case 'null':	flag = '-ay'; break
		case 'clobber':	flag = '-at'; break
		default:
			throw new Error(`Invalid resultion type ${resolution}`)
		}

		let fileInfo
		try {
			fileInfo = await this._execP4Ztag(workspace, ['resolve', flag, `-c${changelist}`])
		}
		catch ([err, output]) {
			let result: string | null = null
			if (output.startsWith('No file(s) to resolve.')) {
				result = 'success'
			}
			else if (output.match(/can't move \(open for delete\)/) ||
				output.match(/can't delete moved file;/)) {
				result = output
			}

			if (result === null) {
				throw err
			}

			return new ResolveResult(null, result)
		}

		let dashNresult: string
		try {
			dashNresult = await this._execP4(workspace, ['resolve', '-N', `-c${changelist}`])
		}
		catch ([err, output]) {
			if (!output.startsWith('No file(s) to resolve.')) {
				throw err
			}
			dashNresult = 'success'
		}

		return new ResolveResult(fileInfo, dashNresult)
	}

	// submit a CL
	// output format is final CL number or false if changes need more resolution
	async submit(roboWorkspace: RoboWorkspace, changelist: number) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace)
		let rawOutput: string
		try {
			rawOutput = await this._execP4(workspace, ['-ztag', 'submit', '-f', 'submitunchanged', '-c', changelist.toString()])
		}
		catch ([err, output]) {
			const out = output.trim()
			if (out.startsWith('Merges still pending --')) {
				// concurrent edits (try again)
				util.log(`=== SUBMIT FAIL === \nERR:${err}\nOUT:${out}`);

				return 0
			}
			else if (out.startsWith('Out of date files must be resolved or reverted')) {
				util.log(`=== SUBMIT FAIL === \nERR:${err}\nOUT:${out}`);

				// concurrent edits (try again)
				return 0
			}
			else if (out.startsWith('No files to submit.')) {
				util.log(`=== SUBMIT FAIL === \nERR:${err}\nOUT:${out}`);
				await this.deleteCl(workspace, changelist)
				return 0
			}
			throw err
		}

		// success, parse the final CL
		const result: any[] = parseZTag(rawOutput)
		const final = result.pop()
		const final_cl = final ? final.submittedChange : 0
		if (final_cl) {
			// return the final CL
			return final_cl
		}

		util.log(`=== SUBMIT FAIL === \nOUT:${rawOutput}`)
		throw new Error(`Unable to find submittedChange in P4 results:\n${rawOutput}`)
	}


// SHOULDN'T REALLY LEAK [err, result] FORMAT IN REJECTIONS

// 	- thinking about having internal and external exec functions, but, along
// 	- with ztag variants, that's quite messy

// should refactor so that:
//	- exec never fails, instead returns a rich result type
//	- ztag is an option
//	- then can maybe have a simple wrapper


	// delete a CL
	// output format is just error or not
	deleteCl(roboWorkspace: RoboWorkspace, changelist: number) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		return this._execP4(workspace, ["change", "-d", changelist.toString()]);
	}

	// run p4 'opened' command: lists files in changelist with details of edit state (e.g. if a copy, provides source path)
	opened(arg: number | string) {
		const args = ['opened']
		if (typeof arg === 'number') {
			// see what files are open in the given changelist
			args.push('-c', arg.toString())
		}
		else {
			// see which workspace has a file checked out/added
			args.push('-a', arg)
		}
		return this._execP4Ztag(null, args) as Promise<OpenedFileRecord[]>
	}

	// revert a CL deleting any files marked for add
	// output format is just error or not
	async revert(roboWorkspace: RoboWorkspace, changelist: number, opts?: string[]) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		try {
			await this._execP4(workspace, ['revert', /*"-w", seems to cause ENOENT occasionally*/ ...(opts || []), '-c', changelist.toString(), '//...']);
		}
		catch ([err, output]) {
			// this happens if there's literally nothing in the CL. consider this a success
			if (!output.match(/file\(s\) not opened on this client./)) {
				throw err;
			}
		}
	}

	// list files in changelist that need to be resolved
	async listFilesToResolve(roboWorkspace: RoboWorkspace, changelist: number) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		try {
			return await this._execP4Ztag(workspace, ['resolve', '-n', '-c', changelist.toString()]);
		}
		catch (err) {
			if (err.toString().toLowerCase().includes('no file(s) to resolve')) {
				return [];
			}
			throw(err);
		}
	}

	async move(roboWorkspace: RoboWorkspace, cl: number, src: string, target: string, opts?: string[]) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		return this._execP4(workspace, ['move', ...(opts || []), '-c', cl.toString(), src, target]);
	}

	/**
	 * Run a P4 command by name on one or more files (one call per file, run in parallel)
	 */
	run(roboWorkspace: RoboWorkspace, action: string, cl: number, filePaths: string[], opts?: string[]) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		const args = [action, ...(opts || []), '-c', cl.toString()]
		return Promise.all(filePaths.map(
			path => this._execP4(workspace, [...args, path])
		));
	}

	// shelve a CL
	// output format is just error or not
	async shelve(roboWorkspace: RoboWorkspace, changelist: number) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace)
		try {
			await this._execP4(workspace, ['shelve', '-f', '-c', changelist.toString()])
		}
		catch ([err, output]) {
			if (err) {
				if (output && output.match && output.match(/No files to shelve/)) {
					return false
				}
				throw err
			}
		}
		return true
	}

	// delete shelved files from a CL
	delete_shelved(roboWorkspace: RoboWorkspace, changelist: number) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		return this._execP4(workspace, ["shelve", "-d", "-c", changelist.toString(), "-f"]);
	}

	// get the email (according to P4) for a specific user
	async getEmail(username: string) {
		const output = await this._execP4(null, ['user', '-o', username]);
		// look for the email field
		let m = output.match(/\nEmail:\s+([^\n]+)\n/);
		return m && m[1];
	}

	/** Check out a file into a specific changelist ** ASYNC ** */
	async edit(roboWorkspace: RoboWorkspace, cl: number, filePath: string) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		return this._execP4(workspace, ['edit', '-c', cl.toString(), filePath]);
	}

	async describe(cl: number) {
		// const workspace = Perforce.coerceWorkspace(roboWorkspace);
		const ztagResult = await this._execP4Ztag(null, ['describe', cl.toString()], {multiline:true});

		if (ztagResult.length !== 2) {
			throw new Error('Unexpected describe result')
		}

		const clInfo: any = ztagResult[0];
		const result: DescribeResult = {
			user: clInfo.user || '',
			status: clInfo.status || '',
			description: clInfo.desc || '',
			entries: []
		};

		for (const obj of readObjectListFromZtagOutput(ztagResult[1], ['depotFile', 'action', 'rev'])) {
			let rev = -1;
			if (obj.rev) {
				const num = parseInt(obj.rev);
				if (!isNaN(num)) {
					rev = num;
				}
			}
			result.entries.push({
				depotFile: obj.depotFile || '',
				action: obj.action || '',
				rev: rev 
			});
		}
		return result;
	}

	static testDescribe() {
		const p4 = new Perforce('test');
		p4.start()
		.then(() => p4.describe(4051187))
		.then((result: DescribeResult) => {

			console.log(result.user);
			for (const entry of result.entries) {
				console.log(`${entry.depotFile}#${entry.rev}: ${entry.action}`);
			}
		});
	}

	// change the description on an existing CL
	// output format is just error or not
	async editDescription(roboWorkspace: RoboWorkspace, changelist: number, description: string) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);

		// get the current changelist description
		const output = await this._execP4(workspace, ['change', '-o', changelist.toString()]);
		// replace the description
		let new_desc = '\nDescription:\n\t' + this._sanitizeDescription(description);
		let form = output.replace(/\nDescription:\n(\t[^\n]*\n)*/, new_desc.replace(/\$/g, '$$$$'));

		// run the P4 change command to update
		util.log("Executing: 'p4 change -i -u' to edit description on CL" + changelist);
		await this._execP4(workspace, ['change', '-i', '-u'], {stdin: form, quiet: true});
	}

	// change the owner of an existing CL
	// output format is just error or not
	async editOwner(roboWorkspace: RoboWorkspace, changelist: number, newOwner: string, opts?: EditOwnerOpts) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);

		opts = opts || {}; // optional newWorkspace:string and/or changeSubmitted:boolean
		// get the current changelist description
		const output = await this._execP4(workspace, ['change', '-o', changelist.toString()]);

		// replace the description
		let form = output.replace(/\nUser:\t[^\n]*\n/, `\nUser:\t${newOwner}\n`);
		if (opts.newWorkspace) {
			form = form.replace(/\nClient:\t[^\n]*\n/, `\nClient:\t${opts.newWorkspace}\n`);
		}

		// run the P4 change command to update
		const changeFlag = opts.changeSubmitted ? '-f' : '-u';
		util.log(`Executing: 'p4 change -i ${changeFlag}' to edit user/client on CL${changelist}`);

		await this._execP4(workspace, ['change', '-i', changeFlag], {stdin: form, quiet: true});
	}


	private _sanitizeDescription(description: string) {
		return description.trim().replace(/\n\n\.\.\.\s/g, "\n\n ... ").replace(/\n/g, "\n\t");
	}

	// execute a perforce command
	private _execP4(roboWorkspace: RoboWorkspace, args: string[], optsIn?: ExecOpts) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		// add the client explicitly if one is set (should be done at call time)

		const opts = optsIn || {};

		// Perforce can get mighty confused if you log into multiple accounts at any point (only relevant to local debugging)
		if (this.username) {
			args = ['-u', this.username, ...args];
		}

		if (workspace && workspace.name) {
			args = ['-c', workspace.name, ...args];
		}

		args = ['-zprog=robomerge', '-zversion=' + this.robomergeVersion, ...args]

		// log what we're running
		let cmd_rec = new CommandRecord('p4 ' + args.join(' '));
		// if (!quiet || this.verbose)
		// 	util.log("Executing: " + cmd_rec.cmd);
		this.running.add(cmd_rec);

		// we need to run within the workspace directory so p4 selects the correct AltRoot
		let options: ExecFileOptions = { maxBuffer: 100*1024*1024 };
		if (workspace && workspace.directory && !opts.noCwd) {
			options.cwd = workspace.directory;
		}

		if (!opts.quiet || this.verbose) {
			util.log(`  cmd: ${p4exe} ${args.join(' ')} ` + (options.cwd ? `(from ${options.cwd})` : ''));
		}

		// darwin p4 client seems to need this
		if (options.cwd)
		{
			options.env = { };
			for (let key in process.env)
				options.env[key] = process.env[key];
			options.env.PWD = path.resolve(options.cwd);
		}

		// run the child process
		return new Promise<string>((done, fail) => {
			const child = execFile(p4exe, args, options, (err, stdout, stderr) => {
				if (this.verbose)
					util.log("Command Completed: " + cmd_rec.cmd);
				this.running.delete(cmd_rec);

				// run the callback
				if (stderr) {
					let errstr = "P4 Error: "+cmd_rec.cmd+"\n";
					errstr += "STDERR:\n"+stderr+"\n";
					errstr += "STDOUT:\n"+stdout+"\n";
					if (opts.stdin)
						errstr += "STDIN:\n"+opts.stdin+"\n";
					fail([new Error(errstr), stderr.toString().replace(newline_rex, '\n')]);
				}
				else if (err) {
					util.log(err.toString());
					let errstr = "P4 Error: "+cmd_rec.cmd+"\n"+err.toString()+"\n";

					if (stdout || stderr)
					{
						if (stdout)
							errstr += "STDOUT:\n"+stdout+"\n";
						if (stderr)
							errstr += "STDERR:\n"+stderr+"\n";
					}

					if (opts.stdin)
						errstr += "STDIN:\n"+opts.stdin+"\n";

					fail([new Error(errstr), stdout ? stdout.toString() : '']);
				}
				else {
					done(stdout.toString().replace(newline_rex, '\n'));
				}
			});

			// write some stdin if requested
			if (opts.stdin) {
				try {
					if (this.verbose) {
						util.log('-> Writing to p4 stdin:');
						util.log(opts.stdin);
					}
					child.stdin.write(opts.stdin);
					child.stdin.end();

					if (this.verbose) {
						util.log('<-');
					}
				}
				catch (ex) {
					// usually means P4 process exited immediately with an error, which should be logged above
					console.log(ex);
				}
			}
		});
	}

	async _execP4Ztag(roboWorkspace: RoboWorkspace, args: string[], opts?: ExecZtagOpts) {
		const workspace = Perforce.coerceWorkspace(roboWorkspace);
		return parseZTag(await this._execP4(workspace, ['-ztag', ...args], opts), opts && opts.multiline);
	}

	private readonly robomergeVersion: string
}

if (process.argv.indexOf('__TEST__') !== -1) {
	Perforce.testDescribe();
}
