type OK = { ok: true, error: undefined }
type ERR = { ok: false, error: { name: string, message: string }}
interface Monitor {
  start(callback: (info: { pid: number, message: string }) => void): OK | ERR
  stop(): boolean
}
export declare const monitor: Monitor
export declare function OutputDebugString(message: string): void
