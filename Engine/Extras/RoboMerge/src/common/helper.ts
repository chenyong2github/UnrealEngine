// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
import * as util from 'util';
import {execFile, ExecFileOptions} from 'child_process';
import {Workspace} from '../common/perforce'

// fail hard on exceptions thrown in async functions
process.on('unhandledRejection', err => {
	throw err; 	// this is unexpected but not the fault of the committed changelist
});

// arg [0]: node, [1]: <script>.js, [2]: first client arg
export const FIRST_CMD_ARG = process.argv.length > 2 ? process.argv[2] : '';

// will maybe distinguish between dev and main TS, but remove JS branches then
export const DEPLOYMENT_BRANCH = 
	process.argv.indexOf('--dev') > 0 ? 'Dev-Robomerge' :
	process.argv.indexOf('--ts') > 0 ? 'robo-ts' :
	'main';

interface DeploymentSettings {
	ws: Workspace
	depot: string // p4 path
	dockerRepo: string // identifier in docker hub
	robomergeDockerFilename: string
}

const ROOT_DIRECTORY = process.platform.toString() === 'win32' ? 'd:/ROBO/src' : '/robo'

export const DEPLOYMENT_SETTINGS: DeploymentSettings = {
	ws: {name: '', directory: ROOT_DIRECTORY},
	depot: '', // must override
	dockerRepo: '', // must override
	robomergeDockerFilename: 'Dockerfile'
}

switch (DEPLOYMENT_BRANCH) {
default: throw new Error('unknown deployment branch ' + DEPLOYMENT_BRANCH)

case 'main':
	DEPLOYMENT_SETTINGS.ws.name = 'robomerge-internal-build'
	DEPLOYMENT_SETTINGS.depot = '//GamePlugins/Main/Programs/RoboMerge'
	DEPLOYMENT_SETTINGS.dockerRepo = 'MAIN'
	break;

case 'Dev-Robomerge':
	DEPLOYMENT_SETTINGS.ws.name = 'robomerge-build-DEV'
	DEPLOYMENT_SETTINGS.depot = '//depot/usr/james-hopkin/Dev-Robomerge'
	DEPLOYMENT_SETTINGS.dockerRepo = 'DEV'
	break;

case 'robo-ts':
	DEPLOYMENT_SETTINGS.ws.name = 'robomerge-build-dev-ts'
	DEPLOYMENT_SETTINGS.ws.directory = 'D:/robomerge-test-robo-ts'
	DEPLOYMENT_SETTINGS.depot = '//depot/usr/james-hopkin/robo-ts'
	DEPLOYMENT_SETTINGS.dockerRepo = 'TSDEV'
	DEPLOYMENT_SETTINGS.robomergeDockerFilename = 'robo-docker'
	break;
}


export const _nextTick = util.promisify(process.nextTick);
export const _setTimeout = util.promisify(setTimeout);


export const onExit: ((()=>void)[]) = [];

export async function cmdAsync(exe: string, args: string[], cwd?: string): Promise<string> {
	return new Promise<string>((done, fail) => {
		let options: ExecFileOptions = {};
		if (cwd) {
			options.cwd = cwd;
		}

		util.log(`  cmd: ${exe} ${args.join(' ')} ` + (cwd ? `(from ${cwd})` : ''));

		execFile(exe, args, options, (err: Error, stdout: string, stderr: string) => {
			if (err) {
				fail(err);
			}
			else {
				util.log(`${exe}: ${stderr}`);
				done(stdout === null || stdout === undefined ? 'ok' : stdout);
			}
		});
	});
}

export function doForever(f: () => void, desc: string, intervalSeconds: number) {
	f();
	if (FIRST_CMD_ARG === '--forever') {
		const timeStr = intervalSeconds > 120 ? `${intervalSeconds / 60} minutes` : `${intervalSeconds} seconds`;
		util.log(`Began polling ${desc} every ${timeStr}`)
		const handle = setInterval(f, intervalSeconds*1000);

		const stop = () => {
			for (const f of onExit) {
				f();
			}
			util.log('bye!');
			clearInterval(handle);
		};
		process.on('SIGINT', stop);
		process.on('SIGTERM', stop);
	}
}

export function setDefault<K, V>(map: Map<K, V>, key: K, def: V) {
	const val = map.get(key)
	if (val) {
		return val
	}
	map.set(key, def)
	return def
}

export class Random {
	static randint(minInc: number, maxInc: number) {
		return Math.floor(Math.random() * (maxInc - minInc)) + minInc
	}

	static choose(container: any[]) {
		return container[Random.randint(0, container.length - 1)]
	}
}
