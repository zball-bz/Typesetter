// The fragment parser of the native tools (plan P2-13): the executor's
// opts.parse answered by `tsrc --fragments=-` — the same codegen the WASM
// export runs (tsr2_fragments), one process per parse.
import { execFileSync } from 'node:child_process';

export const nativeParse = (tsrc) => (req) => new Uint8Array(execFileSync(tsrc, ['--fragments=-'], { input: req }));
