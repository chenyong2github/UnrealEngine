import { Controller, Get, Post, Put, Delete } from '@overnightjs/core';
import { Request, Response } from 'express';
import { UnrealEngine } from './';


interface ISendOptions {
  type?: string;
  nullIsNotFound?: boolean;
}

@Controller('api')
export class Api {
  
  public async initialize(): Promise<void> {
  }

  @Get('connected')
  private async connected(req: Request, res: Response): Promise<void> {
    this.send(res, Promise.resolve({ connected: UnrealEngine.isConnected() }));
  }

  @Get('presets')
  private async presets(req: Request, res: Response): Promise<void> {
    this.send(res, UnrealEngine.getPresets());
  }

  @Get('presets/payload')
  private async payload(req: Request, res: Response): Promise<void> {
    this.send(res, UnrealEngine.getPayload(req.query.preset?.toString()));
  }

  @Get('presets/view')
  private async view(req: Request, res: Response): Promise<void> {
    this.send(res, UnrealEngine.getView(req.query.preset?.toString()));
  }

  @Get('assets/search')
  private async search(req: Request, res: Response): Promise<void> {
    this.send(res, UnrealEngine.search(req.query.q?.toString()));
  }

  protected async send(res: Response, promise: Promise<any>, options?: ISendOptions): Promise<any> {
    try {

      const result = await promise;
      if (result === null && options?.nullIsNotFound)
        return res.status(404).send({ status: 'ERROR', error: 'Not found' });

      if (options?.type)
        res.set('Content-Type', options.type);

      res.send(result);

    } catch (err) {
      const error = err?.message ?? err;
      res.status(500)
          .send({ status: 'ERROR', error });
    }
  }
}
