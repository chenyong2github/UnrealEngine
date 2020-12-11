import { IView, PropertyValue } from '@Shared';
import { Server } from 'http';
import socketio from 'socket.io';
import { UnrealEngine } from './UnrealEngine';

                  
export namespace Notify {
  var io = socketio({ path: '/api/io' });

  export async function initialize(server: Server): Promise<void> {
    io.attach(server);

    io.sockets.on('connection', (socket: socketio.Socket) => {
      socket
        .on('view', onViewChange)
        .on('value', UnrealEngine.setPayloadValue)
        .on('reset', UnrealEngine.resetPayloadValue)
        .on('execute', UnrealEngine.executeFunction)
        .on('asset', UnrealEngine.executeAssetAction);

      if (UnrealEngine.isConnected())
        socket.emit('connected', true);
    });
  }

  export function emit(what: 'presets' | 'payloads' | 'connected', value: any) {
    io.emit(what, value);
  }

  export function onViewChange(preset: string, view: IView) {
    UnrealEngine.setView(preset, view);
    io.emit('view', preset, view);
  }

  export function emitValueChange(preset: string, group: string, property: string, value: PropertyValue) {
    io.emit('value', preset, group, property, value);
  }
}