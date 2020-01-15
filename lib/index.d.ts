declare type OK = {
  ok: true
  error: undefined
}
declare type ERR = {
  ok: false
  error: {
    name: string
    message: string
  }
}
interface monitor {
  start(callback: (info: { pid: number; message: string }) => void): OK | ERR
  stop(): boolean
}

export declare const monitor: monitor
export declare function OutputDebugString(message: string): void
