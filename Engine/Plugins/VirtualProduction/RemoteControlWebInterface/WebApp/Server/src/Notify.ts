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
        .on('execute', UnrealEngine.executeFunction)
        .on('metadata', UnrealEngine.setPresetPropertyMetadata);

      if (UnrealEngine.isConnected())
        socket.emit('connected', true);

      if (UnrealEngine.isLoading())
        socket.emit('loading', true);
    });
  }

  export function emit(what: 'presets' | 'payloads' | 'connected' | 'loading', value: any) {
    io.emit(what, value);
  }

  export function onViewChange(preset: string, view: IView, supressUnrealNotification?: boolean) {
    if (!supressUnrealNotification)
      UnrealEngine.setView(preset, view);
    io.emit('view', preset, view);
  }

  export function emitValueChange(preset: string, property: string, value: PropertyValue) {
    io.emit('value', preset, property, value);
  }
}