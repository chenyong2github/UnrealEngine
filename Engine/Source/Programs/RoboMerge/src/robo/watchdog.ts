// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import * as fs from 'fs'
import * as os from 'os'
import * as util from 'util'


import {fork, ChildProcess,ForkOptions} from 'child_process'

import {RoboServer} from './roboserver'
import {WebServer, WebRequest, Handler} from '../common/webserver'
import {URL, format} from 'url'
import {Session} from './session'
import {Analytics} from '../common/analytics'
import {Arg, readProcessArgs} from '../common/args'

// fail hard on exceptions thrown in async functions
process.on('unhandledRejection', err => {
	throw err;
});

const ENTRY_POINT = 'dist/robo/robo.js'
const RESPAWN_DELAY = 30
const LOG_BUFFER_LINES = 2000
const AUTO_RESPAWN = false
const MEM_USAGE_INTERVAL_SECONDS = 5 * 60

const COMMAND_LINE_ARGS: {[param: string]: (Arg<any>)} = {
	hostInfo: {
		match: /^-hostInfo=(.+)$/,
		env: 'ROBO_HOST_INFO',
		dflt: os.hostname()
	}
}

const args = readProcessArgs(COMMAND_LINE_ARGS);

// intercept stdout
let logbuffer = new Array<string>(LOG_BUFFER_LINES)
let last_buffer_pos = 0

/* */ ; /* */ ; /* */ ; /* */ ; /* */

function writeLogs(line: string) {
	logbuffer[last_buffer_pos] = line
	last_buffer_pos = (last_buffer_pos + 1) % LOG_BUFFER_LINES
}

function intercept(stream: any) {
	let write = stream.write
	stream.write = function(str: string, encoding: any, fd: any) {
		writeLogs(str)
		write.call(stream, str, encoding, fd)
	}
}

intercept(process.stdout)
intercept(process.stderr)

class RedirectApp {
	constructor(request: WebRequest) {
		this.request = request;
	}

	@Handler('GET', /.*/)
	redirect() {

		const targetUrl = new URL(this.request.url as any) // @types seems to be missing copy constructor
		targetUrl.protocol = 'https:'
		targetUrl.port = ""

		// slight hack - redirect from plain 'http://robomerge' to 'https://robomerge.epicgames.net', since
		//	TLS cert is not valid for 'https://robomerge'
		const hostLower = targetUrl.hostname.toLowerCase()
		if (hostLower !== 'localhost' && hostLower.indexOf('.') === -1) {
			targetUrl.hostname = targetUrl.hostname + '.epicgames.net'
		}

		return {statusCode: 301, message: 'Now secure!', headers: [['Location', format(targetUrl)]]}
	}

	private request: WebRequest
}

class Watchdog {
	child: ChildProcess | null = null
	statusServer: RoboServer
	redirectServer: WebServer | null = null

	shutdown = false
	paused = false
	lastSpawnStart = -1

	lastCrash: string | null = null

	cbMap = new Map<number, Function>()
	nextCbId = 1

	constructor() {
		// expose a webserver that can be used to check status
		this.statusServer = new RoboServer(
			(name: string, args: any[]) => this.sendMessage(name, args), 
			() => this.getLogTail(), () => this.getLastCrash(), () => this.stopBot(), () => this.startBot()
		)

		this.respawnTimer = setImmediate(() => this.spawnEntryPoint())
		this.memUsageTimer = setInterval(() => {
			if (this.analytics) {
				this.analytics.reportMemoryUsage('watchdog', process.memoryUsage().heapUsed)
			}
		}, MEM_USAGE_INTERVAL_SECONDS * 1000) 
	}

	getLastCrash() {
		return this.lastCrash
	}

	getLogTail() {
		let out = ''
		for (let index = 0; index != logbuffer.length; ++index) {
			const idx = (last_buffer_pos + index) % logbuffer.length
			const str = logbuffer[idx]
			if (str) {
				out += str
			}
		}
		return out
	}

	sendMessage(name: string, args: any[]) {
		return new Promise<Object | null>((done, _fail) => {
			if (this.child && !this.paused) {
				// send the message
				let cbid = this.nextCbId++
				this.cbMap.set(cbid, done)
				this.child.send({cbid: cbid, name: name, args: args})
			}
			else {
				done(null)
			}
		})
	}

	stopBot() {
		if (!this.paused) {
			console.log("Pausing bot")
			this.paused = true
		}

		// cancel any timers
		if (this.respawnTimer) {
			clearTimeout(this.respawnTimer)
			this.respawnTimer = null
		}

		// tell the child to shutdown
		if (this.child !== null) {
			this.child.kill('SIGINT')
		}
	}

	startBot() {
		if (this.paused) {
			console.log("Unpausing bot")
			this.paused = false
		}

		// cancel any timers
		if (this.respawnTimer) {
			clearTimeout(this.respawnTimer)
			this.respawnTimer = null
		}

		// respawn now
		if (this.child === null) {
			this.respawn(true)
		}
	}

	startServer() {
		// for now, require HTTPS vault settings - eventually have a proper dev
		// setting that can allow local testing without
		let filesInVault: string[]
		try {

			filesInVault = fs.readdirSync(Session.VAULT_PATH)
		}
		catch (err) {
			this.statusServer.open(8080, 'http').then(() => 
				console.log(`Warning! HTTP web server opened on port 8080`)
			)
			return
		}

		let tlsKeyFilename: string | null = null
		for (const fn of filesInVault) {
			if (fn.endsWith('.key')) {
				tlsKeyFilename = fn
				break
			}
		}

		if (!tlsKeyFilename) {
			throw new Error('TLS private key not found in vault')
		}

		const certFiles = {
			key: fs.readFileSync(`${Session.VAULT_PATH}/${tlsKeyFilename}`, 'ascii'),
			cert: fs.readFileSync('./certs/robomerge.pem', 'ascii')
		}
		// Removed ca: [fs.readFileSync('./certs/thwate.cer')] - JR 10/4

		this.statusServer.open(4433, 'https', certFiles).then(() => 
			console.log(`Web server opened on port 4433`)
		)

		// start redirect server
		if (this.statusServer && this.statusServer.isSecure()) {
			this.redirectServer = new WebServer()
			this.redirectServer.addApp(RedirectApp)
			this.redirectServer.open(8080, 'http')
			.then(() => console.log('Started redirect server'))

			this.analytics = new Analytics(args.hostInfo)
			Session.onLoginAttempt = (result: string) => this.analytics.reportLoginAttempt(result)
		}

	}

	private childExited() {
		if (!this.child) {
			return
		}
		this.child = null

		// capture crash logs
		this.lastCrash = this.getLogTail()

		// cancel callbacks
		for (const cb of this.cbMap.values()) {
			setTimeout(() => cb(null), 0)
		}
		this.cbMap.clear()
	}

	// spawn the main app
	private spawnEntryPoint() {
		this.lastSpawnStart = process.hrtime()[0]
		this.respawnTimer = null
		if (this.child !== null)
			throw new Error('child is not null')
		const args = process.argv.slice(2);
		console.log(`Spawning ${ENTRY_POINT} with args ${args}`)
		let options : ForkOptions = { stdio: ['pipe', 'pipe', 'pipe', 'ipc'] }

		// prevent passing same debugger port under VSCode (FIXME: shouldn't be necessary due to autoAttachChildProcesses)
		let opts = [...process.execArgv];
		for (let i=0;i<opts.length;++i) {
			if (opts[i].startsWith("--inspect-brk="))
				opts[i] = "--inspect";
		}
		options.execArgv = opts;
		
		// fork the child
		this.child = fork(ENTRY_POINT, args, options)
		this.child.once('close', (code, signal) => {
			this.childExited()
			if (code !== null) {
				console.log(`Child process exited with code=${code}`)
				if (code !== 0) {
					this.respawn()
				}
			}
			else {
				console.log(`Child process exited with signal=${signal}`)
				this.respawn()
			}
		})
		this.child.once('error', err => {
			console.log(`Error from ${ENTRY_POINT}`, err)
			this.childExited()
			this.respawn()
		})
		this.child.on('message', (msg) => {
			if (this.child === null)
				return
			let callback = this.cbMap.get(msg.cbid)
			if (callback !== undefined) {
				this.cbMap.delete(msg.cbid)
				if (msg.error)
					console.log("Error: ", msg.error)
				callback(msg.args)
			}
		})
		this.child.stderr.on('data', (chunk) => {
			process.stderr.write(chunk)
		})
		this.child.stdout.on('data', (chunk) => {
			process.stdout.write(chunk)
		})
	}

	respawn(now?: boolean) {
		if (this.shutdown || this.paused) {
			return // shutting down or paused
		}
		if (this.respawnTimer) {
			return // already queued
		}
		if (!AUTO_RESPAWN && !now)
		{
			console.log("Not configured to auto respawn")
			return
		}

		// log respawn
		console.log('---------------------------------------------------------------------')
		let delay = now ? 0 : RESPAWN_DELAY
		console.log(`Respawning in ${delay} seconds`)
		this.respawnTimer = setTimeout(() => this.spawnEntryPoint(), delay * 1000);
	}

	shutDownProcess(sig: NodeJS.Signals) {
		console.log(`Watchdog caught ${sig} (will not reboot)`)
		if (this.shutdown) {
			console.log("Immediate exit")
			process.exit()
		}

		this.shutdown = true
		this.stopBot()

		// close the status server
		if (this.statusServer) {
			console.log('Closing Web Status server')
			this.statusServer.close().then(() =>
				console.log('Web server closed.')
			)
		}

		if (this.redirectServer) {
			this.redirectServer.close().then(() =>
				console.log('Redirect server closed.')
			)
		}

		if (this.analytics) {
			this.analytics.stop()
		}

		clearTimeout(this.memUsageTimer)

		setTimeout(() => {
			const dump = (x: any) => (x.constructor ? x.constructor.name : x);

			const proc = process as any
			const handles = proc._getActiveHandles()
			if (handles.length !== 0) {
				util.log('Active handles: ' + handles.map(dump))
			}

			const requests = proc._getActiveRequests()
			if (requests.length !== 0) {
				util.log('Active requests: ' + requests.map(dump))
			}
		}, 3000);
	}

	private analytics: Analytics

	private respawnTimer: NodeJS.Timer | null = null
	private memUsageTimer: NodeJS.Timer
}

const watchdog = new Watchdog
function onSignal(sig: NodeJS.Signals) {
	watchdog.shutDownProcess(sig)
}

process.on('SIGINT', onSignal)
process.on('SIGTERM', onSignal) // docker stop sends SIGTERM

watchdog.startServer()
