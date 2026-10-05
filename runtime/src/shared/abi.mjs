// The ABI handshake (plan P1-01, D-H06): before writing a single op the host
// checks that the engine reads every buffer version this runtime can write
// and was generated from the same vocabulary schema. A stale wasm next to a
// regenerated runtime (or the reverse) fails here with one clear message
// instead of as scattered ops-invalid diagnostics.
import { OPS_VERSION, OPS_MIN_COMPAT, SCHEMA_HASH } from './ops.gen.mjs';
import { SYNTAX_VERSION } from './syntax.gen.mjs';
import { RES_VERSION } from './resources.gen.mjs';

export function checkAbi(M) {
  if (typeof M._tsr2_abi !== 'function') throw new Error('tsr: engine predates the ABI handshake (tsr2_abi)');
  const abi = JSON.parse(M.UTF8ToString(M._tsr2_abi()));
  const [lo, hi] = abi.opsWindow;
  if (lo > OPS_MIN_COMPAT || hi < OPS_VERSION)
    throw new Error(`tsr: the engine reads ops ${lo}..${hi}, this runtime writes ${OPS_MIN_COMPAT}..${OPS_VERSION}`);
  if (abi.schemaHash !== SCHEMA_HASH)
    throw new Error(`tsr: engine schema ${abi.schemaHash} differs from runtime schema ${SCHEMA_HASH}` +
                    ' (rebuild the wasm or re-vendor the runtime)');
  // the runtime's syntax facts (token tags, …) come from the same syntax.def
  if (abi.syntaxVersion !== SYNTAX_VERSION)
    throw new Error(`tsr: engine syntax ${abi.syntaxVersion} differs from runtime syntax ${SYNTAX_VERSION}` +
                    ' (rebuild the wasm or re-vendor the runtime)');
  // the resource pull's wire format (plan P1-19): resources.def on both sides
  if (abi.resVersion !== RES_VERSION)
    throw new Error(`tsr: engine resources ${abi.resVersion} differ from runtime resources ${RES_VERSION}` +
                    ' (rebuild the wasm or re-vendor the runtime)');
  return abi;
}
