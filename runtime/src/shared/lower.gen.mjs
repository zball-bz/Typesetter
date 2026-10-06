// GENERATED from engine/src/codegen/lower.def by tools/gen-lower.mjs — do not edit.
export const LOWER_VERSION = 1;
export const LOWER_PROTOCOL = 2;
export const PROGRAM_ABI = 0x3d3ccfbb;
export const LOP_ASYNC = 0x80;
export const LOP = Object.freeze({ TEXT: 1, CALL: 2, HOLE: 3, FRAME: 4, FENCE: 5, REGION: 6, STMT: 8, VERBATIM: 9, IF: 10, SCOPE: 11, LOOP: 12, LET: 13 });
export const LCONST = Object.freeze({ Null: 0, False: 1, True: 2, Uint: 3, F64: 4, Str: 5, Array: 6 });
export const LBLOCK = Object.freeze({ Content: 0, Stmt: 1, Verbatim: 2 });
export const LPIECE = Object.freeze({ Hole: 0, Verbatim: 1 });
export const BFLAG = Object.freeze({ User: 1, Framed: 2, Async: 4 });
export const CFLAG = Object.freeze({ Spanned: 1 });
