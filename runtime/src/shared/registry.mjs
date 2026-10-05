// The registry (plan P2-03; design T2 S6 "Registry + frames";
// docs/ctor-design.md): constructors, fence handlers and formatters are
// entries in one table per namespace, each entry a function of one bound
// call. define() captures the entry it replaces as `next`, so an override
// extends a constructor instead of copying it; registration runs in
// document order, so the chain is deterministic. Sealed entries refuse.
// Engine-agnostic: one registry per execution.
export class Registry {
  constructor() {
    this.ns = { ctor: new Map(), fence: new Map(), format: new Map() };
  }
  get(ns, name) {
    return this.ns[ns].get(name);
  }
  // factory(next) → the entry's function; next is the previous entry's
  // function (or null). opts: spec (kept from the previous entry when
  // absent), sealed, user (runs inside a hook frame).
  define(ns, name, factory, { spec, sealed = false, user = false } = {}) {
    const table = this.ns[ns];
    if (!table) throw new TypeError(`unknown registry namespace ${ns}`);
    const prev = table.get(name);
    if (prev?.sealed) throw new TypeError(`${name} is sealed: it cannot be redefined`);
    const fn = factory(prev ? prev.fn : null);
    if (typeof fn !== 'function') throw new TypeError(`${ns} ${name}: the definition must give a function`);
    const entry = Object.freeze({ fn, spec: spec ?? prev?.spec, sealed, user: user || !!prev?.user });
    table.set(name, entry);
    return entry;
  }
  names(ns) {
    return [...this.ns[ns].keys()];
  }
}
